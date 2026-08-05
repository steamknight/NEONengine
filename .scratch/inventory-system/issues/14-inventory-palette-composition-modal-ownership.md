# 14 — Inventory palette composition (32–255 modal ownership)

**What to build:** When the inventory opens it takes ownership of palette indices
32–255 for the duration of the session. On entry it loads the inventory UI chrome
into 32–63 and the shared item-visual palette into 64–255, then publishes both
changes at once using the fade-safe publish path. On exit it restores the snapshot
it took on entry and publishes again. Indices 0–31 are never touched — they hold
the fixed UI and cursor colors throughout.

Item visuals share one palette across the whole inventory screen; there are no
per-page palette subsets.

This follows the same snapshot/restore pattern used by the dialogue portrait band.

**Blocked by:** 11 — Inventory modal UI skeleton — layout and pagination.

**Status:** ready-for-agent

- [ ] On inventory open, palette indices 32–255 are snapshotted
- [ ] Inventory UI chrome colors are loaded into indices 32–63
- [ ] Shared inventory item-visual colors are loaded into indices 64–255
- [ ] The full 32–255 range is published in one call using the fade-safe publish path
- [ ] Palette indices 0–31 are unchanged while the inventory is open
- [ ] On inventory close, the snapshotted 32–255 range is restored and published
- [ ] The restored palette matches the scene state that was active before the inventory opened
