#include "noir_vm.h"

#include "core/logger.h"
#include "core/noir_opcodes.h"
#include "mtl/utility.h"

NE_LOG_DOMAIN(Script);

namespace NEONengine
{
    using namespace noir_op;
    using namespace noir_mode;
    using namespace noir_sr;

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

    noir_vm::noir_vm(uint8_t* flags,
        uint16_t flags_size,
        uint16_t* values,
        uint16_t values_count,
        uint8_t const* bytecode,
        uint32_t bytecode_size,
        uint32_t const* func_toc,
        uint16_t func_count,
        noir_bridge const* bridge,
        uint16_t stack_size) noexcept
        : _flags(flags)
        , _flags_size(flags_size)
        , _values(values)
        , _values_count(values_count)
        , _bytecode(bytecode)
        , _bytecode_size(bytecode_size)
        , _func_toc(func_toc)
        , _func_count(func_count)
        , _bridge(bridge)
        , _stack(mtl::to<size_t>(stack_size))
        , _d{ 0, 0, 0, 0 }
        , _sp(nullptr)
        , _fp(nullptr)
        , _pc(nullptr)
        , _sr(0)
        , _reentry_level(0)
        , _call_depth(0)
        , _opcodes_per_update(10)
        , _opcodes_remaining(0)
        , _blocking_counter(0)
        , _wait_counter(0)
        , _exit_code(0)
        , _last_error(noir_status::ok)
        , _state(noir_state::idle)
        , _cached_dst_mode(0)
        , _cached_dst_param(0)
    {
        // Grow the vector to fill its capacity so the memory is allocated
        _stack.assign(mtl::to<size_t>(stack_size), uint8_t(0));

        // SP starts at the top of the stack (grows downward)
        _sp = _stack.data() + _stack.size();
        _fp = _sp;

#ifdef GAME_DEBUG
        _debug_stats.reset();
#endif
    }

    // ─── execute(func_id) ───────────────────────────────────────────────────────

    noir_status noir_vm::execute(uint16_t func_id) noexcept
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
            push16(_d[0]);
            push16(_d[1]);
            push16(_d[2]);
            push16(_d[3]);
            push16(mtl::to<uint16_t>(_sr));  // padded to 16-bit
            push32(mtl::force_to<uint32_t>(_sp));
            push32(mtl::force_to<uint32_t>(_fp));
            push32(mtl::force_to<uint32_t>(_pc));
        }

        // Set PC to function entry
        _pc = _bytecode + _func_toc[func_id];

        // Set up initial stack frame (sentinel return address)
        push32(0x00000000);  // sentinel return PC
        push32(mtl::force_to<uint32_t>(_fp));
        _fp = _sp;

        _state = noir_state::running;
        return noir_status::ok;
    }

    // ─── reset_execution() ────────────────────────────────────────────────────────

    void noir_vm::reset_execution() noexcept
    {
        _d[0] = 0;
        _d[1] = 0;
        _d[2] = 0;
        _d[3] = 0;
        _sr   = 0;

        _sp = _stack.data() + _stack.size();
        _fp = _sp;
        _pc = nullptr;

        _reentry_level     = 0;
        _call_depth        = 0;
        _opcodes_remaining = 0;
        _blocking_counter  = 0;
        _wait_counter      = 0;

        _exit_code        = 0;
        _last_error       = noir_status::ok;
        _state            = noir_state::idle;
        _cached_dst_mode  = 0;
        _cached_dst_param = 0;
    }

    // ─── update() ───────────────────────────────────────────────────────────────

    noir_status noir_vm::update() noexcept
    {
        if (_state == noir_state::idle) return noir_status::ok;
        if (_state == noir_state::halted) return _last_error;

        // Check if blocked ops have completed
        if (_state == noir_state::blocked)
        {
            if (_blocking_counter > 0) return noir_status::yielded;
            _state = noir_state::running;
        }

        // Check if WAIT timer has expired
        if (_state == noir_state::waiting)
        {
            if (_wait_counter > 0)
            {
                --_wait_counter;
                return noir_status::yielded;
            }
            _state = noir_state::running;
        }

        _state             = noir_state::running;
        _opcodes_remaining = _opcodes_per_update;

        while (_opcodes_remaining > 0 && _state == noir_state::running)
        {
#ifdef GAME_DEBUG
            ++_debug_stats.opcode_histogram[*_pc];
            update_stack_high_water();
#endif
            auto status = execute_one_opcode();
            if (status != noir_status::ok && status != noir_status::yielded
                && status != noir_status::completed)
            {
                _last_error = status;
                _state      = noir_state::halted;
                return status;
            }
            if (_state != noir_state::running) break;
            --_opcodes_remaining;
        }

        if (_state == noir_state::running)
        {
            _state = noir_state::yielded;
            return noir_status::yielded;
        }
        if (_state == noir_state::blocked || _state == noir_state::waiting
            || _state == noir_state::yielded)
        {
            return noir_status::yielded;
        }
        return (_state == noir_state::halted) ? _last_error : noir_status::completed;
    }

    // ─── Opcode dispatch ────────────────────────────────────────────────────────

    noir_status noir_vm::execute_one_opcode() noexcept
    {
        uint8_t opcode   = read_u8();
        uint8_t category = opcode >> 4;

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

    noir_status noir_vm::op_script_exec(uint8_t op) noexcept
    {
        switch (op)
        {
            case NOP: return noir_status::ok;

            case WAIT:
            {
                uint16_t frames = read_u16();
                _wait_counter += frames;
                _state = noir_state::waiting;
                return noir_status::ok;
            }

            case HALT:
                NE_LOG("HALT executed");
                _state      = noir_state::halted;
                _last_error = noir_status::completed;
                return noir_status::completed;

            case YIELD: _state = noir_state::yielded; return noir_status::yielded;

            case CALL:
            {
                uint16_t func_id = read_u16();
                if (func_id >= _func_count)
                {
                    NE_LOG("CALL invalid func_id %u", func_id);
                    return noir_status::err_invalid_func;
                }
                auto target = _bytecode + _func_toc[func_id];
                push32(mtl::force_to<uint32_t>(_pc));
                push32(mtl::force_to<uint32_t>(_fp));
                _fp = _sp;
                _pc = target;
#ifdef GAME_DEBUG
                ++_call_depth;
                if (_call_depth > _debug_stats.max_call_depth)
                    _debug_stats.max_call_depth = _call_depth;
#endif
                return noir_status::ok;
            }

            case LOCALS:
            {
                uint8_t n = read_u8();
                ensure_stack(n);
                _sp -= n;
                return noir_status::ok;
            }

            case RET:
                do_ret();
                return (_state == noir_state::idle) ? noir_status::completed : noir_status::ok;

            case RETZ:
                if (_sr & Z)
                {
                    do_ret();
                    return (_state == noir_state::idle) ? noir_status::completed : noir_status::ok;
                }
                return noir_status::ok;

            case RETC:
                if (_sr & C)
                {
                    do_ret();
                    return (_state == noir_state::idle) ? noir_status::completed : noir_status::ok;
                }
                return noir_status::ok;

            default: return noir_status::err_invalid_opcode;
        }
    }

    // ─── Data Movement opcodes ──────────────────────────────────────────────────

    noir_status noir_vm::op_data_move(uint8_t op) noexcept
    {
        switch (op)
        {
            case MOV8:
            {
                uint8_t mode     = read_u8();
                uint8_t dst_mode = mode >> DST_SHIFT;
                uint8_t src_mode = mode & SRC_MASK;
                read_operand_cached(dst_mode);
                uint16_t val = read_operand(src_mode) & 0xFF;
                write_operand_cached(val);
                return noir_status::ok;
            }

            case MOV16:
            {
                uint8_t mode     = read_u8();
                uint8_t dst_mode = mode >> DST_SHIFT;
                uint8_t src_mode = mode & SRC_MASK;
                read_operand_cached(dst_mode);
                uint16_t val = read_operand(src_mode);
                write_operand_cached(val);
                return noir_status::ok;
            }

            case XCHG:
            {
                uint8_t regs = read_u8();
                uint8_t ra   = (regs >> DST_SHIFT) - REG_D0;
                uint8_t rb   = (regs & SRC_MASK) - REG_D0;
                uint16_t tmp = _d[ra & 3];
                _d[ra & 3]   = _d[rb & 3];
                _d[rb & 3]   = tmp;
                return noir_status::ok;
            }

            case PUSH:
            {
                uint8_t mode     = read_u8();
                uint8_t src_mode = mode & SRC_MASK;
                uint16_t val     = read_operand(src_mode);
                push16(val);
                return noir_status::ok;
            }

            case POP:
            {
                uint8_t mode     = read_u8();
                uint8_t dst_mode = mode >> DST_SHIFT;
                uint16_t val     = pop16();
                write_operand(dst_mode, val);
                return noir_status::ok;
            }

            case TOP:
            {
                uint8_t mode     = read_u8();
                uint8_t dst_mode = mode >> DST_SHIFT;
                uint16_t val     = peek16();
                write_operand(dst_mode, val);
                return noir_status::ok;
            }

            case DUP:
            {
                uint16_t val = peek16();
                push16(val);
                return noir_status::ok;
            }

            case CLR:
            {
                uint8_t mode     = read_u8();
                uint8_t dst_mode = mode >> DST_SHIFT;
                write_operand(dst_mode, 0);
                return noir_status::ok;
            }

            default: return noir_status::err_invalid_opcode;
        }
    }

    // ─── Arithmetic opcodes ─────────────────────────────────────────────────────

    noir_status noir_vm::op_arithmetic(uint8_t op) noexcept
    {
        uint8_t mode     = read_u8();
        uint8_t dst_mode = mode >> DST_SHIFT;
        uint8_t src_mode = mode & SRC_MASK;

        uint16_t a = read_operand_cached(dst_mode);

        switch (op)
        {
            case ADD:
            {
                uint16_t b      = read_operand(src_mode);
                uint32_t result = mtl::to<uint32_t>(a) + mtl::to<uint32_t>(b);
                uint16_t r      = mtl::to<uint16_t>(result & 0xFFFF);
                set_flags_nzcv_add(a, b, result);
                write_operand_cached(r);
                return noir_status::ok;
            }

            case SUB:
            {
                uint16_t b      = read_operand(src_mode);
                uint32_t result = mtl::to<uint32_t>(a) - mtl::to<uint32_t>(b);
                uint16_t r      = mtl::to<uint16_t>(result & 0xFFFF);
                set_flags_nzcv_sub(a, b, result);
                write_operand_cached(r);
                return noir_status::ok;
            }

            case MUL:
            {
                uint16_t b   = read_operand(src_mode);
                int32_t sa   = mtl::to<int16_t>(a);
                int32_t sb   = mtl::to<int16_t>(b);
                int32_t full = sa * sb;
                uint16_t r   = mtl::to<uint16_t>(full & 0xFFFF);

                _sr = 0;
                if (r & 0x8000) _sr |= N;
                if (r == 0) _sr |= Z;
                if (full != mtl::to<int32_t>(mtl::to<int16_t>(r))) _sr |= V;
                write_operand_cached(r);
                return noir_status::ok;
            }

            case DIV:
            {
                uint16_t b = read_operand(src_mode);
                if (b == 0)
                {
                    NE_LOG("division by zero");
                    return noir_status::err_division_zero;
                }
                int16_t sa  = mtl::to<int16_t>(a);
                int16_t sb  = mtl::to<int16_t>(b);
                int16_t r   = sa / sb;
                uint16_t ur = mtl::to<uint16_t>(r);
                set_flags_nz(ur);
                write_operand_cached(ur);
                return noir_status::ok;
            }

            case MOD:
            {
                uint16_t b = read_operand(src_mode);
                if (b == 0)
                {
                    NE_LOG("modulo by zero");
                    return noir_status::err_division_zero;
                }
                int16_t sa  = mtl::to<int16_t>(a);
                int16_t sb  = mtl::to<int16_t>(b);
                int16_t r   = sa % sb;
                uint16_t ur = mtl::to<uint16_t>(r);
                set_flags_nz(ur);
                write_operand_cached(ur);
                return noir_status::ok;
            }

            case CMP:
            {
                uint16_t b      = read_operand(src_mode);
                uint32_t result = mtl::to<uint32_t>(a) - mtl::to<uint32_t>(b);
                set_flags_nzcv_sub(a, b, result);
                // Result is discarded
                return noir_status::ok;
            }

            case NEG:
            {
                uint32_t result = mtl::to<uint32_t>(-mtl::to<int16_t>(a)) & 0xFFFF;
                uint16_t r      = mtl::to<uint16_t>(result);
                set_flags_nzcv_sub(0, a, mtl::to<uint32_t>(0) - mtl::to<uint32_t>(a));
                write_operand_cached(r);
                return noir_status::ok;
            }

            case INC:
            {
                uint32_t result = mtl::to<uint32_t>(a) + 1;
                uint16_t r      = mtl::to<uint16_t>(result & 0xFFFF);
                set_flags_nzcv_add(a, 1, result);
                write_operand_cached(r);
                return noir_status::ok;
            }

            case DEC:
            {
                uint32_t result = mtl::to<uint32_t>(a) - 1;
                uint16_t r      = mtl::to<uint16_t>(result & 0xFFFF);
                set_flags_nzcv_sub(a, 1, result);
                write_operand_cached(r);
                return noir_status::ok;
            }

            default: return noir_status::err_invalid_opcode;
        }
    }

    // ─── Logic opcodes ──────────────────────────────────────────────────────────

    noir_status noir_vm::op_logic(uint8_t op) noexcept
    {
        uint8_t mode     = read_u8();
        uint8_t dst_mode = mode >> DST_SHIFT;
        uint8_t src_mode = mode & SRC_MASK;

        uint16_t a = read_operand_cached(dst_mode);

        switch (op)
        {
            case AND:
            {
                uint16_t b = read_operand(src_mode);
                uint16_t r = a & b;
                set_flags_nz(r);
                write_operand_cached(r);
                return noir_status::ok;
            }

            case OR:
            {
                uint16_t b = read_operand(src_mode);
                uint16_t r = a | b;
                set_flags_nz(r);
                write_operand_cached(r);
                return noir_status::ok;
            }

            case XOR:
            {
                uint16_t b = read_operand(src_mode);
                uint16_t r = a ^ b;
                set_flags_nz(r);
                write_operand_cached(r);
                return noir_status::ok;
            }

            case TST:
            {
                uint16_t b = read_operand(src_mode);
                uint16_t r = a & b;
                set_flags_nz(r);
                // Result discarded
                return noir_status::ok;
            }

            case NOT:
            {
                uint16_t r = ~a;
                set_flags_nz(r);
                write_operand_cached(r);
                return noir_status::ok;
            }

            case SHL:
            {
                uint8_t count = read_u8();
                _sr           = 0;
                if (count > 0 && count <= 16)
                {
                    if (a & (1u << (16 - count))) _sr |= C;
                }
                uint16_t r = (count < 16) ? mtl::to<uint16_t>(a << count) : 0;
                if (r & 0x8000) _sr |= N;
                if (r == 0) _sr |= Z;
                write_operand_cached(r);
                return noir_status::ok;
            }

            case SHR:
            {
                uint8_t count = read_u8();
                _sr           = 0;
                if (count > 0 && count <= 16)
                {
                    if (a & (1u << (count - 1))) _sr |= C;
                }
                uint16_t r = (count < 16) ? mtl::to<uint16_t>(a >> count) : 0;
                if (r & 0x8000) _sr |= N;
                if (r == 0) _sr |= Z;
                write_operand_cached(r);
                return noir_status::ok;
            }

            case ASR:
            {
                uint8_t count = read_u8();
                int16_t sa    = mtl::to<int16_t>(a);
                _sr           = 0;
                if (count > 0 && count <= 16)
                {
                    if (a & (1u << (count - 1))) _sr |= C;
                }
                int16_t r   = (count < 16) ? mtl::to<int16_t>(sa >> count) : (sa < 0 ? -1 : 0);
                uint16_t ur = mtl::to<uint16_t>(r);
                if (ur & 0x8000) _sr |= N;
                if (ur == 0) _sr |= Z;
                write_operand_cached(ur);
                return noir_status::ok;
            }

            default: return noir_status::err_invalid_opcode;
        }
    }

    // ─── Control Flow opcodes ───────────────────────────────────────────────────

    noir_status noir_vm::op_control_flow(uint8_t op) noexcept
    {
        int16_t offset = read_s16();

        bool take = false;

        switch (op)
        {
            case JMP: take = true; break;
            case BEQ: take = (_sr & Z) != 0; break;
            case BNE: take = (_sr & Z) == 0; break;
            case BLT: take = ((_sr & N) != 0) != ((_sr & V) != 0); break;
            case BGE: take = ((_sr & N) != 0) == ((_sr & V) != 0); break;
            case BGT: take = ((_sr & Z) == 0) && (((_sr & N) != 0) == ((_sr & V) != 0)); break;
            case BLE: take = ((_sr & Z) != 0) || (((_sr & N) != 0) != ((_sr & V) != 0)); break;
            case BCS: take = (_sr & C) != 0; break;
            case BCC: take = (_sr & C) == 0; break;
            default: return noir_status::err_invalid_opcode;
        }

        if (take)
        {
            _pc += offset;
        }

        return noir_status::ok;
    }

    // ─── Flag Operations ────────────────────────────────────────────────────────

    noir_status noir_vm::op_flag_ops(uint8_t op) noexcept
    {
        switch (op)
        {
            // Immediate index variants: [op] [u16 index]
            case SETF:
            {
                uint16_t idx = read_u16();
                if (!validate_flag_index(idx)) return noir_status::err_flag_oob;
                set_flag(idx, true);
                return noir_status::ok;
            }
            case CLRF:
            {
                uint16_t idx = read_u16();
                if (!validate_flag_index(idx)) return noir_status::err_flag_oob;
                set_flag(idx, false);
                return noir_status::ok;
            }
            case TOGF:
            {
                uint16_t idx = read_u16();
                if (!validate_flag_index(idx)) return noir_status::err_flag_oob;
                set_flag(idx, !get_flag(idx));
                return noir_status::ok;
            }
            case TSTF:
            {
                uint16_t idx = read_u16();
                if (!validate_flag_index(idx)) return noir_status::err_flag_oob;
                // Z=1 when flag is clear, Z=0 when flag is set
                _sr = get_flag(idx) ? 0 : Z;
                return noir_status::ok;
            }

            // Register-indexed variants: [op] [reg_id]
            case SETFR:
            {
                uint8_t reg  = read_u8();
                uint16_t idx = _d[reg & 3];
                if (!validate_flag_index(idx)) return noir_status::err_flag_oob;
                set_flag(idx, true);
                return noir_status::ok;
            }
            case CLRFR:
            {
                uint8_t reg  = read_u8();
                uint16_t idx = _d[reg & 3];
                if (!validate_flag_index(idx)) return noir_status::err_flag_oob;
                set_flag(idx, false);
                return noir_status::ok;
            }
            case TOGFR:
            {
                uint8_t reg  = read_u8();
                uint16_t idx = _d[reg & 3];
                if (!validate_flag_index(idx)) return noir_status::err_flag_oob;
                set_flag(idx, !get_flag(idx));
                return noir_status::ok;
            }
            case TSTFR:
            {
                uint8_t reg  = read_u8();
                uint16_t idx = _d[reg & 3];
                if (!validate_flag_index(idx)) return noir_status::err_flag_oob;
                _sr = get_flag(idx) ? 0 : Z;
                return noir_status::ok;
            }

            // Flag ↔ Register transfer: [op] [reg_id] [u16 index]
            case LDFR:
            {
                uint8_t reg  = read_u8();
                uint16_t idx = read_u16();
                if (!validate_flag_index(idx)) return noir_status::err_flag_oob;
                uint16_t val = get_flag(idx) ? 1 : 0;
                _d[reg & 3]  = val;
                set_flags_nz(val);
                return noir_status::ok;
            }
            case STFR:
            {
                uint8_t reg  = read_u8();
                uint16_t idx = read_u16();
                if (!validate_flag_index(idx)) return noir_status::err_flag_oob;
                set_flag(idx, (_d[reg & 3] & 1) != 0);
                return noir_status::ok;
            }

            default: return noir_status::err_invalid_opcode;
        }
    }

    // ─── Value Operations ───────────────────────────────────────────────────────

    noir_status noir_vm::op_value_ops(uint8_t op) noexcept
    {
        uint16_t idx = read_u16();
        if (!validate_value_index(idx)) return noir_status::err_value_oob;

        switch (op)
        {
            case CMPV:
            {
                uint16_t imm    = read_u16();
                uint16_t a      = _values[idx];
                uint32_t result = mtl::to<uint32_t>(a) - mtl::to<uint32_t>(imm);
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
                uint16_t a      = _values[idx];
                uint32_t result = mtl::to<uint32_t>(a) + 1;
                uint16_t r      = mtl::to<uint16_t>(result & 0xFFFF);
                set_flags_nzcv_add(a, 1, result);
                _values[idx] = r;
                return noir_status::ok;
            }

            case DECV:
            {
                uint16_t a      = _values[idx];
                uint32_t result = mtl::to<uint32_t>(a) - 1;
                uint16_t r      = mtl::to<uint16_t>(result & 0xFFFF);
                set_flags_nzcv_sub(a, 1, result);
                _values[idx] = r;
                return noir_status::ok;
            }

            default: return noir_status::err_invalid_opcode;
        }
    }

    // ─── Game Calls — Inventory ─────────────────────────────────────────────────

    noir_status noir_vm::op_game_inventory(uint8_t op) noexcept
    {
        switch (op)
        {
            case INV_ADD: _bridge->inv_add(_d[0]); return noir_status::ok;

            case INV_REMOVE: _bridge->inv_remove(_d[0]); return noir_status::ok;

            case INV_HAS:
            {
                bool has = _bridge->inv_has(_d[0]);
                _sr      = has ? 0 : Z;
                return noir_status::ok;
            }

            case INV_COUNT: _d[0] = _bridge->inv_count(); return noir_status::ok;

            case INV_CLEAR: _bridge->inv_clear(); return noir_status::ok;

            default: return noir_status::err_invalid_opcode;
        }
    }

    // ─── Game Calls — Timeline ──────────────────────────────────────────────────

    noir_status noir_vm::op_game_timeline(uint8_t op) noexcept
    {
        switch (op)
        {
            case ANIM_PLAY: _bridge->anim_play(_d[0]); return noir_status::ok;

            case ANIM_STOP: _bridge->anim_stop(_d[0]); return noir_status::ok;

            case ANIM_WAIT:
                _bridge->anim_wait(_d[0]);
                enter_blocked();
                return noir_status::ok;

            default: return noir_status::err_invalid_opcode;
        }
    }

    // ─── Game Calls — Scenes ────────────────────────────────────────────────────

    noir_status noir_vm::op_game_scenes(uint8_t op) noexcept
    {
        switch (op)
        {
            case GOTO_SCENE:
                _bridge->goto_scene(_d[0]);
                enter_blocked();
                return noir_status::ok;

            case GET_SCENE: _d[0] = _bridge->get_scene(); return noir_status::ok;

            case HOTSPOT_ON: _bridge->hotspot_on(_d[0]); return noir_status::ok;

            case HOTSPOT_OFF: _bridge->hotspot_off(_d[0]); return noir_status::ok;

            case SET_POINTER: _bridge->set_pointer(_d[0]); return noir_status::ok;

            default: return noir_status::err_invalid_opcode;
        }
    }

    // ─── Game Calls — Dialogue / Audio ──────────────────────────────────────────

    noir_status noir_vm::op_game_dialogue(uint8_t op) noexcept
    {
        switch (op)
        {
            case SAY:
                _bridge->say(_d[0], _d[1]);
                enter_blocked();
                return noir_status::ok;

            case SHOW_DIALOG:
                _bridge->show_dialog(_d[0]);
                enter_blocked();
                return noir_status::ok;

            case SHOW_TEXT: _bridge->show_text(_d[0], _d[1], _d[2]); return noir_status::ok;

            case CLEAR_TEXT: _bridge->clear_text(); return noir_status::ok;

            case PLAY_MUSIC: _bridge->play_music(_d[0]); return noir_status::ok;

            case STOP_MUSIC: _bridge->stop_music(); return noir_status::ok;

            case PLAY_SFX: _bridge->play_sfx(_d[0]); return noir_status::ok;

            default: return noir_status::err_invalid_opcode;
        }
    }

    // ─── Game Calls — Menu / Settings ───────────────────────────────────────────

    noir_status noir_vm::op_game_menu(uint8_t op) noexcept
    {
        switch (op)
        {
            case FADE_OUT:
                _bridge->fade_out(_d[0]);
                enter_blocked();
                return noir_status::ok;

            case FADE_IN:
                _bridge->fade_in(_d[0]);
                enter_blocked();
                return noir_status::ok;

            case SCREEN_CLR: _bridge->screen_clear(_d[0]); return noir_status::ok;

            case SET_LANG: _bridge->set_lang(_d[0]); return noir_status::ok;

            case SAVE_GAME: _bridge->save_game(_d[0]); return noir_status::ok;

            case LOAD_GAME: _bridge->load_game(_d[0]); return noir_status::ok;

            case NEW_GAME: _bridge->new_game(); return noir_status::ok;

            case QUIT_GAME: _bridge->quit_game(); return noir_status::ok;

            default: return noir_status::err_invalid_opcode;
        }
    }

    // ─── Debug opcodes ──────────────────────────────────────────────────────────

    noir_status noir_vm::op_debug(uint8_t op) noexcept
    {
#ifdef GAME_DEBUG
        switch (op)
        {
            case DBG_LOG: _bridge->debug_log(_d[0]); break;

            case DBG_LOGS:
            {
                uint8_t len = read_u8();
                auto str    = reinterpret_cast<char const*>(_pc);
                _bridge->debug_print(str, len);
                _pc += len;
                break;
            }

            case DBG_REG:
                NE_LOG("REG: D0=%04X D1=%04X D2=%04X D3=%04X SR=%02X",
                    _d[0],
                    _d[1],
                    _d[2],
                    _d[3],
                    _sr);
                NE_LOG("REG: SP=%p FP=%p PC=%p", _sp, _fp, _pc);
                break;

            case DBG_STK:
            {
                uint8_t const* p   = _sp;
                uint8_t const* end = _stack.data() + _stack.size();
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
                if (_d[0] == 0xFFFF)
                {
                    uint16_t total = mtl::to<uint16_t>(_flags_size * 8);
                    NE_LOG("FLAGS: (all %u)", total);
                    for (uint16_t i = 0; i < total; ++i)
                    {
                        if (get_flag(i)) NE_LOG("  flag[%u] = 1", i);
                    }
                }
                else
                {
                    if (validate_flag_index(_d[0]))
                        NE_LOG("FLAG[%u] = %u", _d[0], get_flag(_d[0]) ? 1 : 0);
                }
                break;
            }

            case DBG_VAL:
            {
                if (_d[0] == 0xFFFF)
                {
                    NE_LOG("VALUES: (all %u)", _values_count);
                    for (uint16_t i = 0; i < _values_count; ++i)
                        NE_LOG("  value[%u] = %u", i, _values[i]);
                }
                else
                {
                    if (validate_value_index(_d[0]))
                        NE_LOG("VALUE[%u] = %u", _d[0], _values[_d[0]]);
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
            uint8_t len = read_u8();
            _pc += len;
        }
#endif
        return noir_status::ok;
    }

    // ─── RET implementation ─────────────────────────────────────────────────────

    void noir_vm::do_ret() noexcept
    {
        // Tear down frame
        _sp = _fp;
        _fp = mtl::force_to<uint8_t*>(pop32());
        _pc = mtl::force_to<uint8_t const*>(pop32());

#ifdef GAME_DEBUG
        if (_call_depth > 0) --_call_depth;
#endif

        // Check if this was an entry-point return
        if (_reentry_level > 1)
        {
            // Detect entry-point return by sentinel PC (null)
            if (_pc == nullptr)
            {
                // Restore registers saved during execute()
                _pc   = mtl::force_to<uint8_t const*>(pop32());
                _fp   = mtl::force_to<uint8_t*>(pop32());
                _sp   = mtl::force_to<uint8_t*>(pop32());
                _sr   = mtl::to<uint8_t>(pop16());
                _d[3] = pop16();
                _d[2] = pop16();
                _d[1] = pop16();
                _d[0] = pop16();
                --_reentry_level;
                // Continue running at restored PC
            }
        }
        else if (_reentry_level == 1)
        {
            // Detect entry-point return by sentinel PC
            if (_pc == nullptr)
            {
                _exit_code     = _d[0];
                _reentry_level = 0;
                _state         = noir_state::idle;
            }
        }
    }

    // ─── Stack operations ───────────────────────────────────────────────────────

    void noir_vm::push16(uint16_t value) noexcept
    {
        ensure_stack(2);
        _sp -= 2;
        _sp[0] = mtl::to<uint8_t>(value >> 8);  // big-endian
        _sp[1] = mtl::to<uint8_t>(value & 0xFF);
    }

    void noir_vm::push32(uint32_t value) noexcept
    {
        ensure_stack(4);
        _sp -= 4;
        _sp[0] = mtl::to<uint8_t>(value >> 24);
        _sp[1] = mtl::to<uint8_t>(value >> 16);
        _sp[2] = mtl::to<uint8_t>(value >> 8);
        _sp[3] = mtl::to<uint8_t>(value & 0xFF);
    }

    uint16_t noir_vm::pop16() noexcept
    {
        uint16_t value = mtl::to<uint16_t>((mtl::to<uint16_t>(_sp[0]) << 8) | _sp[1]);
        _sp += 2;
        return value;
    }

    uint32_t noir_vm::pop32() noexcept
    {
        uint32_t value = (mtl::to<uint32_t>(_sp[0]) << 24) | (mtl::to<uint32_t>(_sp[1]) << 16)
                         | (mtl::to<uint32_t>(_sp[2]) << 8) | mtl::to<uint32_t>(_sp[3]);
        _sp += 4;
        return value;
    }

    uint16_t noir_vm::peek16() const noexcept
    {
        return mtl::to<uint16_t>((mtl::to<uint16_t>(_sp[0]) << 8) | _sp[1]);
    }

    void noir_vm::ensure_stack(uint16_t bytes_needed) noexcept
    {
        if (_sp - bytes_needed >= _stack.data()) return;

        // Calculate offsets before reallocation
        auto sp_offset = _stack.data() + _stack.size() - _sp;
        auto fp_offset = _stack.data() + _stack.size() - _fp;

        size_t old_size = _stack.size();
        size_t new_size = old_size * 2;
        _stack.reserve(new_size);

        // Extend the vector's size to fill new capacity
        while (_stack.size() < new_size) _stack.push_back(0);

        // Copy old data to top of new allocation (stack grows down)
        auto new_base = _stack.data();
        auto old_top  = new_base + old_size;
        auto new_top  = new_base + new_size;

        // Move old stack content to the top of the new allocation
        for (size_t i = 0; i < old_size; ++i)
        {
            new_top[-(mtl::to<int>(i)) - 1] = old_top[-(mtl::to<int>(i)) - 1];
        }

        // Rebase pointers from the new top
        _sp = new_top - sp_offset;
        _fp = new_top - fp_offset;

#ifdef GAME_DEBUG
        NE_LOG("stack grown to %lu bytes", new_size);
#endif
    }

    // ─── Bytecode reading ───────────────────────────────────────────────────────

    uint8_t noir_vm::read_u8() noexcept
    {
        return *_pc++;
    }

    uint16_t noir_vm::read_u16() noexcept
    {
        uint16_t hi = *_pc++;
        uint16_t lo = *_pc++;
        return mtl::to<uint16_t>((hi << 8) | lo);
    }

    int16_t noir_vm::read_s16() noexcept
    {
        return mtl::to<int16_t>(read_u16());
    }

    // ─── Mode byte helpers ──────────────────────────────────────────────────────

    uint16_t noir_vm::read_operand(uint8_t mode) noexcept
    {
        switch (mode)
        {
            case IMM8: return mtl::to<uint16_t>(read_u8());
            case IMM16: return read_u16();
            case REG_D0: return _d[0];
            case REG_D1: return _d[1];
            case REG_D2: return _d[2];
            case REG_D3: return _d[3];

            case LOCAL:
            {
                int8_t offset = mtl::to<int8_t>(read_u8());
                uint8_t* addr = _fp + offset;
                return mtl::to<uint16_t>((mtl::to<uint16_t>(addr[0]) << 8) | addr[1]);
            }

            case VALUE_IMM:
            {
                uint16_t idx = read_u16();
                if (!validate_value_index(idx))
                {
                    _last_error = noir_status::err_value_oob;
                    _state      = noir_state::halted;
                    return 0;
                }
                return _values[idx];
            }

            case VALUE_D0:
            {
                if (!validate_value_index(_d[0]))
                {
                    _last_error = noir_status::err_value_oob;
                    _state      = noir_state::halted;
                    return 0;
                }
                return _values[_d[0]];
            }

            default: return 0;
        }
    }

    void noir_vm::write_operand(uint8_t mode, uint16_t value) noexcept
    {
        switch (mode)
        {
            case REG_D0: _d[0] = value; break;
            case REG_D1: _d[1] = value; break;
            case REG_D2: _d[2] = value; break;
            case REG_D3: _d[3] = value; break;

            case LOCAL:
            {
                int8_t offset = mtl::to<int8_t>(read_u8());
                uint8_t* addr = _fp + offset;
                addr[0]       = mtl::to<uint8_t>(value >> 8);
                addr[1]       = mtl::to<uint8_t>(value & 0xFF);
                break;
            }

            case VALUE_IMM:
            {
                uint16_t idx = read_u16();
                if (!validate_value_index(idx))
                {
                    _last_error = noir_status::err_value_oob;
                    _state      = noir_state::halted;
                    break;
                }
                _values[idx] = value;
                break;
            }

            case VALUE_D0:
            {
                if (!validate_value_index(_d[0]))
                {
                    _last_error = noir_status::err_value_oob;
                    _state      = noir_state::halted;
                    break;
                }
                _values[_d[0]] = value;
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

    uint16_t noir_vm::read_operand_cached(uint8_t mode) noexcept
    {
        _cached_dst_mode = mode;
        switch (mode)
        {
            case IMM8: return mtl::to<uint16_t>(read_u8());
            case IMM16: return read_u16();
            case REG_D0: return _d[0];
            case REG_D1: return _d[1];
            case REG_D2: return _d[2];
            case REG_D3: return _d[3];

            case LOCAL:
            {
                int8_t offset     = mtl::to<int8_t>(read_u8());
                _cached_dst_param = mtl::to<uint16_t>(mtl::to<uint8_t>(offset));
                uint8_t* addr     = _fp + offset;
                return mtl::to<uint16_t>((mtl::to<uint16_t>(addr[0]) << 8) | addr[1]);
            }

            case VALUE_IMM:
            {
                uint16_t idx      = read_u16();
                _cached_dst_param = idx;
                if (!validate_value_index(idx))
                {
                    _last_error = noir_status::err_value_oob;
                    _state      = noir_state::halted;
                    return 0;
                }
                return _values[idx];
            }

            case VALUE_D0:
            {
                if (!validate_value_index(_d[0]))
                {
                    _last_error = noir_status::err_value_oob;
                    _state      = noir_state::halted;
                    return 0;
                }
                return _values[_d[0]];
            }

            default: return 0;
        }
    }

    void noir_vm::write_operand_cached(uint16_t value) noexcept
    {
        switch (_cached_dst_mode)
        {
            case REG_D0: _d[0] = value; break;
            case REG_D1: _d[1] = value; break;
            case REG_D2: _d[2] = value; break;
            case REG_D3: _d[3] = value; break;

            case LOCAL:
            {
                int8_t offset = mtl::to<int8_t>(mtl::to<uint8_t>(_cached_dst_param));
                uint8_t* addr = _fp + offset;
                addr[0]       = mtl::to<uint8_t>(value >> 8);
                addr[1]       = mtl::to<uint8_t>(value & 0xFF);
                break;
            }

            case VALUE_IMM:
            {
                if (!validate_value_index(_cached_dst_param))
                {
                    _last_error = noir_status::err_value_oob;
                    _state      = noir_state::halted;
                    break;
                }
                _values[_cached_dst_param] = value;
                break;
            }

            case VALUE_D0:
            {
                if (!validate_value_index(_d[0]))
                {
                    _last_error = noir_status::err_value_oob;
                    _state      = noir_state::halted;
                    break;
                }
                _values[_d[0]] = value;
                break;
            }

            default: break;
        }
    }

    // ─── SR flag helpers ────────────────────────────────────────────────────────

    void noir_vm::set_flags_nz(uint16_t result) noexcept
    {
        _sr = 0;
        if (result & 0x8000) _sr |= N;
        if (result == 0) _sr |= Z;
    }

    void noir_vm::set_flags_nzcv_add(uint16_t a, uint16_t b, uint32_t result) noexcept
    {
        uint16_t r = mtl::to<uint16_t>(result & 0xFFFF);
        _sr        = 0;
        if (r & 0x8000) _sr |= N;
        if (r == 0) _sr |= Z;
        if (result > 0xFFFF) _sr |= C;
        // Signed overflow: both operands same sign, result different sign
        if ((~(a ^ b) & (a ^ r)) & 0x8000) _sr |= V;
    }

    void noir_vm::set_flags_nzcv_sub(uint16_t a, uint16_t b, uint32_t result) noexcept
    {
        uint16_t r = mtl::to<uint16_t>(result & 0xFFFF);
        _sr        = 0;
        if (r & 0x8000) _sr |= N;
        if (r == 0) _sr |= Z;
        if (a < b) _sr |= C;  // Borrow occurred
        // Signed overflow: operands different sign, result sign differs from a
        if (((a ^ b) & (a ^ r)) & 0x8000) _sr |= V;
    }

    // ─── Flag/Value helpers ─────────────────────────────────────────────────────

    bool noir_vm::get_flag(uint16_t index) const noexcept
    {
        uint16_t byte_idx = index / 8;
        uint8_t bit_idx   = mtl::to<uint8_t>(index % 8);
        return (_flags[byte_idx] & (1u << bit_idx)) != 0;
    }

    void noir_vm::set_flag(uint16_t index, bool value) noexcept
    {
        uint16_t byte_idx = index / 8;
        uint8_t bit_idx   = mtl::to<uint8_t>(index % 8);
        if (value)
            _flags[byte_idx] |= mtl::to<uint8_t>(1u << bit_idx);
        else
            _flags[byte_idx] &= mtl::to<uint8_t>(~(1u << bit_idx));
    }

    bool noir_vm::validate_flag_index(uint16_t index) const noexcept
    {
        if (index < mtl::to<uint16_t>(_flags_size * 8)) return true;
        NE_LOG("flag index %u out of bounds (max=%u)", index, _flags_size * 8);
        return false;
    }

    bool noir_vm::validate_value_index(uint16_t index) const noexcept
    {
        if (index < _values_count) return true;
        NE_LOG("value index %u out of bounds (max=%u)", index, _values_count);
        return false;
    }

    // ─── Debug ──────────────────────────────────────────────────────────────────

#ifdef GAME_DEBUG
    void noir_vm::update_stack_high_water() noexcept
    {
        auto stack_top = _stack.data() + _stack.size();
        uint32_t used  = mtl::to<uint32_t>(stack_top - _sp);
        if (used > _debug_stats.max_stack_used) _debug_stats.max_stack_used = used;
    }
#endif

    // ─── Blocking helper ────────────────────────────────────────────────────────

    void noir_vm::enter_blocked() noexcept
    {
        ++_blocking_counter;
        _state = noir_state::blocked;
    }

}  // namespace NEONengine
