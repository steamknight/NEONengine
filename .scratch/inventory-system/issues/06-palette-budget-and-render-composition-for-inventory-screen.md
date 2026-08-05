# Palette budget and render composition for inventory screen

Type: research
Status: resolved
Blocked by: 04

## Question

Given the requirement for a full-screen inventory screen that reserves indices 32-63 for UI and uses 64-255 for inventory item visuals collectively, what render/palette composition strategy is feasible on the target and consistent with existing engine palette systems?

Research should cover:

- constraints implied by current engine palette ownership and modal overlays
- implications for shared item spritesheet authoring/export
- risks/trade-offs of candidate composition strategies

This ticket is resolved by research findings and a recommended direction.

## Research context pointer

See findings: [06-palette-budget-and-render-composition](../research/06-palette-budget-and-render-composition.md)

Gist: Recommend a dedicated modal inventory palette compositor that temporarily owns indices 32–255 (chrome 32–63, items 64–255), using fade-safe palette publish and restore semantics consistent with existing dialogue/scene palette workflows.

## Answer

Decision: **Palette budget and render composition for inventory screen** is locked.

- While inventory is open, inventory UI owns palette indices 32–255.
  - 32–63: inventory UI chrome.
  - 64–255: inventory item visuals.
- Palette ownership is modal with explicit snapshot/restore of indices 32–255 on open/close.
- Palette updates use fade-safe publish semantics consistent with existing engine palette workflows.
- Inventory item visuals use one shared 64–255 palette contract across the inventory screen (not per-page subsets).
