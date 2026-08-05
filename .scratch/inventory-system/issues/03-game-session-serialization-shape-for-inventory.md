# Game-session serialization shape for inventory

Type: grilling
Status: resolved
Blocked by: 01

## Question

How should inventory ownership/count data be represented inside game-session persistence, including:

- canonical serialized shape (binary/layout-level decision scope)
- versioning and compatibility boundaries for save/load
- interaction with existing game-session state responsibilities
- invariants that must hold after save/load round-trips

This ticket resolves persistence design choices, not implementation.

## Answer

Decision: **Game-session serialization shape for inventory** is locked as follows.

- Container: dedicated game-session inventory payload (`INVN`).
- Body shape:
  - `u16 entry_count` (big-endian), followed by
  - `entry_count` big-endian `u16` counts.
- Mapping rule: serialized value at index `i` is count for authored `INVT` item id `i`.
- Save rule: write fixed-length array in `INVT` index order.
- Load mismatch rule (version-tolerant):
  - copy `min(saved_count, current_INVT_count)`,
  - zero-fill remaining current entries,
  - surface a warning through existing error/report mechanisms rather than hard-failing load.
