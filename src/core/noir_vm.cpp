#include "noir_vm.h"

#include "types.h"

#include "core/logger.h"
#include "core/noir_opcodes.h"
#include "mtl/utility.h"

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
        max_stack_used    = 0;
        max_call_depth    = 0;
        max_reentry_level = 0;
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
        , _cur{}
        , _reentry_level(0)
        , _call_depth(0)
        , _opcodes_per_update(10)
        , _blocking_counter(0)
        , _exit_code(0)
        , _last_error(noir_status::ok)
        , _cached_dst_mode(0)
        , _cached_dst_param(0)
    {
        // Grow the vector to fill its capacity so the memory is allocated
        _stack.assign(to<size_t>(stack_size) * SESSION_SLOTS, u8(0));

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

    noir_status noir_vm::execute(u16 func_id) noexcept
    {
        if (func_id >= _func_count)
        {
            NE_LOG("invalid func_id %u (count=%u)", func_id, _func_count);
            return noir_status::err_invalid_func;
        }

        ++_reentry_level;

#ifdef GAME_DEBUG
        if (_reentry_level > _debug_stats.max_reentry_level)
            _debug_stats.max_reentry_level = _reentry_level;
#endif

        // Save all registers on re-entry (reentry_level > 1)
        if (_reentry_level > 1)
        {
            push16(_cur.d[0]);
            push16(_cur.d[1]);
            push16(_cur.d[2]);
            push16(_cur.d[3]);
            push16(to<u16>(_cur.sr));  // padded to 16-bit
            push32(to<u32>(_cur.sp));
            push32(to<u32>(_cur.fp));
            push32(to<u32>(_cur.pc));
        }

        _cur.pc        = to<u16>(_func_toc[func_id]);
        _cur.script_id = func_id;

        // Set up initial stack frame (sentinel return address)
        push32(SENTINEL_PC);
        push32(to<u32>(_cur.fp));

        // A frame that would not fit leaves the session dead; do not revive it.
        if (_cur.state == noir_state::halted) return _last_error;

        _cur.fp = _cur.sp;

        _cur.state = noir_state::running;
        return noir_status::ok;
    }

    // ─── reset_session() ────────────────────────────────────────────────────────

    void noir_vm::reset_session() noexcept
    {
        _cur = noir_session{};

        _stack_base = stack_base_for(_cur.stack_index);

        // SP starts at the top of the stack (grows downward)
        _cur.sp    = _stack_bytes;
        _cur.fp    = _cur.sp;
        _cur.state = noir_state::idle;
    }

    // ─── reset_execution() ────────────────────────────────────────────────────────

    void noir_vm::reset_execution() noexcept
    {
        reset_session();

        _reentry_level    = 0;
        _call_depth       = 0;
        _blocking_counter = 0;

        _exit_code        = 0;
        _last_error       = noir_status::ok;
        _cached_dst_mode  = 0;
        _cached_dst_param = 0;
    }

    // ─── update() ───────────────────────────────────────────────────────────────

    noir_status noir_vm::update() noexcept
    {
        if (_cur.state == noir_state::idle) return noir_status::ok;
        if (_cur.state == noir_state::halted) return _last_error;

        if (_cur.state == noir_state::blocked)
        {
            if (_blocking_counter > 0) return noir_status::yielded;
            _cur.state = noir_state::running;
        }

        if (_cur.state == noir_state::waiting)
        {
            if (_cur.wait_counter > 0)
            {
                --_cur.wait_counter;
                return noir_status::yielded;
            }
            _cur.state = noir_state::running;
        }

        _cur.state             = noir_state::running;
        _cur.instructions_left = _opcodes_per_update;

        while (_cur.instructions_left > 0 && _cur.state == noir_state::running)
        {
#ifdef GAME_DEBUG
            ++_debug_stats.opcode_histogram[_bytecode[_cur.pc]];
#endif
            auto status = execute_one_opcode();
            if (status != noir_status::ok && status != noir_status::yielded
                && status != noir_status::completed)
            {
                _last_error = status;
                _cur.state  = noir_state::halted;
                return status;
            }
            if (_cur.state != noir_state::running) break;
            --_cur.instructions_left;
        }

        if (_cur.state == noir_state::running)
        {
            _cur.state = noir_state::yielded;
            return noir_status::yielded;
        }
        if (_cur.state == noir_state::blocked || _cur.state == noir_state::waiting
            || _cur.state == noir_state::yielded)
        {
            return noir_status::yielded;
        }
        return (_cur.state == noir_state::halted) ? _last_error : noir_status::completed;
    }

    // ─── Opcode dispatch ────────────────────────────────────────────────────────

    noir_status noir_vm::execute_one_opcode() noexcept
    {
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
                return noir_status::err_invalid_opcode;
        }
    }

    // ─── Script Execution opcodes ───────────────────────────────────────────────

    noir_status noir_vm::op_script_exec(u8 op) noexcept
    {
        switch (op)
        {
            case NOP: return noir_status::ok;

            case WAIT:
            {
                u16 frames = read_u16();
                _cur.wait_counter += frames;
                _cur.state = noir_state::waiting;
                return noir_status::ok;
            }

            case HALT:
                NE_LOG("HALT executed");
                _cur.state  = noir_state::halted;
                _last_error = noir_status::completed;
                return noir_status::completed;

            case YIELD: _cur.state = noir_state::yielded; return noir_status::yielded;

            case CALL:
            {
                u16 func_id = read_u16();
                if (func_id >= _func_count)
                {
                    NE_LOG("CALL invalid func_id %u", func_id);
                    return noir_status::err_invalid_func;
                }
                auto target = to<u16>(_func_toc[func_id]);
                push32(to<u32>(_cur.pc));
                push32(to<u32>(_cur.fp));

                if (_cur.state == noir_state::halted) return _last_error;

                _cur.fp = _cur.sp;
                _cur.pc = target;
#ifdef GAME_DEBUG
                ++_call_depth;
                if (_call_depth > _debug_stats.max_call_depth)
                    _debug_stats.max_call_depth = _call_depth;
#endif
                return noir_status::ok;
            }

            case LOCALS:
            {
                u8 n = read_u8();
                if (stack_overflow(n)) return noir_status::err_stack_overflow;
                _cur.sp -= n;
                return noir_status::ok;
            }

            case RET:
                do_ret();
                return (_cur.state == noir_state::idle) ? noir_status::completed : noir_status::ok;

            case RETZ:
                if (_cur.sr & Z)
                {
                    do_ret();
                    return (_cur.state == noir_state::idle) ? noir_status::completed : noir_status::ok;
                }
                return noir_status::ok;

            case RETC:
                if (_cur.sr & C)
                {
                    do_ret();
                    return (_cur.state == noir_state::idle) ? noir_status::completed : noir_status::ok;
                }
                return noir_status::ok;

            default: return noir_status::err_invalid_opcode;
        }
    }

    // ─── Data Movement opcodes ──────────────────────────────────────────────────

    noir_status noir_vm::op_data_move(u8 op) noexcept
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
                return noir_status::ok;
            }

            case MOV16:
            {
                u8 mode     = read_u8();
                u8 dst_mode = mode >> DST_SHIFT;
                u8 src_mode = mode & SRC_MASK;
                read_operand_cached(dst_mode);
                u16 val = read_operand(src_mode);
                write_operand_cached(val);
                return noir_status::ok;
            }

            case XCHG:
            {
                u8 regs = read_u8();
                u8 ra   = (regs >> DST_SHIFT) - REG_D0;
                u8 rb   = (regs & SRC_MASK) - REG_D0;
                u16 tmp = _cur.d[ra & 3];
                _cur.d[ra & 3] = _cur.d[rb & 3];
                _cur.d[rb & 3] = tmp;
                return noir_status::ok;
            }

            case PUSH:
            {
                u8 mode     = read_u8();
                u8 src_mode = mode & SRC_MASK;
                u16 val     = read_operand(src_mode);
                push16(val);
                return noir_status::ok;
            }

            case POP:
            {
                u8 mode     = read_u8();
                u8 dst_mode = mode >> DST_SHIFT;
                u16 val     = pop16();
                write_operand(dst_mode, val);
                return noir_status::ok;
            }

            case TOP:
            {
                u8 mode     = read_u8();
                u8 dst_mode = mode >> DST_SHIFT;
                u16 val     = peek16();
                write_operand(dst_mode, val);
                return noir_status::ok;
            }

            case DUP:
            {
                u16 val = peek16();
                push16(val);
                return noir_status::ok;
            }

            case CLR:
            {
                u8 mode     = read_u8();
                u8 dst_mode = mode >> DST_SHIFT;
                write_operand(dst_mode, 0);
                return noir_status::ok;
            }

            default: return noir_status::err_invalid_opcode;
        }
    }

    // ─── Arithmetic opcodes ─────────────────────────────────────────────────────

    noir_status noir_vm::op_arithmetic(u8 op) noexcept
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
                return noir_status::ok;
            }

            case SUB:
            {
                u16 b      = read_operand(src_mode);
                u32 result = to<u32>(a) - to<u32>(b);
                u16 r      = to<u16>(result & 0xFFFF);
                set_flags_nzcv_sub(a, b, result);
                write_operand_cached(r);
                return noir_status::ok;
            }

            case MUL:
            {
                u16 b   = read_operand(src_mode);
                i32 sa   = to<i16>(a);
                i32 sb   = to<i16>(b);
                i32 full = sa * sb;
                u16 r   = to<u16>(full & 0xFFFF);

                _cur.sr = 0;
                if (r & 0x8000) _cur.sr |= N;
                if (r == 0) _cur.sr |= Z;
                if (full != to<i32>(to<i16>(r))) _cur.sr |= V;
                write_operand_cached(r);
                return noir_status::ok;
            }

            case DIV:
            {
                u16 b = read_operand(src_mode);
                if (b == 0)
                {
                    NE_LOG("division by zero");
                    return noir_status::err_division_zero;
                }
                i16 sa  = to<i16>(a);
                i16 sb  = to<i16>(b);
                i16 r   = sa / sb;
                u16 ur = to<u16>(r);
                set_flags_nz(ur);
                write_operand_cached(ur);
                return noir_status::ok;
            }

            case MOD:
            {
                u16 b = read_operand(src_mode);
                if (b == 0)
                {
                    NE_LOG("modulo by zero");
                    return noir_status::err_division_zero;
                }
                i16 sa  = to<i16>(a);
                i16 sb  = to<i16>(b);
                i16 r   = sa % sb;
                u16 ur = to<u16>(r);
                set_flags_nz(ur);
                write_operand_cached(ur);
                return noir_status::ok;
            }

            case CMP:
            {
                u16 b      = read_operand(src_mode);
                u32 result = to<u32>(a) - to<u32>(b);
                set_flags_nzcv_sub(a, b, result);
                return noir_status::ok;
            }

            case NEG:
            {
                u32 result = to<u32>(-to<i16>(a)) & 0xFFFF;
                u16 r      = to<u16>(result);
                set_flags_nzcv_sub(0, a, to<u32>(0) - to<u32>(a));
                write_operand_cached(r);
                return noir_status::ok;
            }

            case INC:
            {
                u32 result = to<u32>(a) + 1;
                u16 r      = to<u16>(result & 0xFFFF);
                set_flags_nzcv_add(a, 1, result);
                write_operand_cached(r);
                return noir_status::ok;
            }

            case DEC:
            {
                u32 result = to<u32>(a) - 1;
                u16 r      = to<u16>(result & 0xFFFF);
                set_flags_nzcv_sub(a, 1, result);
                write_operand_cached(r);
                return noir_status::ok;
            }

            default: return noir_status::err_invalid_opcode;
        }
    }

    // ─── Logic opcodes ──────────────────────────────────────────────────────────

    noir_status noir_vm::op_logic(u8 op) noexcept
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
                return noir_status::ok;
            }

            case OR:
            {
                u16 b = read_operand(src_mode);
                u16 r = a | b;
                set_flags_nz(r);
                write_operand_cached(r);
                return noir_status::ok;
            }

            case XOR:
            {
                u16 b = read_operand(src_mode);
                u16 r = a ^ b;
                set_flags_nz(r);
                write_operand_cached(r);
                return noir_status::ok;
            }

            case TST:
            {
                u16 b = read_operand(src_mode);
                u16 r = a & b;
                set_flags_nz(r);
                return noir_status::ok;
            }

            case NOT:
            {
                u16 r = ~a;
                set_flags_nz(r);
                write_operand_cached(r);
                return noir_status::ok;
            }

            case SHL:
            {
                u8 count = read_u8();
                _cur.sr = 0;
                if (count > 0 && count <= 16)
                {
                    if (a & (1u << (16 - count))) _cur.sr |= C;
                }
                u16 r = (count < 16) ? to<u16>(a << count) : 0;
                if (r & 0x8000) _cur.sr |= N;
                if (r == 0) _cur.sr |= Z;
                write_operand_cached(r);
                return noir_status::ok;
            }

            case SHR:
            {
                u8 count = read_u8();
                _cur.sr = 0;
                if (count > 0 && count <= 16)
                {
                    if (a & (1u << (count - 1))) _cur.sr |= C;
                }
                u16 r = (count < 16) ? to<u16>(a >> count) : 0;
                if (r & 0x8000) _cur.sr |= N;
                if (r == 0) _cur.sr |= Z;
                write_operand_cached(r);
                return noir_status::ok;
            }

            case ASR:
            {
                u8 count = read_u8();
                i16 sa    = to<i16>(a);
                _cur.sr = 0;
                if (count > 0 && count <= 16)
                {
                    if (a & (1u << (count - 1))) _cur.sr |= C;
                }
                i16 r   = (count < 16) ? to<i16>(sa >> count) : (sa < 0 ? -1 : 0);
                u16 ur = to<u16>(r);
                if (ur & 0x8000) _cur.sr |= N;
                if (ur == 0) _cur.sr |= Z;
                write_operand_cached(ur);
                return noir_status::ok;
            }

            default: return noir_status::err_invalid_opcode;
        }
    }

    // ─── Control Flow opcodes ───────────────────────────────────────────────────

    noir_status noir_vm::op_control_flow(u8 op) noexcept
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
            case BGT: take = ((_cur.sr & Z) == 0) && (((_cur.sr & N) != 0) == ((_cur.sr & V) != 0)); break;
            case BLE: take = ((_cur.sr & Z) != 0) || (((_cur.sr & N) != 0) != ((_cur.sr & V) != 0)); break;
            case BCS: take = (_cur.sr & C) != 0; break;
            case BCC: take = (_cur.sr & C) == 0; break;
            default: return noir_status::err_invalid_opcode;
        }

        if (take)
        {
            _cur.pc = to<u16>(_cur.pc + offset);
        }

        return noir_status::ok;
    }

    // ─── Flag Operations ────────────────────────────────────────────────────────

    noir_status noir_vm::op_flag_ops(u8 op) noexcept
    {
        switch (op)
        {
            // Immediate index variants: [op] [u16 index]
            case SETF:
            {
                u16 idx = read_u16();
                if (!validate_flag_index(idx)) return noir_status::err_flag_oob;
                set_flag(idx, true);
                return noir_status::ok;
            }
            case CLRF:
            {
                u16 idx = read_u16();
                if (!validate_flag_index(idx)) return noir_status::err_flag_oob;
                set_flag(idx, false);
                return noir_status::ok;
            }
            case TOGF:
            {
                u16 idx = read_u16();
                if (!validate_flag_index(idx)) return noir_status::err_flag_oob;
                set_flag(idx, !get_flag(idx));
                return noir_status::ok;
            }
            case TSTF:
            {
                u16 idx = read_u16();
                if (!validate_flag_index(idx)) return noir_status::err_flag_oob;
                // Z=1 when flag is clear, Z=0 when flag is set
                _cur.sr = get_flag(idx) ? 0 : Z;
                return noir_status::ok;
            }

            // Register-indexed variants: [op] [reg_id]
            case SETFR:
            {
                u8 reg  = read_u8();
                u16 idx = _cur.d[reg & 3];
                if (!validate_flag_index(idx)) return noir_status::err_flag_oob;
                set_flag(idx, true);
                return noir_status::ok;
            }
            case CLRFR:
            {
                u8 reg  = read_u8();
                u16 idx = _cur.d[reg & 3];
                if (!validate_flag_index(idx)) return noir_status::err_flag_oob;
                set_flag(idx, false);
                return noir_status::ok;
            }
            case TOGFR:
            {
                u8 reg  = read_u8();
                u16 idx = _cur.d[reg & 3];
                if (!validate_flag_index(idx)) return noir_status::err_flag_oob;
                set_flag(idx, !get_flag(idx));
                return noir_status::ok;
            }
            case TSTFR:
            {
                u8 reg  = read_u8();
                u16 idx = _cur.d[reg & 3];
                if (!validate_flag_index(idx)) return noir_status::err_flag_oob;
                _cur.sr = get_flag(idx) ? 0 : Z;
                return noir_status::ok;
            }

            // Flag ↔ Register transfer: [op] [reg_id] [u16 index]
            case LDFR:
            {
                u8 reg  = read_u8();
                u16 idx = read_u16();
                if (!validate_flag_index(idx)) return noir_status::err_flag_oob;
                u16 val = get_flag(idx) ? 1 : 0;
                _cur.d[reg & 3] = val;
                set_flags_nz(val);
                return noir_status::ok;
            }
            case STFR:
            {
                u8 reg  = read_u8();
                u16 idx = read_u16();
                if (!validate_flag_index(idx)) return noir_status::err_flag_oob;
                set_flag(idx, (_cur.d[reg & 3] & 1) != 0);
                return noir_status::ok;
            }

            default: return noir_status::err_invalid_opcode;
        }
    }

    // ─── Value Operations ───────────────────────────────────────────────────────

    noir_status noir_vm::op_value_ops(u8 op) noexcept
    {
        u16 idx = read_u16();
        if (!validate_value_index(idx)) return noir_status::err_value_oob;

        switch (op)
        {
            case CMPV:
            {
                u16 imm    = read_u16();
                u16 a      = _values[idx];
                u32 result = to<u32>(a) - to<u32>(imm);
                set_flags_nzcv_sub(a, imm, result);
                return noir_status::ok;
            }

            case TSTV:
            {
                set_flags_nz(_values[idx]);
                return noir_status::ok;
            }

            case CLRV:
            {
                _values[idx] = 0;
                return noir_status::ok;
            }

            case INCV:
            {
                u16 a      = _values[idx];
                u32 result = to<u32>(a) + 1;
                u16 r      = to<u16>(result & 0xFFFF);
                set_flags_nzcv_add(a, 1, result);
                _values[idx] = r;
                return noir_status::ok;
            }

            case DECV:
            {
                u16 a      = _values[idx];
                u32 result = to<u32>(a) - 1;
                u16 r      = to<u16>(result & 0xFFFF);
                set_flags_nzcv_sub(a, 1, result);
                _values[idx] = r;
                return noir_status::ok;
            }

            default: return noir_status::err_invalid_opcode;
        }
    }

    // ─── Game Calls — Inventory ─────────────────────────────────────────────────

    noir_status noir_vm::op_game_inventory(u8 op) noexcept
    {
        switch (op)
        {
            case INV_ADD: _bridge->inv_add(_cur.d[0]); return noir_status::ok;

            case INV_REMOVE: _bridge->inv_remove(_cur.d[0]); return noir_status::ok;

            case INV_HAS:
            {
                bool has = _bridge->inv_has(_cur.d[0]);
                _cur.sr  = has ? 0 : Z;
                return noir_status::ok;
            }

            case INV_COUNT: _cur.d[0] = _bridge->inv_count(); return noir_status::ok;

            case INV_CLEAR: _bridge->inv_clear(); return noir_status::ok;

            default: return noir_status::err_invalid_opcode;
        }
    }

    // ─── Game Calls — Timeline ──────────────────────────────────────────────────

    noir_status noir_vm::op_game_timeline(u8 op) noexcept
    {
        switch (op)
        {
            case ANIM_PLAY: _bridge->anim_play(_cur.d[0]); return noir_status::ok;

            case ANIM_STOP: _bridge->anim_stop(_cur.d[0]); return noir_status::ok;

            case ANIM_WAIT:
                _bridge->anim_wait(_cur.d[0]);
                increment_blocking();
                return noir_status::ok;

            default: return noir_status::err_invalid_opcode;
        }
    }

    // ─── Game Calls — Scenes ────────────────────────────────────────────────────

    noir_status noir_vm::op_game_scenes(u8 op) noexcept
    {
        switch (op)
        {
            case GOTO_SCENE:
                _bridge->goto_scene(_cur.d[0]);
                increment_blocking();
                return noir_status::ok;

            case GET_SCENE: _cur.d[0] = _bridge->get_scene(); return noir_status::ok;

            case HOTSPOT_ON: _bridge->hotspot_on(_cur.d[0]); return noir_status::ok;

            case HOTSPOT_OFF: _bridge->hotspot_off(_cur.d[0]); return noir_status::ok;

            case SET_POINTER: _bridge->set_pointer(_cur.d[0]); return noir_status::ok;

            default: return noir_status::err_invalid_opcode;
        }
    }

    // ─── Game Calls — Dialogue / Audio ──────────────────────────────────────────

    noir_status noir_vm::op_game_dialogue(u8 op) noexcept
    {
        switch (op)
        {
            case SAY:
                _bridge->say(_cur.d[0], _cur.d[1]);
                increment_blocking();
                return noir_status::ok;

            case SHOW_DIALOG:
                _bridge->show_dialog(_cur.d[0]);
                increment_blocking();
                return noir_status::ok;

            case SHOW_TEXT: _bridge->show_text(_cur.d[0], _cur.d[1], _cur.d[2]); return noir_status::ok;

            case CLEAR_TEXT: _bridge->clear_text(); return noir_status::ok;

            case PLAY_MUSIC: _bridge->play_music(_cur.d[0]); return noir_status::ok;

            case STOP_MUSIC: _bridge->stop_music(); return noir_status::ok;

            case PLAY_SFX: _bridge->play_sfx(_cur.d[0]); return noir_status::ok;

            default: return noir_status::err_invalid_opcode;
        }
    }

    // ─── Game Calls — Menu / Settings ───────────────────────────────────────────

    noir_status noir_vm::op_game_menu(u8 op) noexcept
    {
        switch (op)
        {
            case FADE_OUT:
                _bridge->fade_out(_cur.d[0]);
                increment_blocking();
                return noir_status::ok;

            case FADE_IN:
                _bridge->fade_in(_cur.d[0]);
                increment_blocking();
                return noir_status::ok;

            case SCREEN_CLR: _bridge->screen_clear(_cur.d[0]); return noir_status::ok;

            case SET_LANG: _bridge->set_lang(_cur.d[0]); return noir_status::ok;

            case SAVE_GAME: _bridge->save_game(_cur.d[0]); return noir_status::ok;

            case LOAD_GAME: _bridge->load_game(_cur.d[0]); return noir_status::ok;

            case NEW_GAME: _bridge->new_game(); return noir_status::ok;

            case QUIT_GAME: _bridge->quit_game(); return noir_status::ok;

            default: return noir_status::err_invalid_opcode;
        }
    }

    // ─── Debug opcodes ──────────────────────────────────────────────────────────

    noir_status noir_vm::op_debug(u8 op) noexcept
    {
#ifdef GAME_DEBUG
        switch (op)
        {
            case DBG_LOG: _bridge->debug_log(_cur.d[0]); break;

            case DBG_LOGS:
            {
                u8 len   = read_u8();
                auto str = force_to<char const*>(_bytecode + _cur.pc);
                _bridge->debug_print(str, len);
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
                NE_LOG("STATS: max_stack=%lu max_call=%u max_reentry=%u",
                    _debug_stats.max_stack_used,
                    _debug_stats.max_call_depth,
                    _debug_stats.max_reentry_level);
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
        return noir_status::ok;
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

        if (_reentry_level > 1)
        {
            // Restore registers saved during execute()
            _cur.pc   = to<u16>(pop32());
            _cur.fp   = to<u16>(pop32());
            _cur.sp   = to<u16>(pop32());
            _cur.sr   = to<u8>(pop16());
            _cur.d[3] = pop16();
            _cur.d[2] = pop16();
            _cur.d[1] = pop16();
            _cur.d[0] = pop16();
            --_reentry_level;
        }
        else if (_reentry_level == 1)
        {
            _exit_code     = _cur.d[0];
            _reentry_level = 0;
            _cur.state     = noir_state::idle;
        }
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
        u32 value     = (to<u32>(p[0]) << 24) | (to<u32>(p[1]) << 16) | (to<u32>(p[2]) << 8)
                    | to<u32>(p[3]);
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
            _last_error = noir_status::err_stack_overflow;
            _cur.state  = noir_state::halted;
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
            _last_error = noir_status::err_stack_underflow;
            _cur.state  = noir_state::halted;
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
                    _last_error = noir_status::err_value_oob;
                    _cur.state  = noir_state::halted;
                    return 0;
                }
                return _values[idx];
            }

            case VALUE_D0:
            {
                if (!validate_value_index(_cur.d[0]))
                {
                    _last_error = noir_status::err_value_oob;
                    _cur.state  = noir_state::halted;
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
                    _last_error = noir_status::err_value_oob;
                    _cur.state  = noir_state::halted;
                    break;
                }
                _values[idx] = value;
                break;
            }

            case VALUE_D0:
            {
                if (!validate_value_index(_cur.d[0]))
                {
                    _last_error = noir_status::err_value_oob;
                    _cur.state  = noir_state::halted;
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
                u16 idx      = read_u16();
                _cached_dst_param = idx;
                if (!validate_value_index(idx))
                {
                    _last_error = noir_status::err_value_oob;
                    _cur.state  = noir_state::halted;
                    return 0;
                }
                return _values[idx];
            }

            case VALUE_D0:
            {
                if (!validate_value_index(_cur.d[0]))
                {
                    _last_error = noir_status::err_value_oob;
                    _cur.state  = noir_state::halted;
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
                    _last_error = noir_status::err_value_oob;
                    _cur.state  = noir_state::halted;
                    break;
                }
                _values[_cached_dst_param] = value;
                break;
            }

            case VALUE_D0:
            {
                if (!validate_value_index(_cur.d[0]))
                {
                    _last_error = noir_status::err_value_oob;
                    _cur.state  = noir_state::halted;
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

    void noir_vm::set_flags_nzcv_add(u16 a, u16 b, u32 result) noexcept
    {
        u16 r = to<u16>(result & 0xFFFF);
        _cur.sr = 0;
        if (r & 0x8000) _cur.sr |= N;
        if (r == 0) _cur.sr |= Z;
        if (result > 0xFFFF) _cur.sr |= C;
        // Signed overflow: both operands same sign, result different sign
        if ((~(a ^ b) & (a ^ r)) & 0x8000) _cur.sr |= V;
    }

    void noir_vm::set_flags_nzcv_sub(u16 a, u16 b, u32 result) noexcept
    {
        u16 r = to<u16>(result & 0xFFFF);
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
