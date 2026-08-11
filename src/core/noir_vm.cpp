#include "noir_vm.h"

#include <stdarg.h>
#include <stdio.h>

#include "core/logger.h"
#include "core/noir_opcodes.h"
#include "mtl/utility.h"
#include "types.h"

NE_LOG_DOMAIN(Script);

namespace NEONengine
{
    using namespace noir_op;
    using namespace noir_mode;
    using namespace noir_sr;

    /// The return address under a script's first frame. No bytecode offset can
    /// equal it.
    constexpr u32 SENTINEL_PC = 0xFFFFFFFFu;

    /// The PC is 16-bit, so it cannot reach bytecode past this.
    constexpr u32 MAX_BYTECODE_SIZE = 0x10000u;

    /// Stacks the VM has room for before the vector has to grow.
    constexpr size_t SESSION_SLOTS = 4;

    /// The stack execute_now runs on. No session may take it.
    constexpr u16 IMMEDIATE_SLOT = 0;

    /// Stacks the VM starts with: one per session, plus the immediate one.
    constexpr size_t INITIAL_SLOTS = SESSION_SLOTS + 1;

#ifdef GAME_DEBUG
    /// Updates between lines while the frame budget keeps running out.
    constexpr u16 SATURATION_LOG_EVERY = 50;
#endif

    // ─── Debug stats ────────────────────────────────────────────────────────────

#ifdef GAME_DEBUG
    void noir_debug_stats::reset() noexcept
    {
        max_stack_used   = 0;
        max_stack_script = 0;
        for (auto& c : opcode_histogram) c = 0;
        consecutive_saturated_updates = 0;
    }
#endif

    // ─── Constructor ────────────────────────────────────────────────────────────

    noir_vm::noir_vm(mtl::bitset<u32>& flags,
        mtl::vector<u16>& values,
        u8 const* bytecode,
        u32 bytecode_size,
        u32 const* func_toc,
        u16 func_count,
        noir_bridge const* bridge,
        u16 stack_size) noexcept
        : _flags(&flags)
        , _values(&values)
        , _bytecode(bytecode)
        , _bytecode_size(bytecode_size)
        , _func_toc(func_toc)
        , _func_count(func_count)
        , _bridge(bridge)
        , _stack(to<size_t>(stack_size) * INITIAL_SLOTS, u8(0))
        , _stack_bytes(stack_size)
        , _stack_slots(INITIAL_SLOTS)
        , _sessions(SESSION_SLOTS)
    {
        reset_session();

        if (stack_size == 0)
        {
            NE_LOG("stack size is 0; every script will die on its first push");
        }

        if (bytecode_size >= MAX_BYTECODE_SIZE)
        {
            NE_LOG("bytecode is %lu bytes; the PC addresses only %lu",
                to<ULONG>(bytecode_size),
                to<ULONG>(MAX_BYTECODE_SIZE));
        }
    }

    // ─── fail() ─────────────────────────────────────────────────────────────────

    noir_result noir_vm::fail(noir_error reason, char const* fmt, ...) noexcept
    {
        _last_error = reason;

        char message[NOIR_ERROR_MESSAGE_SIZE];
        int n = snprintf(message, sizeof(message), "script %u: ", _current_session.script_id);
        if (n < 0 || to<size_t>(n) >= sizeof(message)) n = 0;

        va_list args;
        va_start(args, fmt);
        vsnprintf(message + n, sizeof(message) - to<size_t>(n), fmt, args);
        va_end(args);

        NE_LOG("%s", message);

        _bridge->report_error(message);

        // terminated is a latch, and losing it would let the VM run sessions in
        // a scene that is over. A death cannot follow a terminal call inside an
        // update - the call ends the walk - but execute() and execute_now()
        // both run outside one.
        if (_vm_state != noir_vm_state::terminated) _vm_state = noir_vm_state::failed;

        _current_session.state = noir_session_state::died;
        return noir_result::failed;
    }

    // ─── execute(func_id) ───────────────────────────────────────────────────────

    mtl::expected<void, noir_error> noir_vm::execute(u16 func_id) noexcept
    {
        if (func_id >= _func_count)
        {
            NE_LOG("invalid func_id %u (count=%u)", func_id, _func_count);
            return mtl::unexpected(noir_error::invalid_func);
        }

        for (size_t i = 0; i < _sessions.size(); ++i)
        {
            auto const& s = _sessions[i];
            if (s.script_id == func_id && session_live(s))
            {
                NE_LOG("script %u is already running; request dropped", func_id);
                return mtl::unexpected(noir_error::duplicate_script);
            }
        }

        u16 const slot = alloc_stack_slot();
        if (!ensure_stack_slots(to<u16>(slot + 1)))
        {
            NE_LOG("no stack for script %u; request dropped", func_id);
            return mtl::unexpected(noir_error::out_of_memory);
        }

        // The frame push goes through the running session's window, so the
        // caller's registers have to survive it: a bridge function that calls
        // this is inside an opcode.
        noir_session const outer = _current_session;

        enter_script(func_id, slot);

        // A frame that would not fit leaves the session dead; do not revive it.
        bool const fits = _current_session.state != noir_session_state::died;
        if (fits) _sessions.push_back(_current_session);

        _current_session        = outer;
        _stack_base = stack_base_for(_current_session.stack_index);

        if (fits) return {};
        return mtl::unexpected(_last_error);
    }

    // ─── execute_now(func_id) ───────────────────────────────────────────────────

    mtl::expected<i16, noir_error> noir_vm::execute_now(u16 func_id) noexcept
    {
        if (_run_mode != run_mode::idle)
        {
            NE_LOG("script %u cannot run now: the VM is already running one", func_id);
            return mtl::unexpected(noir_error::nested_immediate);
        }

        if (func_id >= _func_count)
        {
            NE_LOG("invalid func_id %u (count=%u)", func_id, _func_count);
            return mtl::unexpected(noir_error::invalid_func);
        }

        noir_session const outer = _current_session;

        // The machine's own state belongs to the outer run, not this one.
        // execute_now exists to run while the VM is stopped, so it must hand
        // back the same stop it found: a dialogue script that dies would
        // otherwise clear the modal latch and set the parked sessions running
        // again behind the box that is still on screen.
        noir_vm_state const outer_state = _vm_state;

        _run_mode            = run_mode::immediate;

        enter_script(func_id, IMMEDIATE_SLOT);
        _current_session.instructions_left = _immediate_cap;

        auto const result = run_slice();

        // Out of instructions and still running. A deferred call also leaves it
        // running, so check the result too: that one stopped on purpose.
        if (result == noir_result::ok && _current_session.state == noir_session_state::running)
        {
            fail(noir_error::runaway, "ran past %u instructions", _immediate_cap);
        }

        // Whatever it left in D0. A script stopped by a terminal call never
        // reached its RET, so D0 is whatever it last held.
        i16 const answer = to<i16>(_current_session.d[0]);
        bool const died  = _current_session.state == noir_session_state::died;

        _current_session          = outer;
        _stack_base   = stack_base_for(_current_session.stack_index);
        _run_mode = run_mode::idle;

        // A terminal call is the one verdict that outranks the outer run: the
        // scene is over whatever the VM was doing before. Everything else gives
        // the outer state back. Why the script died is carried by _last_error
        // and the return value, so nothing is lost.
        if (_vm_state != noir_vm_state::terminated) _vm_state = outer_state;

        if (died) return mtl::unexpected(_last_error);
        return answer;
    }

    // ─── Stack slots ────────────────────────────────────────────────────────────

    u16 noir_vm::alloc_stack_slot() const noexcept
    {
        for (u16 slot = IMMEDIATE_SLOT + 1;; ++slot)
        {
            bool taken = false;
            for (size_t i = 0; i < _sessions.size(); ++i)
            {
                auto const& s = _sessions[i];
                if (session_live(s) && s.stack_index == slot)
                {
                    taken = true;
                    break;
                }
            }
            if (!taken) return slot;
        }
    }

    bool noir_vm::ensure_stack_slots(u16 slots) noexcept
    {
        if (slots <= _stack_slots) return true;

        size_t const wanted = to<size_t>(slots) * _stack_bytes;

        // resize() keeps writing into the old block if the allocator refuses,
        // so take the memory first and grow only once it is there.
        _stack.reserve(wanted);
        if (_stack.capacity() < wanted)
        {
            NE_LOG("no memory for %u stacks; script not started", slots);
            return false;
        }

        _stack.resize(wanted, u8(0));
        _stack_slots = slots;
        return true;
    }

    // ─── Register window ────────────────────────────────────────────────────────

    void noir_vm::load_session(size_t index) noexcept
    {
        _current_session        = _sessions[index];
        _stack_base = stack_base_for(_current_session.stack_index);
    }

    void noir_vm::store_session(size_t index) noexcept
    {
        // A bridge function can drop the run list from inside an instruction,
        // so the entry this session came from may be gone.
        if (index >= _sessions.size()) return;
        _sessions[index] = _current_session;
    }

    // ─── reset_session() ────────────────────────────────────────────────────────

    void noir_vm::reset_session() noexcept
    {
        _current_session = noir_session{};

        _stack_base = stack_base_for(_current_session.stack_index);

        // SP starts at the top of the stack (grows downward)
        _current_session.sp    = _stack_bytes;
        _current_session.fp    = _current_session.sp;
        _current_session.state = noir_session_state::ended;
    }

    // ─── enter_script() ─────────────────────────────────────────────────────────

    void noir_vm::enter_script(u16 func_id, u16 slot) noexcept
    {
        noir_session started{};
        started.script_id   = func_id;
        started.stack_index = slot;
        started.sp          = _stack_bytes;
        started.fp          = _stack_bytes;
        started.pc          = to<u16>(_func_toc[func_id]);
        started.state       = noir_session_state::running;

        // A run list session gets its budget at the top of the next update, so
        // one started from inside an update waits for the one after it.
        // execute_now hands out its own.
        started.instructions_left = 0;

        _current_session        = started;
        _stack_base = stack_base_for(slot);

        // What do_ret() unwinds. No bytecode offset can equal the sentinel, so
        // do_ret() can tell the top frame from any other.
        push32(SENTINEL_PC);
        push32(to<u32>(_current_session.fp));

        if (_current_session.state == noir_session_state::died) return;
        _current_session.fp = _current_session.sp;
    }

    // ─── reset_execution() ────────────────────────────────────────────────────────

    void noir_vm::reset_execution() noexcept
    {
        _sessions.clear();

        reset_session();

        _rotation   = 0;
        _vm_state   = noir_vm_state::ok;

        _exit_code        = 0;
        _last_error       = noir_error::none;
        _cached_dst_mode  = 0;
        _cached_dst_param = 0;
    }

    // ─── resume_execution() ───────────────────────────────────────────────────────

    void noir_vm::resume_execution() noexcept
    {
        // Only the modal latch. A teardown owed is not something an overlay
        // coming down may cancel, and clearing `failed` here would hide it from
        // whoever is about to read it.
        if (_vm_state == noir_vm_state::blocked) _vm_state = noir_vm_state::ok;
    }

    // ─── update() ───────────────────────────────────────────────────────────────

    mtl::expected<noir_result, noir_error> noir_vm::update() noexcept
    {
        if (_vm_state == noir_vm_state::blocked) return noir_result::blocked;

        if (_vm_state == noir_vm_state::terminated) return noir_result::terminated;

        // Last update's failure no longer applies.
        _vm_state = noir_vm_state::ok;

        if (_sessions.empty()) return noir_result::ok;

        begin_update_budgets();

        u16 frame_left     = _frame_budget;
        size_t const start = _rotation % _sessions.size();

#ifdef GAME_DEBUG
        walk_tally tally;
        bool ended_on_deferred = false;
        for (size_t i = 0; i < _sessions.size(); ++i)
        {
            if (!session_live(_sessions[i])) continue;
            ++tally.live;
            if (_sessions[i].state == noir_session_state::running) ++tally.ready;
        }
#endif

        // One walk. Every way a session can stop short of its budget - spending
        // it, parking on a WAIT, making a deferred call, or ending - leaves it
        // unable to run again in this update. A second walk would find the same
        // sessions and turn all of them down.
        _run_mode = run_mode::update;

        for (size_t k = 0; k < _sessions.size() && frame_left > 0; ++k)
        {
            size_t const i = (start + k) % _sessions.size();
            if (_sessions[i].state != noir_session_state::running) continue;

            load_session(i);

            u16 const budget = _current_session.instructions_left;
            u16 const slice  = (budget < frame_left) ? budget : frame_left;
            _current_session.instructions_left = slice;

#ifdef GAME_DEBUG
            size_t const before = _sessions.size();
            bool const capped   = frame_left < budget;
#endif

            auto const result = run_slice();

            // What it did not use goes back to the frame for the sessions after it.
            u16 const used = to<u16>(slice - _current_session.instructions_left);
            _current_session.instructions_left = to<u16>(budget - used);
            frame_left = to<u16>(frame_left - used);

#ifdef GAME_DEBUG
            // Only a terminal call may empty the run list, and a terminal call
            // always returns deferred.
            if (_sessions.size() < before && result != noir_result::deferred)
            {
                NE_LOG("a bridge call dropped sessions without a terminal call");
            }

            // Being capped is not enough on its own. A session given less than
            // it asked for may have ended or parked inside that smaller slice,
            // in which case it would not have used the rest either. It went
            // short only if it spent every instruction it got and is still
            // running.
            bool const wants_more = used == slice
                && _current_session.state == noir_session_state::running;
            if (capped && wants_more) ++tally.cut_short;
            else ++tally.ran_fully;
#endif

            // A terminal call leaves nothing worth writing back: the teardown
            // clears the whole run list.
            if (_vm_state != noir_vm_state::terminated) store_session(i);

            // A deferred call ends the walk, which is what stops the next
            // session - and this one - running again this frame.
            if (result == noir_result::deferred)
            {
#ifdef GAME_DEBUG
                ended_on_deferred = true;
#endif
                break;
            }
        }

        _run_mode = run_mode::idle;

#ifdef GAME_DEBUG
        tally.frame_ran_out = frame_left == 0 && !ended_on_deferred;
        report_saturation(tally);
#endif

        ++_rotation;
        remove_dead_sessions();

        // A deferred call ends the walk, so nothing can fail after one: if both
        // happened, the deferred call was last and its verdict stands.
        switch (_vm_state)
        {
            case noir_vm_state::blocked: return noir_result::blocked;
            case noir_vm_state::terminated: return noir_result::terminated;
            case noir_vm_state::failed: return mtl::unexpected(_last_error);
            case noir_vm_state::ok: break;
        }

        return _sessions.empty() ? noir_result::completed : noir_result::yielded;
    }

    // ─── Scheduling ─────────────────────────────────────────────────────────────

    void noir_vm::begin_update_budgets() noexcept
    {
        for (size_t i = 0; i < _sessions.size(); ++i)
        {
            auto& s             = _sessions[i];
            s.instructions_left = 0;

            if (!session_live(s)) continue;

            if (s.state == noir_session_state::waiting)
            {
                if (s.wait_counter > 0)
                {
                    --s.wait_counter;
                    continue;
                }
                s.state = noir_session_state::running;
            }

            s.instructions_left = _session_budget;
        }
    }

    noir_result noir_vm::run_slice() noexcept
    {
        while (_current_session.instructions_left > 0 && _current_session.state == noir_session_state::running)
        {
            auto const result = execute_instruction();

            // A deferred call has run and the program counter has moved on, so
            // the session stays runnable, parked on the instruction after it.
            // A failure has already killed the session.
            if (result == noir_result::deferred || result == noir_result::failed) return result;
        }
        return noir_result::ok;
    }

#ifdef GAME_DEBUG
    void noir_vm::report_saturation(walk_tally const& tally) noexcept
    {
        u16 const given         = to<u16>(tally.ran_fully + tally.cut_short);
        u16 const never_reached = (tally.ready > given) ? to<u16>(tally.ready - given) : 0;

        // An empty budget on its own says nothing: the last session may have
        // ended on the last instruction. Somebody has to have been turned down.
        bool const saturated = tally.frame_ran_out && (tally.cut_short > 0 || never_reached > 0);

        u16& updates = _debug_stats.consecutive_saturated_updates;

        if (!saturated)
        {
            if (updates > 0)
            {
                NE_LOG("frame budget: demand fits again, after %u updates", updates);
                updates = 0;
            }
            return;
        }

        if (updates % SATURATION_LOG_EVERY == 0)
        {
            NE_LOG("frame budget of %u ran out: %u live, %u ran fully, %u cut short, %u never reached",
                _frame_budget,
                tally.live,
                tally.ran_fully,
                tally.cut_short,
                never_reached);
        }

        ++updates;
    }
#endif

    void noir_vm::remove_dead_sessions() noexcept
    {
        size_t write = 0;
        for (size_t read = 0; read < _sessions.size(); ++read)
        {
            if (!session_live(_sessions[read])) continue;
            if (write != read) _sessions[write] = _sessions[read];
            ++write;
        }
        while (_sessions.size() > write) _sessions.pop_back();
    }

    // ─── Instruction dispatch ───────────────────────────────────────────────────

    noir_result noir_vm::execute_instruction() noexcept
    {
        // Counted first, so every way the instruction can end - running,
        // deferring, or killing the session - costs the same one.
        --_current_session.instructions_left;

        // Read before counting: the histogram indexes the bytecode, so counting
        // first would be the one read that goes past the end of it.
        u8 const opcode = read_u8();
        if (_current_session.state == noir_session_state::died) return noir_result::failed;

#ifdef GAME_DEBUG
        ++_debug_stats.opcode_histogram[opcode];
#endif

        u8 category = opcode >> 4;

        switch (category)
        {
            case 0x0: return op_script_exec(opcode);
            case 0x1: return op_data_move(opcode);
            case 0x2: return op_arithmetic(opcode);
            case 0x3: return op_logic(opcode);
            case 0x4: return op_control_flow(opcode);
            case 0x5: return op_flag_ops(opcode);
            case 0x6: return op_value_ops(opcode);
            case 0x8: return op_game_inventory(opcode);
            case 0x9: return op_game_timeline(opcode);
            case 0xA: return op_game_scenes(opcode);
            case 0xB: return op_game_dialogue(opcode);
            case 0xC: return op_game_menu(opcode);
            case 0xE: return op_debug(opcode);
            default: return fail(noir_error::invalid_opcode, "unknown opcode 0x%02X", opcode);
        }
    }

    // ─── Script Execution opcodes ───────────────────────────────────────────────

    noir_result noir_vm::op_script_exec(u8 op) noexcept
    {
        switch (op)
        {
            case NOP: return noir_result::ok;

            case WAIT:
            {
                u16 frames = read_u16();

                // Nothing ages a wait here: no frame passes until execute_now
                // returns, so honouring it would stop the script for good. The
                // compiler bans it; this is what happens when one gets through.
                if (_run_mode == run_mode::immediate)
                {
                    NE_LOG("script %u: WAIT under execute_now ignored", _current_session.script_id);
                    return noir_result::ok;
                }

                _current_session.wait_counter += frames;
                _current_session.state = noir_session_state::waiting;
                return noir_result::ok;
            }

            case HALT:
                // Not "finished" - a case the author says cannot happen. It
                // kills this session and reports, like any other script error.
                return fail(noir_error::halt, "HALT");

            case CALL:
            {
                u16 func_id = read_u16();
                if (func_id >= _func_count)
                {
                    return fail(noir_error::invalid_func,
                        "CALL to function %u, which does not exist",
                        func_id);
                }
                auto target = to<u16>(_func_toc[func_id]);
                push32(to<u32>(_current_session.pc));
                push32(to<u32>(_current_session.fp));

                if (_current_session.state == noir_session_state::died) return noir_result::failed;

                _current_session.fp = _current_session.sp;
                _current_session.pc = target;
                return noir_result::ok;
            }

            case LOCALS:
            {
                u8 n = read_u8();
                if (stack_overflow(n)) return noir_result::failed;
                _current_session.sp -= n;
                return noir_result::ok;
            }

            case RET: do_ret(); return noir_result::ok;

            case RETZ:
                if (_current_session.sr & Z) do_ret();
                return noir_result::ok;

            case RETC:
                if (_current_session.sr & C) do_ret();
                return noir_result::ok;

            default: return fail(noir_error::invalid_opcode, "unknown opcode 0x%02X", op);
        }
    }

    // ─── Data Movement opcodes ──────────────────────────────────────────────────

    noir_result noir_vm::op_data_move(u8 op) noexcept
    {
        switch (op)
        {
            case MOV8:
            {
                u8 mode     = read_u8();
                u8 dst_mode = mode >> DST_SHIFT;
                u8 src_mode = mode & SRC_MASK;
                read_operand_cached(dst_mode);
                u16 val = read_operand(src_mode) & 0xFF;
                write_operand_cached(val);
                return noir_result::ok;
            }

            case MOV16:
            {
                u8 mode     = read_u8();
                u8 dst_mode = mode >> DST_SHIFT;
                u8 src_mode = mode & SRC_MASK;
                read_operand_cached(dst_mode);
                u16 val = read_operand(src_mode);
                write_operand_cached(val);
                return noir_result::ok;
            }

            case XCHG:
            {
                u8 regs        = read_u8();
                u8 ra          = (regs >> DST_SHIFT) - REG_D0;
                u8 rb          = (regs & SRC_MASK) - REG_D0;
                u16 tmp        = _current_session.d[ra & 3];
                _current_session.d[ra & 3] = _current_session.d[rb & 3];
                _current_session.d[rb & 3] = tmp;
                return noir_result::ok;
            }

            case PUSH:
            {
                u8 mode     = read_u8();
                u8 src_mode = mode & SRC_MASK;
                u16 val     = read_operand(src_mode);
                push16(val);
                return noir_result::ok;
            }

            case POP:
            {
                u8 mode     = read_u8();
                u8 dst_mode = mode >> DST_SHIFT;
                u16 val     = pop16();
                write_operand(dst_mode, val);
                return noir_result::ok;
            }

            case TOP:
            {
                u8 mode     = read_u8();
                u8 dst_mode = mode >> DST_SHIFT;
                u16 val     = peek16();
                write_operand(dst_mode, val);
                return noir_result::ok;
            }

            case DUP:
            {
                u16 val = peek16();
                push16(val);
                return noir_result::ok;
            }

            case CLR:
            {
                u8 mode     = read_u8();
                u8 dst_mode = mode >> DST_SHIFT;
                write_operand(dst_mode, 0);
                return noir_result::ok;
            }

            default: return fail(noir_error::invalid_opcode, "unknown opcode 0x%02X", op);
        }
    }

    // ─── Arithmetic opcodes ─────────────────────────────────────────────────────

    noir_result noir_vm::op_arithmetic(u8 op) noexcept
    {
        u8 mode     = read_u8();
        u8 dst_mode = mode >> DST_SHIFT;
        u8 src_mode = mode & SRC_MASK;

        u16 a = read_operand_cached(dst_mode);

        switch (op)
        {
            case ADD:
            {
                u16 b      = read_operand(src_mode);
                u32 result = to<u32>(a) + to<u32>(b);
                u16 r      = to<u16>(result & 0xFFFF);
                set_flags_nzcv_add(a, b, result);
                write_operand_cached(r);
                return noir_result::ok;
            }

            case SUB:
            {
                u16 b      = read_operand(src_mode);
                u32 result = to<u32>(a) - to<u32>(b);
                u16 r      = to<u16>(result & 0xFFFF);
                set_flags_nzcv_sub(a, b, result);
                write_operand_cached(r);
                return noir_result::ok;
            }

            case MUL:
            {
                u16 b    = read_operand(src_mode);
                i32 sa   = to<i16>(a);
                i32 sb   = to<i16>(b);
                i32 full = sa * sb;
                u16 r    = to<u16>(full & 0xFFFF);

                _current_session.sr = 0;
                if (r & 0x8000) _current_session.sr |= N;
                if (r == 0) _current_session.sr |= Z;
                if (full != to<i32>(to<i16>(r))) _current_session.sr |= V;
                write_operand_cached(r);
                return noir_result::ok;
            }

            case DIV:
            {
                u16 b = read_operand(src_mode);
                if (b == 0)
                {
                    return fail(noir_error::division_zero, "divided by zero");
                }
                i16 sa = to<i16>(a);
                i16 sb = to<i16>(b);
                i16 r  = sa / sb;
                u16 ur = to<u16>(r);
                set_flags_nz(ur);
                write_operand_cached(ur);
                return noir_result::ok;
            }

            case MOD:
            {
                u16 b = read_operand(src_mode);
                if (b == 0)
                {
                    return fail(noir_error::division_zero, "took a modulo by zero");
                }
                i16 sa = to<i16>(a);
                i16 sb = to<i16>(b);
                i16 r  = sa % sb;
                u16 ur = to<u16>(r);
                set_flags_nz(ur);
                write_operand_cached(ur);
                return noir_result::ok;
            }

            case CMP:
            {
                u16 b      = read_operand(src_mode);
                u32 result = to<u32>(a) - to<u32>(b);
                set_flags_nzcv_sub(a, b, result);
                return noir_result::ok;
            }

            case NEG:
            {
                u32 result = to<u32>(-to<i16>(a)) & 0xFFFF;
                u16 r      = to<u16>(result);
                set_flags_nzcv_sub(0, a, to<u32>(0) - to<u32>(a));
                write_operand_cached(r);
                return noir_result::ok;
            }

            case INC:
            {
                u32 result = to<u32>(a) + 1;
                u16 r      = to<u16>(result & 0xFFFF);
                set_flags_nzcv_add(a, 1, result);
                write_operand_cached(r);
                return noir_result::ok;
            }

            case DEC:
            {
                u32 result = to<u32>(a) - 1;
                u16 r      = to<u16>(result & 0xFFFF);
                set_flags_nzcv_sub(a, 1, result);
                write_operand_cached(r);
                return noir_result::ok;
            }

            default: return fail(noir_error::invalid_opcode, "unknown opcode 0x%02X", op);
        }
    }

    // ─── Logic opcodes ──────────────────────────────────────────────────────────

    noir_result noir_vm::op_logic(u8 op) noexcept
    {
        u8 mode     = read_u8();
        u8 dst_mode = mode >> DST_SHIFT;
        u8 src_mode = mode & SRC_MASK;

        u16 a = read_operand_cached(dst_mode);

        switch (op)
        {
            case AND:
            {
                u16 b = read_operand(src_mode);
                u16 r = a & b;
                set_flags_nz(r);
                write_operand_cached(r);
                return noir_result::ok;
            }

            case OR:
            {
                u16 b = read_operand(src_mode);
                u16 r = a | b;
                set_flags_nz(r);
                write_operand_cached(r);
                return noir_result::ok;
            }

            case XOR:
            {
                u16 b = read_operand(src_mode);
                u16 r = a ^ b;
                set_flags_nz(r);
                write_operand_cached(r);
                return noir_result::ok;
            }

            case TST:
            {
                u16 b = read_operand(src_mode);
                u16 r = a & b;
                set_flags_nz(r);
                return noir_result::ok;
            }

            case NOT:
            {
                u16 r = ~a;
                set_flags_nz(r);
                write_operand_cached(r);
                return noir_result::ok;
            }

            case SHL:
            {
                u8 count = read_u8();
                _current_session.sr  = 0;
                if (count > 0 && count <= 16)
                {
                    if (a & (1u << (16 - count))) _current_session.sr |= C;
                }
                u16 r = (count < 16) ? to<u16>(a << count) : 0;
                if (r & 0x8000) _current_session.sr |= N;
                if (r == 0) _current_session.sr |= Z;
                write_operand_cached(r);
                return noir_result::ok;
            }

            case SHR:
            {
                u8 count = read_u8();
                _current_session.sr  = 0;
                if (count > 0 && count <= 16)
                {
                    if (a & (1u << (count - 1))) _current_session.sr |= C;
                }
                u16 r = (count < 16) ? to<u16>(a >> count) : 0;
                if (r & 0x8000) _current_session.sr |= N;
                if (r == 0) _current_session.sr |= Z;
                write_operand_cached(r);
                return noir_result::ok;
            }

            case ASR:
            {
                u8 count = read_u8();
                i16 sa   = to<i16>(a);
                _current_session.sr  = 0;
                if (count > 0 && count <= 16)
                {
                    if (a & (1u << (count - 1))) _current_session.sr |= C;
                }
                i16 r  = (count < 16) ? to<i16>(sa >> count) : (sa < 0 ? -1 : 0);
                u16 ur = to<u16>(r);
                if (ur & 0x8000) _current_session.sr |= N;
                if (ur == 0) _current_session.sr |= Z;
                write_operand_cached(ur);
                return noir_result::ok;
            }

            default: return fail(noir_error::invalid_opcode, "unknown opcode 0x%02X", op);
        }
    }

    // ─── Control Flow opcodes ───────────────────────────────────────────────────

    noir_result noir_vm::op_control_flow(u8 op) noexcept
    {
        i16 offset = read_s16();

        bool take = false;

        switch (op)
        {
            case JMP: take = true; break;
            case BEQ: take = (_current_session.sr & Z) != 0; break;
            case BNE: take = (_current_session.sr & Z) == 0; break;
            case BLT: take = ((_current_session.sr & N) != 0) != ((_current_session.sr & V) != 0); break;
            case BGE: take = ((_current_session.sr & N) != 0) == ((_current_session.sr & V) != 0); break;
            case BGT:
                take = ((_current_session.sr & Z) == 0) && (((_current_session.sr & N) != 0) == ((_current_session.sr & V) != 0));
                break;
            case BLE:
                take = ((_current_session.sr & Z) != 0) || (((_current_session.sr & N) != 0) != ((_current_session.sr & V) != 0));
                break;
            case BCS: take = (_current_session.sr & C) != 0; break;
            case BCC: take = (_current_session.sr & C) == 0; break;
            default: return fail(noir_error::invalid_opcode, "unknown opcode 0x%02X", op);
        }

        if (take)
        {
            _current_session.pc = to<u16>(_current_session.pc + offset);
        }

        return noir_result::ok;
    }

    // ─── Flag Operations ────────────────────────────────────────────────────────

    noir_result noir_vm::op_flag_ops(u8 op) noexcept
    {
        switch (op)
        {
            // Immediate index variants: [op] [u16 index]
            case SETF:
            {
                u16 idx = read_u16();
                if (!validate_flag_index(idx))
                    return fail(noir_error::flag_oob, "flag %u is out of range", idx);
                set_flag(idx, true);
                return noir_result::ok;
            }
            case CLRF:
            {
                u16 idx = read_u16();
                if (!validate_flag_index(idx))
                    return fail(noir_error::flag_oob, "flag %u is out of range", idx);
                set_flag(idx, false);
                return noir_result::ok;
            }
            case TOGF:
            {
                u16 idx = read_u16();
                if (!validate_flag_index(idx))
                    return fail(noir_error::flag_oob, "flag %u is out of range", idx);
                set_flag(idx, !get_flag(idx));
                return noir_result::ok;
            }
            case TSTF:
            {
                u16 idx = read_u16();
                if (!validate_flag_index(idx))
                    return fail(noir_error::flag_oob, "flag %u is out of range", idx);
                // Z=1 when flag is clear, Z=0 when flag is set
                _current_session.sr = get_flag(idx) ? 0 : Z;
                return noir_result::ok;
            }

            // Register-indexed variants: [op] [reg_id]
            case SETFR:
            {
                u8 reg  = read_u8();
                u16 idx = _current_session.d[reg & 3];
                if (!validate_flag_index(idx))
                    return fail(noir_error::flag_oob, "flag %u is out of range", idx);
                set_flag(idx, true);
                return noir_result::ok;
            }
            case CLRFR:
            {
                u8 reg  = read_u8();
                u16 idx = _current_session.d[reg & 3];
                if (!validate_flag_index(idx))
                    return fail(noir_error::flag_oob, "flag %u is out of range", idx);
                set_flag(idx, false);
                return noir_result::ok;
            }
            case TOGFR:
            {
                u8 reg  = read_u8();
                u16 idx = _current_session.d[reg & 3];
                if (!validate_flag_index(idx))
                    return fail(noir_error::flag_oob, "flag %u is out of range", idx);
                set_flag(idx, !get_flag(idx));
                return noir_result::ok;
            }
            case TSTFR:
            {
                u8 reg  = read_u8();
                u16 idx = _current_session.d[reg & 3];
                if (!validate_flag_index(idx))
                    return fail(noir_error::flag_oob, "flag %u is out of range", idx);
                _current_session.sr = get_flag(idx) ? 0 : Z;
                return noir_result::ok;
            }

            // Flag ↔ Register transfer: [op] [reg_id] [u16 index]
            case LDFR:
            {
                u8 reg  = read_u8();
                u16 idx = read_u16();
                if (!validate_flag_index(idx))
                    return fail(noir_error::flag_oob, "flag %u is out of range", idx);
                u16 val         = get_flag(idx) ? 1 : 0;
                _current_session.d[reg & 3] = val;
                set_flags_nz(val);
                return noir_result::ok;
            }
            case STFR:
            {
                u8 reg  = read_u8();
                u16 idx = read_u16();
                if (!validate_flag_index(idx))
                    return fail(noir_error::flag_oob, "flag %u is out of range", idx);
                set_flag(idx, (_current_session.d[reg & 3] & 1) != 0);
                return noir_result::ok;
            }

            default: return fail(noir_error::invalid_opcode, "unknown opcode 0x%02X", op);
        }
    }

    // ─── Value Operations ───────────────────────────────────────────────────────

    noir_result noir_vm::op_value_ops(u8 op) noexcept
    {
        u16 idx = read_u16();
        if (!validate_value_index(idx))
            return fail(noir_error::value_oob, "value %u is out of range", idx);

        switch (op)
        {
            case CMPV:
            {
                u16 imm    = read_u16();
                u16 a      = (*_values)[idx];
                u32 result = to<u32>(a) - to<u32>(imm);
                set_flags_nzcv_sub(a, imm, result);
                return noir_result::ok;
            }

            case TSTV:
            {
                set_flags_nz((*_values)[idx]);
                return noir_result::ok;
            }

            case CLRV:
            {
                (*_values)[idx] = 0;
                return noir_result::ok;
            }

            case INCV:
            {
                u16 a      = (*_values)[idx];
                u32 result = to<u32>(a) + 1;
                u16 r      = to<u16>(result & 0xFFFF);
                set_flags_nzcv_add(a, 1, result);
                (*_values)[idx] = r;
                return noir_result::ok;
            }

            case DECV:
            {
                u16 a      = (*_values)[idx];
                u32 result = to<u32>(a) - 1;
                u16 r      = to<u16>(result & 0xFFFF);
                set_flags_nzcv_sub(a, 1, result);
                (*_values)[idx] = r;
                return noir_result::ok;
            }

            default: return fail(noir_error::invalid_opcode, "unknown opcode 0x%02X", op);
        }
    }

    // ─── Game Calls — Inventory ─────────────────────────────────────────────────

    noir_result noir_vm::op_game_inventory(u8 op) noexcept
    {
        switch (op)
        {
            case INV_ADD: return call(_bridge->inv_add, _current_session.d[0]);

            case INV_REMOVE: return call(_bridge->inv_remove, _current_session.d[0]);

            case INV_HAS: return call_flag(_bridge->inv_has, _current_session.d[0]);

            case INV_COUNT: return call_result(_bridge->inv_count, _current_session.d[0]);

            case INV_CLEAR: return call(_bridge->inv_clear_item, _current_session.d[0]);

            case INV_SELECTED: return call_result(_bridge->inv_selected_item);

            default: return fail(noir_error::invalid_opcode, "unknown opcode 0x%02X", op);
        }
    }

    // ─── Game Calls — Timeline ──────────────────────────────────────────────────

    noir_result noir_vm::op_game_timeline(u8 op) noexcept
    {
        switch (op)
        {
            case ANIM_PLAY: return call(_bridge->anim_play, _current_session.d[0]);

            case ANIM_STOP: return call(_bridge->anim_stop, _current_session.d[0]);

            default: return fail(noir_error::invalid_opcode, "unknown opcode 0x%02X", op);
        }
    }

    // ─── Game Calls — Scenes ────────────────────────────────────────────────────

    noir_result noir_vm::op_game_scenes(u8 op) noexcept
    {
        switch (op)
        {
            case GOTO_SCENE: return call(_bridge->goto_scene, _current_session.d[0]);

            case GET_SCENE: return call_result(_bridge->get_scene);

            case HOTSPOT_ON: return call(_bridge->hotspot_on, _current_session.d[0]);

            case HOTSPOT_OFF: return call(_bridge->hotspot_off, _current_session.d[0]);

            case SET_POINTER: return call(_bridge->set_pointer, _current_session.d[0]);

            default: return fail(noir_error::invalid_opcode, "unknown opcode 0x%02X", op);
        }
    }

    // ─── Game Calls — Dialogue / Audio ──────────────────────────────────────────

    noir_result noir_vm::op_game_dialogue(u8 op) noexcept
    {
        switch (op)
        {
            case SAY: return call(_bridge->say, _current_session.d[0], _current_session.d[1]);

            case SHOW_DIALOG: return call(_bridge->show_dialog, _current_session.d[0]);

            case SHOW_TEXT: return call(_bridge->show_text, _current_session.d[0], _current_session.d[1], _current_session.d[2]);

            case CLEAR_TEXT: return call(_bridge->clear_text);

            case PLAY_MUSIC: return call(_bridge->play_music, _current_session.d[0]);

            case STOP_MUSIC: return call(_bridge->stop_music);

            case PLAY_SFX: return call(_bridge->play_sfx, _current_session.d[0]);

            default: return fail(noir_error::invalid_opcode, "unknown opcode 0x%02X", op);
        }
    }

    // ─── Game Calls — Menu / Settings ───────────────────────────────────────────

    noir_result noir_vm::op_game_menu(u8 op) noexcept
    {
        switch (op)
        {
            case FADE_OUT: return call(_bridge->fade_out, _current_session.d[0]);

            case FADE_IN: return call(_bridge->fade_in, _current_session.d[0]);

            case SCREEN_CLR: return call(_bridge->screen_clear, _current_session.d[0]);

            case SET_LANG: return call(_bridge->set_lang, _current_session.d[0]);

            case SAVE_GAME: return call(_bridge->save_game, _current_session.d[0]);

            case LOAD_GAME: return call(_bridge->load_game, _current_session.d[0]);

            case NEW_GAME: return call(_bridge->new_game);

            case QUIT_GAME: return call(_bridge->quit_game);

            default: return fail(noir_error::invalid_opcode, "unknown opcode 0x%02X", op);
        }
    }

    // ─── Debug opcodes ──────────────────────────────────────────────────────────

    noir_result noir_vm::op_debug(u8 op) noexcept
    {
#ifdef GAME_DEBUG
        switch (op)
        {
            case DBG_LOG: call(_bridge->debug_log, _current_session.d[0]); break;

            case DBG_LOGS:
            {
                u8 len   = read_u8();
                auto str = force_to<char const*>(_bytecode + _current_session.pc);
                call(_bridge->debug_print, str, len);
                _current_session.pc = to<u16>(_current_session.pc + len);
                break;
            }

            case DBG_REG:
                NE_LOG("REG: D0=%04X D1=%04X D2=%04X D3=%04X SR=%02X",
                    _current_session.d[0],
                    _current_session.d[1],
                    _current_session.d[2],
                    _current_session.d[3],
                    _current_session.sr);
                NE_LOG("REG: SP=%04X FP=%04X PC=%04X", _current_session.sp, _current_session.fp, _current_session.pc);
                break;

            case DBG_STK:
            {
                u8 const* p   = stack_at(_current_session.sp);
                u8 const* end = _stack.data() + _stack.size();
                NE_LOG("STK: (top → bottom, up to 16 bytes)");
                for (int i = 0; i < 16 && p < end; ++i, ++p) NE_LOG("  [SP+%d] = %02X", i, *p);
                break;
            }

            case DBG_STAT:
                NE_LOG("STATS: max_stack=%lu bytes, by script %u",
                    _debug_stats.max_stack_used,
                    _debug_stats.max_stack_script);
                break;

            case DBG_FLAG:
            {
                if (_current_session.d[0] == 0xFFFF)
                {
                    u16 total = to<u16>(_flags->size() * 8);
                    NE_LOG("FLAGS: (all %u)", total);
                    for (u16 i = 0; i < total; ++i)
                    {
                        if (get_flag(i)) NE_LOG("  flag[%u] = 1", i);
                    }
                }
                else
                {
                    if (validate_flag_index(_current_session.d[0]))
                        NE_LOG("FLAG[%u] = %u", _current_session.d[0], get_flag(_current_session.d[0]) ? 1 : 0);
                }
                break;
            }

            case DBG_VAL:
            {
                if (_current_session.d[0] == 0xFFFF)
                {
                    u16 total = to<u16>(_values->size());
                    NE_LOG("VALUES: (all %u)", total);
                    for (u16 i = 0; i < total; ++i)
                        NE_LOG("  value[%u] = %u", i, (*_values)[i]);
                }
                else
                {
                    if (validate_value_index(_current_session.d[0]))
                        NE_LOG("VALUE[%u] = %u", _current_session.d[0], (*_values)[_current_session.d[0]]);
                }
                break;
            }

            default:
                // Unknown debug opcode — treat as NOP
                break;
        }
#else
        // Release build: skip inline data for DBG_LOGS, otherwise NOP
        if (op == DBG_LOGS)
        {
            u8 len  = read_u8();
            _current_session.pc = to<u16>(_current_session.pc + len);
        }
#endif
        return noir_result::ok;
    }

    // ─── RET implementation ─────────────────────────────────────────────────────

    void noir_vm::do_ret() noexcept
    {
        _current_session.sp = _current_session.fp;
        _current_session.fp = to<u16>(pop32());

        u32 return_pc = pop32();
        _current_session.pc       = to<u16>(return_pc);

        if (return_pc != SENTINEL_PC) return;

        _exit_code = _current_session.d[0];
        _current_session.state = noir_session_state::ended;
    }

    // ─── Stack operations ───────────────────────────────────────────────────────

    void noir_vm::push16(u16 value) noexcept
    {
        if (stack_overflow(2)) return;
        _current_session.sp -= 2;
        auto* p = stack_at(_current_session.sp);
        p[0]    = to<u8>(value >> 8);  // big-endian
        p[1]    = to<u8>(value & 0xFF);
    }

    void noir_vm::push32(u32 value) noexcept
    {
        if (stack_overflow(4)) return;
        _current_session.sp -= 4;
        auto* p = stack_at(_current_session.sp);
        p[0]    = to<u8>(value >> 24);
        p[1]    = to<u8>(value >> 16);
        p[2]    = to<u8>(value >> 8);
        p[3]    = to<u8>(value & 0xFF);
    }

    u16 noir_vm::pop16() noexcept
    {
        if (stack_underflow(2)) return 0;
        auto const* p = stack_at(_current_session.sp);
        u16 value     = to<u16>((to<u16>(p[0]) << 8) | p[1]);
        _current_session.sp += 2;
        return value;
    }

    u32 noir_vm::pop32() noexcept
    {
        if (stack_underflow(4)) return 0;
        auto const* p = stack_at(_current_session.sp);
        u32 value
            = (to<u32>(p[0]) << 24) | (to<u32>(p[1]) << 16) | (to<u32>(p[2]) << 8) | to<u32>(p[3]);
        _current_session.sp += 4;
        return value;
    }

    u16 noir_vm::peek16() noexcept
    {
        if (stack_underflow(2)) return 0;
        auto const* p = stack_at(_current_session.sp);
        return to<u16>((to<u16>(p[0]) << 8) | p[1]);
    }

    bool noir_vm::stack_overflow(u16 bytes) noexcept
    {
        if (_current_session.sp < bytes)
        {
            fail(noir_error::stack_overflow, "ran out of its %u byte stack", _stack_bytes);
            return true;
        }

#ifdef GAME_DEBUG
        u16 used = to<u16>(_stack_bytes - (_current_session.sp - bytes));
        if (used > _debug_stats.max_stack_used)
        {
            _debug_stats.max_stack_used   = used;
            _debug_stats.max_stack_script = _current_session.script_id;
        }
#endif
        return false;
    }

    bool noir_vm::stack_underflow(u16 bytes) noexcept
    {
        if (_current_session.sp + bytes > _stack_bytes)
        {
            fail(noir_error::stack_underflow, "popped more than it pushed");
            return true;
        }
        return false;
    }

    // ─── Bytecode reading ───────────────────────────────────────────────────────

    u8 noir_vm::read_u8() noexcept
    {
        if (to<u32>(_current_session.pc) >= _bytecode_size)
        {
            fail(noir_error::invalid_address, "ran past the end of the bytecode at %u", _current_session.pc);
            return 0;
        }
        return _bytecode[_current_session.pc++];
    }

    u16 noir_vm::read_u16() noexcept
    {
        if (to<u32>(_current_session.pc) + 2 > _bytecode_size)
        {
            fail(noir_error::invalid_address, "ran past the end of the bytecode at %u", _current_session.pc);
            return 0;
        }
        u16 hi = _bytecode[_current_session.pc++];
        u16 lo = _bytecode[_current_session.pc++];
        return to<u16>((hi << 8) | lo);
    }

    i16 noir_vm::read_s16() noexcept
    {
        return to<i16>(read_u16());
    }

    // ─── Locals ─────────────────────────────────────────────────────────────────

    u8* noir_vm::local_at(i8 offset) noexcept
    {
        // Signed on purpose: a negative offset must not wrap back into range.
        i32 const at = to<i32>(_current_session.fp) + offset;

        // The two ends of a stack already have error names, so a local that
        // leaves through one reports the name of that end.
        if (at < 0)
        {
            fail(noir_error::stack_overflow,
                "local %d falls below its %u byte stack",
                offset,
                _stack_bytes);
            return nullptr;
        }

        if (at + 2 > to<i32>(_stack_bytes))
        {
            fail(noir_error::stack_underflow,
                "local %d reaches past its %u byte stack",
                offset,
                _stack_bytes);
            return nullptr;
        }

        return stack_at(to<u16>(at));
    }

    // ─── Mode byte helpers ──────────────────────────────────────────────────────

    u16 noir_vm::read_operand(u8 mode) noexcept
    {
        switch (mode)
        {
            case IMM8: return to<u16>(read_u8());
            case IMM16: return read_u16();
            case REG_D0: return _current_session.d[0];
            case REG_D1: return _current_session.d[1];
            case REG_D2: return _current_session.d[2];
            case REG_D3: return _current_session.d[3];

            case LOCAL:
            {
                i8 offset = to<i8>(read_u8());
                u8* addr  = local_at(offset);
                if (!addr) return 0;
                return to<u16>((to<u16>(addr[0]) << 8) | addr[1]);
            }

            case VALUE_IMM:
            {
                u16 idx = read_u16();
                if (!validate_value_index(idx))
                {
                    fail(noir_error::value_oob, "value %u is out of range", idx);
                    return 0;
                }
                return (*_values)[idx];
            }

            case VALUE_D0:
            {
                if (!validate_value_index(_current_session.d[0]))
                {
                    fail(noir_error::value_oob, "value %u is out of range", _current_session.d[0]);
                    return 0;
                }
                return (*_values)[_current_session.d[0]];
            }

            default: return 0;
        }
    }

    void noir_vm::write_operand(u8 mode, u16 value) noexcept
    {
        switch (mode)
        {
            case REG_D0: _current_session.d[0] = value; break;
            case REG_D1: _current_session.d[1] = value; break;
            case REG_D2: _current_session.d[2] = value; break;
            case REG_D3: _current_session.d[3] = value; break;

            case LOCAL:
            {
                i8 offset = to<i8>(read_u8());
                u8* addr  = local_at(offset);
                if (!addr) break;
                addr[0] = to<u8>(value >> 8);
                addr[1] = to<u8>(value & 0xFF);
                break;
            }

            case VALUE_IMM:
            {
                u16 idx = read_u16();
                if (!validate_value_index(idx))
                {
                    fail(noir_error::value_oob, "value %u is out of range", idx);
                    break;
                }
                (*_values)[idx] = value;
                break;
            }

            case VALUE_D0:
            {
                if (!validate_value_index(_current_session.d[0]))
                {
                    fail(noir_error::value_oob, "value %u is out of range", _current_session.d[0]);
                    break;
                }
                (*_values)[_current_session.d[0]] = value;
                break;
            }

            default: break;
        }
    }

    // ─── Cached destination read / write ────────────────────────────────────────
    //
    // Two-operand opcodes that read-modify-write the destination (arithmetic,
    // logic, MOV) carry their inline data in dst→src order, matching the mode
    // byte layout [dst:4][src:4].
    //
    // read_operand_cached() reads the destination's inline data and keeps it, so
    // write_operand_cached() can write back without reading the stream again.

    u16 noir_vm::read_operand_cached(u8 mode) noexcept
    {
        _cached_dst_mode = mode;
        switch (mode)
        {
            case IMM8: return to<u16>(read_u8());
            case IMM16: return read_u16();
            case REG_D0: return _current_session.d[0];
            case REG_D1: return _current_session.d[1];
            case REG_D2: return _current_session.d[2];
            case REG_D3: return _current_session.d[3];

            case LOCAL:
            {
                i8 offset         = to<i8>(read_u8());
                _cached_dst_param = to<u16>(to<u8>(offset));
                u8* addr          = local_at(offset);
                if (!addr) return 0;
                return to<u16>((to<u16>(addr[0]) << 8) | addr[1]);
            }

            case VALUE_IMM:
            {
                u16 idx           = read_u16();
                _cached_dst_param = idx;
                if (!validate_value_index(idx))
                {
                    fail(noir_error::value_oob, "value %u is out of range", idx);
                    return 0;
                }
                return (*_values)[idx];
            }

            case VALUE_D0:
            {
                if (!validate_value_index(_current_session.d[0]))
                {
                    fail(noir_error::value_oob, "value %u is out of range", _current_session.d[0]);
                    return 0;
                }
                return (*_values)[_current_session.d[0]];
            }

            default: return 0;
        }
    }

    void noir_vm::write_operand_cached(u16 value) noexcept
    {
        switch (_cached_dst_mode)
        {
            case REG_D0: _current_session.d[0] = value; break;
            case REG_D1: _current_session.d[1] = value; break;
            case REG_D2: _current_session.d[2] = value; break;
            case REG_D3: _current_session.d[3] = value; break;

            case LOCAL:
            {
                i8 offset = to<i8>(to<u8>(_cached_dst_param));
                u8* addr  = local_at(offset);
                if (!addr) break;
                addr[0] = to<u8>(value >> 8);
                addr[1] = to<u8>(value & 0xFF);
                break;
            }

            case VALUE_IMM:
            {
                if (!validate_value_index(_cached_dst_param))
                {
                    fail(noir_error::value_oob, "value %u is out of range", _cached_dst_param);
                    break;
                }
                (*_values)[_cached_dst_param] = value;
                break;
            }

            case VALUE_D0:
            {
                if (!validate_value_index(_current_session.d[0]))
                {
                    fail(noir_error::value_oob, "value %u is out of range", _current_session.d[0]);
                    break;
                }
                (*_values)[_current_session.d[0]] = value;
                break;
            }

            default: break;
        }
    }

    // ─── SR flag helpers ────────────────────────────────────────────────────────

    void noir_vm::set_flags_nz(u16 result) noexcept
    {
        _current_session.sr = 0;
        if (result & 0x8000) _current_session.sr |= N;
        if (result == 0) _current_session.sr |= Z;
    }

    void noir_vm::set_flag_result(bool value) noexcept
    {
        _current_session.sr = value ? 0 : Z;
    }

    void noir_vm::set_flags_nzcv_add(u16 a, u16 b, u32 result) noexcept
    {
        u16 r   = to<u16>(result & 0xFFFF);
        _current_session.sr = 0;
        if (r & 0x8000) _current_session.sr |= N;
        if (r == 0) _current_session.sr |= Z;
        if (result > 0xFFFF) _current_session.sr |= C;
        // Signed overflow: both operands same sign, result different sign
        if ((~(a ^ b) & (a ^ r)) & 0x8000) _current_session.sr |= V;
    }

    void noir_vm::set_flags_nzcv_sub(u16 a, u16 b, u32 result) noexcept
    {
        u16 r   = to<u16>(result & 0xFFFF);
        _current_session.sr = 0;
        if (r & 0x8000) _current_session.sr |= N;
        if (r == 0) _current_session.sr |= Z;
        if (a < b) _current_session.sr |= C;  // Borrow occurred
        // Signed overflow: operands different sign, result sign differs from a
        if (((a ^ b) & (a ^ r)) & 0x8000) _current_session.sr |= V;
    }

    // ─── Flag/Value helpers ─────────────────────────────────────────────────────

    bool noir_vm::get_flag(u16 index) const noexcept
    {
        return _flags->test(index);
    }

    void noir_vm::set_flag(u16 index, bool value) noexcept
    {
        _flags->set(index, value);
    }

    bool noir_vm::validate_flag_index(u16 index) const noexcept
    {
        return index < to<u16>(_flags->size() * 8);
    }

    bool noir_vm::validate_value_index(u16 index) const noexcept
    {
        return index < to<u16>(_values->size());
    }

    // ─── Debug ──────────────────────────────────────────────────────────────────

}  // namespace NEONengine
