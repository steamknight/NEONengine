# 10 — Wire VM inventory opcodes — bridge redefinition and `game_session` wiring

**What to build:** Scripts that call inventory opcodes now actually change and query
what the player owns. This ticket both redefines the two stubs whose signatures have
changed and wires all five opcodes to the real `game_session` store.

`INV_COUNT` takes an item id in D0 and puts that item's count back in D0. The
bridge hook changes from a no-argument function to `u16 inv_count(u16 item_id)`.

`INV_CLEAR` takes an item id in D0 and zeros that item's count. The bridge hook
is renamed to `inv_clear_item(u16 item_id)` to make the per-item contract explicit
in code. There is no clear-all opcode in this version.

The mock bridge in the test utilities is updated to match both new signatures
before anything else compiles. The existing VM inventory tests are rewritten to
assert the new per-item semantics rather than the old no-arg behavior.

`INV_ADD`, `INV_REMOVE`, and `INV_HAS` keep their existing opcode bytes and D0
argument convention; only their bridge implementations change from no-ops to real
calls.

The `INV_HAS` condition-flag behavior is unchanged. All other inventory opcodes
still leave SR unmodified.

Both reference documents (`noir_script_reference.md` and
`neon_assembly_reference.md`) are updated to show the new per-item argument for
`INV_COUNT` and `INV_CLEAR`.

**Blocked by:** 09 — `game_session` inventory store.

**Status:** ready-for-agent

- [ ] `INV_ADD` increments the item id in D0 by one in the game session
- [ ] `INV_REMOVE` decrements the item id in D0 by one in the game session
- [ ] `INV_HAS` sets Z when the player owns at least one of the item in D0
- [ ] `INV_COUNT` takes item id from D0 and puts that item's count in D0
- [ ] `INV_CLEAR` takes item id from D0 and zeroes that item's count
- [ ] The bridge hook for count is `u16 inv_count(u16 item_id)`
- [ ] The bridge hook for clear is `inv_clear_item(u16 item_id)`; the old `inv_clear` form is gone
- [ ] The mock bridge in the test utilities reflects both signature changes
- [ ] Existing `noir_vm_inventory_tests.h` tests for the old no-arg `INV_COUNT` and `INV_CLEAR` are replaced with per-item equivalents
- [ ] An out-of-range id on any inventory opcode surfaces an error via the VM error channel in debug builds without changing game state
- [ ] SR is unchanged after `INV_ADD`, `INV_REMOVE`, `INV_COUNT`, and `INV_CLEAR`
- [ ] Both reference documents reflect the updated per-item argument for `INV_COUNT` and `INV_CLEAR`
