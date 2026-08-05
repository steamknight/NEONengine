# Inventory System Wayfinding Map

Labels: wayfinder:map

## Destination

A signed-off implementation spec for NEONengine + avNEONedit inventory: runtime ownership/count store, VM bridge integration, game-session serialization, and full-screen inventory UI behavior, ready for build execution.

## Notes

- Planning-only map: resolve decisions, do not implement features here unless a task ticket is required to unblock a decision.
- Vocabulary should follow `CONTEXT.md` (use "game session", "dialogue", "scene", etc.).
- Treat `..\avNEONedit\` as a compatibility reference, not as hard doctrine; engine-first decisions are allowed with explicit editor-delta decisions.
- Consult these references while resolving tickets: `neonengine_spec.md` (Inventory System + palette sections), `src/core/noir_bridge.cpp`, `src/core/game_session.h`, and `..\avNEONedit\docs\neonedit_spec.md` inventory sections.

## Decisions so far

<!-- the index — one line per closed ticket: enough to judge relevance, then zoom the link for the detail the ticket holds -->

- [Inventory ownership store semantics and runtime API](issues/01-inventory-ownership-store-semantics-and-runtime-api.md) — Use a `game_session`-owned dense `uint16` per-item count store, saturating math, invalid-id no-op with debug VM-style reporting, author-order enumeration, and `INV_COUNT(item_id)` semantics.
- [VM bridge contract for inventory operations](issues/02-vm-bridge-contract-for-inventory-operations.md) — Redefine `INV_COUNT` and `INV_CLEAR` as per-item operations in `D0`, rename bridge clear hook to `inv_clear_item`, keep SR behavior, and surface invalid ids through VM error reporting while preserving safe no-op state behavior.
- [Game-session serialization shape for inventory](issues/03-game-session-serialization-shape-for-inventory.md) — Persist inventory in a dedicated `INVN` payload as `entry_count + big-endian u16 counts` in `INVT` order, with tolerant load on length mismatch (copy-min, zero-fill, warn).
- [Full-screen inventory UX flow and interaction contract](issues/04-full-screen-inventory-ux-flow-and-interaction-contract.md) — Inventory is player-opened modal full-screen UI; hover shows one-line item name; single-click opens dismissible description popup; Esc/Close exits inventory; pagination is non-wrapping Prev/Next; inventory open is blocked during other modals.
- [Inventory screen layout, grid, and pagination rules](issues/05-inventory-screen-layout-grid-and-pagination-rules.md) — Full-screen layout is title + hover-name strip + unscaled item grid + bottom controls; page capacity is geometry-derived; focus uses 32–63 UI-band marching-ants palette cycling; detailed wireframe captured in prototype artifact.
- [Palette budget and render composition for inventory screen](issues/06-palette-budget-and-render-composition-for-inventory-screen.md) — Inventory modal owns palette 32–255 while open (UI 32–63, items 64–255), with snapshot/restore of 32–255 and fade-safe publish; item visuals share one global 64–255 palette contract.
- [Editor authoring and export contract for shared inventory sheet](issues/07-editor-authoring-and-export-contract-for-shared-inventory-sheet.md) — Keep v1 shared-sheet schema/export model, add explicit positional item-id stability warning policy, and update editor export/validation for the approved 64–255 inventory-visual palette budget.
- [Implementation handoff plan and acceptance slices](issues/08-implementation-handoff-plan-and-acceptance-slices.md) — Execution order and first-pass non-goals are locked; build-ready handoff captured in the Inventory System Implementation Spec.

## Not yet specified

None for this destination.

## Out of scope

- Inventory item-combination mechanics and authoring (deferred beyond this destination).
- Save/load pipeline implementation (serialization contract documented only in this effort).
