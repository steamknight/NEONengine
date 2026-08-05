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
   - [Portrait and Inventory Palettes](#portrait-and-inventory-palettes)
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
    - [Shared Binary Authority](#shared-binary-authority)
    - [External ACE Dependency](#external-ace-dependency)
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
| ---------- | ------- |
| CPU | Motorola 68020+ |
| Chipset | AGA (Advanced Graphics Architecture) |
| RAM | 4 MB minimum (Chip + Fast) |
| Storage | HDD only (no floppy) |
| Resolution | 320×200 or 320×256, 8-bit indexed color (256 colors) |
| Refresh | 50 Hz PAL |
| Audio | 4-channel Paula, ProTracker MOD playback via ptplayer |

---

## Architecture

### Layer Stack

```text
┌──────────────────────────────────────────────────────────┐
│                    Game States (states/)                 │
│         splash, lang_selection, dialogue_test, ...       │
├──────────────────────────────────────────────────────────┤
│                   Core Systems (core/)                   │
│   Screen, TextRender, Layer, GameData, Music, NinePatch  │
│   NoirVM (scripting), StringTable, MousePointer          │
├──────────────────────────────────────────────────────────┤
│               C++ Wrapper Layer (ace++/)                 │
│        RAII smart pointers for ACE C resources           │
├──────────────────────────────────────────────────────────┤
│            Minimal Template Library (mtl/)               │
│     vector, array, list, expected, unique_ptr, bstr      │
├──────────────────────────────────────────────────────────┤
│                    ACE Library                           │
│       View/Viewport, Blitter, Copper, Input, Audio       │
├──────────────────────────────────────────────────────────┤
│                   Amiga Hardware                         │
│         Custom chips (Agnus, Denise, Paula)              │
└──────────────────────────────────────────────────────────┘
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
| -------- | ------ | --------- |
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

- 320×200 with PAL vertical centering, or full-height 320×256
- Double-buffered: `screenGetBackBuffer()` / `screenGetFrontBuffer()`
- Blitting: `screenBlitCopy()`, `screenTextCopy()`, `screenClear()`
- Mouse binding: adjusts bounds for PAL offset

### Palette Layout

256 colors divided into fixed-purpose ranges:

| Index Range | Purpose | Notes |
| ------------- | --------- | ------- |
| 0–31 | UI / hardware sprites | Fixed project UI palette |
| 32–39 | BOB reverse slot 5 / portrait / inventory | 8-color physical bank |
| 40–47 | BOB reverse slot 4 / portrait / inventory | 8-color physical bank |
| 48–55 | BOB reverse slot 3 / portrait / inventory | 8-color physical bank |
| 56–63 | BOB reverse slot 2 / portrait / inventory | 8-color physical bank |
| 64–71 | BOB reverse slot 1 | 8-color physical bank |
| 72–79 | BOB reverse slot 0 | 8-color physical bank |
| 80–255 | Background | Scene-specific (176 colors) |

Colors 0–31 remain constant across all scenes. Each scene has its own palette for indices 80–255 (and optionally 32–79).

Runtime palette colors are ACE `.plt` entries in the game pak, addressed by the
Adler32 hash of their normalized runtime paths. Scene setup loads the
background palette first, overlays the project UI palette at indices 0–31, then
overlays active portrait and BOB palettes at indices 32–79. The modal inventory
screen replaces 32–63 with its own palette and restores the scene composition
when closed. Palette cycling operates on the composed register buffer and does
not reopen palette assets.

### BOB Palette Slots

Each instance has an explicit reverse-numbered palette slot. The slot base is
`72 - (slot * 8)`. For bitmap depth `d`, reserve `max(8, 1 << d)` colors. The
base must be aligned to that block and the block must end at or before index 79.

- 1-8 colors: any slot.
- 9-16 colors: slot 1 (64-79), slot 3 (48-63), or slot 5 (32-47).
- 17-32 colors: slot 5 (32-63).
- More than 32 colors: unsupported in the BOB region.

The same palette hash may share one aligned block. Different live palettes may
not overlap, and the manager does not silently wrap to another slot. The
portrait and inventory each occupy 32-63 and conflict with reverse slots 2-5;
they are mutually exclusive.

### Bitplane Shifting

For each opaque bit in the mask generated by `bitmap_conv`, source planes are
cookie-cut into the corresponding low destination planes:

```text
D = (D & ~M) | (S & M)
```

Every destination plane above the source depth is set or cleared under the same
mask according to the aligned base index:

```text
base bit 1: D = D | M
base bit 0: D = D & ~M
```

Where the mask is transparent, the destination remains unchanged. Alignment
ensures that OR-ing the fixed base bits with the local source index is
equivalent to adding the base. The renderer uses the generated mask directly;
it does not allocate a full-size dummy constant bitmap in CHIP RAM.

### Portrait and Inventory Palettes

Uses indices 32-63 (32 colors). It clobbers reverse BOB slots 2-5, which must be
restored when dialogue or inventory mode ends. Dialogue and inventory are
mutually exclusive.

Portrait colors come from the active speaker image. Inventory colors come from
the shared inventory PNG, are padded to exactly 32 entries, and are packaged as
their own `.plt`. The inventory sheet is five bitplanes with local indices
0-31; the aligned masked draw path maps it to 32-63.

Cursor and nine-patch source PNG palettes are conversion-only. Their source
RGB values do not define runtime colors. Cursor local indices 0-15 map to UI
entries 16-31, with local zero transparent, for the attached 4-bitplane
hardware sprite. Nine-patch indices map directly to UI entries 0-31. Their
`BMPS` records reference the shared UI `.plt`, and no individual UI-asset
palettes are stored in the pak. Fonts likewise have no independent palette;
text selects UI palette indices at render time.

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

Context-sensitive persisted cursor values:

| Cursor | Icon | Condition |
| -------- | ------ | ----------- |
| DEFAULT | Arrow | No more specific interaction |
| LOOK | Magnifying Glass | No click action, has description |
| USE | Pointing Finger | Has click script |
| TALK | Chat Bubble | Hotspot override for talkable NPCs |
| GOTO | Exit Sign | Has `gotoSceneId` set |

Persisted `HOTS.cursor` values map directly to fixed cursor slots 0-4 in that
order. Slot 5 is the engine-controlled Wait cursor. Using an inventory item is
a transient interaction mode and displays the Use cursor; it is not a persisted
hotspot cursor value.

Version 2 cursor frames are fixed at 16×16 and rendered through the attached
4-bitplane hardware-sprite path. UIST retains the dimensions for explicit
validation and future format evolution.

`SCEN.show_game_ui` controls both the menu/inventory overlay buttons and cursor
visibility. Clearing it does not disable hotspot hit-testing or click handling,
allowing cursorless cutscene interactions. If any scene sets it, the loaded
game data must provide the complete UI palette, font, nine-patch, and six-cursor
bundle. The inventory button is omitted when inventory is disabled.

A dialogue with one or more currently eligible choices temporarily shows the
cursor even when its scene suppresses normal game UI. Closing dialogue restores
the scene's cursor state. A nonempty CHOC table therefore requires the
six-cursor component.

**AUTO mode resolution order:**

1. `gotoSceneId ≠ 0xFFFF` → GOTO
2. `clickFuncId ≠ 0xFFFF` → USE
3. `hoverStringId ≠ 0xFFFF` → LOOK
4. Else → default arrow

---

## Localization

### String Table Format (.noir)

Separate external localized files, one per configured language, named
`<language>.noir`. Version 2 supports EN, IT, and DE. The exact header,
record framing, checksum, language IDs, and empty-catalog form are defined only
by NEONedit's
[binary contract](https://github.com/steamknight/NEONedit/blob/main/docs/game_data_format.md#noir-string-files).

### Runtime Loading

1. Validate the physical file, version 2 header, checksum, requested language,
   aligned records, exact payload consumption, and complete
   `{version, buildPeriod, buildNumber}` match using the shared binary and
   validation contracts.
2. For a nonempty catalog, evaluate
   `payloadSize + stringCount * 4` in `uint64`, reject a result above
   `0xFFFFFFFF`, and allocate one 4-byte-aligned block of that checked size; the
   offset table begins at `base + payloadSize`. A zero-string catalog uses no
   allocation.
3. Read the payload once, record each string's payload-relative offset, and
   expose O(1) lookup only after complete validation.

Build-version tuple matching detects mixed generations within one project. It
is not a cross-project fingerprint; published artifact sets from different
projects must not be mixed.

---

## Audio

Simple ProTracker MOD playback via ptplayer:

- `musicLoad(path)` — loads .mod file
- `musicPlayCurrent(loop)` — plays with optional loop
- `musicFree()` — cleanup

Sound effects handled via game call opcodes (`PLAY_SFX`, `STOP_MUSIC`, etc.) through the NoirBridge.

`scene_audio` holds the current scene's sound effects resident in Chip RAM for
the life of the scene. ptplayer replays samples straight out of Chip RAM, and
neither a keyframe firing at 50Hz nor a click can stop to read the pak, so every
distinct sound the scene names — from an `SFX_PLAY` keyframe or a hotspot's
click sound — is loaded once at scene load and freed when the scene ends. The
table is keyed by hash, so a sound named several times is read once.

Music is not held that way: a module is far larger than an effect and a scene may
name several, so `MUSIC_PLAY` reads its module when it fires and pays for it with
a hitch.

Nothing stops an effect. A one-shot is over in a few frames, so the replayer
picks the least busy channel and hands it back to the music when the sound ends,
which leaves a four-voice module all four voices for all but those few frames.

---

## Nine-Patch UI Frames

Scalable UI frame rendering for dialogue boxes and panels:

- Optional for dialogue; when absent, portrait/text/choices render without a
  frame

- Constructor takes `ace::bitmap_ptr` source + border sizes (left/top/right/bottom)
- `render(width, height, flags)` → `ace::bitmap_ptr` — produces stretched nine-patch bitmap

---

## Game Data Format (.neon)

> **Replacement format contract:** NEONedit's
> [`game_data_build_plan.md`](https://github.com/steamknight/NEONedit/blob/main/docs/game_data_build_plan.md)
> is the implementation roadmap and linked contract index for the replacement
> version 2 `.neon`, `.noir`, and pak integration. It supersedes the
> incompatible legacy v2 loader layout while retaining the agreed version word
> `2`.
> The
> [game-data format](https://github.com/steamknight/NEONedit/blob/main/docs/game_data_format.md)
> owns the framing contract, and the
> [record-layout document](https://github.com/steamknight/NEONedit/blob/main/docs/game_data_record_layouts.md)
> is the sole authoritative shared byte-layout table. The material below is the
> NEONengine-facing summary of those contracts.

The `.neon` file is a chunk-based binary format containing all structured game
data. The configured `.neon` file and all reachable assets are entries in one
ACE pak. Localization files remain external so the selected `<language>.noir`
can be loaded independently.

### Runtime asset identity

NEONengine never consumes NEONedit's source-path lookup keys. Every asset hash
in `.neon` is the Adler32 of the final normalized entry path relative to the
pak root.

For example:

```text
project source: assets/images/blake.png
pak entry:      assets/images/blake.bm
runtime hash:   Adler32(ASCII("assets/images/blake.bm"))
```

Runtime paths are lowercase ASCII with `/` separators. They contain no rooted
or parent-traversal components. The compiler creates the staging path using
exactly the bytes it hashes, and final pak verification requires the TOC
checksum to match. Images are referenced through `BMPS` IDs whose records hold
bitmap, mask, and palette path hashes; music, SFX, and fonts may be referenced
by path hash directly.

**Design principles:**

- Single owning allocation: load the complete file into one 4-byte-aligned
  block, validate in place, and expose chunk arrays through a fixed TOC
- Zero per-chunk allocation/copy on the big-endian target
- Range indexing for collections (no embedded pointers)
- TOC struct populated at load time for O(1) access
- Exactly two chunk kinds: self-contained Data payloads and fixed-element
  Collection arrays, using the common header defined by the
  [binary contract](https://github.com/steamknight/NEONedit/blob/main/docs/game_data_format.md#neon-container)
- FLGS and VALS names, order, indexes, and initial state come exclusively from
  NEONedit project catalogs. NNASM references those external indexes but cannot
  declare state.

The release loader treats checksum-valid output from NEONedit as trusted game
content. For startup speed on the 68020, it validates the header and performs
one checked structural chunk scan while building the private TOC. The scan
compares known Collection strides and the fixed CONF, UIST, and INVC payload
sizes before exposing typed views. It does not prewalk every record/reference,
predecode all SCRP functions, or pre-open every asset. NEONedit Validate/Build
and post-build staging verification own exhaustive semantic and asset
validation. Assets are resolved on demand and fail explicitly through their
normal loader paths.

After that structural scan, ordinary compiled record IDs, ranges, enums, and
cross-record relationships are trusted; runtime consumers do not duplicate
host validation before forming their views. Bounds checks on dynamic
script/API inputs remain part of those APIs.

NoirVM also trusts staging-verified instruction lengths, branch targets, and
FUNC offsets. The release VM does not perform a remaining-byte check before
each operand read; it retains checks driven by dynamic execution state,
including external function IDs, PC dispatch, logical state indexes, stack
bounds, division by zero, and invalid opcodes.

The structural scan includes the FLGS internal envelope: its logical envelope,
derived from the leading `uint16` flagCount, must be exactly
`2 + ceil(logicalFlagCount / 8)` bytes, and the chunk `size` header field is that
envelope aligned up to 4 bytes, before NoirVM receives the bitfield. Validation
of unused high bits remains part of host staging verification.

For compressed or raw pak entries, the loader sizes its single persistent
GameData allocation from the pak directory's uncompressed size and streams the
entry into that block once. The in-place `.neon` header must report the same
total size before chunk validation. Temporary ACE file/decompression wrappers
are closed after the read and are not GameData-owned or per-chunk allocations.
Before allocation, the uncompressed size must be 4-byte aligned and within
`516..0xFFFFFFFC`.

### Shared binary authority

The version 2 header, build-version tuple, Range semantics, chunk
framing/catalog, numeric domains,
palette references, local field rules, and project/artifact validation are
jointly defined by NEONedit's
[binary contract](https://github.com/steamknight/NEONedit/blob/main/docs/game_data_format.md),
[record layouts](https://github.com/steamknight/NEONedit/blob/main/docs/game_data_record_layouts.md),
and
[validation contract](https://github.com/steamknight/NEONedit/blob/main/docs/game_data_validation.md).

NEONengine declarations match the record-layout tables with naturally aligned
members, no packed structs, and `static_assert` checks for every `sizeof` and
`offsetof`. Runtime code does not maintain a second copy of those tables in
this specification.

#### External ACE dependency

The configured host tools and NEONengine runtime must implement the
compilation contract's `.plt` v2 AGA profile: mode byte 1, big-endian `uint16`
count, and `00 RR GG BB` entries. They must also read the required `.bm`,
`.fnt`, `.sfx`, and hash-only pak outputs and resolve pak entries by path hash.
The legacy checked-in ACE submodule predates this profile and must be updated
for the version 2 loader; capability, rather than an executable version
string, is normative.

### Table of Contents (Runtime)

Populated at load time with pointer/count views into the one owning GameData
block. It exposes `CONF`, `UIST`, `INVC`, and FLGS metadata; pointer/count views
for `CURS`, `BMPS`, `INVT`, `SPKR`, `HOTS`, `TXTR`, `GFXS`, `GFXI`, `SDES`,
`KEYS`, `TRAK`, `TIME`, `FUNC`, `VALS`, `CHOC`, `PAGS`, `DLGS`, and `SCEN`; plus
pointer/byte-size views for `SCRP` and the packed FLGS bytes. No TOC member owns
or copies chunk memory.

FUNC and SCRP are both empty or both nonempty. Nonempty FUNC offsets begin at
zero and strictly increase, partitioning all SCRP bytes into function spans.
Post-build staging verification decodes each span exactly, requires its final
instruction to be unconditional RET, requires every branch target to be an
instruction boundary inside that span, and validates CALL plus intrinsically
typed flag/value operands. Generic immediates have no retained catalog type and
are checked from source/relocation information during Validate/Build. The
release loader does not repeat either pass; NoirVM retains its normal
execution-time bounds and invalid-opcode handling.

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
- Mouse constraint and game-UI visibility

The SCEN offset and dimensions define a background rectangle that must fit
entirely within the configured 320×200/320×256 display. A present background
bitmap exactly matches the rectangle dimensions and is blitted at its stored
offset without clipping. Compiled hotspot, text-region, graphics-instance, and
GFX_MOVE coordinates are already absolute display coordinates; NEONedit applies
the scene offset during compilation. Mouse bounds are derived once at scene
entry when constrain-mouse is enabled, otherwise they cover the full display.

### Hotspot Data

Each hotspot defines a rectangular interactive area with:

- Signed position, unsigned dimensions, and signed z-order
- Context-sensitive cursor (Default/Auto or explicit override)
- Hover text for descriptions
- Click script for interaction logic
- Direct `gotoSceneId` shortcut for trivial transitions
- Initial enabled state
- Optional state graphic, whose frames show normal, hovered, pressed, and
  disabled
- Optional click sound, named by pak-path hash

A click runs the sound first, then the click script, then the transition. The
sound leads because the script may block on a dialogue for as long as it likes
and a `gotoSceneId` tears the scene down, so anything queued behind them may
never be heard.

Click sounds are resident for the life of the scene, loaded alongside the
timeline's effects by `scene_audio`; see [Audio](#audio). A
hotspot that starts disabled still contributes its sound, because a script may
switch it on at any point and the click that follows cannot stop to read the
pak.

Overlapping hotspots are supported. Hit testing selects the enabled hotspot with
the highest z-index; scene list order breaks equal-z ties. The editor warns
about overlaps because they may be difficult to author intentionally.

### Scene Transitions

Fade to black → load scene → fade in. `CONF` supplies the project-wide default
duration in PAL frames. Triggered by the `GOTO_SCENE` VM opcode.

### Scene-Visited Tracking

Automatic bitfield (1 bit per scene), persisted in save game. Engine checks before showing `first_visit_text`.

---

## Dialogue System

### Hierarchy

```text
Dialogue (container)
  └── Page[] (graph — can point to any page in the dialogue)
        └── Choice[] (can only point to pages in same dialogue)
```

### Page & Choice Data

**Page:** Speaker, text, optional flag set/clear/check, on-display script, next-page navigation, choices range, enabled/disable-after-read flags.

**Choice:** Text, optional flag set/clear/check, goto-page navigation, on-click script, enabled/disable-after-read flags.

### Display Layout

One frame, not two — see ADR-0003. The frame is pinned to the top-left beside the
portrait and holds the speaker name, the page text and the choices together. A page
with no speaker, or a speaker with no portrait, gives the frame that column too and
it starts at the left margin instead.

```text
┌─────────────────────────────────────────┐
│ ┌────────┐  ┌─────────────────────────┐ │
│ │        │  │      Speaker Name       │ │
│ │ 96×96  │  │ Page text here...       │ │
│ │portrait│  │ wrapped as needed...    │ │
│ └────────┘  │                         │ │
│             │ 1. Choice one           │ │
│             │ 2. Choice two           │ │
│             │ 3. More...              │ │
│             └─────────────────────────┘ │
└─────────────────────────────────────────┘
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

The inventory display is modal. On entry, it loads the shared inventory
spritesheet's 32-color `.plt` into registers 32-63 and suspends conflicting
portrait/BOB use. On exit, it restores the active scene palette composition.
Registers 0-31 retain the fixed UI palette throughout.

### Pool-Backed Linked List

Fixed memory allocation, linked list semantics, zero heap fragmentation.

```c++
InventoryNode (6 bytes):
    uint16_t itemId     // Index into INVT chunk
    uint16_t count      // Stack count
    uint8_t  next       // Offset to next node (0xFF = end)
    uint8_t  pad
```

### Inventory Operations

| Operation | Complexity | Description |
| ----------- | ------------ | ------------- |
| has(id) | O(n) | Walk from head |
| get_count(id) | O(n) | Walk from head, return count or 0 |
| add(id, n) | O(n) | Increment if exists; else allocate from free list |
| remove(id, n) | O(n) | Decrement; if zero, return node to free list |

### Item Combining (Deferred)

Version 2 has no `CMBN` chunk or built-in item-combination flow. This remains
future work; game-specific interactions use scripts and the current use-item
mode.

### Using Items on Hotspots

1. Player selects item in inventory, clicks "Use" → engine enters use-item mode
   and displays the normal Use cursor
2. Player clicks hotspot → hotspot's click script executes with item ID in D0
3. Script checks item ID and acts accordingly; wrong item → failure SFX

---

## Object / Character Interaction

### Verb System

No verb bar — cursor changes automatically based on hotspot properties (see [Cursor Resolution](#cursor-resolution)).

Characters are simply hotspots with `cursorId = TALK`. Click triggers dialogue via script.

### Right-Click Look

Holding right-click forces LOOK mode on all hotspots with descriptions. Releases when button released.

---

## Sprite Manager

Higher-level manager built on top of ACE's BOB manager.

### Sprite Data

`GFXS` stores the bitmap ID, source x/y within a shared sheet, frame dimensions,
frame count, and animation delay. Frames form a horizontal strip from that
origin, so multiple graphics may select independent or overlapping strips from
one bitmap. Per-instance position, z-order, palette slot, and initial visibility
are stored in `GFXI`.

### Runtime Sprite State

Per-instance: sprite ID, position (x, y), current frame, visibility, palette slot.

### Z-Ordering

- Primary sort: z-order (lower draws first = background)
- Secondary sort: stable scene source order

### Palette Slot Tracking

Tracks ownership of the six reverse-numbered 8-color physical banks and the
palette pak-path hash loaded into each aligned block. Reuses a block when the
same hash and block geometry are requested. Invalid alignment, insufficient
capacity, and conflicting live allocations are errors; there is no automatic
wrap.

NEONedit conservatively treats every GFXI owned by one scene as potentially
live during build validation, regardless of initial or timeline visibility.
Overlapping allocations are valid only with the same palette hash and exact
base/size block.

**Constraints:** Max BOB size 320×256. No hard limit on active BOBs (performance-bound).

**API:** `sprite_show`, `sprite_hide`, `sprite_move`, `sprite_set_frame`, `sprite_animate`, `sprite_set_z`.

---

## Timeline / Animation System

### Timing & Playback

- Serialized timing is always PAL frames (1/50 second); milliseconds are an
  editor display conversion only
- A scene owns zero or one timeline, which runs as part of that scene
- None requires count zero and plays one forward pass
- Restart count zero is infinite; finite N counts complete forward passes
- Ping-pong count zero is infinite; finite N counts complete out-and-back cycles
- Loop mode, count and duration belong to the track, so tracks of one timeline
  loop on their own terms

### Keyframe Data

Each keyframe (12 bytes): time, type enum, flags (interpolation/fire-once), 8-byte type-specific data union.

- **GFX_MOVE** — Move graphic instance (with optional linear interpolation)
- **GFX_VISIBILITY** — Show/hide graphic instance
- **PALETTE_CYCLE_START/END** — Rotate a band of palette entries. The pair is a
  *span*, not two events: the band belongs to the cycle for as long as the
  playhead stands within `[start, end)`, and the rotation is read off the
  playhead's position rather than stepped by a counter, exactly as an
  interpolated `GFX_MOVE` is. That has three consequences worth relying on. A
  ping-pong track's return leg walks its positions backwards, so the band
  rotates backwards with it, at no cost and with no direction flag in the
  format. A band outside its span holds where the span left it rather than
  snapping back, so a ping-pong goes out, holds, and returns the way it came
  with no jump anywhere. And a cycle costs no color arithmetic at all, because
  it moves colors the scene already has: one rotation of a band of entries,
  toward the higher index, with the last wrapping round to the first.

  The one discontinuity left is a `Restart` loop whose span is not a whole
  number of revolutions: it lands back on offset zero at the position the span
  opens at. A restart is a discontinuity by definition, and authoring the span
  so that `end - start == n * width * delay` removes even that one.

  A cycle rotates the *reference* palette (`screenGetPalette()`, which is what
  the scene looks like at full brightness) rather than the display, so a fade
  composes with a running cycle for free — the fade already rebuilds the display
  from the reference every frame and picks the rotation up on its way past.
  Where no fade is running, `palette_cycle_commit()` carries the turned bands
  across itself, scaled to the brightness the screen currently stands at, and
  rebuilds the hardware palette once per frame however many bands moved.

  Two bands of one track may not overlap, which Build enforces. Two bands of
  *different* tracks may, because their clocks are independent and Build cannot
  know where either playhead will be; overlapping rotations do not commute, so
  an authored overlap scrambles both and is left to do so.
- **SFX_PLAY** — Play a sound effect. There is no stop: an effect is a one-shot
  that is over long before anything could ask it to stop, so the replayer picks
  the channel and hands it back to the music when the sound ends. Type id 5 was
  once SFX_STOP and is not reused.
- **MUSIC_PLAY / MUSIC_STOP** — Replace the song, or silence it while leaving
  sound effects playing
- **EXECUTE_SCRIPT** — Run a script function

### Timeline Data

A scene's optional timeline is an 8-byte TIME record holding only a range of
TRAK records. Each 12-byte TRAK is one independently playable track: its
keyframe range, duration, loop mode, loop count, an `auto_start` flag, and a
build-assigned track-name id.

Tracks of one timeline run concurrently on independent clocks. An `auto_start`
track begins when the scene loads; any other waits for a script to play it, and
a track started later begins at its own time zero rather than joining a track
already running. Tracks do not outlive their scene.

`ANIM_PLAY` and `ANIM_STOP` take a track-name id, written in NNASM as
`&track.Name`, and resolve it against the current scene's tracks only. Names are
deliberately reused across scenes so a script that is not owned by any scene can
still address "my scene's Flicker".

Scene-level `goto_scene`, rather than a keyframe, runs when a track completes
its finite playback. The track need not be `auto_start` — an elevator that
departs four seconds after the player presses the button is a script-started
goto track, and the scene simply stays put until it runs. Where several tracks
could complete, the first one wins and the goto fires once.

---

## NoirScript VM

The scripting virtual machine is fully specified in [noir_script_reference.md](noir_script_reference.md),
and its execution model in [noir_vm_execution_model.md](noir_vm_execution_model.md).

**Summary:**

- **Register-based** — 4 general-purpose data registers (D0–D3, 16-bit), status register (N/Z/C/V flags), stack/frame/program counter offsets
- **16-bit word size**, big-endian bytecode
- **Several scripts at once** — sessions in a run list, each with its own stack and registers, each given a slice of a per-frame instruction budget
- **Addressing modes** packed in a single mode byte — immediates, registers, FP-relative locals, VALUES array (direct and register-indexed)
- **External game state**: borrowed FLAGS bitfield plus its exact logical flag
  count, and a VALUES array of 16-bit integers; padding bits in the final FLAGS
  byte are never addressable
- **Engine bridge**: `NoirBridge` struct of function pointers decouples VM from all engine systems (inventory, scenes, dialogue, audio, screen, animations, save/load)
- **Deferred bridge calls**: the kind is declared with the function in the bridge table. A modal one (SAY, SHOW_DIALOG, SAVE_GAME, FADE_OUT/IN) records a request and spends the rest of the frame budget, ending the update; a terminal one (GOTO_SCENE, LOAD_GAME, NEW_GAME, QUIT_GAME) also stops the VM until it is torn down. The scene acts on the request and decides when the VM runs again
- **Waiting**: `WAIT n` parks one session for n VM updates
- **~70 opcodes** across 10 categories: script execution, data movement, arithmetic, logic, control flow, flag operations, value operations, game calls (inventory / timeline / scenes / dialogue-audio / menu-settings), and debug
- **Error handling**: all errors returned as `noir_result` codes — no exceptions. An error kills the session that caused it and leaves the rest running

---

## Save Game Format (.nsg)

### File Discovery

Scan dedicated save directory for `*.nsg` files. Sort by file timestamp for display order. Header contains user-defined save name + scene name for save-slot UI.

### Save Header

64 bytes:

| Offset | Size | Field | Description |
| -------- | ------ | ------- | ------------- |
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
| ------- | --------- |
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
| ----------- | --------- |
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
| -------- | -------- |
| Screen / fading | **Complete** |
| Text rendering + layout | **Complete** |
| UI hotspots (Layer) | **Complete** |
| Music playback | **Complete** |
| Nine-patch UI frames | **Complete** |
| Mouse pointer system | **Complete** |
| NoirScript VM | **Complete** — all opcodes implemented + 24 test suites |
| Game data loading (.neon) | Legacy v2 loader complete; replacement v2 loader planned in game-data build Phase 7 |
| Localization (string table) | Legacy v2 loader complete; replacement v2 loader planned in game-data build Phase 7 |
| Test infrastructure | **Complete** — 19 test suites |
| Dialogue branching | Data model defined, UI partial |
| Scene navigation | Data model only |
| Inventory system | Bridge interface defined, no runtime |
| Sprite / BOB manager | Spec complete, not implemented |
| Timeline / Animation | Spec complete, not implemented |
| Save / Load system | Not started |
