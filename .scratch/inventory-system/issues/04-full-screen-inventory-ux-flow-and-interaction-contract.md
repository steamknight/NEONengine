# Full-screen inventory UX flow and interaction contract

Type: grilling
Status: resolved
Blocked by: none

## Question

What is the canonical player interaction contract for the inventory screen, including:

- invocation and close behavior
- selection, hover, click, and double-click semantics
- description presentation behavior
- interaction constraints with modal systems (especially dialogue) and script flow

This ticket resolves behavior-level UX decisions before detailed layout and rendering tickets.

## Answer

Decision: **Full-screen inventory UX flow and interaction contract** is locked as follows.

- Inventory is a modal full-screen state:
  - scene interaction and VM progression are paused while open,
  - close returns to scene exactly where the player left it.
- Entry point is player-initiated only (no script-initiated open in v1).
- Side description panel is removed.
- Hover behavior: one-line hover text is filled with the item's name.
- Click behavior: single-click sets focus and opens a dismissible description popup for that item.
- Popup close controls: mouse click or Space.
- Inventory close controls: Esc or explicit Close button.
- Pagination behavior: explicit Prev/Next controls (optional keyboard Left/Right), non-wrapping, with disabled controls at first/last page.
- Modal conflict rule: inventory open is blocked while dialogue or any other modal is active.
