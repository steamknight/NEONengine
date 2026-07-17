# NEONengine Feature Plan (In Progress)

## High-Level Features
1. Scene/Hotspot system (core loop)
2. Object/character interaction
3. Dialogue tree system
4. Game file loader
5. **Bytecode interpreter** ← SPEC COMPLETE
6. Timeline/Animation system
7. Save/Load system

---

## Bytecode Interpreter Specification

### Architecture
- **Execution model**: Blocking (not yielding), reentrant
- **Registers**: A, B, C, D (parameters), R (return value) — all 8-bit
- **Pointers**: IP (instruction pointer), SP (stack pointer)

### Memory Pools (FAST RAM, configurable sizes)
| Pool | Storage | Default |
|------|---------|---------|
| Boolean flags | `uint32_t[]` bitfield | TBD |
| Integer values | `uint8_t[]` (unsigned) | TBD |
| Stack | `uint8_t[]` | 1024 bytes |

### Reentrant Design
- External systems (dialogue, hotspot) can call scripts mid-execution
- Each call creates new stack frame within shared stack
- On `ret`: restore IP from stack, return value in R

### Instruction Set (Draft)

**Load/Store**
- `ld_a_lit val` — A = literal
- `ld_a_pool idx` — A = pool[idx]
- `ld_a_reg R` — A = register
- (same for B, C, D, R)
- `st_pool idx` — pool[idx] = A

**Arithmetic** (operate on A, store in A)
- `add_lit`, `add_pool`, `add_reg`
- `sub_lit`, `sub_pool`, `sub_reg`
- `mul_lit`, `mul_pool`, `mul_reg`
- `div_lit`, `div_pool`, `div_reg`
- `inc_a`, `dec_a`

**Boolean Pool**
- `set_flag id` — flags[id] = 1
- `clr_flag id` — flags[id] = 0
- `if_set id, addr` — jump if flag set
- `if_not_set id, addr` — jump if flag clear

**Conditionals** (compare A to operand, jump if true)
- `if_eq_lit val, addr`
- `if_eq_pool idx, addr`
- `if_eq_reg R, addr`
- `if_neq_lit`, `if_neq_pool`, `if_neq_reg`
- `if_gt_lit`, `if_gt_pool`, `if_gt_reg`
- `if_gte_lit`, `if_gte_pool`, `if_gte_reg`
- `if_lt_lit`, `if_lt_pool`, `if_lt_reg`
- `if_lte_lit`, `if_lte_pool`, `if_lte_reg`

**Logical** (operate on A)
- `and_lit`, `and_pool`, `and_reg`
- `or_lit`, `or_pool`, `or_reg`
- `xor_lit`, `xor_pool`, `xor_reg`
- `not_a`

**Flow Control**
- `goto addr` — unconditional jump
- `call addr` — push IP+registers, jump
- `ret` — pop IP, R has return value
- `end` — reset SP, terminate script

**External System Calls**
- `goto_scene id` — implicit `end`
- `show_dialogue id` — blocking
- `trigger_anim id` — fire-and-forget
- `play_music id` — fire-and-forget
- `play_sfx id` — fire-and-forget
- `set_volume val`
- `inv_add id` — add item to inventory
- `inv_remove id` — remove item
- `show_text ...` — TBD interface
- `quit` — exit game

---

## Timeline/Animation System ← SPEC COMPLETE

### Timing
- Configurable: frames (1/50s PAL) or milliseconds
- Runtime debug toggle for profiling

### Playback
- Multiple timelines can run concurrently
- Loop modes: none, restart, ping-pong
- Loop count: 0 to infinite
- Scripts can pause/stop timelines by ID

### Data Structure (flat keyframes)
```
Timeline {
    id: uint16
    loop_mode: enum { NONE, RESTART, PINGPONG }
    loop_count: uint8 (0 = infinite)
    keyframes: Keyframe[]
}

Keyframe {
    time: uint16 (frames or ms)
    type: enum { BOB_MOVE, PALETTE_CYCLE, SFX, MUSIC, SCENE, SCRIPT, ... }
    fire_on_loop: bool (default false)
    data: union (varies by type)
}
```

### BOB Keyframe Data
- `bob_id`: uint16
- `x`, `y`: int16
- `visible`: bool
- `frame_id`: uint16

### Events
- Fire at keyframe time (not interpolated)
- `fire_on_loop` flag controls whether event triggers on loop iterations

### VM Opcodes (additions)
- `timeline_start id`
- `timeline_stop id`
- `timeline_pause id`
- `timeline_resume id`

---

## Sprite Manager ← SPEC NEEDED

Central BOB management system. Provides consistent interface for:
- Loading sprite data from game file
- Draw/erase by ID
- Move by ID
- Animate by ID
- Query position/visibility

(Not yet fully specified)

---

## Dialogue System ← SPEC COMPLETE

### Hierarchy
```
Dialogue (container)
  └── Page[] (graph structure, can point to any page)
        └── Choice[] (can only point to pages in same dialogue)
```

### Speaker (global structure)
```
Speaker {
    id: uint16
    name_text_id: uint16
    portrait_bob_id: uint16  // 96×96 BOB
}
```

### Page Data Structure
```
Page {
    speaker_id: uint16
    text_id: uint16
    
    // Flag operations on display
    set_flag_id: int16 (-1 = none)
    clear_flag_id: int16 (-1 = none)
    
    // Script on display
    on_display_script_id: int16 (-1 = none)
    
    // Navigation
    next_page_id: int16 (-1 = end dialogue)
    
    // Conditional display
    check_flag_id: int16 (-1 = always show)
    enabled: bool
    disable_after_read: bool  // tracked in save-game bitfield
    
    // Player choices
    choices: Choice[]
}
```

### Choice Data Structure
```
Choice {
    text_id: uint16
    
    // Flag operations on selection
    set_flag_id: int16 (-1 = none)
    clear_flag_id: int16 (-1 = none)
    
    // Navigation
    goto_page_id: int16 (-1 = end dialogue)
    
    // Script
    on_click_script_id: int16 (-1 = none)
    
    // Conditional display
    check_flag_id: int16 (-1 = always show)
    enabled: bool
    disable_after_read: bool  // tracked in save-game bitfield
}
```

### Display Layout
```
┌────────────────────────────────────────┐
│ ┌──────┐  ┌─────────────────────────┐  │
│ │      │  │ Speaker Name            │  │
│ │ 96×96│  │ Page text here...       │  │
│ │portrait│ │ wrapped as needed...   │  │
│ └──────┘  └─────────────────────────┘  │
│                                        │
│ ┌────────────────────────────────────┐ │
│ │ 1. Choice one                      │ │
│ │ 2. Choice two                      │ │
│ │ 3. Choice three           ▼ more   │ │
│ └────────────────────────────────────┘ │
└────────────────────────────────────────┘
```

### Player Input
- **No choices**: Click anywhere or Spacebar to advance
- **With choices**: Number keys 1-9, or click
- **Overflow**: Max 9 visible choices; scroll indicator if more

### Choice Frame Sizing
- Grows from bottom of screen
- Max height = 256 - (portrait/text area height) - frame padding
- Scrolls if choices exceed available space

### Script Integration
- Page `on_display_script_id` runs when page shown
- Choice `on_click_script_id` runs when selected
- Scripts execute and return control to dialogue manager (reentrant VM)

### Portrait Optimization
- Only update portrait BOB if speaker changes from previous page

### Persistence (save game)
- Page "read once" bitfield
- Choice "read once" bitfield

---

## Scene/Hotspot System ← SPEC COMPLETE

### Scene Overview
The Scene is the core concept — everything is a Scene (gameplay, menus, cutscenes, title).

### Scene Data Structure
```
Scene {
    id: uint16
    background_image_id: uint16
    
    // Layout
    width: uint16 (default 320)
    height: uint16 (default 256)
    offset_x: int16 (default 0)
    offset_y: int16 (default 0)
    
    // Behavior
    constrain_mouse: bool (default false)
    fade_duration_ms: uint16 (default 500)
    
    // Audio
    music_id: int16 (-1 = none, -2 = keep playing)
    
    // Scripts
    on_enter_script_id: int16 (-1 = none)
    on_exit_script_id: int16 (-1 = none)
    
    // First visit
    first_visit_text_id: int16 (-1 = none)
    // Visited flag tracked automatically in save-game bitfield
    
    // Content
    hotspots: Hotspot[]
    bobs: uint16[] (BOB IDs for non-interactive sprites)
}
```

### Hotspot Data Structure
```
Hotspot {
    x, y: int16
    width, height: uint16
    enabled: bool
    cursor_id: uint8
    
    // Optional (use -1 for none)
    image_idle_id: int16
    image_hovered_id: int16
    image_clicked_id: int16
    image_disabled_id: int16
    hover_text_id: int16
    click_script_id: int16
    goto_scene_id: int16  // Shortcut for trivial transitions
}
```

### Hotspot Images
- Rendered as BOBs (overlay on background)
- BOB manager handles background restoration on state change
- 4 states: Idle, Hovered, Clicked, Disabled

### Hotspot Priority
- No overlapping allowed (editor should flag)
- If overlap occurs: first in list wins

### Scene Transitions
- Fade to black → fade in (default 0.5s)
- Duration overridable per-scene
- `goto_scene` VM opcode triggers transition

### Scrolling (TBD)
- If feasible on 8-bitplane 320×256: controlled by timeline/script
- Otherwise: max scene size is 320×256

### Scene-Visited Tracking
- Automatic bitfield array (1 bit per scene)
- Persisted in save game
- Engine checks before showing `first_visit_text`

### VM Opcodes (scene-related)
- `goto_scene id` — fade out, load scene, fade in, implicit `end`
- `scene_scroll x, y` — if scrolling enabled
- `hotspot_enable id`
- `hotspot_disable id`

---

## Next: Game File Format
(Not yet specified)

## Next: Save/Load System
(Not yet specified)
