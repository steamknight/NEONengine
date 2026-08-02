#include "noir_vm.h"

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

    /// Pushed under a script's first frame. No bytecode offset can equal it.
    constexpr u32 SENTINEL_PC = 0xFFFFFFFFu;

    /// The PC is 16-bit, so bytecode past this cannot be addressed.
    constexpr u32 MAX_BYTECODE_SIZE = 0x10000u;

    /// Stacks the VM has room for before the vector has to grow.
    constexpr size_t SESSION_SLOTS = 4;

    // ─── Debug stats ────────────────────────────────────────────────────────────

#ifdef GAME_DEBUG
    void noir_debug_stats::reset() noexcept
    {
        max_stack_used = 0;
        max_call_depth = 0;
        for (auto& c : opcode_histogram) c = 0;
    }
#endif

    // ─── Constructor ────────────────────────────────────────────────────────────

    noir_vm::noir_vm(u8* flags,
        u16 flags_size,
        u16* values,
        u16 values_count,
        u8 const* bytecode,
        u32 bytecode_size,
        u32 const* func_toc,
        u16 func_count,
        noir_bridge const* bridge,
        u16 stack_size) noexcept
        : _flags(flags)
        , _flags_size(flags_size)
        , _values(values)
        , _values_count(values_count)
        , _bytecode(bytecode)
        , _bytecode_size(bytecode_size)
        , _func_toc(func_toc)
        , _func_count(func_count)
        , _bridge(bridge)
        , _stack(to<size_t>(stack_size) * SESSION_SLOTS)
        , _stack_base(nullptr)
        , _stack_bytes(stack_size)
        , _stack_slots(SESSION_SLOTS)
        , _sessions()
        , _cur{}
        , _call_depth(0)
        , _session_budget(10)
        , _frame_pool(40)
        , _rotation(0)
        , _vm_state(noir_vm_state::ok)
        , _exit_code(0)
        , _last_error(noir_error::none)
        , _cached_dst_mode(0)
        , _cached_dst_param(0)
    {
        // Grow the vector to fill its capacity so the memory is allocated
        _stack.assign(to<size_t>(stack_size) * SESSION_SLOTS, u8(0));
        _sessions.reserve(SESSION_SLOTS);

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

#ifdef GAME_DEBUG
        _debug_stats.reset();
#endif
    }

    // ─── execute(func_id) ───────────────────────────────────────────────────────

    noir_result noir_vm::execute(u16 func_id) noexcept
    {
        if (func_id >= _func_count)
        {
            NE_LOG("invalid func_id %u (count=%u)", func_id, _func_count);
            return refuse(noir_error::invalid_func);
        }

        for (size_t i = 0; i < _sessions.size(); ++i)
        {
            auto const& s = _sessions[i];
            if (s.script_id == func_id && session_live(s))
            {
                NE_LOG("script %u is already running; request dropped", func_id);
                return refuse(noir_error::duplicate_script);
            }
        }

        u16 const slot = alloc_stack_slot();
        if (!ensure_stack_slots(to<u16>(slot + 1)))
        {
            NE_LOG("no stack for script %u; request dropped", func_id);
            return refuse(noir_error::out_of_memory);
        }

        noir_session started{};
        started.script_id   = func_id;
        started.stack_index = slot;
        started.sp          = _stack_bytes;
        started.fp          = _stack_bytes;
        started.pc          = to<u16>(_func_toc[func_id]);
        started.state       = noir_session_state::running;

        // The budget arrives at the top of the next update, so a session
        // started from inside one does not run until the update after it.
        started.instructions_left = 0;

        // The frame push goes through the running session's window, so the
        // caller's - a bridge function is inside an opcode - has to survive it.
        noir_session const outer = _cur;

        _cur        = started;
        _stack_base = stack_base_for(slot);

        push32(SENTINEL_PC);
        push32(to<u32>(_cur.fp));

        // A frame that would not fit leaves the session dead; do not revive it.
        bool const fits = _cur.state != noir_session_state::halted;
        if (fits)
        {
            _cur.fp = _cur.sp;
            _sessions.push_back(_cur);
        }

        _cur        = outer;
        _stack_base = stack_base_for(_cur.stack_index);

        return fits ? noir_result::ok : noir_result::failed;
    }

    // ─── Stack slots ────────────────────────────────────────────────────────────

    u16 noir_vm::alloc_stack_slot() const noexcept
    {
        for (u16 slot = 0;; ++slot)
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

        // resize() writes through whatever it already has when the allocation
        // is refused, so take the memory first and only grow once it is there.
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
        _cur        = _sessions[index];
        _stack_base = stack_base_for(_cur.stack_index);
    }

    void noir_vm::store_session(size_t index) noexcept
    {
        // A bridge function can drop the run list from inside an instruction,
        // so the entry this session was loaded from may be gone.
        if (index >= _sessions.size()) return;
        _sessions[index] = _cur;
    }

    // ─── reset_session() ────────────────────────────────────────────────────────

    void noir_vm::reset_session() noexcept
    {
        _cur = noir_session{};

        _stack_base = stack_base_for(_cur.stack_index);

        // SP starts at the top of the stack (grows downward)
        _cur.sp    = _stack_bytes;
        _cur.fp    = _cur.sp;
        _cur.state = noir_session_state::halted;
    }

    // ─── reset_execution() ────────────────────────────────────────────────────────

    void noir_vm::reset_execution() noexcept
    {
        _sessions.clear();

        reset_session();

        _call_depth = 0;
        _rotation   = 0;
        _vm_state   = noir_vm_state::ok;

        _exit_code        = 0;
        _last_error       = noir_error::none;
        _cached_dst_mode  = 0;
        _cached_dst_param = 0;
    }

    // ─── update() ───────────────────────────────────────────────────────────────

    noir_result noir_vm::update() noexcept
    {
        // A terminal call ended the scene these sessions belong to. Resuming one
        // would run it in a scene that is already over, so nothing runs until
        // the teardown it asked for has happened.
        if (_vm_state == noir_vm_state::terminated) return noir_result::terminated;

        // Whatever went wrong last update is history.
        _vm_state = noir_vm_state::ok;

        if (_sessions.empty()) return noir_result::ok;

        begin_update_budgets();

        u16 pool           = _frame_pool;
        size_t const start = _rotation % _sessions.size();

        // One walk. Every way a session can stop short of its budget - spending
        // it, parking on a WAIT, making a deferred call, or ending - leaves it
        // unable to run again in this update. A second walk would find the same
        // sessions and turn all of them down.
        for (size_t k = 0; k < _sessions.size() && pool > 0; ++k)
        {
            size_t const i = (start + k) % _sessions.size();
            if (_sessions[i].state != noir_session_state::running) continue;

            load_session(i);

            u16 const budget       = _cur.instructions_left;
            u16 const slice        = (budget < pool) ? budget : pool;
            _cur.instructions_left = slice;

#ifdef GAME_DEBUG
            size_t const before = _sessions.size();
#endif

            auto const result = run_slice();

            // What it did not use goes back for whoever comes after it.
            u16 const used         = to<u16>(slice - _cur.instructions_left);
            _cur.instructions_left = to<u16>(budget - used);
            pool                   = to<u16>(pool - used);

#ifdef GAME_DEBUG
            // Only a terminal call may take the run list away, and only a
            // deferred result can carry one.
            if (_sessions.size() < before && result != noir_result::deferred)
            {
                NE_LOG("a bridge call dropped sessions without a terminal call");
            }
#endif

            // A terminal call leaves nothing worth writing back: the whole run
            // list is about to be discarded.
            if (_vm_state != noir_vm_state::terminated) store_session(i);

            // A deferred call ends the walk, which is what stops the next
            // session - and this one - running again this frame.
            if (result == noir_result::deferred) break;
        }

        ++_rotation;
        remove_dead_sessions();

        // A terminal call ends the walk, so nothing can fail after one: if both
        // happened, the terminal call was last and its verdict stands.
        switch (_vm_state)
        {
            case noir_vm_state::terminated: return noir_result::terminated;
            case noir_vm_state::failed: return noir_result::failed;
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
        while (_cur.instructions_left > 0 && _cur.state == noir_session_state::running)
        {
            auto const result = execute_instruction();

            // A deferred call has already run and the program counter has
            // already moved on, so the session stays runnable, parked on the
            // instruction after it. A failure has already killed the session.
            if (result == noir_result::deferred || result == noir_result::failed) return result;
        }
        return noir_result::ok;
    }

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
        // Charged first, so every way the instruction can end - running,
        // deferring, or killing the session - costs the same one.
        --_cur.instructions_left;

#ifdef GAME_DEBUG
        ++_debug_stats.opcode_histogram[_bytecode[_cur.pc]];
#endif

        u8 opcode   = read_u8();
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
            default:
                NE_LOG("invalid opcode 0x%02X", opcode);
                return fail(noir_error::invalid_opcode);
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
                _cur.wait_counter += frames;
                _cur.state = noir_session_state::waiting;
                return noir_result::ok;
            }

            case HALT:
                // Not "finished" - a case the author says cannot happen. It
                // kills this session and reports, like any other script error.
                NE_LOG("HALT executed in script %hu", _cur.script_id);
                return fail(noir_error::halt);

            case CALL:
            {
                u16 func_id = read_u16();
                if (func_id >= _func_count)
                {
                    NE_LOG("CALL invalid func_id %u", func_id);
                    return fail(noir_error::invalid_func);
                }
                auto target = to<u16>(_func_toc[func_id]);
                push32(to<u32>(_cur.pc));
                push32(to<u32>(_cur.fp));

                if (_cur.state == noir_session_state::halted) return noir_result::failed;

                _cur.fp = _cur.sp;
                _cur.pc = target;
#ifdef GAME_DEBUG
                ++_call_depth;
                if (_call_depth > _debug_stats.max_call_depth)
                    _debug_stats.max_call_depth = _call_depth;
#endif
                return noir_result::ok;
            }

            case LOCALS:
            {
                u8 n = read_u8();
                if (stack_overflow(n)) return fail(noir_error::stack_overflow);
                _cur.sp -= n;
                return noir_result::ok;
            }

            case RET: do_ret(); return noir_result::ok;

            case RETZ:
                if (_cur.sr & Z)
                {
                    do_ret();
                    return noir_result::ok;
                }
                return noir_result::ok;

            case RETC:
                if (_cur.sr & C)
                {
                    do_ret();
                    return noir_result::ok;
                }
                return noir_result::ok;

            default: return fail(noir_error::invalid_opcode);
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
                u16 tmp        = _cur.d[ra & 3];
                _cur.d[ra & 3] = _cur.d[rb & 3];
                _cur.d[rb & 3] = tmp;
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

            default: return fail(noir_error::invalid_opcode);
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

                _cur.sr = 0;
                if (r & 0x8000) _cur.sr |= N;
                if (r == 0) _cur.sr |= Z;
                if (full != to<i32>(to<i16>(r))) _cur.sr |= V;
                write_operand_cached(r);
                return noir_result::ok;
            }

            case DIV:
            {
                u16 b = read_operand(src_mode);
                if (b == 0)
                {
                    NE_LOG("division by zero");
                    return fail(noir_error::division_zero);
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
                    NE_LOG("modulo by zero");
                    return fail(noir_error::division_zero);
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

            default: return fail(noir_error::invalid_opcode);
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
                _cur.sr  = 0;
                if (count > 0 && count <= 16)
                {
                    if (a & (1u << (16 - count))) _cur.sr |= C;
                }
                u16 r = (count < 16) ? to<u16>(a << count) : 0;
                if (r & 0x8000) _cur.sr |= N;
                if (r == 0) _cur.sr |= Z;
                write_operand_cached(r);
                return noir_result::ok;
            }

            case SHR:
            {
                u8 count = read_u8();
                _cur.sr  = 0;
                if (count > 0 && count <= 16)
                {
                    if (a & (1u << (count - 1))) _cur.sr |= C;
                }
                u16 r = (count < 16) ? to<u16>(a >> count) : 0;
                if (r & 0x8000) _cur.sr |= N;
                if (r == 0) _cur.sr |= Z;
                write_operand_cached(r);
                return noir_result::ok;
            }

            case ASR:
            {
                u8 count = read_u8();
                i16 sa   = to<i16>(a);
                _cur.sr  = 0;
                if (count > 0 && count <= 16)
                {
                    if (a & (1u << (count - 1))) _cur.sr |= C;
                }
                i16 r  = (count < 16) ? to<i16>(sa >> count) : (sa < 0 ? -1 : 0);
                u16 ur = to<u16>(r);
                if (ur & 0x8000) _cur.sr |= N;
                if (ur == 0) _cur.sr |= Z;
                write_operand_cached(ur);
                return noir_result::ok;
            }

            default: return fail(noir_error::invalid_opcode);
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
            case BEQ: take = (_cur.sr & Z) != 0; break;
            case BNE: take = (_cur.sr & Z) == 0; break;
            case BLT: take = ((_cur.sr & N) != 0) != ((_cur.sr & V) != 0); break;
            case BGE: take = ((_cur.sr & N) != 0) == ((_cur.sr & V) != 0); break;
            case BGT:
                take = ((_cur.sr & Z) == 0) && (((_cur.sr & N) != 0) == ((_cur.sr & V) != 0));
                break;
            case BLE:
                take = ((_cur.sr & Z) != 0) || (((_cur.sr & N) != 0) != ((_cur.sr & V) != 0));
                break;
            case BCS: take = (_cur.sr & C) != 0; break;
            case BCC: take = (_cur.sr & C) == 0; break;
            default: return fail(noir_error::invalid_opcode);
        }

        if (take)
        {
            _cur.pc = to<u16>(_cur.pc + offset);
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
                if (!validate_flag_index(idx)) return fail(noir_error::flag_oob);
                set_flag(idx, true);
                return noir_result::ok;
            }
            case CLRF:
            {
                u16 idx = read_u16();
                if (!validate_flag_index(idx)) return fail(noir_error::flag_oob);
                set_flag(idx, false);
                return noir_result::ok;
            }
            case TOGF:
            {
                u16 idx = read_u16();
                if (!validate_flag_index(idx)) return fail(noir_error::flag_oob);
                set_flag(idx, !get_flag(idx));
                return noir_result::ok;
            }
            case TSTF:
            {
                u16 idx = read_u16();
                if (!validate_flag_index(idx)) return fail(noir_error::flag_oob);
                // Z=1 when flag is clear, Z=0 when flag is set
                _cur.sr = get_flag(idx) ? 0 : Z;
                return noir_result::ok;
            }

            // Register-indexed variants: [op] [reg_id]
            case SETFR:
            {
                u8 reg  = read_u8();
                u16 idx = _cur.d[reg & 3];
                if (!validate_flag_index(idx)) return fail(noir_error::flag_oob);
                set_flag(idx, true);
                return noir_result::ok;
            }
            case CLRFR:
            {
                u8 reg  = read_u8();
                u16 idx = _cur.d[reg & 3];
                if (!validate_flag_index(idx)) return fail(noir_error::flag_oob);
                set_flag(idx, false);
                return noir_result::ok;
            }
            case TOGFR:
            {
                u8 reg  = read_u8();
                u16 idx = _cur.d[reg & 3];
                if (!validate_flag_index(idx)) return fail(noir_error::flag_oob);
                set_flag(idx, !get_flag(idx));
                return noir_result::ok;
            }
            case TSTFR:
            {
                u8 reg  = read_u8();
                u16 idx = _cur.d[reg & 3];
                if (!validate_flag_index(idx)) return fail(noir_error::flag_oob);
                _cur.sr = get_flag(idx) ? 0 : Z;
                return noir_result::ok;
            }

            // Flag ↔ Register transfer: [op] [reg_id] [u16 index]
            case LDFR:
            {
                u8 reg  = read_u8();
                u16 idx = read_u16();
                if (!validate_flag_index(idx)) return fail(noir_error::flag_oob);
                u16 val         = get_flag(idx) ? 1 : 0;
                _cur.d[reg & 3] = val;
                set_flags_nz(val);
                return noir_result::ok;
            }
            case STFR:
            {
                u8 reg  = read_u8();
                u16 idx = read_u16();
                if (!validate_flag_index(idx)) return fail(noir_error::flag_oob);
                set_flag(idx, (_cur.d[reg & 3] & 1) != 0);
                return noir_result::ok;
            }

            default: return fail(noir_error::invalid_opcode);
        }
    }

    // ─── Value Operations ───────────────────────────────────────────────────────

    noir_result noir_vm::op_value_ops(u8 op) noexcept
    {
        u16 idx = read_u16();
        if (!validate_value_index(idx)) return fail(noir_error::value_oob);

        switch (op)
        {
            case CMPV:
            {
                u16 imm    = read_u16();
                u16 a      = _values[idx];
                u32 result = to<u32>(a) - to<u32>(imm);
                set_flags_nzcv_sub(a, imm, result);
                return noir_result::ok;
            }

            case TSTV:
            {
                set_flags_nz(_values[idx]);
                return noir_result::ok;
            }

            case CLRV:
            {
                _values[idx] = 0;
                return noir_result::ok;
            }

            case INCV:
            {
                u16 a      = _values[idx];
                u32 result = to<u32>(a) + 1;
                u16 r      = to<u16>(result & 0xFFFF);
                set_flags_nzcv_add(a, 1, result);
                _values[idx] = r;
                return noir_result::ok;
            }

            case DECV:
            {
                u16 a      = _values[idx];
                u32 result = to<u32>(a) - 1;
                u16 r      = to<u16>(result & 0xFFFF);
                set_flags_nzcv_sub(a, 1, result);
                _values[idx] = r;
                return noir_result::ok;
            }

            default: return fail(noir_error::invalid_opcode);
        }
    }

    // ─── Game Calls — Inventory ─────────────────────────────────────────────────

    noir_result noir_vm::op_game_inventory(u8 op) noexcept
    {
        switch (op)
        {
            case INV_ADD: return call(_bridge->inv_add, _cur.d[0]);

            case INV_REMOVE: return call(_bridge->inv_remove, _cur.d[0]);

            case INV_HAS: return call_flag(_bridge->inv_has, _cur.d[0]);

            case INV_COUNT: return call_result(_bridge->inv_count);

            case INV_CLEAR: return call(_bridge->inv_clear);

            default: return fail(noir_error::invalid_opcode);
        }
    }

    // ─── Game Calls — Timeline ──────────────────────────────────────────────────

    noir_result noir_vm::op_game_timeline(u8 op) noexcept
    {
        switch (op)
        {
            case ANIM_PLAY: return call(_bridge->anim_play, _cur.d[0]);

            case ANIM_STOP: return call(_bridge->anim_stop, _cur.d[0]);

            default: return fail(noir_error::invalid_opcode);
        }
    }

    // ─── Game Calls — Scenes ────────────────────────────────────────────────────

    noir_result noir_vm::op_game_scenes(u8 op) noexcept
    {
        switch (op)
        {
            case GOTO_SCENE: return call(_bridge->goto_scene, _cur.d[0]);

            case GET_SCENE: return call_result(_bridge->get_scene);

            case HOTSPOT_ON: return call(_bridge->hotspot_on, _cur.d[0]);

            case HOTSPOT_OFF: return call(_bridge->hotspot_off, _cur.d[0]);

            case SET_POINTER: return call(_bridge->set_pointer, _cur.d[0]);

            default: return fail(noir_error::invalid_opcode);
        }
    }

    // ─── Game Calls — Dialogue / Audio ──────────────────────────────────────────

    noir_result noir_vm::op_game_dialogue(u8 op) noexcept
    {
        switch (op)
        {
            case SAY: return call(_bridge->say, _cur.d[0], _cur.d[1]);

            case SHOW_DIALOG: return call(_bridge->show_dialog, _cur.d[0]);

            case SHOW_TEXT: return call(_bridge->show_text, _cur.d[0], _cur.d[1], _cur.d[2]);

            case CLEAR_TEXT: return call(_bridge->clear_text);

            case PLAY_MUSIC: return call(_bridge->play_music, _cur.d[0]);

            case STOP_MUSIC: return call(_bridge->stop_music);

            case PLAY_SFX: return call(_bridge->play_sfx, _cur.d[0]);

            default: return fail(noir_error::invalid_opcode);
        }
    }

    // ─── Game Calls — Menu / Settings ───────────────────────────────────────────

    noir_result noir_vm::op_game_menu(u8 op) noexcept
    {
        switch (op)
        {
            case FADE_OUT: return call(_bridge->fade_out, _cur.d[0]);

            case FADE_IN: return call(_bridge->fade_in, _cur.d[0]);

            case SCREEN_CLR: return call(_bridge->screen_clear, _cur.d[0]);

            case SET_LANG: return call(_bridge->set_lang, _cur.d[0]);

            case SAVE_GAME: return call(_bridge->save_game, _cur.d[0]);

            case LOAD_GAME: return call(_bridge->load_game, _cur.d[0]);

            case NEW_GAME: return call(_bridge->new_game);

            case QUIT_GAME: return call(_bridge->quit_game);

            default: return fail(noir_error::invalid_opcode);
        }
    }

    // ─── Debug opcodes ──────────────────────────────────────────────────────────

    noir_result noir_vm::op_debug(u8 op) noexcept
    {
#ifdef GAME_DEBUG
        switch (op)
        {
            case DBG_LOG: call(_bridge->debug_log, _cur.d[0]); break;

            case DBG_LOGS:
            {
                u8 len   = read_u8();
                auto str = force_to<char const*>(_bytecode + _cur.pc);
                call(_bridge->debug_print, str, len);
                _cur.pc = to<u16>(_cur.pc + len);
                break;
            }

            case DBG_REG:
                NE_LOG("REG: D0=%04X D1=%04X D2=%04X D3=%04X SR=%02X",
                    _cur.d[0],
                    _cur.d[1],
                    _cur.d[2],
                    _cur.d[3],
                    _cur.sr);
                NE_LOG("REG: SP=%04X FP=%04X PC=%04X", _cur.sp, _cur.fp, _cur.pc);
                break;

            case DBG_STK:
            {
                u8 const* p   = stack_at(_cur.sp);
                u8 const* end = _stack.data() + _stack.size();
                NE_LOG("STK: (top → bottom, up to 16 bytes)");
                for (int i = 0; i < 16 && p < end; ++i, ++p) NE_LOG("  [SP+%d] = %02X", i, *p);
                break;
            }

            case DBG_STAT:
                NE_LOG("STATS: max_stack=%lu max_call=%u",
                    _debug_stats.max_stack_used,
                    _debug_stats.max_call_depth);
                break;

            case DBG_FLAG:
            {
                if (_cur.d[0] == 0xFFFF)
                {
                    u16 total = to<u16>(_flags_size * 8);
                    NE_LOG("FLAGS: (all %u)", total);
                    for (u16 i = 0; i < total; ++i)
                    {
                        if (get_flag(i)) NE_LOG("  flag[%u] = 1", i);
                    }
                }
                else
                {
                    if (validate_flag_index(_cur.d[0]))
                        NE_LOG("FLAG[%u] = %u", _cur.d[0], get_flag(_cur.d[0]) ? 1 : 0);
                }
                break;
            }

            case DBG_VAL:
            {
                if (_cur.d[0] == 0xFFFF)
                {
                    NE_LOG("VALUES: (all %u)", _values_count);
                    for (u16 i = 0; i < _values_count; ++i)
                        NE_LOG("  value[%u] = %u", i, _values[i]);
                }
                else
                {
                    if (validate_value_index(_cur.d[0]))
                        NE_LOG("VALUE[%u] = %u", _cur.d[0], _values[_cur.d[0]]);
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
            _cur.pc = to<u16>(_cur.pc + len);
        }
#endif
        return noir_result::ok;
    }

    // ─── RET implementation ─────────────────────────────────────────────────────

    void noir_vm::do_ret() noexcept
    {
        _cur.sp = _cur.fp;
        _cur.fp = to<u16>(pop32());

        u32 return_pc = pop32();
        _cur.pc       = to<u16>(return_pc);

#ifdef GAME_DEBUG
        if (_call_depth > 0) --_call_depth;
#endif

        if (return_pc != SENTINEL_PC) return;

        _exit_code = _cur.d[0];
        _cur.state = noir_session_state::halted;
    }

    // ─── Stack operations ───────────────────────────────────────────────────────

    void noir_vm::push16(u16 value) noexcept
    {
        if (stack_overflow(2)) return;
        _cur.sp -= 2;
        auto* p = stack_at(_cur.sp);
        p[0]    = to<u8>(value >> 8);  // big-endian
        p[1]    = to<u8>(value & 0xFF);
    }

    void noir_vm::push32(u32 value) noexcept
    {
        if (stack_overflow(4)) return;
        _cur.sp -= 4;
        auto* p = stack_at(_cur.sp);
        p[0]    = to<u8>(value >> 24);
        p[1]    = to<u8>(value >> 16);
        p[2]    = to<u8>(value >> 8);
        p[3]    = to<u8>(value & 0xFF);
    }

    u16 noir_vm::pop16() noexcept
    {
        if (stack_underflow(2)) return 0;
        auto const* p = stack_at(_cur.sp);
        u16 value     = to<u16>((to<u16>(p[0]) << 8) | p[1]);
        _cur.sp += 2;
        return value;
    }

    u32 noir_vm::pop32() noexcept
    {
        if (stack_underflow(4)) return 0;
        auto const* p = stack_at(_cur.sp);
        u32 value
            = (to<u32>(p[0]) << 24) | (to<u32>(p[1]) << 16) | (to<u32>(p[2]) << 8) | to<u32>(p[3]);
        _cur.sp += 4;
        return value;
    }

    u16 noir_vm::peek16() noexcept
    {
        if (stack_underflow(2)) return 0;
        auto const* p = stack_at(_cur.sp);
        return to<u16>((to<u16>(p[0]) << 8) | p[1]);
    }

    bool noir_vm::stack_overflow(u16 bytes) noexcept
    {
        if (_cur.sp < bytes)
        {
            NE_LOG("script %u overran its %u byte stack", _cur.script_id, _stack_bytes);
            fail(noir_error::stack_overflow);
            return true;
        }

#ifdef GAME_DEBUG
        u16 used = to<u16>(_stack_bytes - (_cur.sp - bytes));
        if (used > _debug_stats.max_stack_used) _debug_stats.max_stack_used = used;
#endif
        return false;
    }

    bool noir_vm::stack_underflow(u16 bytes) noexcept
    {
        if (_cur.sp + bytes > _stack_bytes)
        {
            NE_LOG("script %u popped more than it pushed", _cur.script_id);
            fail(noir_error::stack_underflow);
            return true;
        }
        return false;
    }

    // ─── Bytecode reading ───────────────────────────────────────────────────────

    u8 noir_vm::read_u8() noexcept
    {
        return _bytecode[_cur.pc++];
    }

    u16 noir_vm::read_u16() noexcept
    {
        u16 hi = _bytecode[_cur.pc++];
        u16 lo = _bytecode[_cur.pc++];
        return to<u16>((hi << 8) | lo);
    }

    i16 noir_vm::read_s16() noexcept
    {
        return to<i16>(read_u16());
    }

    // ─── Mode byte helpers ──────────────────────────────────────────────────────

    u16 noir_vm::read_operand(u8 mode) noexcept
    {
        switch (mode)
        {
            case IMM8: return to<u16>(read_u8());
            case IMM16: return read_u16();
            case REG_D0: return _cur.d[0];
            case REG_D1: return _cur.d[1];
            case REG_D2: return _cur.d[2];
            case REG_D3: return _cur.d[3];

            case LOCAL:
            {
                i8 offset = to<i8>(read_u8());
                u8* addr  = stack_at(_cur.fp) + offset;
                return to<u16>((to<u16>(addr[0]) << 8) | addr[1]);
            }

            case VALUE_IMM:
            {
                u16 idx = read_u16();
                if (!validate_value_index(idx))
                {
                    _last_error = noir_error::value_oob;
                    _cur.state  = noir_session_state::halted;
                    return 0;
                }
                return _values[idx];
            }

            case VALUE_D0:
            {
                if (!validate_value_index(_cur.d[0]))
                {
                    _last_error = noir_error::value_oob;
                    _cur.state  = noir_session_state::halted;
                    return 0;
                }
                return _values[_cur.d[0]];
            }

            default: return 0;
        }
    }

    void noir_vm::write_operand(u8 mode, u16 value) noexcept
    {
        switch (mode)
        {
            case REG_D0: _cur.d[0] = value; break;
            case REG_D1: _cur.d[1] = value; break;
            case REG_D2: _cur.d[2] = value; break;
            case REG_D3: _cur.d[3] = value; break;

            case LOCAL:
            {
                i8 offset = to<i8>(read_u8());
                u8* addr  = stack_at(_cur.fp) + offset;
                addr[0]   = to<u8>(value >> 8);
                addr[1]   = to<u8>(value & 0xFF);
                break;
            }

            case VALUE_IMM:
            {
                u16 idx = read_u16();
                if (!validate_value_index(idx))
                {
                    _last_error = noir_error::value_oob;
                    _cur.state  = noir_session_state::halted;
                    break;
                }
                _values[idx] = value;
                break;
            }

            case VALUE_D0:
            {
                if (!validate_value_index(_cur.d[0]))
                {
                    _last_error = noir_error::value_oob;
                    _cur.state  = noir_session_state::halted;
                    break;
                }
                _values[_cur.d[0]] = value;
                break;
            }

            default: break;
        }
    }

    // ─── Cached destination read / write ────────────────────────────────────────
    //
    // For two-operand opcodes that read-modify-write the destination (arithmetic,
    // logic, MOV), inline data in the bytecode stream follows dst→src order
    // (matching the mode byte layout [dst:4][src:4]).
    //
    // read_operand_cached() consumes the destination's inline data and caches it
    // so that write_operand_cached() can write back without re-reading the stream.

    u16 noir_vm::read_operand_cached(u8 mode) noexcept
    {
        _cached_dst_mode = mode;
        switch (mode)
        {
            case IMM8: return to<u16>(read_u8());
            case IMM16: return read_u16();
            case REG_D0: return _cur.d[0];
            case REG_D1: return _cur.d[1];
            case REG_D2: return _cur.d[2];
            case REG_D3: return _cur.d[3];

            case LOCAL:
            {
                i8 offset         = to<i8>(read_u8());
                _cached_dst_param = to<u16>(to<u8>(offset));
                u8* addr          = stack_at(_cur.fp) + offset;
                return to<u16>((to<u16>(addr[0]) << 8) | addr[1]);
            }

            case VALUE_IMM:
            {
                u16 idx           = read_u16();
                _cached_dst_param = idx;
                if (!validate_value_index(idx))
                {
                    _last_error = noir_error::value_oob;
                    _cur.state  = noir_session_state::halted;
                    return 0;
                }
                return _values[idx];
            }

            case VALUE_D0:
            {
                if (!validate_value_index(_cur.d[0]))
                {
                    _last_error = noir_error::value_oob;
                    _cur.state  = noir_session_state::halted;
                    return 0;
                }
                return _values[_cur.d[0]];
            }

            default: return 0;
        }
    }

    void noir_vm::write_operand_cached(u16 value) noexcept
    {
        switch (_cached_dst_mode)
        {
            case REG_D0: _cur.d[0] = value; break;
            case REG_D1: _cur.d[1] = value; break;
            case REG_D2: _cur.d[2] = value; break;
            case REG_D3: _cur.d[3] = value; break;

            case LOCAL:
            {
                i8 offset = to<i8>(to<u8>(_cached_dst_param));
                u8* addr  = stack_at(_cur.fp) + offset;
                addr[0]   = to<u8>(value >> 8);
                addr[1]   = to<u8>(value & 0xFF);
                break;
            }

            case VALUE_IMM:
            {
                if (!validate_value_index(_cached_dst_param))
                {
                    _last_error = noir_error::value_oob;
                    _cur.state  = noir_session_state::halted;
                    break;
                }
                _values[_cached_dst_param] = value;
                break;
            }

            case VALUE_D0:
            {
                if (!validate_value_index(_cur.d[0]))
                {
                    _last_error = noir_error::value_oob;
                    _cur.state  = noir_session_state::halted;
                    break;
                }
                _values[_cur.d[0]] = value;
                break;
            }

            default: break;
        }
    }

    // ─── SR flag helpers ────────────────────────────────────────────────────────

    void noir_vm::set_flags_nz(u16 result) noexcept
    {
        _cur.sr = 0;
        if (result & 0x8000) _cur.sr |= N;
        if (result == 0) _cur.sr |= Z;
    }

    void noir_vm::set_flag_result(bool value) noexcept
    {
        _cur.sr = value ? 0 : Z;
    }

    void noir_vm::set_flags_nzcv_add(u16 a, u16 b, u32 result) noexcept
    {
        u16 r   = to<u16>(result & 0xFFFF);
        _cur.sr = 0;
        if (r & 0x8000) _cur.sr |= N;
        if (r == 0) _cur.sr |= Z;
        if (result > 0xFFFF) _cur.sr |= C;
        // Signed overflow: both operands same sign, result different sign
        if ((~(a ^ b) & (a ^ r)) & 0x8000) _cur.sr |= V;
    }

    void noir_vm::set_flags_nzcv_sub(u16 a, u16 b, u32 result) noexcept
    {
        u16 r   = to<u16>(result & 0xFFFF);
        _cur.sr = 0;
        if (r & 0x8000) _cur.sr |= N;
        if (r == 0) _cur.sr |= Z;
        if (a < b) _cur.sr |= C;  // Borrow occurred
        // Signed overflow: operands different sign, result sign differs from a
        if (((a ^ b) & (a ^ r)) & 0x8000) _cur.sr |= V;
    }

    // ─── Flag/Value helpers ─────────────────────────────────────────────────────

    bool noir_vm::get_flag(u16 index) const noexcept
    {
        u16 byte_idx = index / 8;
        u8 bit_idx   = to<u8>(index % 8);
        return (_flags[byte_idx] & (1u << bit_idx)) != 0;
    }

    void noir_vm::set_flag(u16 index, bool value) noexcept
    {
        u16 byte_idx = index / 8;
        u8 bit_idx   = to<u8>(index % 8);
        if (value)
            _flags[byte_idx] |= to<u8>(1u << bit_idx);
        else
            _flags[byte_idx] &= to<u8>(~(1u << bit_idx));
    }

    bool noir_vm::validate_flag_index(u16 index) const noexcept
    {
        if (index < to<u16>(_flags_size * 8)) return true;
        NE_LOG("flag index %u out of bounds (max=%u)", index, _flags_size * 8);
        return false;
    }

    bool noir_vm::validate_value_index(u16 index) const noexcept
    {
        if (index < _values_count) return true;
        NE_LOG("value index %u out of bounds (max=%u)", index, _values_count);
        return false;
    }

    // ─── Debug ──────────────────────────────────────────────────────────────────

}  // namespace NEONengine
