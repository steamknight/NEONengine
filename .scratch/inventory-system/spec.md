# Inventory System

Status: ready-for-agent

## Problem Statement

The game engine has authored inventory data (`INVC`/`INVT` chunks) and VM opcodes that scripts can call, but the runtime store behind those opcodes is entirely stubbed — every bridge function is a no-op. Scripts cannot give the player an item, check whether they have one, or react to inventory state. There is also no way for the player to look at what they are carrying.

## Solution

Wire up the stubbed bridge so that a `game session` tracks what the player owns, how much of each item, and that state survives the playthrough. Add a full-screen inventory state the player can open from the game UI to browse their items, see item names and descriptions, and page through their collection.

## User Stories

1. As a script author, I want `INV_ADD` to add one of an item to the player's inventory, so that a hotspot can give the player an object.
2. As a script author, I want `INV_REMOVE` to remove one of an item from the player's inventory, so that scripts can consume or use items.
3. As a script author, I want `INV_HAS` to set the Z flag based on whether the player owns at least one of an item, so that conditional jumps can branch on possession.
4. As a script author, I want `INV_COUNT` to return how many of a specific item the player owns, so that scripts can react to item quantities.
5. As a script author, I want `INV_CLEAR` to set a specific item's count to zero, so that scripts can fully remove an item without repeated `INV_REMOVE` calls.
6. As a script author, I want adding an item beyond the maximum to saturate rather than overflow, so that edge cases in scripts do not corrupt inventory state.
7. As a script author, I want removing an item from zero to remain at zero, so that scripts cannot accidentally underflow into a false large count.
8. As a script author, I want calling an inventory opcode with an item id that is out of range to produce a visible error in debug builds, so that authoring mistakes are diagnosable.
9. As a script author, I want an out-of-range id to leave inventory state unchanged, so that a mistake in one script does not corrupt the rest of the playthrough.
10. As a player, I want to open the inventory from the game UI, so that I can see what I am carrying.
11. As a player, I want the inventory to cover the full screen, so that I can see my items clearly.
12. As a player, I want to see my items laid out in a grid, so that I can scan everything I have at a glance.
13. As a player, I want the inventory to tell me the name of whichever item my pointer is over, so that I can identify items quickly.
14. As a player, I want a single click to open a description popup for an item, so that I can read what the item is.
15. As a player, I want the description popup to close when I click or press Space, so that the closing method matches how other popups work.
16. As a player, I want to close the inventory with Esc or a Close button, so that there is always an obvious way out.
17. As a player, I want Prev and Next controls to move between pages of items, so that I can reach items that do not fit on one screen.
18. As a player, I want the Prev control to be disabled on the first page and the Next control disabled on the last page, so that I know when I have reached the ends.
19. As a player, I want the focused item to show a marching-ants selection effect, so that I can tell which item is selected.
20. As a player, I want the scene to be exactly as I left it when I close the inventory, so that browsing items does not disrupt what is happening in the world.
21. As a player, I want the inventory to be unreachable while a dialogue is on screen, so that the two full-screen states cannot conflict.
22. As a writer, I want all inventory items to share one spritesheet, so that palette management and asset setup are simple.
23. As a writer, I want the authoring workflow in avNEONedit to remain unchanged for the shared-sheet model, so that existing projects do not need to be re-authored.
24. As a developer, I want inventory state to live in the game session, so that it resets on a new game and can be included in save data.
25. As a developer, I want inventory items enumerated in authored order when the UI needs to page through them, so that the display is predictable and deterministic.
26. As a developer, I want the inventory serialization shape defined now even though save/load plumbing is out of scope, so that the save format does not need rethinking later.
27. As a developer, I want inventory bridge changes to not require new opcodes, so that the bytecode format is stable.
28. As a developer, I want the inventory state to be testable independently of hardware, so that store logic can be verified in the test runner.
29. As a developer, I want the VM bridge inventory tests to test the new per-item count semantics, so that the contract changes are covered.

## Implementation Decisions

### Inventory store

**`game_session` extension**: Inventory state is added to the existing `game_session` class as a dense `u16` count array, sized from the loaded `INVT` count at session creation. An item is owned when its count is greater than zero.

**Count bounds**: Saturating arithmetic — `add` clamps at 65535, `remove` floors at zero. No error is reported for saturating; it is silent and correct.

**Invalid item id policy**: Any opcode that receives an item id outside the `INVT` range treats the id as nonexistent. Reads return `false`/`0`; mutations are no-ops. In debug builds the error is surfaced through the same bridge `report_error` channel that script errors use today — the player sees the same kind of error box, the game state stays intact.

**Enumeration order**: Consumers (the inventory UI, the serializer) enumerate owned items in ascending `INVT` author-index order.

### VM bridge/opcode contract

All five inventory opcodes remain at their existing opcode bytes (`0x80`–`0x84`). Two are redefined:

- **`INV_COUNT` (`0x83`)**: Changed from a no-arg aggregate to a per-item query. Input: `D0=item_id`. Output: `D0=count(item_id)`. Bridge hook signature changes from `u16 inv_count()` to `u16 inv_count(u16 item_id)`.
- **`INV_CLEAR` (`0x84`)**: Changed from a clear-all to a per-item clear. Input: `D0=item_id`. Effect: sets count for that item to zero. Bridge hook is renamed from `inv_clear` to `inv_clear_item(u16 item_id)` to make the per-item contract explicit in code.

Status register behavior is unchanged: only `INV_HAS` writes condition flags (Z flag as today). All other inventory opcodes leave SR unmodified.

The invalid-id VM error surfacing applies consistently to all five opcodes.

### Game-session serialization contract (format, not implementation)

The `INVN` payload in the future game-session save format is:

```
u16  entry_count          (big-endian, equals INVT count at save time)
u16  counts[entry_count]  (big-endian, index i = count for INVT item id i)
```

Load mismatch policy (for future save/load implementation): copy `min(saved_count, current_INVT_count)`, zero-fill remaining current entries, surface a warning rather than hard-failing. Big-endian encoding is consistent with all other engine data conventions.

### Inventory UI state

The inventory screen is a new ACE state, modelled on the dialogue and message states. It is modal: the scene loop is suspended while it is active, and resumes unchanged when the state is popped. Open is player-initiated only (via the existing game-UI inventory button). The state cannot be entered while any other modal is active.

**Layout** (see prototype wireframe at `.scratch/inventory-system/prototype/05-layout-wireframe.md`):

```
+--------------------------------------------------------------+
| Inventory                                                    |  ← title row
|--------------------------------------------------------------|
| Hover: <item name>                                           |  ← one-line name strip
|--------------------------------------------------------------|
| [ ] [ ] [ ] [ ] [ ] [ ] [ ] [ ] [ ] [ ] [ ] [ ]             |
| [ ] [ ] [ ] [ ] [ ] [ ] [ ] [ ] [ ] [ ] [ ] [ ]             |
| [ ] [ ] [ ] [ ] [ ] [ ] [ ] [ ] [ ] [ ] [ ] [ ]             |  ← item grid
|                                                              |
|--------------------------------------------------------------|
| Prev        Page 1 / N             Next              Close   |  ← control strip
+--------------------------------------------------------------+
```

- Item art is drawn unscaled at its authored frame size; each cell adds fixed padding for affordance.
- Items-per-page is computed from `floor(gridWidth / cellWidth) × floor(gridHeight / cellHeight)`; it is not hard-coded.
- Spacing follows the 8px/4px/8px rhythm used by the existing dialogue and message frames.
- There is no side description panel.

**Interaction**:
- Hover over a cell fills the one-line name strip with that item's name.
- Single-click on a cell opens a dismissible description popup for that item.
- Description popup dismisses on mouse click or Space, matching existing popup behavior.
- Inventory closes on Esc or the Close button.
- Prev/Next pagination is non-wrapping; controls disable at the first/last page.

**Focus effect**: Two colors from the UI-reserved band (palette 32–63) with palette cycling produce a marching-ants selection effect on the focused cell.

### Palette composition

The inventory modal owns palette indices 32–255 while open:
- 32–63: inventory UI chrome.
- 64–255: inventory item visuals.

On open, the engine snapshots reference entries 32–255. On close, it restores them and publishes. Indices 0–31 (fixed UI and cursor) are never touched. Palette publication uses the fade-safe `screenPublishPalette` path, matching the pattern used by dialogue/portrait palette ownership.

Inventory item visuals share **one 64–255 palette contract across the whole inventory screen** — no per-page or per-category palette subsets.

### Editor contract (avNEONedit)

The avNEONedit shared-sheet authoring model and base `INVC`/`INVT` schema/export contract require **no schema changes** for v1. Two updates are required:

1. Build/export validation must be updated to enforce the new 64–255 inventory-visual palette budget.
2. Item-id stability must be explicitly documented and surfaced in the editor: runtime item id equals `INVT` list index, so reorder/insert/delete renumbers ids and affects future save compatibility.

### Implementation slice order

1. `game_session` inventory store + VM bridge/opcode changes.
2. Inventory modal UI skeleton + layout/pagination.
3. Popup/hover/focus interactions.
4. Palette 32–255 composition and open/close lifecycle.
5. avNEONedit export/validation updates.
6. Integration tests + serialization-contract documentation.

## Testing Decisions

A good test for this feature checks externally observable behavior — what the store returns, what opcodes produce, what the bridge receives — not internal representation. Tests should not inspect private fields or depend on the count array's memory layout directly.

**`game_session` inventory store** — new test suite alongside the existing `game_session` tests:
- session starts with all counts zero and sized from the loaded `INVT` count,
- add increments the count for the addressed item,
- add saturates at 65535 rather than overflowing,
- remove decrements the count for the addressed item,
- remove floors at zero rather than underflowing,
- has returns true when count > 0, false otherwise,
- count returns the exact count for the addressed item,
- clear_item sets the addressed item's count to zero,
- operations on an out-of-range id leave all other counts unchanged,
- the store resets fully on `create()` called a second time.

Prior art: `game_session_tests.h` — same test-data-driven style, helper factory functions, boundary and isolation cases.

**VM bridge inventory opcodes** — update and extend the existing `noir_vm_inventory_tests.h`:
- `INV_COUNT` passes `D0` as item_id and puts the bridge result in `D0`.
- `INV_CLEAR` (now `INV_CLEAR_ITEM`) passes `D0` as item_id to `inv_clear_item`.
- `INV_COUNT` and `INV_CLEAR` previously took no argument; existing tests for the old no-arg semantics must be replaced with per-item tests.
- Invalid-id opcodes place an error message in the bridge's report-error channel and do not modify other state.
- SR is unchanged for `INV_ADD`, `INV_REMOVE`, `INV_COUNT`, `INV_CLEAR`.

Prior art: `noir_vm_inventory_tests.h` + `noir_vm_test_utils.h` — `bytecode_builder`, `g_mock_log`, `g_mock_bridge`, `message_has()`.

**Mock bridge** — the `mock_inv_count` and `mock_inv_clear` implementations in `noir_vm_test_utils.h` must be updated to accept an `item_id` argument matching the new bridge signatures.

The inventory UI state and palette composition are hardware-dependent and are not testable in the headless test runner; they are verified through manual play and integration runs.

## Out of Scope

- **Inventory item-combination mechanics.** Scripted use-item-on-hotspot interactions remain the mechanism for game-specific item interactions; no built-in combination system is planned in this effort.
- **Script-initiated inventory open.** The inventory screen can only be opened by the player through the game UI in v1.
- **Save/load pipeline implementation.** The `INVN` payload shape is documented here as a contract for future implementation; no save or load code is written.

## Further Notes

The `INV_COUNT` and `INV_CLEAR` opcode redefinitions are breaking changes relative to the current stub signatures. The existing `noir_vm_inventory_tests.h` tests for those opcodes must be updated in the same change that updates the bridge and VM. The `noir_script_reference.md` and `neon_assembly_reference.md` docs should be updated to reflect the new per-item argument semantics before the implementation lands.
