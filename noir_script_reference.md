# NoirScript VM Reference

> Register-based bytecode VM for NEONengine · Amiga 1200 (68020) · Big-endian · 16-bit word size

---

## Table of Contents

1. [Overview](#overview)
2. [Architecture](#architecture)
   - [Registers](#registers)
   - [PC Convention](#pc-convention)
   - [Status Register (SR)](#status-register-sr)
   - [Memory Model](#memory-model)
   - [Stack](#stack)
3. [Addressing Modes](#addressing-modes)
   - [Mode Byte Encoding](#mode-byte-encoding)
   - [Single-Operand Convention](#single-operand-convention)
   - [Instruction Layout Examples](#instruction-layout-examples)
4. [Opcode Map](#opcode-map)
5. [Script Execution (0x00–0x0F)](#script-execution-0x000x0f)
6. [Data Movement (0x10–0x1F)](#data-movement-0x100x1f)
7. [Arithmetic (0x20–0x2F)](#arithmetic-0x200x2f)
8. [Logic (0x30–0x3F)](#logic-0x300x3f)
9. [Control Flow (0x40–0x4F)](#control-flow-0x400x4f)
   - [Signed Comparison Flag Logic](#signed-comparison-flag-logic)
10. [Flag Operations (0x50–0x5F)](#flag-operations-0x500x5f)
11. [Value Operations (0x60–0x6F)](#value-operations-0x600x6f)
12. [Game Calls (0x80–0xCF)](#game-calls-0x800xcf)
    - [Blocking Operations](#blocking-operations)
    - [Inventory (0x80–0x8F)](#inventory-0x800x8f)
    - [Timeline (0x90–0x9F)](#timeline-0x900x9f)
    - [Scenes (0xA0–0xAF)](#scenes-0xa00xaf)
    - [Dialogue / Audio (0xB0–0xBF)](#dialogue--audio-0xb00xbf)
    - [Menu / Settings (0xC0–0xCF)](#menu--settings-0xc00xcf)
13. [Debug Opcodes (0xE0–0xEF)](#debug-opcodes-0xe00xef)
14. [Calling Convention](#calling-convention)
    - [CALL / RET Mechanics](#call--ret-mechanics)
    - [LOCALS (Local Variables)](#locals-local-variables)
    - [Stack Frame Layout](#stack-frame-layout)
    - [Re-Entrancy](#re-entrancy)
15. [VM Lifecycle](#vm-lifecycle)
    - [Initialization](#initialization)
    - [Update Loop](#update-loop)
    - [States](#states)
    - [Error Handling](#error-handling)
16. [Engine Bridge](#engine-bridge)
17. [Assembly Patterns](#assembly-patterns)

---

## Overview

NoirScript is a bytecode format and virtual machine for driving game scripts in NEONengine — dialogue, inventory, scene transitions, animations, and game flow. Designed for the Amiga 1200 (68020 CPU, limited RAM, big-endian).

**Key properties:**
- Register-based (4 data registers + stack)
- 16-bit word size, big-endian bytecode
- Cooperative yielding (time-sliced execution)
- Re-entrant (engine can call VM while VM is running)
- No exceptions — all errors returned as status codes
- External FLAGS (bitfield) and VALUES (16-bit array) for game state

---

## Architecture

### Registers

| Register | Size | Purpose |
|----------|------|---------|
| D0–D3 | 16-bit | General-purpose data |
| SP | 32-bit | Stack pointer (raw address, grows downward) |
| FP | 32-bit | Frame pointer (raw address) |
| PC | 32-bit | Program counter (raw address) |
| SR | 8-bit | Status register |

SP, FP, and PC are raw 32-bit pointers into memory — not indices. This avoids index-to-pointer math on every opcode.

### PC Convention

PC always points to the **next byte to read**. The dispatcher reads `*pc++` to fetch the opcode, then reads operands by further `*pc++` / `read_u16()` calls. By the time operands are consumed, PC already points past the instruction. Return addresses are pushed as-is — no manual size calculation needed.

### Status Register (SR)

```
Bit 7   Bit 6   Bit 5   Bit 4   Bits 3-0
  N       Z       C       V       (unused)
```

| Flag | Name | Meaning |
|------|------|---------|
| N | Negative | Result bit 15 is set |
| Z | Zero | Result is zero |
| C | Carry | Unsigned overflow / borrow / last bit shifted out |
| V | Overflow | Signed overflow |

### Memory Model

```
External (borrowed, caller-owned):
┌─────────────────────────┐
│ FLAGS (bitfield)        │  Game state flags (1 bit each)
├─────────────────────────┤
│ VALUES (u16 array)      │  Game state values (16-bit each)
├─────────────────────────┤
│ BYTECODE (read-only)    │  All scripts, accessed via function TOC
└─────────────────────────┘

Internal (VM-owned):
┌─────────────────────────┐
│ STACK                   │  mtl::vector<u8>, growable, in FAST RAM
└─────────────────────────┘
```

### Stack

- Grows **downward** (SP decreases on push, increases on pop)
- All operations are **16-bit aligned** (pushing 8-bit pads to 2 bytes)
- SP initialized to top of allocation: `stack.data() + stack.capacity()`
- Overflow check on push, frame creation, and LOCALS: if `sp - needed < stack.data()`, the stack is doubled and SP/FP rebased

---

## Addressing Modes

### Mode Byte Encoding

Two 4-bit fields packed into one byte: `[dst_mode:4][src_mode:4]`

| Value | Mode | Extra Bytes | Description |
|-------|------|-------------|-------------|
| 0x0 | imm8 | 1 | 8-bit constant follows |
| 0x1 | imm16 | 2 | 16-bit constant follows (big-endian) |
| 0x2 | D0 | 0 | Register D0 |
| 0x3 | D1 | 0 | Register D1 |
| 0x4 | D2 | 0 | Register D2 |
| 0x5 | D3 | 0 | Register D3 |
| 0x6 | local | 1 | FP-relative, signed 8-bit offset follows (−128 to +127) |
| 0x7 | value[imm16] | 2 | VALUES array indexed by 16-bit constant |
| 0x8 | value[D0] | 0 | VALUES array indexed by D0 |
| 0x9–0xF | — | — | Reserved |

**Bounds check:** Value modes (0x7 and 0x8) validate the index at runtime. If `value_index >= values_count`, the VM halts with `ValueIndexOOB`. This applies to all opcodes that use the general mode byte system, not only the dedicated value shortcut opcodes.

### Single-Operand Convention

Opcodes that use only one half of the mode byte set the unused nibble to `0000`:

- **Dst-only** (POP, TOP, CLR, NEG, INC, DEC, NOT): `[dst:4][0000]`
- **Src-only** (PUSH): `[0000][src:4]`

### Instruction Layout Examples

When both operands carry inline data, the **destination's extra bytes come first**,
followed by the source's — matching the mode byte layout `[dst:4][src:4]`.

```
MOV.16 D0, #42       →  [0x11] [0x21] [0x00] [0x2A]
                          op    dst=D0  imm16=42
                               src=imm16

MOV.16 D1, [FP-4]    →  [0x11] [0x36] [0xFC]
                          op    dst=D1  offset=-4
                               src=local

MOV.16 [FP-4], #42   →  [0x11] [0x61] [0xFC] [0x00] [0x2A]
                          op    dst=local offset=-4  imm16=42
                               src=imm16

ADD D0, D1            →  [0x20] [0x23]
                          op    dst=D0
                               src=D1
```

---

## Opcode Map

Byte 0 encodes the opcode: upper 4 bits = category, lower 4 bits = operation.

| Bits 7–4 | Category | Range |
|----------|----------|-------|
| 0x0 | Script Execution | 0x00–0x0F |
| 0x1 | Data Movement | 0x10–0x1F |
| 0x2 | Arithmetic | 0x20–0x2F |
| 0x3 | Logic | 0x30–0x3F |
| 0x4 | Control Flow | 0x40–0x4F |
| 0x5 | Flag Operations | 0x50–0x5F |
| 0x6 | Value Operations | 0x60–0x6F |
| 0x7 | Reserved | 0x70–0x7F |
| 0x8 | Game: Inventory | 0x80–0x8F |
| 0x9 | Game: Timeline | 0x90–0x9F |
| 0xA | Game: Scenes | 0xA0–0xAF |
| 0xB | Game: Dialogue/Audio | 0xB0–0xBF |
| 0xC | Game: Menu/Settings | 0xC0–0xCF |
| 0xD | Reserved | 0xD0–0xDF |
| 0xE | Debug | 0xE0–0xEF |
| 0xF | Reserved | 0xF0–0xFF |

---

## Script Execution (0x00–0x0F)

No mode byte — fixed or implicit operands.

| Opcode | Name | Operands | Description |
|--------|------|----------|-------------|
| 0x00 | NOP | — | No operation |
| 0x01 | WAIT | u16 frames | Increment `wait_counter`, enter Waiting state |
| 0x02 | HALT | — | Stop execution, set error state |
| 0x03 | YIELD | — | Voluntarily give up time slice |
| 0x04 | CALL | u16 func_id | Call function by TOC ID |
| 0x05 | LOCALS | u8 n | Reserve n bytes for locals (must be even) |
| 0x06 | RET | — | Return: SP=FP; pop FP; pop PC |
| 0x07 | RETZ | — | Return if Z=1 |
| 0x08 | RETC | — | Return if C=1 |
| 0x09–0x0F | — | — | Reserved |

**LOCALS** operand `n` must be even (16-bit aligned). If `n` is odd, the VM halts with an error. The assembler always emits an even value (local count × 2), so this check guards against hand-crafted or corrupt bytecode.

**WAIT** increments `wait_counter += frames` (additive — multiple WAITs accumulate). The VM enters `State::Waiting` and decrements the counter once per `update()` call. Execution resumes when the counter reaches 0.

**CALL** looks up `func_id` in the function TOC, pushes return PC and FP, sets FP=SP, then jumps to the target. See [Calling Convention](#calling-convention).

**RETZ / RETC** are conditional early returns. RETZ returns if the Zero flag is set; RETC returns if the Carry flag is set. If the condition is false, execution continues to the next instruction.

---

## Data Movement (0x10–0x1F)

| Opcode | Name | Mode Byte | Valid Dst | Valid Src |
|--------|------|-----------|-----------|-----------|
| 0x10 | MOV.8 | `[dst:4][src:4]` | reg, local, value | imm8, reg, local, value |
| 0x11 | MOV.16 | `[dst:4][src:4]` | reg, local, value | imm8, imm16, reg, local, value |
| 0x12 | XCHG | `[regA:4][regB:4]` | reg | reg |
| 0x13 | — | — | — | Reserved |
| 0x14 | PUSH | `[0000][src:4]` | (stack) | imm8, imm16, reg, local, value |
| 0x15 | POP | `[dst:4][0000]` | reg, local, value | (stack) |
| 0x16 | TOP | `[dst:4][0000]` | reg | (stack, non-destructive) |
| 0x17 | DUP | — | (stack) | (stack) |
| 0x18 | CLR | `[dst:4][0000]` | reg | — |
| 0x19–0x1F | — | — | — | Reserved |

**MOV.8** moves the low byte only; high byte of destination is zeroed.

**XCHG** is register-to-register only: `[0x12] [regA:4 | regB:4]` (2 bytes total). Register nibbles use mode encoding values (0x2=D0, 0x3=D1, 0x4=D2, 0x5=D3).

FLAGS are **not** accessible through data movement — use [Flag Operations](#flag-operations-0x500x5f).

---

## Arithmetic (0x20–0x2F)

All operations are **signed 16-bit**.

### Two-Operand — mode byte `[dst:4][src:4]`, result in dst

| Opcode | Name | Flags | Description |
|--------|------|-------|-------------|
| 0x20 | ADD | NZCV | dst = dst + src |
| 0x21 | SUB | NZCV | dst = dst − src |
| 0x22 | MUL | NZCV | dst = dst × src (low 16 bits; V=1 if truncated) |
| 0x23 | DIV | NZ | dst = dst / src (division by zero → HALT) |
| 0x24 | MOD | NZ | dst = dst % src (division by zero → HALT) |
| 0x25 | CMP | NZCV | dst − src → sets flags, discards result |

### Single-Operand — mode byte `[dst:4][0000]`

| Opcode | Name | Flags | Description |
|--------|------|-------|-------------|
| 0x26 | NEG | NZCV | dst = −dst |
| 0x27 | INC | NZCV | dst = dst + 1 |
| 0x28 | DEC | NZCV | dst = dst − 1 |
| 0x29–0x2F | — | — | Reserved |

**Carry flag convention:** For SUB and CMP, the carry flag (C) is set when an unsigned borrow occurs (`a < b` as unsigned). This differs from the Motorola 68K convention where C=0 indicates borrow. The signed comparison branches (BLT, BGE, BGT, BLE) use N⊕V and are unaffected; unsigned branches BCS/BCC are defined against this convention: `BCS` = "borrow occurred" = unsigned less-than, `BCC` = "no borrow" = unsigned greater-or-equal.

### Valid Modes

- **dst**: reg (D0–D3), local, value[imm16], value[D0]
- **src** (two-operand): imm8, imm16, reg (D0–D3), local, value[imm16], value[D0]

---

## Logic (0x30–0x3F)

### Two-Operand — mode byte `[dst:4][src:4]`

| Opcode | Name | Flags | Description |
|--------|------|-------|-------------|
| 0x30 | AND | NZ | dst = dst & src |
| 0x31 | OR | NZ | dst = dst \| src |
| 0x32 | XOR | NZ | dst = dst ^ src |
| 0x33 | TST | NZ | dst & src → sets flags, discards result |

### Single-Operand / Shifts

| Opcode | Name | Flags | Encoding | Description |
|--------|------|-------|----------|-------------|
| 0x34 | NOT | NZ | `[op] [dst:4\|0000] [dst extra...]` | dst = ~dst |
| 0x35 | SHL | NZC | `[op] [dst:4\|0000] [dst extra...] [u8 count]` | dst <<= count |
| 0x36 | SHR | NZC | `[op] [dst:4\|0000] [dst extra...] [u8 count]` | dst >>= count (logical) |
| 0x37 | ASR | NZC | `[op] [dst:4\|0000] [dst extra...] [u8 count]` | dst >>= count (arithmetic) |
| 0x38–0x3F | — | — | — | Reserved |

**Shift count** is always an immediate byte (not via the mode system). C flag = last bit shifted out.

### Valid Modes

- **dst**: reg (D0–D3), local, value[imm16], value[D0]
- **src** (AND, OR, XOR, TST): imm8, imm16, reg, local, value[imm16], value[D0]

---

## Control Flow (0x40–0x4F)

No mode byte. All use a **signed 16-bit offset** relative to the start of the next instruction.

Encoding: `[opcode] [s16 offset high] [s16 offset low]` — 3 bytes total.

| Opcode | Name | Condition | Description |
|--------|------|-----------|-------------|
| 0x40 | JMP | — | Unconditional jump |
| 0x41 | BEQ | Z=1 | Branch if equal / zero |
| 0x42 | BNE | Z=0 | Branch if not equal |
| 0x43 | BLT | N⊕V=1 | Branch if less than (signed) |
| 0x44 | BGE | N⊕V=0 | Branch if greater or equal (signed) |
| 0x45 | BGT | Z=0 ∧ N⊕V=0 | Branch if greater than (signed) |
| 0x46 | BLE | Z=1 ∨ N⊕V=1 | Branch if less or equal (signed) |
| 0x47 | BCS | C=1 | Branch if carry set (unsigned <) |
| 0x48 | BCC | C=0 | Branch if carry clear (unsigned >=) |
| 0x49–0x4F | — | — | Reserved |

### Signed Comparison Flag Logic

After `CMP A, B` (computes A − B):

| Relation | Branch | Condition |
|----------|--------|-----------|
| A == B | BEQ | Z=1 |
| A ≠ B | BNE | Z=0 |
| A < B (signed) | BLT | N⊕V=1 |
| A ≥ B (signed) | BGE | N⊕V=0 |
| A > B (signed) | BGT | Z=0 ∧ N⊕V=0 |
| A ≤ B (signed) | BLE | Z=1 ∨ N⊕V=1 |
| A < B (unsigned) | BCS | C=1 |
| A ≥ B (unsigned) | BCC | C=0 |

> No BHI/BLS — reverse operand order and use BCS/BCC instead.  
> No DJNZ — use `DEC Dn; BNE label` (1 extra byte).

---

## Flag Operations (0x50–0x5F)

FLAGS is a borrowed bitfield array. Each flag is a single bit, addressed by a 16-bit index. These opcodes are the **only** way to access FLAGS.

### Immediate Index — `[op] [u16 index]` (3 bytes)

| Opcode | Name | SR Flags | Description |
|--------|------|----------|-------------|
| 0x50 | SETF | — | Set flag to 1 |
| 0x51 | CLRF | — | Clear flag to 0 |
| 0x52 | TOGF | — | Toggle flag |
| 0x53 | TSTF | Z | Test flag; Z=1 if clear, Z=0 if set |

### Register-Indexed — `[op] [reg_id]` (2 bytes)

| Opcode | Name | SR Flags | Description |
|--------|------|----------|-------------|
| 0x54 | SETFR | — | Set flag[Dn] to 1 |
| 0x55 | CLRFR | — | Clear flag[Dn] to 0 |
| 0x56 | TOGFR | — | Toggle flag[Dn] |
| 0x57 | TSTFR | Z | Test flag[Dn] |

### Flag ↔ Register Transfer — `[op] [reg_id] [u16 index]` (4 bytes)

| Opcode | Name | SR Flags | Description |
|--------|------|----------|-------------|
| 0x58 | LDFR | NZ | Dn = flag[imm16] (result is 0 or 1) |
| 0x59 | STFR | — | flag[imm16] = Dn bit 0 |
| 0x5A–0x5F | — | — | Reserved |

`reg_id`: 0=D0, 1=D1, 2=D2, 3=D3.

**TSTF** sets Z to the inverse of the flag value (Z=1 when flag=0), matching CMP/TST convention: `BNE` = "flag is set", `BEQ` = "flag is clear".

FLAGS remain a packed bitfield, but the VM stores the exact logical catalog
count separately from its byte capacity. If `flag_index >= flag_count`, the VM
halts with `FlagIndexOOB`; padding bits in the final byte are never addressable.

### Encoding Examples

```
SETF #42          →  [0x50] [0x00] [0x2A]
TSTF #42          →  [0x53] [0x00] [0x2A]
SETFR D1          →  [0x54] [0x01]
TSTFR D0          →  [0x57] [0x00]
LDFR D1, #42     →  [0x58] [0x01] [0x00] [0x2A]
STFR D0, #42     →  [0x59] [0x00] [0x00] [0x2A]
```

---

## Value Operations (0x60–0x6F)

VALUES is a borrowed u16 array. Values are also accessible via the general mode byte (modes 0x7 and 0x8) in any opcode that supports them. These dedicated opcodes provide shorter encodings for common patterns.

### Value Shortcuts — `[op] [u16 index]` unless noted

| Opcode | Name | SR Flags | Encoding | Description |
|--------|------|----------|----------|-------------|
| 0x60 | CMPV | NZCV | `[op] [u16 idx] [u16 imm]` | Compare value[idx] with immediate |
| 0x61 | TSTV | NZ | `[op] [u16 idx]` | Test value[idx] == 0 |
| 0x62 | CLRV | — | `[op] [u16 idx]` | value[idx] = 0 |
| 0x63 | INCV | NZCV | `[op] [u16 idx]` | value[idx] += 1 |
| 0x64 | DECV | NZCV | `[op] [u16 idx]` | value[idx] -= 1 |
| 0x65–0x6F | — | — | — | Reserved |

**Byte savings** vs equivalent mode-byte operations:

| Shortcut | Bytes | Equivalent | Bytes |
|----------|-------|------------|-------|
| `INCV #10` | 3 | `INC value[10]` | 4 |
| `TSTV #10` | 3 | `CMP value[10], #0` | 5 |
| `CLRV #10` | 3 | `MOV.16 value[10], #0` | 6 |

**Bounds check:** If `value_index >= values_count` → HALT with `ValueIndexOOB`.

---

## Game Calls (0x80–0xCF)

Game call opcodes invoke engine systems through the `NoirBridge` function pointer table. All game call opcodes are **1 byte** (opcode only) — arguments are passed in D0–D3, return values in D0.

### Blocking Operations

Some game calls are **blocking** — the VM enters `State::Blocked` until the engine signals completion:

1. Opcode handler calls bridge function (e.g., `bridge->say(d[0], d[1])`)
2. Opcode handler increments `blocking_counter` and sets `state = Blocked`
3. VM yields
4. Engine processes the operation (shows dialogue, runs fade, etc.)
5. Engine decrements `blocking_counter` when operation completes
6. On next `update()`, VM checks `blocking_counter == 0` → resumes

Multiple blocking ops can overlap. VM resumes only when **all** complete (counter reaches 0).

### Inventory (0x80–0x8F)

| Opcode | Name | Args | Bridge Call | Description |
|--------|------|------|-------------|-------------|
| 0x80 | INV_ADD | D0=item_id | `inv_add(d[0])` | Add item to inventory |
| 0x81 | INV_REMOVE | D0=item_id | `inv_remove(d[0])` | Remove item |
| 0x82 | INV_HAS | D0=item_id → Z | `inv_has(d[0])` | Test if has item; Z=0 = yes |
| 0x83 | INV_COUNT | → D0 | `inv_count()` | Get inventory count |
| 0x84 | INV_CLEAR | — | `inv_clear()` | Clear entire inventory |
| 0x85–0x8F | — | — | — | Reserved |

### Timeline (0x90–0x9F)

| Opcode | Name | Args | Bridge Call | Blocking | Description |
|--------|------|------|-------------|----------|-------------|
| 0x90 | ANIM_PLAY | D0=anim_id | `anim_play(d[0])` | No | Start animation |
| 0x91 | ANIM_STOP | D0=anim_id | `anim_stop(d[0])` | No | Stop animation |
| 0x92 | ANIM_WAIT | D0=anim_id | `anim_wait(d[0])` | **Yes** | Wait for animation to finish |
| 0x93–0x9F | — | — | — | — | Reserved |

### Scenes (0xA0–0xAF)

| Opcode | Name | Args | Bridge Call | Blocking | Description |
|--------|------|------|-------------|----------|-------------|
| 0xA0 | GOTO_SCENE | D0=scene_id | `goto_scene(d[0])` | **Yes** | Transition to scene |
| 0xA1 | GET_SCENE | → D0 | `get_scene()` | No | Get current scene ID |
| 0xA2 | HOTSPOT_ON | D0=id | `hotspot_on(d[0])` | No | Enable hotspot |
| 0xA3 | HOTSPOT_OFF | D0=id | `hotspot_off(d[0])` | No | Disable hotspot |
| 0xA4 | SET_POINTER | D0=type | `set_pointer(d[0])` | No | Set cursor type |
| 0xA5–0xAF | — | — | — | — | Reserved |

### Dialogue / Audio (0xB0–0xBF)

| Opcode | Name | Args | Bridge Call | Blocking | Description |
|--------|------|------|-------------|----------|-------------|
| 0xB0 | SAY | D0=string, D1=speaker | `say(d[0], d[1])` | **Yes** | Show dialogue |
| 0xB1 | SHOW_DIALOG | D0=dialogue_id | `show_dialog(d[0])` | **Yes** | Show dialogue tree |
| 0xB2 | SHOW_TEXT | D0=string, D1=x, D2=y | `show_text(d[0], d[1], d[2])` | No | Display text |
| 0xB3 | CLEAR_TEXT | — | `clear_text()` | No | Clear displayed text |
| 0xB4 | PLAY_MUSIC | D0=music_id | `play_music(d[0])` | No | Play MOD track |
| 0xB5 | STOP_MUSIC | — | `stop_music()` | No | Stop music |
| 0xB6 | PLAY_SFX | D0=sfx_id | `play_sfx(d[0])` | No | Play sound effect |
| 0xB7–0xBF | — | — | — | — | Reserved |

SHOW_DIALOG is blocking but does not return a value. Dialogue tree logic is handled by the engine.

### Menu / Settings (0xC0–0xCF)

| Opcode | Name | Args | Bridge Call | Blocking | Description |
|--------|------|------|-------------|----------|-------------|
| 0xC0 | FADE_OUT | D0=frames | `fade_out(d[0])` | **Yes** | Fade to black |
| 0xC1 | FADE_IN | D0=frames | `fade_in(d[0])` | **Yes** | Fade from black |
| 0xC2 | SCREEN_CLR | D0=color | `screen_clear(d[0])` | No | Clear screen |
| 0xC3 | SET_LANG | D0=lang_id | `set_lang(d[0])` | No | Set language |
| 0xC4 | SAVE_GAME | D0=slot | `save_game(d[0])` | No | Save game |
| 0xC5 | LOAD_GAME | D0=slot | `load_game(d[0])` | No | Load game |
| 0xC6 | NEW_GAME | — | `new_game()` | No | Start new game |
| 0xC7 | QUIT_GAME | — | `quit_game()` | No | Quit to OS |
| 0xC8–0xCF | — | — | — | — | Reserved |

---

## Debug Opcodes (0xE0–0xEF)

All debug opcodes are guarded by `#ifdef GAME_DEBUG`. In **release builds**, they are treated as NOP (the VM still decodes DBG_LOGS to skip its inline data).

| Opcode | Name | Args / Encoding | Description |
|--------|------|-----------------|-------------|
| 0xE0 | DBG_LOG | D0=string_id | Log string table entry via `bridge->debug_log` |
| 0xE1 | DBG_LOGS | `[op] [u8 len] [bytes...]` | Log inline ASCII string via `bridge->debug_print` |
| 0xE2 | DBG_REG | — | Dump all registers to log |
| 0xE3 | DBG_STK | — | Dump top N stack entries to log |
| 0xE4 | DBG_STAT | — | Dump debug stats to log |
| 0xE5 | DBG_FLAG | D0=index | Dump flag (0xFFFF = all) |
| 0xE6 | DBG_VAL | D0=index | Dump value (0xFFFF = all) |
| 0xE7–0xEF | — | — | Reserved |

### DBG_LOGS Encoding

```
[0xE1] [u8 len] [byte0] [byte1] ... [byteN-1]

Example: DBG_LOGS "hello"
[0xE1] [0x05] [0x68] [0x65] [0x6C] [0x6C] [0x6F]
```

In release builds, the VM skips past the length byte + string bytes (NOP behavior).

---

## Calling Convention

### CALL / RET Mechanics

**Arguments:** Passed in D0–D3 (up to 4 arguments). **Return value:** In D0.  
**Register ownership:** All registers are caller-saved (callee may clobber any register).

**CALL func_id** performs:
1. Read func_id operand (PC advances past it)
2. Look up target: `bytecode + func_toc[func_id]`
3. Push PC (return address — already past the CALL instruction)
4. Push FP (save caller's frame pointer)
5. FP = SP (establish new frame)
6. PC = target (jump to function)

**RET** performs:
1. SP = FP (discard locals)
2. Pop FP (restore caller's frame pointer)
3. Pop PC (jump to return address)

**RETZ** — same as RET but only if Z=1, otherwise continues.  
**RETC** — same as RET but only if C=1, otherwise continues.

### LOCALS (Local Variables)

`LOCALS n` — optional, only needed if the function uses local variables.

Reserves `n` bytes of stack space below FP. `n` must be **even** (16-bit aligned); the assembler is responsible for rounding up.

| Access | Offset | Description |
|--------|--------|-------------|
| Local 0 | [FP−2] | First 16-bit local |
| Local 1 | [FP−4] | Second 16-bit local |
| Local N | [FP−2N] | Nth 16-bit local |
| Saved FP | [FP+0] | Internal — do not access |
| Return PC | [FP+4] | Internal — do not access |

### Stack Frame Layout

```
After CALL (no locals):        After CALL + LOCALS 6:
┌─────────────────┐            ┌─────────────────┐
│ Return PC (4)   │            │ Return PC (4)   │
├─────────────────┤            ├─────────────────┤
│ Saved FP (4)    │            │ Saved FP (4)    │
├─────────────────┤ ← FP, SP  ├─────────────────┤ ← FP
                               │ Local 0 [FP-2]  │
                               │ Local 1 [FP-4]  │
                               │ Local 2 [FP-6]  │
                               ├─────────────────┤ ← SP
```

### Re-Entrancy

The engine can call `execute(func_id)` while the VM is already running (e.g., from a bridge callback). This is a **re-entry**:

| Type | Trigger | Registers Saved |
|------|---------|-----------------|
| Internal CALL | Bytecode `CALL func_id` | Return PC + FP only |
| Re-entry | External `execute(func_id)` | All: D0–D3, SR, SP, FP, PC |

On re-entry (`reentry_level > 1`), the VM pushes all registers (22 bytes), then sets up a fresh execution context. On return, registers are restored in reverse order (pop PC, FP, SP, then SR, then D0–D3).

---

## VM Lifecycle

### Initialization

```cpp
auto vm = NoirVM::create(
    flags, flag_count,
    values, values_count,
    bytecode, bytecode_size,
    func_toc, func_count,
    &bridge,
    initial_stack_size    // e.g. 256
);
```

SP is initialized to `stack.data() + stack.capacity()` (top of allocation). Stack grows downward.

### Update Loop

```
Each frame:
  1. Call vm.update()
  2. Check return status
  3. Repeat until Completed or error
```

`update()` executes up to `opcodes_per_update` (default: 10) opcodes per call. If the VM is Blocked or Waiting, it checks/decrements counters and returns Yielded without executing opcodes.

### States

| State | Meaning |
|-------|---------|
| Idle | No function running |
| Running | Actively executing opcodes |
| Yielded | Time-slice expired, call `update()` again |
| Waiting | WAIT timer active (`wait_counter > 0`, decremented each `update()`) |
| Blocked | Waiting for engine ops (`blocking_counter > 0`, decremented by engine) |
| Halted | Finished or error |

### Error Handling

All errors are returned as `VMStatus` codes from `update()`:

| Status | Cause |
|--------|-------|
| Ok | Normal execution |
| Yielded | Time-slice or wait/block in progress |
| Completed | Entry function returned (`exit_code` = D0) |
| InvalidOpcode | Unknown opcode byte |
| InvalidFuncId | func_id ≥ func_count |
| StackUnderflow | Pop from empty stack |
| DivisionByZero | DIV or MOD with src = 0 |
| InvalidAddress | PC out of bytecode bounds |
| ValueIndexOOB | Value index ≥ values_count |
| FlagIndexOOB | Flag index ≥ logical `flag_count` |

---

## Engine Bridge

The VM communicates with the engine through `NoirBridge` — a struct of function pointers. The bridge is borrowed (must outlive the VM).

```cpp
struct NoirBridge {
    // Inventory
    void (*inv_add)(u16 item_id);
    void (*inv_remove)(u16 item_id);
    bool (*inv_has)(u16 item_id);
    u16  (*inv_count)();
    void (*inv_clear)();

    // Scenes
    void (*goto_scene)(u16 scene_id);        // blocking
    u16  (*get_scene)();
    void (*hotspot_on)(u16 interaction_id);
    void (*hotspot_off)(u16 interaction_id);
    void (*set_pointer)(u16 pointer_type);

    // Dialogue / Text
    void (*say)(u16 string_id, u16 speaker_id);  // blocking
    void (*show_dialog)(u16 dialogue_id);          // blocking
    void (*show_text)(u16 string_id, u16 x, u16 y);
    void (*clear_text)();

    // Audio
    void (*play_music)(u16 music_id);
    void (*stop_music)();
    void (*play_sfx)(u16 sfx_id);

    // Screen
    void (*fade_out)(u16 duration_frames);   // blocking
    void (*fade_in)(u16 duration_frames);    // blocking
    void (*screen_clear)(u16 color_index);

    // Animation
    void (*anim_play)(u16 anim_id);
    void (*anim_stop)(u16 anim_id);
    void (*anim_wait)(u16 anim_id);          // blocking

    // System
    void (*set_lang)(u16 lang_id);
    void (*save_game)(u16 slot);
    void (*load_game)(u16 slot);
    void (*new_game)();
    void (*quit_game)();

#ifdef GAME_DEBUG
    void (*debug_log)(u16 string_id);
    void (*debug_print)(char const* str, u8 len);
#endif
};
```

---

## Assembly Patterns

### Conditional Branch

```asm
; if (D0 == D1) goto label
    CMP D0, D1
    BEQ label

; if (D0 < D1) goto label  (signed)
    CMP D0, D1
    BLT label
```

### Early Return

```asm
; if (D0 == 0) return
    TST D0
    RETZ

; propagate error (carry = error)
    CALL check_bounds
    RETC
```

### Loops

```asm
; while (D0 > 0)
.loop:
    ; ... body ...
    DEC D0
    BNE .loop

; infinite game loop
.loop:
    ; ... per-frame logic ...
    YIELD
    JMP .loop
```

### Flag Testing

```asm
; if (flag[42]) goto label
    TSTF #42
    BNE label           ; Z=0 = flag is set

; if (!flag[42]) goto label
    TSTF #42
    BEQ label           ; Z=1 = flag is clear
```

### Inventory Check

```asm
; if (has key) open door
    MOV D0, #ITEM_KEY
    INV_HAS             ; Z=0 if has it
    BNE .has_key
    JMP .no_key
.has_key:
    MOV D0, #ITEM_KEY
    INV_REMOVE
    SETF #FLAG_DOOR_OPEN
```

### Complete Function

```asm
; u16 add_clamped(u16 a, u16 b)  — a in D0, b in D1
add_clamped:
    LOCALS 2            ; 1 local (2 bytes)
    MOV [FP-2], D0      ; save a
    ADD D0, D1          ; D0 = a + b
    BCC .no_overflow
    MOV D0, #$FFFF      ; clamp to max
.no_overflow:
    RET

; leaf function — no locals, no prologue needed
add:
    ADD D0, D1
    RET

; caller
    MOV D0, #100
    MOV D1, #200
    CALL add_clamped    ; result in D0
```

### Scene Transition with Dialogue

```asm
    MOV D0, #30
    FADE_OUT                ; blocking — fade to black over 30 frames
    MOV D0, #SCENE_OFFICE
    GOTO_SCENE              ; blocking — load new scene
    MOV D0, #30
    FADE_IN                 ; blocking — fade in
    MOV D0, #STR_WELCOME
    MOV D1, #SPEAKER_NARRATOR
    SAY                     ; blocking — show dialogue
    WAIT 60                 ; wait 1 second (at 60fps)
```