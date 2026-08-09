#ifndef __NOIR_OPCODES_H__INCLUDED__
#define __NOIR_OPCODES_H__INCLUDED__

#include <stdint.h>

#include "types.h"

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

        constexpr u8 NOP    = 0x00;  // No operation
        constexpr u8 WAIT   = 0x01;  // u16 frame_count — increment wait_counter
        constexpr u8 HALT   = 0x02;  // Kill this session with err_halt
        constexpr u8 CALL   = 0x04;  // u16 func_id — call via TOC lookup
        constexpr u8 LOCALS = 0x05;  // u8 n — reserve n bytes for locals
        constexpr u8 RET    = 0x06;  // Return: SP=FP; pop FP; pop PC
        constexpr u8 RETZ   = 0x07;  // Return if Z=1
        constexpr u8 RETC   = 0x08;  // Return if C=1

        // ── Data Movement (0x10–0x1F) ───────────────────────────────────────────
        // Mode byte [dst:4][src:4] unless noted.

        constexpr u8 MOV8  = 0x10;  // Move 8-bit (zeroes high byte)
        constexpr u8 MOV16 = 0x11;  // Move 16-bit
        constexpr u8 XCHG  = 0x12;  // Exchange registers [regA:4|regB:4]
        constexpr u8 PUSH  = 0x14;  // Push to stack [0000|src:4]
        constexpr u8 POP   = 0x15;  // Pop from stack [dst:4|0000]
        constexpr u8 TOP   = 0x16;  // Peek stack top [dst:4|0000]
        constexpr u8 DUP   = 0x17;  // Duplicate stack top
        constexpr u8 CLR   = 0x18;  // Clear register [dst:4|0000]

        // ── Arithmetic (0x20–0x2F) ──────────────────────────────────────────────
        // Two-operand: mode byte [dst:4][src:4], result in dst
        // Single-operand: mode byte [dst:4][0000]

        constexpr u8 ADD = 0x20;  // dst += src          NZCV
        constexpr u8 SUB = 0x21;  // dst -= src          NZCV
        constexpr u8 MUL = 0x22;  // dst *= src (low 16) NZCV
        constexpr u8 DIV = 0x23;  // dst /= src (signed) NZ   — div0 → HALT
        constexpr u8 MOD = 0x24;  // dst %= src (signed) NZ   — div0 → HALT
        constexpr u8 CMP = 0x25;  // dst - src → flags   NZCV (discards result)
        constexpr u8 NEG = 0x26;  // dst = -dst          NZCV
        constexpr u8 INC = 0x27;  // dst += 1            NZCV
        constexpr u8 DEC = 0x28;  // dst -= 1            NZCV

        // ── Logic (0x30–0x3F) ───────────────────────────────────────────────────
        // Two-operand: mode byte [dst:4][src:4]
        // Shifts: mode byte [dst:4][0000] + trailing u8 count

        constexpr u8 AND = 0x30;  // dst &= src  NZ
        constexpr u8 OR  = 0x31;  // dst |= src  NZ
        constexpr u8 XOR = 0x32;  // dst ^= src  NZ
        constexpr u8 TST = 0x33;  // dst & src → flags NZ (discards result)
        constexpr u8 NOT = 0x34;  // dst = ~dst  NZ
        constexpr u8 SHL = 0x35;  // dst <<= count NZC
        constexpr u8 SHR = 0x36;  // dst >>= count NZC (logical)
        constexpr u8 ASR = 0x37;  // dst >>= count NZC (arithmetic)

        // ── Control Flow (0x40–0x4F) ────────────────────────────────────────────
        // No mode byte — all use s16 relative offset.

        constexpr u8 JMP = 0x40;  // Unconditional
        constexpr u8 BEQ = 0x41;  // Z=1
        constexpr u8 BNE = 0x42;  // Z=0
        constexpr u8 BLT = 0x43;  // N⊕V=1
        constexpr u8 BGE = 0x44;  // N⊕V=0
        constexpr u8 BGT = 0x45;  // Z=0 AND N⊕V=0
        constexpr u8 BLE = 0x46;  // Z=1 OR N⊕V=1
        constexpr u8 BCS = 0x47;  // C=1
        constexpr u8 BCC = 0x48;  // C=0

        // ── Flag Operations (0x50–0x5F) ─────────────────────────────────────────

        constexpr u8 SETF  = 0x50;  // u16 index — set flag to 1
        constexpr u8 CLRF  = 0x51;  // u16 index — clear flag to 0
        constexpr u8 TOGF  = 0x52;  // u16 index — toggle flag
        constexpr u8 TSTF  = 0x53;  // u16 index — test flag → Z
        constexpr u8 SETFR = 0x54;  // reg_id — set flag[Dn]
        constexpr u8 CLRFR = 0x55;  // reg_id — clear flag[Dn]
        constexpr u8 TOGFR = 0x56;  // reg_id — toggle flag[Dn]
        constexpr u8 TSTFR = 0x57;  // reg_id — test flag[Dn] → Z
        constexpr u8 LDFR  = 0x58;  // reg_id, u16 index — Dn = flag[imm16]
        constexpr u8 STFR  = 0x59;  // reg_id, u16 index — flag[imm16] = Dn bit 0

        // ── Value Operations (0x60–0x6F) ────────────────────────────────────────

        constexpr u8 CMPV = 0x60;  // u16 idx, u16 imm — compare value with immediate
        constexpr u8 TSTV = 0x61;  // u16 idx — test value == 0
        constexpr u8 CLRV = 0x62;  // u16 idx — value = 0
        constexpr u8 INCV = 0x63;  // u16 idx — value += 1
        constexpr u8 DECV = 0x64;  // u16 idx — value -= 1

        // ── Game Calls — Inventory (0x80–0x8F) ─────────────────────────────────
        // 1-byte opcodes, args in D0–D3.

        constexpr u8 INV_ADD      = 0x80;  // D0=item_id
        constexpr u8 INV_REMOVE   = 0x81;  // D0=item_id
        constexpr u8 INV_HAS      = 0x82;  // D0=item_id → Z
        constexpr u8 INV_COUNT    = 0x83;  // D0=item_id → D0=count
        constexpr u8 INV_CLEAR    = 0x84;  // D0=item_id
        constexpr u8 INV_SELECTED = 0x85;  // → D0=selected item_id, 0xFFFF if none

        // ── Game Calls — Timeline (0x90–0x9F) ──────────────────────────────────

        constexpr u8 ANIM_PLAY = 0x90;  // D0=anim_id
        constexpr u8 ANIM_STOP = 0x91;  // D0=anim_id

        // ── Game Calls — Scenes (0xA0–0xAF) ────────────────────────────────────

        constexpr u8 GOTO_SCENE  = 0xA0;  // D0=scene_id — deferred terminal
        constexpr u8 GET_SCENE   = 0xA1;  // → D0
        constexpr u8 HOTSPOT_ON  = 0xA2;  // D0=id
        constexpr u8 HOTSPOT_OFF = 0xA3;  // D0=id
        constexpr u8 SET_POINTER = 0xA4;  // D0=type

        // ── Game Calls — Dialogue / Audio (0xB0–0xBF) ──────────────────────────

        constexpr u8 SAY         = 0xB0;  // D0=string, D1=speaker — deferred modal
        constexpr u8 SHOW_DIALOG = 0xB1;  // D0=dialogue_id — deferred modal
        constexpr u8 SHOW_TEXT   = 0xB2;  // D0=string, D1=x, D2=y
        constexpr u8 CLEAR_TEXT  = 0xB3;
        constexpr u8 PLAY_MUSIC  = 0xB4;  // D0=music_id
        constexpr u8 STOP_MUSIC  = 0xB5;
        constexpr u8 PLAY_SFX    = 0xB6;  // D0=sfx_id

        // ── Game Calls — Menu / Settings (0xC0–0xCF) ───────────────────────────

        constexpr u8 FADE_OUT   = 0xC0;  // D0=frames — deferred modal
        constexpr u8 FADE_IN    = 0xC1;  // D0=frames — deferred modal
        constexpr u8 SCREEN_CLR = 0xC2;  // D0=color
        constexpr u8 SET_LANG   = 0xC3;  // D0=lang_id
        constexpr u8 SAVE_GAME  = 0xC4;  // D0=slot — deferred modal
        constexpr u8 LOAD_GAME  = 0xC5;  // D0=slot
        constexpr u8 NEW_GAME   = 0xC6;
        constexpr u8 QUIT_GAME  = 0xC7;

        // ── Debug (0xE0–0xEF) ──────────────────────────────────────────────────
        // GAME_DEBUG only — release builds treat these as NOP.

        constexpr u8 DBG_LOG  = 0xE0;  // D0=string_id
        constexpr u8 DBG_LOGS = 0xE1;  // u8 len + inline bytes
        constexpr u8 DBG_REG  = 0xE2;  // Dump registers
        constexpr u8 DBG_STK  = 0xE3;  // Dump stack top
        constexpr u8 DBG_STAT = 0xE4;  // Dump debug stats
        constexpr u8 DBG_FLAG = 0xE5;  // D0=index (0xFFFF=all)
        constexpr u8 DBG_VAL  = 0xE6;  // D0=index (0xFFFF=all)

    }  // namespace noir_op

    // ─── Addressing mode encoding ───────────────────────────────────────────────
    //
    // Two 4-bit fields packed into one byte: [dst_mode:4][src_mode:4]
    //

    namespace noir_mode
    {
        constexpr u8 IMM8      = 0x0;  // 8-bit constant follows (1 byte)
        constexpr u8 IMM16     = 0x1;  // 16-bit constant follows (2 bytes, big-endian)
        constexpr u8 REG_D0    = 0x2;
        constexpr u8 REG_D1    = 0x3;
        constexpr u8 REG_D2    = 0x4;
        constexpr u8 REG_D3    = 0x5;
        constexpr u8 LOCAL     = 0x6;  // FP-relative, signed 8-bit offset (1 byte)
        constexpr u8 VALUE_IMM = 0x7;  // VALUES[imm16] (2 bytes)
        constexpr u8 VALUE_D0  = 0x8;  // VALUES[D0]

        constexpr u8 DST_SHIFT = 4;
        constexpr u8 SRC_MASK  = 0x0F;
    }  // namespace noir_mode

    // ─── SR flag bits ───────────────────────────────────────────────────────────

    namespace noir_sr
    {
        constexpr u8 N = 0x80;  // Negative
        constexpr u8 Z = 0x40;  // Zero
        constexpr u8 C = 0x20;  // Carry
        constexpr u8 V = 0x10;  // Overflow
    }  // namespace noir_sr

}  // namespace NEONengine

#endif  // __NOIR_OPCODES_H__INCLUDED__
