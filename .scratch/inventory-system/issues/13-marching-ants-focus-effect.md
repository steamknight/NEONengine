# 13 — Marching-ants focus effect

**What to build:** The focused grid cell shows a selection effect: the border of
the cell cycles between two colors drawn from the UI palette band (indices 32–63),
producing a marching-ants animation. The effect uses the engine's existing palette
cycling mechanism. When no cell is focused there is no cycling effect.

**Blocked by:** 11 — Inventory modal UI skeleton — layout and pagination.

**Status:** ready-for-agent

- [ ] The focused cell's border animates between two palette colors in the 32–63 band
- [ ] The colors cycle over time to produce a marching-ants effect
- [ ] The effect uses the engine's existing palette cycling path
- [ ] Cells that are not focused show no cycling effect
- [ ] Moving focus to a different cell transfers the effect immediately
