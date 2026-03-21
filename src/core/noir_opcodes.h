#ifndef __NOIR_OPCODES_H__INCLUDED__
#define __NOIR_OPCODES_H__INCLUDED__

#include <stdint.h>

namespace NEONengine
{
    // ─── Opcode encoding ────────────────────────────────────────────────────────
    //
    // Byte 0: OPCODE — upper 4 bits = category, lower 4 bits = operation
    // Byte 1+: Operands vary by opcode
    //

    namespace noir_op
    {
        // ── Script Execution (0x00–0x0F) ────────────────────────────────────────
        // No mode byte — fixed or implicit operands.

        constexpr uint8_t NOP    = 0x00;  // No operation
        constexpr uint8_t WAIT   = 0x01;  // u16 frame_count — increment wait_counter
        constexpr uint8_t HALT   = 0x02;  // Stop execution, set error state
        constexpr uint8_t YIELD  = 0x03;  // Give up time slice
        constexpr uint8_t CALL   = 0x04;  // u16 func_id — call via TOC lookup
        constexpr uint8_t LOCALS = 0x05;  // u8 n — reserve n bytes for locals
        constexpr uint8_t RET    = 0x06;  // Return: SP=FP; pop FP; pop PC
        constexpr uint8_t RETZ   = 0x07;  // Return if Z=1
        constexpr uint8_t RETC   = 0x08;  // Return if C=1

        // ── Data Movement (0x10–0x1F) ───────────────────────────────────────────
        // Mode byte [dst:4][src:4] unless noted.

        constexpr uint8_t MOV8  = 0x10;  // Move 8-bit (zeroes high byte)
        constexpr uint8_t MOV16 = 0x11;  // Move 16-bit
        constexpr uint8_t XCHG  = 0x12;  // Exchange registers [regA:4|regB:4]
        constexpr uint8_t PUSH  = 0x14;  // Push to stack [0000|src:4]
        constexpr uint8_t POP   = 0x15;  // Pop from stack [dst:4|0000]
        constexpr uint8_t TOP   = 0x16;  // Peek stack top [dst:4|0000]
        constexpr uint8_t DUP   = 0x17;  // Duplicate stack top
        constexpr uint8_t CLR   = 0x18;  // Clear register [dst:4|0000]

        // ── Arithmetic (0x20–0x2F) ──────────────────────────────────────────────
        // Two-operand: mode byte [dst:4][src:4], result in dst
        // Single-operand: mode byte [dst:4][0000]

        constexpr uint8_t ADD = 0x20;  // dst += src          NZCV
        constexpr uint8_t SUB = 0x21;  // dst -= src          NZCV
        constexpr uint8_t MUL = 0x22;  // dst *= src (low 16) NZCV
        constexpr uint8_t DIV = 0x23;  // dst /= src (signed) NZ   — div0 → HALT
        constexpr uint8_t MOD = 0x24;  // dst %= src (signed) NZ   — div0 → HALT
        constexpr uint8_t CMP = 0x25;  // dst - src → flags   NZCV (discards result)
        constexpr uint8_t NEG = 0x26;  // dst = -dst          NZCV
        constexpr uint8_t INC = 0x27;  // dst += 1            NZCV
        constexpr uint8_t DEC = 0x28;  // dst -= 1            NZCV

        // ── Logic (0x30–0x3F) ───────────────────────────────────────────────────
        // Two-operand: mode byte [dst:4][src:4]
        // Shifts: mode byte [dst:4][0000] + trailing u8 count

        constexpr uint8_t AND = 0x30;  // dst &= src  NZ
        constexpr uint8_t OR  = 0x31;  // dst |= src  NZ
        constexpr uint8_t XOR = 0x32;  // dst ^= src  NZ
        constexpr uint8_t TST = 0x33;  // dst & src → flags NZ (discards result)
        constexpr uint8_t NOT = 0x34;  // dst = ~dst  NZ
        constexpr uint8_t SHL = 0x35;  // dst <<= count NZC
        constexpr uint8_t SHR = 0x36;  // dst >>= count NZC (logical)
        constexpr uint8_t ASR = 0x37;  // dst >>= count NZC (arithmetic)

        // ── Control Flow (0x40–0x4F) ────────────────────────────────────────────
        // No mode byte — all use s16 relative offset.

        constexpr uint8_t JMP = 0x40;  // Unconditional
        constexpr uint8_t BEQ = 0x41;  // Z=1
        constexpr uint8_t BNE = 0x42;  // Z=0
        constexpr uint8_t BLT = 0x43;  // N⊕V=1
        constexpr uint8_t BGE = 0x44;  // N⊕V=0
        constexpr uint8_t BGT = 0x45;  // Z=0 AND N⊕V=0
        constexpr uint8_t BLE = 0x46;  // Z=1 OR N⊕V=1
        constexpr uint8_t BCS = 0x47;  // C=1
        constexpr uint8_t BCC = 0x48;  // C=0

        // ── Flag Operations (0x50–0x5F) ─────────────────────────────────────────

        constexpr uint8_t SETF  = 0x50;  // u16 index — set flag to 1
        constexpr uint8_t CLRF  = 0x51;  // u16 index — clear flag to 0
        constexpr uint8_t TOGF  = 0x52;  // u16 index — toggle flag
        constexpr uint8_t TSTF  = 0x53;  // u16 index — test flag → Z
        constexpr uint8_t SETFR = 0x54;  // reg_id — set flag[Dn]
        constexpr uint8_t CLRFR = 0x55;  // reg_id — clear flag[Dn]
        constexpr uint8_t TOGFR = 0x56;  // reg_id — toggle flag[Dn]
        constexpr uint8_t TSTFR = 0x57;  // reg_id — test flag[Dn] → Z
        constexpr uint8_t LDFR  = 0x58;  // reg_id, u16 index — Dn = flag[imm16]
        constexpr uint8_t STFR  = 0x59;  // reg_id, u16 index — flag[imm16] = Dn bit 0

        // ── Value Operations (0x60–0x6F) ────────────────────────────────────────

        constexpr uint8_t CMPV = 0x60;  // u16 idx, u16 imm — compare value with immediate
        constexpr uint8_t TSTV = 0x61;  // u16 idx — test value == 0
        constexpr uint8_t CLRV = 0x62;  // u16 idx — value = 0
        constexpr uint8_t INCV = 0x63;  // u16 idx — value += 1
        constexpr uint8_t DECV = 0x64;  // u16 idx — value -= 1

        // ── Game Calls — Inventory (0x80–0x8F) ─────────────────────────────────
        // 1-byte opcodes, args in D0–D3.

        constexpr uint8_t INV_ADD    = 0x80;  // D0=item_id
        constexpr uint8_t INV_REMOVE = 0x81;  // D0=item_id
        constexpr uint8_t INV_HAS    = 0x82;  // D0=item_id → Z
        constexpr uint8_t INV_COUNT  = 0x83;  // → D0
        constexpr uint8_t INV_CLEAR  = 0x84;

        // ── Game Calls — Timeline (0x90–0x9F) ──────────────────────────────────

        constexpr uint8_t ANIM_PLAY = 0x90;  // D0=anim_id
        constexpr uint8_t ANIM_STOP = 0x91;  // D0=anim_id
        constexpr uint8_t ANIM_WAIT = 0x92;  // D0=anim_id — blocking

        // ── Game Calls — Scenes (0xA0–0xAF) ────────────────────────────────────

        constexpr uint8_t GOTO_SCENE  = 0xA0;  // D0=scene_id — blocking
        constexpr uint8_t GET_SCENE   = 0xA1;  // → D0
        constexpr uint8_t HOTSPOT_ON  = 0xA2;  // D0=id
        constexpr uint8_t HOTSPOT_OFF = 0xA3;  // D0=id
        constexpr uint8_t SET_POINTER = 0xA4;  // D0=type

        // ── Game Calls — Dialogue / Audio (0xB0–0xBF) ──────────────────────────

        constexpr uint8_t SAY         = 0xB0;  // D0=string, D1=speaker — blocking
        constexpr uint8_t SHOW_DIALOG = 0xB1;  // D0=dialogue_id — blocking
        constexpr uint8_t SHOW_TEXT   = 0xB2;  // D0=string, D1=x, D2=y
        constexpr uint8_t CLEAR_TEXT  = 0xB3;
        constexpr uint8_t PLAY_MUSIC  = 0xB4;  // D0=music_id
        constexpr uint8_t STOP_MUSIC  = 0xB5;
        constexpr uint8_t PLAY_SFX    = 0xB6;  // D0=sfx_id

        // ── Game Calls — Menu / Settings (0xC0–0xCF) ───────────────────────────

        constexpr uint8_t FADE_OUT   = 0xC0;  // D0=frames — blocking
        constexpr uint8_t FADE_IN    = 0xC1;  // D0=frames — blocking
        constexpr uint8_t SCREEN_CLR = 0xC2;  // D0=color
        constexpr uint8_t SET_LANG   = 0xC3;  // D0=lang_id
        constexpr uint8_t SAVE_GAME  = 0xC4;  // D0=slot
        constexpr uint8_t LOAD_GAME  = 0xC5;  // D0=slot
        constexpr uint8_t NEW_GAME   = 0xC6;
        constexpr uint8_t QUIT_GAME  = 0xC7;

        // ── Debug (0xE0–0xEF) ──────────────────────────────────────────────────
        // GAME_DEBUG only — release builds treat these as NOP.

        constexpr uint8_t DBG_LOG  = 0xE0;  // D0=string_id
        constexpr uint8_t DBG_LOGS = 0xE1;  // u8 len + inline bytes
        constexpr uint8_t DBG_REG  = 0xE2;  // Dump registers
        constexpr uint8_t DBG_STK  = 0xE3;  // Dump stack top
        constexpr uint8_t DBG_STAT = 0xE4;  // Dump debug stats
        constexpr uint8_t DBG_FLAG = 0xE5;  // D0=index (0xFFFF=all)
        constexpr uint8_t DBG_VAL  = 0xE6;  // D0=index (0xFFFF=all)

    }  // namespace noir_op

    // ─── Addressing mode encoding ───────────────────────────────────────────────
    //
    // Two 4-bit fields packed into one byte: [dst_mode:4][src_mode:4]
    //

    namespace noir_mode
    {
        constexpr uint8_t IMM8      = 0x0;  // 8-bit constant follows (1 byte)
        constexpr uint8_t IMM16     = 0x1;  // 16-bit constant follows (2 bytes, big-endian)
        constexpr uint8_t REG_D0    = 0x2;
        constexpr uint8_t REG_D1    = 0x3;
        constexpr uint8_t REG_D2    = 0x4;
        constexpr uint8_t REG_D3    = 0x5;
        constexpr uint8_t LOCAL     = 0x6;  // FP-relative, signed 8-bit offset (1 byte)
        constexpr uint8_t VALUE_IMM = 0x7;  // VALUES[imm16] (2 bytes)
        constexpr uint8_t VALUE_D0  = 0x8;  // VALUES[D0]

        constexpr uint8_t DST_SHIFT = 4;
        constexpr uint8_t SRC_MASK  = 0x0F;
    }  // namespace noir_mode

    // ─── SR flag bits ───────────────────────────────────────────────────────────

    namespace noir_sr
    {
        constexpr uint8_t N = 0x80;  // Negative
        constexpr uint8_t Z = 0x40;  // Zero
        constexpr uint8_t C = 0x20;  // Carry
        constexpr uint8_t V = 0x10;  // Overflow
    }  // namespace noir_sr

}  // namespace NEONengine

#endif  // __NOIR_OPCODES_H__INCLUDED__
