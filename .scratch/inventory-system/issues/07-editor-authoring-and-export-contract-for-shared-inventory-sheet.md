# Editor authoring and export contract for shared inventory sheet

Type: research
Status: resolved
Blocked by: 01

## Question

What changes (if any) are required in `..\avNEONedit\` so inventory authoring remains simple while supporting the chosen engine-direction, including:

- one-shared-spritesheet workflow for all inventory items
- frame assignment and data model implications
- export/build contract implications for inventory metadata and palettes
- migration impact for existing project data

This ticket resolves editor contract direction and its dependencies on engine decisions.

## Research context pointer

- [Findings: Editor authoring and export contract for shared inventory sheet](../research/07-editor-authoring-export-contract.md)

Gist: avNEONedit already supports the one-shared-inventory-sheet engine-first direction with compliant `INVC`/`INVT` export and palette/frame validation; no mandatory editor schema change is required now. Main forward risk is positional item-id stability (order-based IDs) for future save compatibility.

## Answer

Decision: **Editor authoring and export contract for shared inventory sheet** is locked.

- Keep current avNEONedit authoring model and base `INVC`/`INVT` export contract for v1 (no mandatory schema change).
- Require explicit positional item-id stability guidance (`INVT` order determines runtime id), with warning/documentation coverage for reorder/insert/delete save-compat impact.
- For approved engine palette direction (UI 32–63, items 64–255), require editor build/export validation updates to enforce the new shared inventory-visual palette budget while keeping schema unchanged.
