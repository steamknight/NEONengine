# NoirScript Assembly Language Specification (NNASM)

> Assembly language for the NoirScript bytecode VM in NEONengine

---

## 1. Overview

NNASM is the assembly language that compiles to NoirScript bytecode. It provides symbolic names for flags, values, strings, and constants, scoped script definitions, named local variables, parameterized macros, and conditional assembly. The assembler is part of a tool pipeline — it produces structured output (bytecode, data sections, debug map) consumed by the host program.

---

## 2. Source Files

- **Extension:** `.nnasm`
- **Encoding:** UTF-8 without BOM. Syntax tokens and identifiers are ASCII;
  comments and string literals may contain Unicode. Localization literals
  emitted to `.noir` must be ISO-8859-1 encodable.
- **Case sensitivity:** Insensitive for mnemonics, directives, and register names. Case-preserving for user-defined identifiers (compared case-insensitively).
- **Line ending:** CR, LF, or CRLF.

---

## 3. Lexical Elements

### 3.1 Comments

Semicolon (`;`) to end of line. No block comments.

```asm
MOV D0, #42    ; this is a comment
; entire line comment
```

### 3.2 Identifiers

Start with a letter or underscore. Contain letters, digits, and underscores. Reserved words (register names, mnemonics, directives) cannot be used.

### 3.3 Numeric Literals

Prefixed with `#` in operand position:

| Format | Syntax | Example |
| -------- | -------- | --------- |
| Decimal | digits | `#42` |
| Hexadecimal | `0x` + hex digits | `#0xFF`, `#0x100` |
| Binary | `0b` + binary digits | `#0b1001`, `#0b0` |
| Negative | `-` prefix | `#-1`, `#-0x100` |

### 3.4 String Literals

Double-quoted. Used in `.strings` sections and `DBG_LOGS`.

```asm
"Hello, world"
"She said \"hello\""
```

Escapes: `\"`, `\\`, `\n`, `\t`.

### 3.5 Registers

| Register | Purpose |
| ---------- | --------- |
| `D0` | General / return value / arg 1 |
| `D1` | General / arg 2 |
| `D2` | General / arg 3 |
| `D3` | General / arg 4 |

### 3.6 Sigils

| Sigil | Namespace | Example | Resolves to | Valid in |
| ------- | ----------- | --------- | ------------- | ---------- |
| `#` | Constant / literal | `#42`, `#ITEM_KEY` | Immediate value | Any operand accepting immediates |
| `$` | String | `$greeting` | String index (immediate) | Any operand accepting immediates |
| `@` | External flag | `@talked_to_guard` | Project flag index | Flag opcodes only |
| `%` | External value | `%money`, `%[D0]` | Project value addressing mode | Any operand accepting value modes |
| `!` | Macro | `!say_line` | Macro expansion | Standalone instruction position |
| `#@` | Flag as immediate | `#@talked_to_guard` | Flag index as immediate | Any operand accepting immediates |
| `&` | Asset | `&scene.intro`, `&track.Flicker` | Asset id (immediate) | Any operand accepting immediates |

`$name` resolves to a numeric immediate (the string's index), so it's valid anywhere an immediate is. `@name` is restricted to flag opcodes (SETF, CLRF, TOGF, TSTF, LDFR, STFR) since the VM has no flag addressing mode in the general mode byte system. `#@name` resolves the flag's index as a numeric immediate, allowing it in any operand position (e.g., `MOV D0, #@has_map` to load a flag's index into a register for `DBG_FLAG` or register-indexed flag operations). `%[D0]` is the only register-indexed value form (the VM only supports D0 for this; `%[D1]`–`%[D3]` are errors). `!name` invokes a macro; the `!` prefix disambiguates macro calls from local variable references and other identifiers.

`&kind.name` names something the host authored rather than something NNASM declares, and resolves to its numeric id at assembly time. The recognized kinds are `scene`, whose id is the scene's position in the project, and `track`, whose id is a project-wide id for the track's *name*. Names are compared case-insensitively and must be valid NNASM identifiers. A name the project does not define anywhere is an assembly error, which is what catches misspellings.

> **Note:** `&track.Name` deliberately identifies a name, not one particular track. A script is not owned by a scene — the same function can run in several — so tracks in different scenes that share a name share an id, and `ANIM_PLAY` acts on whichever of them the *current* scene owns. Naming a track the current scene does not have is not an error; it simply does nothing.

> **Warning:** `#@name` resolves to the flag's numeric **index**, not its boolean value. To read a flag's value into a register, use `LDFR Dn, @name`. Do not confuse `#@flag` (loads index) with `@flag` (operates on the flag directly).

Flags and values are declared in the NEONedit project, not in NNASM. The host
supplies their names and ordered indexes as part of the assembly environment.

---

## 4. Data Sections

Source-defined data sections may appear anywhere in the file, any number of
times. All instances are merged in declaration order. Flags and values are
external project catalogs and are not source-defined data sections.

### 4.1 .consts

Named compile-time constants. Support expressions with `+`, `-`, `*`, `/`, and parentheses. May reference previously defined constants.

```asm
.consts
SPEAKER_GUARD     0
SPEAKER_PLAYER    1
MAX_ITEMS         20
ITEM_SLOTS        (MAX_ITEMS * 2)
```

### 4.2 External flags

NEONedit supplies an ordered catalog of named boolean flags. `@name` and
`#@name` resolve against that catalog. Project order defines each `uint16`
index; initial values are written to FLGS by the game-data compiler.

NNASM source containing `.flags` is an error.

### 4.3 External values

NEONedit supplies an ordered catalog of named signed 16-bit values. `%name`
resolves against that catalog. Project order defines each `uint16` index;
initial values are written to VALS by the game-data compiler.

NNASM source containing `.values` is an error.

### 4.4 .strings

Named localizable strings. The declaration name is the stable string ID and the
literal is its authoritative English text.

```asm
.strings
greeting          "Hello, traveler."
guard_warning     "You shall not pass!"
```

NEONedit supplies editor-authored strings before collecting source declarations,
so NNASM strings follow that host catalog and are indexed in declaration order.
Without a host catalog, indexing starts at 0. A declaration that duplicates a
host string ID is an error.

NNASM-owned IDs and English text remain in source and are not copied into
`en.yaml`. NEONedit may read an exact-ID entry from an optional language YAML
file as a translation overlay; assembly and builds do not rewrite either source.

---

## 5. Program Structure

### 5.1 .script / .end

Scripts are the top-level code units, each compiled as a function in the bytecode TOC. Assigned TOC indices in declaration order starting from 0.

```asm
.script on_enter:
    ; code
    RET
.end
```

- Scripts cannot be nested.
- Forward references to other scripts via `CALL` are allowed.
- After conditional assembly and macro expansion, the final emitted instruction
  in every script must be an unconditional `RET`. The assembler does not insert
  one automatically.

### 5.2 Labels

Branch/jump targets within a script. Scoped to the enclosing `.script`/`.end` block — the same label name may be reused across scripts.

```asm
.script example:
    CMP D0, #10
    BEQ done
    ; ...
done:
    RET
.end
```

Labels end with `:` in definition, referenced without `:`. Must not collide with `.locals` names within the same script.

### 5.3 .locals

Declares named local variables. Must appear immediately after `.script` (before any instructions). The assembler automatically emits the `LOCALS n` opcode.

```asm
.script foo:
    .locals temp, counter    ; emits LOCALS 4
    MOV temp, #42            ; → MOV [FP-2], #42
    MOV counter, D0          ; → MOV [FP-4], D0
    RET
.end
```

Each local is 2 bytes (16-bit). Names map to LOCAL addressing mode (0x6) with computed FP-relative offsets.

`.locals` must be the first non-conditional, non-comment content in a script. It may be wrapped in `.ifdef`/`.ifndef` blocks — conditional directives are transparent for this purpose.

---

## 6. Operand Syntax

### 6.1 Addressing Mode Mapping

| Assembly syntax | VM mode | Extra bytes | Notes |
| ----------------- | --------- | ------------- | ------- |
| `[#value]` | 0x0 (IMM8) | 1 | Forced 8-bit immediate |
| `#value`, `#NAME`, `$name`, `#@name` | 0x0 or 0x1 | 1 or 2 | Auto: [0,255] → IMM8, else IMM16 |
| `D0` | 0x2 | 0 | |
| `D1` | 0x3 | 0 | |
| `D2` | 0x4 | 0 | |
| `D3` | 0x5 | 0 | |
| *local_name* | 0x6 (LOCAL) | 1 | FP-relative, from `.locals` |
| `%name` | 0x7 (VALUE_IMM) | 2 | VALUES[index] |
| `%[D0]` | 0x8 (VALUE_D0) | 0 | VALUES[D0] |

### 6.2 Immediate Auto-Selection

For all instructions with immediate source operands:

- Value ∈ [0, 255] → IMM8 mode (1 byte)
- Otherwise → IMM16 mode (2 bytes)

Negative values are treated as their unsigned 16-bit representation for mode selection (e.g., `#-1` = 0xFFFF → IMM16). The value is encoded as-is in the bytecode.

**MOV opcode selection:** MOV always emits MOV.16 (0x11) unless the source operand uses the forced 8-bit form `[#value]`, which selects MOV.8 (0x10). This means `MOV D0, #200` uses MOV.16 (D0 = 200), while `MOV D0, [#200]` uses MOV.8 (D0 = 200, high byte zeroed). The distinction matters for values 0–255 where both encodings are valid but MOV.8 zeroes the high byte of the destination.

**Forced `[#value]`:** Always IMM8. Values outside [0, 255] truncate to the low byte with a warning (`[#-1]` truncates to 0xFF). Forces MOV.8 for MOV instructions.

---

## 7. Instruction Reference

### 7.1 Script Execution

| Mnemonic | Operands | Description |
| ---------- | ---------- | ------------- |
| `NOP` | — | No operation |
| `WAIT` | `#frames` | Add to wait counter, enter Waiting state |
| `HALT` | — | Stop execution with error |
| `YIELD` | — | Give up time slice |
| `CALL` | *script_name* | Call by name (resolved to TOC index) |
| `LOCALS` | — | *(assembler-emitted only — not valid in assembly source)* |
| `RET` | — | Return |
| `RETZ` | — | Return if Z=1 |
| `RETC` | — | Return if C=1 |

**LOCALS** (opcode 0x05) is a valid VM opcode that reserves stack space for local variables, but it is not written directly in assembly. The assembler emits it automatically from `.locals` declarations (see [Section 5.3](#53-locals)).

**WAIT** is additive — multiple WAIT instructions accumulate. `WAIT #30` followed by `WAIT #20` results in a total wait of 50 frames. The VM enters `Waiting` state and decrements the counter once per `update()` call, resuming execution when it reaches 0.

> **Caution:** WAIT is additive across all sources, including macro expansions. A macro containing `WAIT #10` followed by `WAIT #20` in the caller produces a 30-frame wait, not two separate waits. Use a single WAIT with the total desired duration to avoid confusion.

### 7.2 Data Movement

| Mnemonic | Operands | Description |
| ---------- | ---------- | ------------- |
| `MOV` | dst, src | Move (auto MOV.8/MOV.16) |
| `XCHG` | reg, reg | Exchange two registers |
| `PUSH` | src | Push to stack |
| `POP` | dst | Pop from stack |
| `TOP` | dst | Peek stack top |
| `DUP` | — | Duplicate stack top |
| `CLR` | dst | Clear to zero |

### 7.3 Arithmetic

| Mnemonic | Operands | Flags | Description |
| ---------- | ---------- | ------- | ------------- |
| `ADD` | dst, src | NZCV | dst + src |
| `SUB` | dst, src | NZCV | dst − src |
| `MUL` | dst, src | NZCV | dst × src (low 16) |
| `DIV` | dst, src | NZ | dst / src (halts on ÷0) |
| `MOD` | dst, src | NZ | dst % src (halts on ÷0) |
| `CMP` | dst, src | NZCV | dst − src → flags only |
| `NEG` | dst | NZCV | −dst |
| `INC` | dst | NZCV | dst + 1 |
| `DEC` | dst | NZCV | dst − 1 |

> **Carry flag convention:** For SUB and CMP, the carry flag (C) is set when an unsigned borrow occurs (i.e., when `dst < src` as unsigned values). This differs from the Motorola 68K convention where C=1 means no borrow. See [Section 7.5](#75-control-flow) for the corresponding branch semantics.

### 7.4 Logic

| Mnemonic | Operands | Flags | Description |
| ---------- | ---------- | ------- | ------------- |
| `AND` | dst, src | NZ | dst & src |
| `OR` | dst, src | NZ | dst \| src |
| `XOR` | dst, src | NZ | dst ^ src |
| `TST` | dst, src | NZ | dst & src → flags only |
| `NOT` | dst | NZ | ~dst |
| `SHL` | dst, `#count` | NZC | dst << count |
| `SHR` | dst, `#count` | NZC | dst >> count (logical) |
| `ASR` | dst, `#count` | NZC | dst >> count (arithmetic) |

Shift count is always an immediate byte. Valid range: 0–15. The assembler warns if the count exceeds 15 and takes only the low nibble (count & 0x0F) to constrain it.

### 7.5 Control Flow

All branches take a label name. The assembler calculates the signed 16-bit offset.

| Mnemonic | Condition | Description |
| ---------- | ----------- | ------------- |
| `JMP` | — | Unconditional |
| `BEQ` | Z=1 | Equal / zero |
| `BNE` | Z=0 | Not equal |
| `BLT` | N⊕V=1 | Less than (signed) |
| `BGE` | N⊕V=0 | Greater or equal (signed) |
| `BGT` | Z=0 ∧ N⊕V=0 | Greater than (signed) |
| `BLE` | Z=1 ∨ N⊕V=1 | Less or equal (signed) |
| `BCS` | C=1 | Carry set (unsigned less than) |
| `BCC` | C=0 | Carry clear (unsigned greater or equal) |

> **Note:** `BCS`/`BCC` use the VM's carry convention where C=1 means borrow occurred (dst < src as unsigned). This is the inverse of the Motorola 68K carry convention. After `CMP A, B`: `BCS` branches if A < B unsigned; `BCC` branches if A ≥ B unsigned.

### 7.6 Flag Operations

| Mnemonic | Operands | Description |
| ---------- | ---------- | ------------- |
| `SETF` | `@flag` | Set flag to 1 |
| `CLRF` | `@flag` | Clear flag to 0 |
| `TOGF` | `@flag` | Toggle flag |
| `TSTF` | `@flag` | Test flag → Z (Z=1 if clear) |
| `SETFR` | reg | Set flag[reg] |
| `CLRFR` | reg | Clear flag[reg] |
| `TOGFR` | reg | Toggle flag[reg] |
| `TSTFR` | reg | Test flag[reg] → Z |
| `LDFR` | reg, `@flag` | reg = flag (0 or 1) |
| `STFR` | reg, `@flag` | flag = reg bit 0 |

### 7.6.1 Quick Reference: Flag and Value Testing

| Intent | Code |
| -------- | ------ |
| Jump if flag is set | `TSTF @f` then `BNE label` |
| Jump if flag is clear | `TSTF @f` then `BEQ label` |
| Jump if value is zero | `TSTV %v` then `BEQ label` |
| Jump if value is non-zero | `TSTV %v` then `BNE label` |
| Jump if has inventory item | `INV_HAS` then `BNE label` (Z=0 = has) |
| Return if D0 is zero | `TST D0, D0` then `RETZ` |
| Load flag value into register | `LDFR Dn, @flag` (Dn = 0 or 1) |
| Load flag index into register | `MOV Dn, #@flag` (Dn = flag's numeric index) |

### 7.7 Value Operations

Shortcut opcodes — shorter bytecode than equivalent general instructions with `%`:

| Mnemonic | Operands | Flags | Description |
| ---------- | ---------- | ------- | ------------- |
| `CMPV` | `%value`, `#imm` | NZCV | Compare value with immediate |
| `TSTV` | `%value` | NZ | Test value == 0 |
| `CLRV` | `%value` | — | value = 0 |
| `INCV` | `%value` | NZCV | value += 1 |
| `DECV` | `%value` | NZCV | value -= 1 |

### 7.8 Game Calls

Single-byte opcodes, no inline operands. Arguments in D0–D3 prior to call.

**Inventory:**

| Mnemonic | Args | Description |
| ---------- | ------ | ------------- |
| `INV_ADD` | D0=item | Add item |
| `INV_REMOVE` | D0=item | Remove item |
| `INV_HAS` | D0=item → Z | Has item (Z=0 = yes) |
| `INV_COUNT` | → D0 | Get count |
| `INV_CLEAR` | — | Clear all |

**Timeline:**

| Mnemonic | Args | Blocking | Description |
| ---------- | ------ | ---------- | ------------- |
| `ANIM_PLAY` | D0=track name | — | Play the current scene's track of that name from its start, if it is not already running |
| `ANIM_STOP` | D0=track name | — | Halt that track where it stands |
| `ANIM_WAIT` | D0=track name | Yes | Hold until that track completes a cycle or stops |

Track names come from `&track.Name` (see [Section 3.6](#36-sigils)), so the usual form is:

```nnasm
    MOV D0, &track.DoorOpen
    ANIM_PLAY
    MOV D0, &track.DoorOpen
    ANIM_WAIT
```

`ANIM_PLAY` begins a *run*: it rewinds the playhead to 0 and offers every fire-once keyframe again. A track that is already running is left alone rather than restarted, because a run under way cannot be begun a second time without deciding what becomes of everything it has already done. A script that means to start over stops the track first:

```nnasm
    MOV D0, &track.DoorOpen
    ANIM_STOP
    MOV D0, &track.DoorOpen
    ANIM_PLAY
```

`ANIM_STOP` on a track that is not running passes quietly, so that pairing is safe to write unconditionally. `ANIM_WAIT` ends at the first cycle boundary after it began, so waiting on a track that loops for ever costs one cycle rather than the rest of the scene, and waiting on a track that is not running at all costs a single frame. Naming a track the current scene does not own is not an error: `ANIM_PLAY` and `ANIM_STOP` do nothing, and `ANIM_WAIT` returns immediately.

**Scenes:**

| Mnemonic | Args | Blocking | Description |
| ---------- | ------ | ---------- | ------------- |
| `GOTO_SCENE` | D0=id | Yes | Transition to scene |
| `GET_SCENE` | → D0 | — | Get current scene ID |
| `HOTSPOT_ON` | D0=id | — | Enable hotspot |
| `HOTSPOT_OFF` | D0=id | — | Disable hotspot |
| `SET_POINTER` | D0=type | — | Set cursor type |

**Dialogue / Audio:**

| Mnemonic | Args | Blocking | Description |
| ---------- | ------ | ---------- | ------------- |
| `SAY` | D0=string, D1=speaker | Yes | Show dialogue line |
| `SHOW_DIALOG` | D0=dialogue_id | Yes | Show dialogue tree |
| `SHOW_TEXT` | D0=string, D1=x, D2=y | — | Display text |
| `CLEAR_TEXT` | — | — | Clear text |
| `PLAY_MUSIC` | D0=music_id | — | Play MOD |
| `STOP_MUSIC` | — | — | Stop music |
| `PLAY_SFX` | D0=sfx_id | — | Play SFX |

**Menu / Settings:**

| Mnemonic | Args | Blocking | Description |
| ---------- | ------ | ---------- | ------------- |
| `FADE_OUT` | D0=frames | Yes | Fade to black |
| `FADE_IN` | D0=frames | Yes | Fade from black |
| `SCREEN_CLR` | D0=color | — | Clear screen |
| `SET_LANG` | D0=lang_id | — | Set language |
| `SAVE_GAME` | D0=slot | — | Save game |
| `LOAD_GAME` | D0=slot | — | Load game |
| `NEW_GAME` | — | — | Start new game |
| `QUIT_GAME` | — | — | Quit to OS |

### 7.9 Debug

Guarded by `GAME_DEBUG` conditional symbol. Treated as NOP in release builds.

| Mnemonic | Operands | Description |
| ---------- | ---------- | ------------- |
| `DBG_LOG` | (D0=string_id) | Log string table entry |
| `DBG_LOGS` | `"inline string"` | Log inline ASCII string |
| `DBG_REG` | — | Dump registers |
| `DBG_STK` | — | Dump stack top |
| `DBG_STAT` | — | Dump stats |
| `DBG_FLAG` | (D0=index) | Dump flag (0xFFFF = all) |
| `DBG_VAL` | (D0=index) | Dump value (0xFFFF = all) |

---

## 8. Macros

### 8.1 Definition

```asm
.macro name param1, param2, ...
    ; body — parameters replaced via text substitution
.endmacro
```

### 8.2 Invocation

Macro calls are prefixed with `!` to disambiguate from other identifiers:

```asm
!name arg1, arg2, ...
```

Arguments retain their sigils through expansion:

```asm
.macro say_line speaker, line
    MOV D0, line
    MOV D1, speaker
    SAY
.endmacro

!say_line #SPEAKER_GUARD, $greeting
; Expands to:
;   MOV D0, $greeting
;   MOV D1, #SPEAKER_GUARD
;   SAY
```

### 8.3 Rules

- Macros expand inline (no call overhead).
- Parameters take priority over other symbols during expansion.
- Labels inside macros are auto-suffixed with `__N` per expansion to avoid collisions, where N is a global expansion counter (e.g., `loop` becomes `loop__0`, `loop__1`, etc.). These mangled names appear in the debug map.
- No recursive macros. Macros may invoke other macros (expanded iteratively).
- Visible throughout the global compilation unit.

---

## 9. Conditional Assembly

Symbols are passed externally by the caller. No in-source symbol definition.
NEONedit's Debug profile defines `GAME_DEBUG`; Release defines no profile
symbol.

```asm
.ifdef GAME_DEBUG
    DBG_REG
.endif

.ifndef GAME_DEBUG
    NOP                     ; release-only path
.endif

.ifdef FEATURE_X
    CALL feature_x_handler
.else
    NOP
.endif
```

- `.ifdef` / `.ifndef` / `.else` / `.endif`
- Nesting supported.
- Valid inside and outside `.script`/`.end` blocks.
- Can wrap data section entries when outside scripts.

---

## 10. Project-Wide Compilation

NEONedit supplies direct `<project>/scripts/*.nnasm` files to one global
compilation unit; subdirectories are not scanned. Files are processed by
ordinal project-relative path, then by declaration order within each file.
Constants, strings, macros, scripts, and `CALL` targets are visible across
files.

NNASM does not support `.include`. This avoids a second composition model,
duplicate processing, include cycles, and access to sources outside the project.
An `.include` directive is an error.

---

## 11. Assembly Process

### 11.1 Passes

1. **Collect** — Seed host-supplied flag/value/string indexes, scan every ordered project source, gather source-defined data sections and macros, assign indexes, and evaluate constant expressions.
2. **Layout** — Process scripts. Expand macros. Resolve labels and compute branch offsets. Build function TOC.
3. **Emit** — Generate final bytecode with all references resolved.

### 11.2 Warnings

| Condition | Severity |
| ----------- | ---------- |
| `[#value]` truncation | Warning |
| Unused source-defined string/constant | Warning |
| Shift count exceeds 15 (masked to low nibble) | Warning |

### 11.3 Errors

| Condition | Severity |
| ----------- | ---------- |
| Undefined name reference | Error |
| Wrong sigil for context (`@` in non-flag opcode) | Error |
| Duplicate name within the same global namespace | Error |
| `.flags` or `.values` source declaration | Error |
| `.strings` name duplicates a host-supplied string ID | Error |
| Unsupported `.include` directive | Error |
| Label/local name collision in same script | Error |
| `%[D1]`, `%[D2]`, `%[D3]` (only D0 supported) | Error |
| Missing `.end` | Error |
| Script's final emitted instruction is not unconditional `RET` | Error |
| Nested `.script` | Error |
| `.locals` not first in script | Error |
| `.endmacro` without preceding `.macro` | Error |
| `.end` without preceding `.script` | Error |
| `.else` / `.endif` without preceding `.ifdef` / `.ifndef` | Error |
| `.macro` inside `.script` | Error |
| `.macro` inside `.macro` | Error |
| `.script` inside `.macro` | Error |
| `.locals` outside `.script` | Error |
| Instruction outside `.script` or `.macro` | Error |
| `!name` where name is not a defined macro | Error |
| Label outside `.script` or `.macro` | Error |
| Unclosed `.ifdef` / `.ifndef` (missing `.endif`) | Error |
| Unclosed `.macro` (missing `.endmacro`) | Error |
| Immediate operand not valid as destination | Error |

---

## 12. Assembler Output

The assembler produces structured output (the host tool serializes as needed):

| Output | Content |
| -------- | --------- |
| **Bytecode** | Single contiguous buffer, all scripts concatenated in declaration order |
| **Function TOC** | Strictly increasing byte offsets into bytecode, one per script; the first offset is zero |
| **Strings** | Ordered array of `{ name, content }` — merged into the `.noir` localization file, preserving string IDs assigned during assembly |
| **Referenced state** | External flag/value indexes used by bytecode, for diagnostics only |
| **Debug metadata** | Function, label, local, source-span, and optional analysis-symbol data from which the host selects its external map rows |

---

## 13. Complete Example

This example assumes the NEONedit project supplies external flags
`talked_to_guard` and `door_unlocked`, plus external value `money`.

```asm
.consts
ITEM_KEY          1
SPEAKER_GUARD     0
SPEAKER_NARRATOR  1

.strings
guard_greeting    "Hey, I see you have the key."
no_key_text       "I need to find a key."
door_opened       "The door creaks open."

.macro say_line speaker, line
    MOV D0, line
    MOV D1, speaker
    SAY
.endmacro

.script use_key_on_guard:
    MOV D0, #ITEM_KEY
    INV_HAS
    BEQ no_key

    TSTF @talked_to_guard
    BNE already_done

    MOV D0, #ITEM_KEY
    INV_REMOVE
    SETF @talked_to_guard

    !say_line #SPEAKER_GUARD, $guard_greeting

    SETF @door_unlocked
    RET

no_key:
    !say_line #SPEAKER_NARRATOR, $no_key_text
already_done:
    RET
.end

.script open_door:
    .locals temp

    TSTF @door_unlocked
    BEQ locked

    !say_line #SPEAKER_NARRATOR, $door_opened

    MOV temp, %money
    SUB temp, #10
    MOV %money, temp
    RET

locked:
    RET
.end
```

## Formal Grammar

```ebnf
(* ============================================================ *)
(* 1. Top-Level Structure                                        *)
(* ============================================================ *)

program         = { top_level_item } ;

top_level_item  = data_section
                | script_block
                | macro_def
                | conditional
                ;

(* ============================================================ *)
(* 2. Data Sections                                              *)
(* ============================================================ *)

data_section    = const_section
                | string_section
                ;

const_section   = '.consts' NEWLINE { const_entry } ;
const_entry     = IDENTIFIER const_expr NEWLINE ;

string_section  = '.strings' NEWLINE { string_entry } ;
string_entry    = IDENTIFIER STRING_LITERAL NEWLINE ;
integer_literal = [ '-' ] DECIMAL_NUMBER ;

(* ============================================================ *)
(* 3. Constant Expressions                                       *)
(* ============================================================ *)

const_expr      = const_term { ( '+' | '-' ) const_term } ;
const_term      = const_factor { ( '*' | '/' ) const_factor } ;
const_factor    = integer_literal
                | IDENTIFIER                    (* reference to another constant *)
                | '(' const_expr ')'
                ;

(* ============================================================ *)
(* 4. Script Blocks                                              *)
(* ============================================================ *)

script_block    = '.script' IDENTIFIER ':' NEWLINE
                  [ locals_decl ]
                  { script_line }
                  '.end' NEWLINE ;

locals_decl     = '.locals' ident_list NEWLINE ;
ident_list      = IDENTIFIER { ',' IDENTIFIER } ;

script_line     = label
                | instruction NEWLINE
                | conditional
                ;

label           = IDENTIFIER ':' NEWLINE ;

(* ============================================================ *)
(* 5. Macros                                                     *)
(* ============================================================ *)

macro_def       = '.macro' IDENTIFIER [ param_list ] NEWLINE
                  { macro_body_line }
                  '.endmacro' NEWLINE ;

param_list      = IDENTIFIER { ',' IDENTIFIER } ;

macro_body_line = label
                | instruction NEWLINE
                | conditional
                ;

macro_invoke    = '!' IDENTIFIER [ arg_list ] ;
arg_list        = argument { ',' argument } ;
argument        = operand | IDENTIFIER ;

(* ============================================================ *)
(* 6. Conditional Assembly                                       *)
(* ============================================================ *)

conditional     = ifdef_block | ifndef_block ;

ifdef_block     = '.ifdef' IDENTIFIER NEWLINE
                  { conditional_body }
                  [ '.else' NEWLINE { conditional_body } ]
                  '.endif' NEWLINE ;

ifndef_block    = '.ifndef' IDENTIFIER NEWLINE
                  { conditional_body }
                  [ '.else' NEWLINE { conditional_body } ]
                  '.endif' NEWLINE ;

conditional_body = top_level_item       (* when outside script/macro *)
                 | script_line          (* when inside script *)
                 | macro_body_line      (* when inside macro *)
                 ;

(* ============================================================ *)
(* 7. Instructions                                               *)
(* ============================================================ *)

instruction     = nullary_instr
                | unary_dst_instr
                | unary_src_instr
                | binary_instr
                | shift_instr
                | branch_instr
                | call_instr
                | wait_instr
                | xchg_instr
                | flag_imm_instr
                | flag_reg_instr
                | flag_transfer_instr
                | value_unary_instr
                | value_cmp_instr
                | game_call_instr
                | dbg_logs_instr
                | macro_invoke
                ;

(* --- 8.1 No operands --- *)

nullary_instr   = 'NOP' | 'HALT' | 'YIELD' | 'RET' | 'RETZ' | 'RETC'
                | 'DUP'
                | 'INV_ADD' | 'INV_REMOVE' | 'INV_HAS' | 'INV_COUNT' | 'INV_CLEAR'
                | 'ANIM_PLAY' | 'ANIM_STOP' | 'ANIM_WAIT'
                | 'GOTO_SCENE' | 'GET_SCENE' | 'HOTSPOT_ON' | 'HOTSPOT_OFF' | 'SET_POINTER'
                | 'SAY' | 'SHOW_DIALOG' | 'SHOW_TEXT' | 'CLEAR_TEXT'
                | 'PLAY_MUSIC' | 'STOP_MUSIC' | 'PLAY_SFX'
                | 'FADE_OUT' | 'FADE_IN' | 'SCREEN_CLR' | 'SET_LANG'
                | 'SAVE_GAME' | 'LOAD_GAME' | 'NEW_GAME' | 'QUIT_GAME'
                | 'DBG_LOG' | 'DBG_REG' | 'DBG_STK' | 'DBG_STAT'
                | 'DBG_FLAG' | 'DBG_VAL'
                ;

(* --- 8.2 Single destination operand --- *)

unary_dst_instr = ( 'POP' | 'TOP' | 'CLR' | 'NEG' | 'INC' | 'DEC' | 'NOT' )
                  dst_operand ;

(* --- 8.3 Single source operand --- *)

unary_src_instr = 'PUSH' src_operand ;

(* --- 8.4 Two operands: dst, src --- *)

binary_instr    = ( 'MOV' | 'ADD' | 'SUB' | 'MUL' | 'DIV' | 'MOD' | 'CMP'
                  | 'AND' | 'OR' | 'XOR' | 'TST' )
                  dst_operand ',' src_operand ;

(* --- 8.5 Shifts: dst, #count --- *)

shift_instr     = ( 'SHL' | 'SHR' | 'ASR' )
                  dst_operand ',' immediate ;

(* --- 8.6 Branches --- *)

branch_instr    = ( 'JMP' | 'BEQ' | 'BNE' | 'BLT' | 'BGE' | 'BGT' | 'BLE'
                  | 'BCS' | 'BCC' )
                  IDENTIFIER ;                (* label name *)

(* --- 8.7 Call --- *)

call_instr      = 'CALL' IDENTIFIER ;         (* script name *)

(* --- 8.8 Wait --- *)

wait_instr      = 'WAIT' immediate ;

(* --- 8.9 Exchange --- *)

xchg_instr      = 'XCHG' register ',' register ;

(* --- 8.10 Flag operations — immediate index --- *)

flag_imm_instr  = ( 'SETF' | 'CLRF' | 'TOGF' | 'TSTF' )
                  flag_ref ;

(* --- 8.11 Flag operations — register index --- *)

flag_reg_instr  = ( 'SETFR' | 'CLRFR' | 'TOGFR' | 'TSTFR' )
                  register ;

(* --- 8.12 Flag ↔ register transfer --- *)

flag_transfer_instr = ( 'LDFR' | 'STFR' )
                      register ',' flag_ref ;

(* --- 8.13 Value shortcut — single operand --- *)

value_unary_instr = ( 'TSTV' | 'CLRV' | 'INCV' | 'DECV' )
                    value_ref ;

(* --- 8.14 Value shortcut — compare --- *)

value_cmp_instr = 'CMPV' value_ref ',' immediate ;

(* --- 8.15 Debug log string --- *)

dbg_logs_instr  = 'DBG_LOGS' STRING_LITERAL ;

(* --- 8.16 Game calls --- *)

game_call_instr = nullary_instr ;  (* game calls use no inline operands *)

(* ============================================================ *)
(* 8. Operands                                                   *)
(* ============================================================ *)

dst_operand     = register | local_ref | value_ref ;
src_operand     = register | local_ref | value_ref | immediate ;

register        = 'D0' | 'D1' | 'D2' | 'D3' ;

immediate       = '#' imm_value                (* auto imm8/imm16 *)
                | '[' '#' imm_value ']'         (* forced imm8 *)
                | '$' IDENTIFIER                (* string index as immediate *)
                | '#@' IDENTIFIER               (* flag index as immediate *)
                ;

imm_value       = [ '-' ] number
                | IDENTIFIER                    (* constant name *)
                | const_expr                    (* constant expression *)
                ;

number          = DECIMAL_NUMBER
                | HEX_NUMBER
                | BINARY_NUMBER
                ;

local_ref       = IDENTIFIER ;                 (* resolved from .locals declaration *)

value_ref       = '%' IDENTIFIER               (* VALUES[constant index] *)
                | '%' '[' 'D0' ']'             (* VALUES[D0] *)
                ;

flag_ref        = '@' IDENTIFIER ;             (* FLAGS[constant index] *)

(* ============================================================ *)
(* 9. Lexical Tokens                                             *)
(* ============================================================ *)

IDENTIFIER      = ( LETTER | '_' ) { LETTER | DIGIT | '_' } ;
DECIMAL_NUMBER  = DIGIT { DIGIT } ;
HEX_NUMBER      = '0x' HEX_DIGIT { HEX_DIGIT } ;
BINARY_NUMBER   = '0b' BIN_DIGIT { BIN_DIGIT } ;
STRING_LITERAL  = '"' { CHAR | ESCAPE } '"' ;

ESCAPE          = '\\' ( '"' | '\\' | 'n' | 't' ) ;
LETTER          = 'A'-'Z' | 'a'-'z' ;
DIGIT           = '0'-'9' ;
HEX_DIGIT       = DIGIT | 'A'-'F' | 'a'-'f' ;
BIN_DIGIT       = '0' | '1' ;

COMMENT         = ';' { any character except NEWLINE } ;
NEWLINE         = CR | LF | CR LF ;

(* ============================================================ *)
(* 10. Reserved Keywords                                         *)
(* ============================================================ *)

(*
  The following words are reserved and cannot be used as
  user-defined identifiers (constant names, flag names, value
  names, string names, script names, label names, local names,
  or macro names/parameters).

  Registers:
    D0, D1, D2, D3

  Boolean literals:
    TRUE, FALSE

  All instruction mnemonics:
    NOP, WAIT, HALT, YIELD, CALL, RET, RETZ, RETC,
    MOV, XCHG, PUSH, POP, TOP, DUP, CLR,
    ADD, SUB, MUL, DIV, MOD, CMP, NEG, INC, DEC,
    AND, OR, XOR, TST, NOT, SHL, SHR, ASR,
    JMP, BEQ, BNE, BLT, BGE, BGT, BLE, BCS, BCC,
    SETF, CLRF, TOGF, TSTF, SETFR, CLRFR, TOGFR, TSTFR, LDFR, STFR,
    CMPV, TSTV, CLRV, INCV, DECV,
    INV_ADD, INV_REMOVE, INV_HAS, INV_COUNT, INV_CLEAR,
    ANIM_PLAY, ANIM_STOP, ANIM_WAIT,
    GOTO_SCENE, GET_SCENE, HOTSPOT_ON, HOTSPOT_OFF, SET_POINTER,
    SAY, SHOW_DIALOG, SHOW_TEXT, CLEAR_TEXT,
    PLAY_MUSIC, STOP_MUSIC, PLAY_SFX,
    FADE_OUT, FADE_IN, SCREEN_CLR, SET_LANG,
    SAVE_GAME, LOAD_GAME, NEW_GAME, QUIT_GAME,
    DBG_LOG, DBG_LOGS, DBG_REG, DBG_STK, DBG_STAT, DBG_FLAG, DBG_VAL,
    LOCALS

  All directives (with dot prefix):
    .script, .end, .locals, .consts, .strings,
    .macro, .endmacro, .ifdef, .ifndef, .else, .endif

  Comparison is case-insensitive.
*)

(* ============================================================ *)
(* 11. Disambiguation Rules                                      *)
(* ============================================================ *)

(*
  1. IDENTIFIER in src/dst_operand position:
     - If name matches a register → register
     - If name is in .locals scope → local_ref
     - Otherwise → error (bare names are not valid operands)

  2. IDENTIFIER after '#':
     - Looked up in .consts namespace → resolved to literal value
     - If not found → error

  3. IDENTIFIER after '$':
     - Looked up in .strings namespace → resolved to string index
     - If not found → error

  4. IDENTIFIER after '@':
     - Looked up in the host-supplied project flag catalog
     - Resolved to the project-assigned flag index
     - If not found → error

  5. IDENTIFIER after '%':
     - Looked up in the host-supplied project value catalog
     - Resolved to the project-assigned value index
     - If not found → error

  6. '!' prefix in instruction position:
     - Must be followed by IDENTIFIER
     - IDENTIFIER must match a defined macro name; error if not found

  7. MOV opcode selection:
     - src uses forced [#] form → MOV.8 (0x10)
     - Otherwise (including plain # immediates) → MOV.16 (0x11)

  8. Immediate mode selection:
     - Value in [0, 255] (unsigned) → IMM8 mode (0x0)
     - Value outside [0, 255] or negative → IMM16 mode (0x1)
     - Forced [#value] → always IMM8, warn if truncated

  9. '#@' in immediate position:
     - Followed by IDENTIFIER
     - Looked up in the host-supplied project flag catalog
     - Resolved to the project-assigned flag index as numeric immediate
     - If not found → error
*)

(* ============================================================ *)
(* 12. Implementation Notes                                      *)
(* ============================================================ *)

(*
  13.1 Context Stack for conditional_body

  The conditional_body production is context-sensitive: its valid
  alternatives depend on where the conditional appears. Rather
  than splitting it into three separate grammar rules (which would
  triple the conditional productions), the parser should maintain
  a context stack with three states:

    context = TOP_LEVEL | IN_SCRIPT | IN_MACRO

  State transitions:
    - Entering .script  → push IN_SCRIPT
    - Leaving  .end     → pop (back to TOP_LEVEL)
    - Entering .macro   → push IN_MACRO
    - Leaving  .endmacro → pop (back to TOP_LEVEL)

  When parsing conditional_body, the current context determines
  which alternatives are valid:

    TOP_LEVEL  → top_level_item   (data sections, macros, scripts, nested conditionals)
    IN_SCRIPT  → script_line      (labels, instructions, nested conditionals)
    IN_MACRO   → macro_body_line  (labels, instructions, nested conditionals)

  .ifdef / .ifndef do NOT push a new context — they inherit the
  enclosing one. This means nested conditionals use the same
  context as their parent, which is the correct behavior.

  Error examples caught by context enforcement:
    - Instruction at TOP_LEVEL          → "instruction outside .script or .macro"
    - .consts inside IN_SCRIPT          → "data section inside .script"
    - .script inside IN_MACRO           → ".script inside .macro"
*)
```
