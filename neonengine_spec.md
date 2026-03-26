# NEONengine Design Specification

> Point-and-click adventure game engine for Amiga AGA · 68020 · 4MB RAM · HDD

---

## Table of Contents

1. [Overview](#overview)
2. [Target Platform](#target-platform)
3. [Architecture](#architecture)
   - [Layer Stack](#layer-stack)
   - [Main Loop](#main-loop)
   - [Engine Singleton](#engine-singleton)
   - [State Machine](#state-machine)
4. [Display System](#display-system)
   - [Screen](#screen)
   - [Palette Layout](#palette-layout)
   - [BOB Palette Slots](#bob-palette-slots)
   - [Bitplane Shifting](#bitplane-shifting)
   - [Portrait Palette](#portrait-palette)
   - [Palette Cycling](#palette-cycling)
   - [Fade Transitions](#fade-transitions)
5. [Text Rendering](#text-rendering)
   - [Glyph Width Cache](#glyph-width-cache)
   - [Word Wrapping](#word-wrapping)
   - [Justification](#justification)
6. [UI Layer](#ui-layer)
   - [Hotspot Model](#hotspot-model)
   - [Cursor Resolution](#cursor-resolution)
7. [Localization](#localization)
   - [String Table Format (.noir)](#string-table-format-noir)
   - [Runtime Loading](#runtime-loading)
8. [Audio](#audio)
9. [Nine-Patch UI Frames](#nine-patch-ui-frames)
10. [Game Data Format (.neon)](#game-data-format-neon)
    - [Header](#header)
    - [Range Structure](#range-structure)
    - [Chunk Catalog](#chunk-catalog)
    - [Table of Contents (Runtime)](#table-of-contents-runtime)
    - [Background Caching](#background-caching)
11. [Scene / Hotspot System](#scene--hotspot-system)
    - [Scene Data](#scene-data)
    - [Hotspot Data](#hotspot-data)
    - [Scene Transitions](#scene-transitions)
    - [Scene-Visited Tracking](#scene-visited-tracking)
12. [Dialogue System](#dialogue-system)
    - [Hierarchy](#hierarchy)
    - [Page & Choice Data](#page--choice-data)
    - [Display Layout](#display-layout)
    - [Player Input](#player-input)
    - [Script Integration](#script-integration-dialogue)
    - [Persistence](#persistence-dialogue)
13. [Inventory System](#inventory-system)
    - [Pool-Backed Linked List](#pool-backed-linked-list)
    - [Operations](#inventory-operations)
    - [Item Combining](#item-combining)
    - [Using Items on Hotspots](#using-items-on-hotspots)
14. [Object / Character Interaction](#object--character-interaction)
    - [Verb System](#verb-system)
    - [Right-Click Look](#right-click-look)
15. [Sprite Manager](#sprite-manager)
    - [Sprite Data](#sprite-data)
    - [Runtime State](#runtime-sprite-state)
    - [Z-Ordering](#z-ordering)
    - [Palette Slot Tracking](#palette-slot-tracking)
16. [Timeline / Animation System](#timeline--animation-system)
    - [Timing & Playback](#timing--playback)
    - [Keyframe Data](#keyframe-data)
    - [Timeline Data](#timeline-data)
17. [NoirScript VM](#noirscript-vm)
18. [Save Game Format (.nsg)](#save-game-format-nsg)
    - [File Discovery](#file-discovery)
    - [Header](#save-header)
    - [Chunks](#save-chunks)
    - [Save / Load Procedure](#save--load-procedure)
19. [Minimal Template Library (mtl)](#minimal-template-library-mtl)
20. [Build System](#build-system)
21. [Implementation Status](#implementation-status)

---

## Overview

NEONengine is a dialogue-driven point-and-click adventure game engine, ported from the REALbasic NEONnoir engine to C++20, targeting the Amiga AGA chipset. It runs on the [ACE](modules/ACE/) (Amiga C Engine) graphics library.

The engine drives all gameplay through scripts executed by the NoirScript bytecode VM — scene transitions, inventory logic, dialogue, animations, and game flow are all script-driven with the engine providing system services through a function-pointer bridge.

---

## Target Platform

| Property | Value |
|----------|-------|
| CPU | Motorola 68020+ |
| Chipset | AGA (Advanced Graphics Architecture) |
| RAM | 4 MB minimum (Chip + Fast) |
| Storage | HDD only (no floppy) |
| Resolution | 320×200, 8-bit indexed color (256 colors) |
| Refresh | 50 Hz PAL |
| Audio | 4-channel Paula, ProTracker MOD playback via ptplayer |

---

## Architecture

### Layer Stack

```
┌─────────────────────────────────────────────────────────┐
│                    Game States (states/)                 │
│         splash, lang_selection, dialogue_test, ...      │
├─────────────────────────────────────────────────────────┤
│                   Core Systems (core/)                   │
│   Screen, TextRender, Layer, GameData, Music, NinePatch  │
│   NoirVM (scripting), StringTable, MousePointer          │
├─────────────────────────────────────────────────────────┤
│               C++ Wrapper Layer (ace++/)                  │
│        RAII smart pointers for ACE C resources            │
├─────────────────────────────────────────────────────────┤
│            Minimal Template Library (mtl/)                │
│     vector, array, list, expected, unique_ptr, bstr      │
├─────────────────────────────────────────────────────────┤
│                    ACE Library                            │
│       View/Viewport, Blitter, Copper, Input, Audio       │
├─────────────────────────────────────────────────────────┤
│                   Amiga Hardware                          │
│         Custom chips (Agnus, Denise, Paula)               │
└─────────────────────────────────────────────────────────┘
```

### Main Loop

Uses ACE's `generic/main.h` pattern with three C-linkage callbacks:

- **`genericCreate()`** — Initializes keyboard, mouse, ptplayer, state manager, screen, engine singleton. Pushes initial state.
- **`genericProcess()`** — Per-frame: processes keys, mouse, ptplayer, state machine, screen. F1 toggles debug overlay.
- **`genericDestroy()`** — Tears down screen, music, state manager, input, system, then calls `__cxa_finalize()` for static destructors.

### Engine Singleton

`NEONengine::engine` is the central singleton, accessed via `g_pEngine`. It owns the default font and text renderer, and is constructed via `engine::initialize(fontPath)` which returns `mtl::expected<engine_ptr, error_code>`.

Key globals in the `NEONengine` namespace:

| Global | Type | Purpose |
|--------|------|---------|
| `g_pEngine` | `engine_ptr` | Engine singleton |
| `g_gameStateManager` | `tStateManager*` | ACE state stack |
| `g_mainScreen` | `Screen*` | Primary display |
| `g_gameData` | `GameData*` | Loaded adventure data |

### State Machine

ACE's stack-based `tState` architecture. Each state provides create/loop/destroy/suspend/resume callbacks. States can be pushed/popped for overlay behavior (e.g. debug view over gameplay).

---

## Display System

### Screen

Abstraction over ACE's View/Viewport/SimpleBuffer:

- 320×200 resolution with PAL vertical centering
- Double-buffered: `screenGetBackBuffer()` / `screenGetFrontBuffer()`
- Blitting: `screenBlitCopy()`, `screenTextCopy()`, `screenClear()`
- Mouse binding: adjusts bounds for PAL offset

### Palette Layout

256 colors divided into fixed-purpose ranges:

| Index Range | Purpose | Notes |
|-------------|---------|-------|
| 0–15 | UI (static) | Same for all scenes |
| 16–31 | Hardware sprites / mouse | Amiga sprite HW |
| 32–39 | BOB Slot 1 | 3 bitplanes (8 colors), dynamic |
| 40–47 | BOB Slot 2 | 3 bitplanes (8 colors), dynamic |
| 48–79 | BOB Slot 3+4 / Portrait | 5 bitplanes (32 colors) |
| 80–255 | Background | Scene-specific (176 colors) |

Colors 0–31 remain constant across all scenes. Each scene has its own palette for indices 80–255 (and optionally 32–79).

### BOB Palette Slots

1. When drawing a BOB, find first unused slot
2. If another BOB has the same palette, reuse that slot
3. If different palette, use next available slot
4. If all slots full, wrap to first slot

### Bitplane Shifting

To use a 3-bitplane BOB in slot 32–39 (which requires color index 32+):
1. `blitRect` to write 1s on bitplane 6 (sets bit 5 = +32)
2. `blitCopy` the 3-bitplane BOB image
3. Result: BOB colors are now indices 32–39

### Portrait Palette

Uses indices 48–79 (32 colors). Clobbers BOB slots 3 and 4, which must be reset when dialogue ends.

### Palette Cycling

Managed by Scene Manager. Multiple cycles per scene are allowed, can span any color range. Global effect — all BOBs using affected indices change color.

### Fade Transitions

`screenFadeToBlack()` / `screenFadeFromBlack()` with configurable duration, optional music volume fade, and completion callback. Supports both AGA 24-bit and OCS 12-bit palette dimming.

---

## Text Rendering

The `text_renderer` class provides text layout and rendering to bitmaps.

### Glyph Width Cache

`mtl::array<uint16_t, 256>` pre-computed at initialization for O(1) glyph width lookups.

### Word Wrapping

`break_text_into_lines()` breaks at spaces, falls back to character-level breaks. Uses a scratch buffer for working text copies.

### Justification

Three modes: `text_justify::LEFT`, `CENTER`, `RIGHT`. Output is an `ace::text_bitmap_ptr` via `create_text(bstr_view, maxWidth, justify)`.

---

## UI Layer

### Hotspot Model

`ui_layer` class — hotspot-based point-and-click interaction:

- Hotspots stored in `mtl::forward_list<hotspot>`
- **3-state model**: IDLE → HOVERED → PRESSED
- 6 callbacks per hotspot: idle, hovered, unhovered, pressed, released
- Each hotspot has bounds, pointer ID, context, and ID
- Bounding-box optimization skips update if mouse is outside hotspot group bounds
- Enable/disable per layer

### Cursor Resolution

Context-sensitive — 5 cursor types via `mouse_pointer` enum:

| Cursor | Icon | Condition |
|--------|------|-----------|
| LOOK | Magnifying Glass | No click action, has description |
| USE | Pointing Finger | Has click script |
| SPEAK | Chat Bubble | Hotspot override for talkable NPCs |
| GOTO | Exit Sign | Has `gotoSceneId` set |
| USE_ITEM | Finger + Present | Player is using inventory item |

**AUTO mode resolution order:**
1. `gotoSceneId ≠ -1` → GOTO
2. `clickScriptOffset ≠ -1` → USE
3. `hoverStringId ≠ -1` → LOOK
4. Else → default arrow

---

## Localization

### String Table Format (.noir)

Separate localized files, one per language. Languages supported: EN, IT, DE.

**Header (8 bytes):**

| Field | Size | Description |
|-------|------|-------------|
| magic | 4 | `"NOIR"` |
| version | 2 | Currently 2 |
| languageId | 2 | 0=EN, 1=IT, 2=DE |

**STRG chunk:** 4-byte chunk name + u32 string count + size_t data size, followed by contiguous bstr-format strings (u32 length + char data, no null terminator).

### Runtime Loading

1. Read header, validate magic/version/language
2. Read STRG chunk header
3. Single allocation for entire data block (FAST RAM)
4. Walk data once to build offset table (`mtl::vector<void*>`)
5. O(1) lookup: `get_string(id)` → `bstr_view`

---

## Audio

Simple ProTracker MOD playback via ptplayer:

- `musicLoad(path)` — loads .mod file
- `musicPlayCurrent(loop)` — plays with optional loop
- `musicFree()` — cleanup

Sound effects handled via game call opcodes (`PLAY_SFX`, `STOP_MUSIC`, etc.) through the NoirBridge.

---

## Nine-Patch UI Frames

Scalable UI frame rendering for dialogue boxes and panels:

- Constructor takes `ace::bitmap_ptr` source + border sizes (left/top/right/bottom)
- `render(width, height, flags)` → `ace::bitmap_ptr` — produces stretched nine-patch bitmap

---

## Game Data Format (.neon)

The `.neon` file is a chunk-based binary format containing all structured game data. Assets (images, audio) are stored separately in an ACE pak file.

**Design principles:**
- Zero-allocation: single contiguous memory block
- Range indexing for collections (no embedded pointers)
- TOC struct populated at load time for O(1) access

### Header

64 bytes:

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 4 | magic | `"NEON"` |
| 4 | 2 | formatMajor | File format version major |
| 6 | 2 | formatMinor | File format version minor |
| 8 | 1 | dataYear | Last 2 digits of year |
| 9 | 1 | dataMonth | Month |
| 10 | 1 | dataDay | Day |
| 11 | 1 | dataBuild | Build increment (resets daily) |
| 12 | 52 | reserved | Padding to 64 bytes |

### Range Structure

4 bytes — used throughout to reference sub-arrays within flat collections:

```
struct Range {
    uint16_t start;  // First index (inclusive)
    uint16_t end;    // Last index (inclusive)
};
```

### Chunk Catalog

All chunks: 4-byte identifier + `uint32` count + data. Elements start on 32-bit boundary.

**Size strategy:**
- `uint32`: Adler32 hashes, script offsets, colors, collection counts
- `uint16`: All indices, coordinates, dimensions, string IDs
- `int16`: Optional fields where −1 = none
- `uint8`/`int8`: Small enums, flags, frame counts, z-order

#### UIPT — UI Palette (32 colors)

First chunk after header. Reserved colors 0–31. 8 + 128 = 136 bytes.

#### PLTS / PLET — Palette Collection

Collection of individual palettes. Each PLET: 4-byte ID + u32 color count + u32[] colors.

#### BMPS / BMP_ — Bitmap References

Each BMP_ (10 bytes): `fileId` (Adler32 for pak lookup), `maskFileId` (−1 if none), `paletteId` (into PLTS).

#### GFXS / GFX_ — Graphics Entries

Each GFX_ (10 bytes): `bitmapId`, `frameWidth`, `frameHeight`, `frameCount`, `frameDelay`, `zOrder`, padding.

#### INVT — Inventory Items

Each item (6 bytes): `nameStringId`, `descriptionStringId`, `bitmapId`.

#### CMBN — Item Combination Table

Each entry (8 bytes): `item1Id`, `item2Id`, `resultItemId`, `resultCount`. Order-independent.

#### SPKR — Speakers

Each speaker (4 bytes): `nameStringId`, `portraitBitmapId` (96×96 portrait).

#### HOTS — Hotspots

Each hotspot (28 bytes): bounds, cursor ID, flags, 4 state bitmap IDs, hover string ID, click script offset, goto scene ID, padding.

#### TXTR — Text Regions

Each region (8 bytes): `stringId`, `width`, `height`, `controlBits` (alignment, full width/height flags).

#### KEYS — Keyframes

Each keyframe (12 bytes): `time`, `type` enum, `flags` (interpolation/fire-once), 8-byte data union.

**Keyframe types:** GFX_CREATE, GFX_MOVE, GFX_VISIBILITY, PALETTE_CYCLE_START/END, SFX_PLAY/STOP, MUSIC_PLAY/STOP, GOTO_SCENE, EXECUTE_SCRIPT.

#### TIME — Timelines

Each timeline (8 bytes): keyframe `Range`, `loopMode`, `loopCount`, padding.

#### SCRP — Script Bytecode

4-byte ID + u32 total size + raw bytecode blob. Scripts referenced by byte offset from chunk start.

#### CHOC — Choices

Each choice (16 bytes): text string ID, set/clear/check flag IDs, goto page ID, script offset, flags (enabled, disable-after-read).

#### PAGS — Pages

Each page (20 bytes): speaker ID, text string ID, set/clear/check flag IDs, next page ID, on-display script offset, flags, choices `Range`.

#### DLGS — Dialogues

Each dialogue (4 bytes): pages `Range`.

#### SCEN — Scenes

Each scene (40 bytes): name string ID, dimensions, offset, background bitmap ID, music file ID, enter/exit script offsets, first-visit string ID, fade duration, flags, hotspot/text region/graphics `Range`s.

### Table of Contents (Runtime)

Populated at load time — raw pointers into the loaded data block for O(1) access:

```cpp
struct TOC {
    Palette*    uiPalette;
    Palette*    palettes;
    Bitmap*     bitmaps;
    Graphics*   graphics;
    Inventory*  inventory;
    Speaker*    speakers;
    Hotspot*    hotspots;
    TextRegion* textRegions;
    uint8_t*    scripts;
    Choice*     choices;
    Page*       pages;
    Dialogue*   dialogues;
    Scene*      scenes;
    // Counts for each collection
};
```

### Background Caching

LRU cache with N slots in FAST RAM:
- On scene transition: fade out → memcpy cached BG to CHIP RAM → fade in
- Cache miss: load from pack file during fade (500ms budget)
- N configurable based on available memory

---

## Scene / Hotspot System

### Scene Data

Everything is a Scene — gameplay, menus, cutscenes, title screen. Each scene contains:
- Background image, dimensions, offset
- Music (play, silence, or keep previous)
- Enter/exit scripts
- First-visit narration text
- Hotspots, text regions, and graphics (via Range references)
- Mouse constraint and fade duration options

### Hotspot Data

Each hotspot defines a rectangular interactive area with:
- 4 visual states: idle, hovered, clicked, disabled (each an optional bitmap)
- Context-sensitive cursor (AUTO or explicit override)
- Hover text for descriptions
- Click script for interaction logic
- Direct `gotoSceneId` shortcut for trivial transitions
- Enable/disable flag

No overlapping hotspots allowed (editor should flag). If overlap occurs, first in list wins.

### Scene Transitions

Fade to black → load scene → fade in. Duration overridable per-scene (default 500ms). Triggered by `GOTO_SCENE` VM opcode.

### Scene-Visited Tracking

Automatic bitfield (1 bit per scene), persisted in save game. Engine checks before showing `first_visit_text`.

---

## Dialogue System

### Hierarchy

```
Dialogue (container)
  └── Page[] (graph — can point to any page in the dialogue)
        └── Choice[] (can only point to pages in same dialogue)
```

### Page & Choice Data

**Page:** Speaker, text, optional flag set/clear/check, on-display script, next-page navigation, choices range, enabled/disable-after-read flags.

**Choice:** Text, optional flag set/clear/check, goto-page navigation, on-click script, enabled/disable-after-read flags.

### Display Layout

```
┌────────────────────────────────────────┐
│ ┌──────┐  ┌─────────────────────────┐  │
│ │      │  │ Speaker Name            │  │
│ │ 96×96│  │ Page text here...       │  │
│ │portrait│ │ wrapped as needed...   │  │
│ └──────┘  └─────────────────────────┘  │
│                                        │
│ ┌────────────────────────────────────┐  │
│ │ 1. Choice one                     │  │
│ │ 2. Choice two                     │  │
│ │ 3. Choice three          ▼ more   │  │
│ └────────────────────────────────────┘  │
└────────────────────────────────────────┘
```

### Player Input

- **No choices:** Click anywhere or Spacebar to advance
- **With choices:** Number keys 1–9, or click
- **Overflow:** Max 9 visible choices; scroll indicator if more

### Script Integration (Dialogue)

- Page `on_display_script` runs when page is shown
- Choice `on_click_script` runs when selected
- Scripts execute and return control to dialogue manager (VM is re-entrant)

Portrait bitmap only updates if speaker changes from previous page.

### Persistence (Dialogue)

Page and choice "read once" state tracked in separate bitfields, persisted in save game.

---

## Inventory System

### Pool-Backed Linked List

Fixed memory allocation, linked list semantics, zero heap fragmentation.

```
InventoryNode (6 bytes):
    uint16_t itemId     // Index into INVT chunk
    uint16_t count      // Stack count
    uint8_t  next       // Offset to next node (0xFF = end)
    uint8_t  pad
```

### Inventory Operations

| Operation | Complexity | Description |
|-----------|------------|-------------|
| has(id) | O(n) | Walk from head |
| get_count(id) | O(n) | Walk from head, return count or 0 |
| add(id, n) | O(n) | Increment if exists; else allocate from free list |
| remove(id, n) | O(n) | Decrement; if zero, return node to free list |

### Item Combining

1. Player selects first item, clicks "Combine", then clicks second item
2. Engine searches CMBN chunk (order-independent: checks both (A,B) and (B,A))
3. Match: deduct 1 of each ingredient, add `resultCount` of result
4. No match: play failure SFX

### Using Items on Hotspots

1. Player selects item in inventory, clicks "Use" → cursor changes to USE_ITEM
2. Player clicks hotspot → hotspot's click script executes with item ID in D0
3. Script checks item ID and acts accordingly; wrong item → failure SFX

---

## Object / Character Interaction

### Verb System

No verb bar — cursor changes automatically based on hotspot properties (see [Cursor Resolution](#cursor-resolution)).

Characters are simply hotspots with `cursorId = SPEAK`. Click triggers dialogue via script.

### Right-Click Look

Holding right-click forces LOOK mode on all hotspots with descriptions. Releases when button released.

---

## Sprite Manager

Higher-level manager built on top of ACE's BOB manager.

### Sprite Data

In the game file (GFX_ chunk): bitmap ID, frame dimensions, frame count, animation delay, z-order.

### Runtime Sprite State

Per-instance: sprite ID, position (x, y), current frame, visibility, palette slot.

### Z-Ordering

- Primary sort: z-order (lower draws first = background)
- Secondary sort: Y coordinate (for same z-order, lower Y draws first)

### Palette Slot Tracking

Tracks which palette slots (32–39, 40–47, 48–63, 64–79) are in use and which palette ID is loaded in each. Reuses slot if same palette requested; wraps to first slot if all full.

**Constraints:** Max BOB size 320×256. No hard limit on active BOBs (performance-bound).

**API:** `sprite_show`, `sprite_hide`, `sprite_move`, `sprite_set_frame`, `sprite_animate`, `sprite_set_z`.

---

## Timeline / Animation System

### Timing & Playback

- Configurable: frames (1/50s PAL) or milliseconds
- Multiple timelines can run concurrently
- Loop modes: none, restart, ping-pong
- Loop count: 0 (infinite) to N
- Scripts can start/stop/pause/resume timelines by ID

### Keyframe Data

Each keyframe (12 bytes): time, type enum, flags (interpolation/fire-once), 8-byte type-specific data union.

- **GFX_CREATE** — Instantiate a graphic from GFXS template at position
- **GFX_MOVE** — Move graphic instance (with optional linear interpolation)
- **GFX_VISIBILITY** — Show/hide graphic instance
- **PALETTE_CYCLE_START/END** — Start/stop palette rotation
- **SFX/MUSIC_PLAY/STOP** — Audio control
- **GOTO_SCENE** — Scene transition
- **EXECUTE_SCRIPT** — Run a script function

### Timeline Data

Each timeline (8 bytes): keyframe Range (into KEYS collection), loop mode, loop count.

---

## NoirScript VM

The scripting virtual machine is fully specified in [noir_script_reference.md](noir_script_reference.md).

**Summary:**

- **Register-based** — 4 general-purpose data registers (D0–D3, 16-bit), status register (N/Z/C/V flags), stack/frame/program counter pointers (32-bit raw addresses)
- **16-bit word size**, big-endian bytecode, cooperative yielding with configurable opcode budget per frame
- **Addressing modes** packed in a single mode byte — immediates, registers, FP-relative locals, VALUES array (direct and register-indexed)
- **External game state**: borrowed FLAGS bitfield (1-bit booleans) + VALUES array (16-bit integers) for persistent state, owned by the engine
- **Engine bridge**: `NoirBridge` struct of function pointers decouples VM from all engine systems (inventory, scenes, dialogue, audio, screen, animations, save/load)
- **Blocking operations**: WAIT (frame-counting timer) and bridge calls (SAY, FADE_OUT/IN, GOTO_SCENE, ANIM_WAIT, SHOW_DIALOG) that enter Blocked state until engine signals completion
- **Re-entrant**: engine can call `execute()` while VM is running (e.g. from dialogue or hotspot callbacks); full register context saved/restored automatically
- **~70 opcodes** across 10 categories: script execution, data movement, arithmetic, logic, control flow, flag operations, value operations, game calls (inventory / timeline / scenes / dialogue-audio / menu-settings), and debug
- **Error handling**: all errors returned as `VMStatus` codes — no exceptions

---

## Save Game Format (.nsg)

### File Discovery

Scan dedicated save directory for `*.nsg` files. Sort by file timestamp for display order. Header contains user-defined save name + scene name for save-slot UI.

### Save Header

64 bytes:

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 4 | magic | `"NSAV"` |
| 4 | 2 | version | Format version |
| 6 | 2 | sceneId | Current scene |
| 8 | 4 | sceneNameStrId | Scene name for UI |
| 12 | 4 | timestamp | Unix timestamp |
| 16 | 4 | musicFileId | Adler32, −1 = none |
| 20 | 4 | checksum | Adler32 of bytes 24–EOF |
| 24 | 40 | saveName | User-defined, null-terminated |

### Save Chunks

Each chunk: 4-byte ID + uint16 byte count + data.

| Chunk | Content |
|-------|---------|
| FLGS | Boolean flag pool — raw bitfield copy |
| INTS | Integer pool — raw copy |
| SCNV | Scene-visited bitfield (1 bit per scene) |
| PAGR | Page read-once bitfield |
| CHOR | Choice read-once bitfield |
| HSEN | Hotspot enabled bitfield |
| INVN | Inventory state (item ID + count pairs) |
| TLST | Active timeline state (ID, position, loops remaining, play/pause/stop) |
| GFXI | Active graphic instances (GFX ID, position, frame, visibility, z-order) |
| PCYC | Active palette cycles (index range, offset, delay) |

### Save / Load Procedure

**Save:**
1. Build header with current state
2. Serialize all chunks to buffer
3. Calculate Adler32 of buffer (bytes 24–EOF)
4. Write checksum at offset 20
5. Write `.nsg` file

**Load:**
1. Read header, validate `"NSAV"` magic and version
2. Verify Adler32 checksum
3. Parse chunks, restore all pools and state
4. Transition to saved scene (on-enter script runs)

---

## Minimal Template Library (mtl)

Custom STL replacement to avoid ~300KB binary bloat from libstdc++. All in `src/mtl/`.

| Component | Purpose |
|-----------|---------|
| `unique_ptr<T, Deleter>` | Single-owner pointer with custom deleter |
| `vector<T, MemFlags>` | Dynamic array with CHIP/FAST RAM memory selection |
| `array<T, N>` | Fixed-size, constexpr-compatible |
| `forward_list<T>` | Singly-linked list |
| `expected<T, E>` | Rust-style Result type (value/error union) |
| `MemF` enum | Maps to Amiga MEMF_* flags |
| `cstdint` | Type aliases: i8/i16/i32, u8/u16/u32 |
| Utility | move, forward, swap, `to<T>()`, `round_up<N>()`, `NO_COPY`/`NO_MOVE` macros |

Placement `operator new(size, MemF)` routes to `memAllocFast()`/`memAllocChip()`. Global `new`/`delete` default to FAST RAM.

---

## Build System

- **CMake 3.14+**, C++20, C17
- **Target:** `neonengine.elf` → `neonengine.exe` (via elf2hunk for Bartman compiler)
- **CPU:** 68020
- **Critical flag:** `-fno-optimize-sibling-calls` (tail-call optimization crashes on 68020+)
- **Options:** `GAME_DEBUG` (debug assertions + VM debug opcodes), `ACE_TEST_RUNNER` (unit tests)
- **ACE:** Submodule at `modules/ACE/`
- **Build number:** auto-generated `build_number.h` via pre-build cmake step
- **Asset pipeline:** `convertPalette()`, `convertBitmaps()`, `convertFont()` — CMake functions from ACE that convert PNGs/ACTs to Amiga-native formats at build time

---

## Implementation Status

| System | Status |
|--------|--------|
| Screen / fading | **Complete** |
| Text rendering + layout | **Complete** |
| UI hotspots (Layer) | **Complete** |
| Music playback | **Complete** |
| Nine-patch UI frames | **Complete** |
| Mouse pointer system | **Complete** |
| NoirScript VM | **Complete** — all opcodes implemented + 24 test suites |
| Game data loading (.neon) | **Complete** — chunk-based parser |
| Localization (string table) | **Complete** — .noir file loading |
| Test infrastructure | **Complete** — 19 test suites |
| Dialogue branching | Data model defined, UI partial |
| Scene navigation | Data model only |
| Inventory system | Bridge interface defined, no runtime |
| Sprite / BOB manager | Spec complete, not implemented |
| Timeline / Animation | Spec complete, not implemented |
| Save / Load system | Not started |
