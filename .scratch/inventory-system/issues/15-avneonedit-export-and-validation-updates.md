# 15 — avNEONedit export and validation updates

**What to build:** The editor enforces the approved 64–255 palette budget for
inventory item visuals so a build catches violations before they reach the engine.
It also surfaces the item-id stability rule so authors know that reordering,
inserting, or deleting items renumbers their runtime ids and can affect save data.

The underlying data schema and export contract are unchanged — `INVC`/`INVT` are
emitted exactly as today. Only the palette validation rules and the authoring
guidance change.

**Blocked by:** 14 — Inventory palette composition (32–255 modal ownership).

**Status:** ready-for-agent

- [ ] The build validates that inventory item visuals fit within the 64–255 palette budget
- [ ] A violation produces a diagnostic that identifies which item and what the overage is
- [ ] The Inventory Manager UI indicates that item order determines runtime id
- [ ] A reorder, insert, or delete that would renumber existing ids surfaces a warning
- [ ] The `INVC`/`INVT` export shape is unchanged
