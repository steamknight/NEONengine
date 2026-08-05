# 16 — Serialization contract documentation

**What to build:** The `INVN` payload shape is written into the engine's format
reference so the save-slot format is not improvised later. No save or load code is
written — this is documentation only.

The payload:

```
u16  entry_count          (big-endian — equals INVT count at save time)
u16  counts[entry_count]  (big-endian — index i = count for INVT item id i)
```

The mismatch load policy is also documented: if the saved count and the current
`INVT` count differ, copy the minimum of the two, zero-fill the remainder, and
surface a warning rather than refusing the load.

**Blocked by:** 09 — `game_session` inventory store.

**Status:** ready-for-agent

- [ ] The `INVN` payload shape is added to the engine format reference document
- [ ] The big-endian `u16` encoding convention is noted
- [ ] The mismatch load policy (copy-min, zero-fill, warn) is documented
- [ ] No save or load implementation code is written
