# Inventory screen layout, grid, and pagination rules

Type: prototype
Status: resolved
Blocked by: 04

## Question

What concrete layout contract should the full-screen inventory use, including:

- title, close button, one-line hover-name region, grid region, and side description area geometry
- horizontal grid flow rules and page capacity
- pagination control placement and behavior
- visual interaction states needed for clear affordances on Amiga hardware

Resolve this with a lightweight prototype artifact and explicit layout rules suitable for implementation.

## Answer

Decision: **Inventory screen layout and pagination rules** are locked.

- Composition:
  - title row (top),
  - one-line hover-name strip below title,
  - central horizontal-flow wrapping grid,
  - bottom control strip: Prev / page indicator / Next / Close.
- Side description panel is removed.
- Item visuals are unscaled authored frames in padded fixed cells.
- Page capacity is computed from grid geometry and cell size (not hard-coded).
- Pagination is non-wrapping with disabled ends.
- Selection/focus effect uses two colors in UI palette band 32–63 with palette-cycled marching-ants effect.
- Spacing rhythm reuses existing UI conventions (8/4/8).

Prototype artifact:
- [Inventory UI Prototype — Layout Wireframe](../prototype/05-layout-wireframe.md)
