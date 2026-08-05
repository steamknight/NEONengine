# Inventory ownership store semantics and runtime API

Type: grilling
Status: resolved
Blocked by: none

## Question

What are the canonical runtime semantics for inventory ownership and counts in a playthrough, including:

- valid item-id domain and handling of unknown ids
- stack/count behavior (`add`, `remove`, `has`, `count`, `clear`) and edge cases
- runtime data shape and memory constraints for the Amiga target
- the engine-facing API contract that other systems (VM bridge, UI, save/load) will consume

This ticket should resolve the core inventory-store contract in one place so downstream tickets can depend on it.

## Answer

Decision: **Inventory ownership store semantics and runtime API** are locked as follows.

- Store shape: dense `uint16` count array indexed by authored `INVT` item id.
- Ownership rule: `count > 0` means owned; `count == 0` means not owned.
- Bounds rule: counts are `0..65535`; mutations saturate (`add` caps at max, `remove` floors at 0).
- Invalid id behavior: treat as nonexistent (`has=false`, `count=0`, mutations no-op), with debug log and VM-style error reporting in debug builds.
- VM semantics alignment:
  - `INV_ADD` / `INV_REMOVE` are single-step (`+1` / `-1`) on item id in `D0`.
  - `INV_COUNT` is redefined to require item id in `D0` and return that item's count in `D0`; no no-arg aggregate variant in v1.
- Enumeration order for consumers (inventory UI pages): ascending `INVT` author order.
- Ownership location: inventory store belongs to `game_session` (playthrough state, reset on new game, included in save/load).
