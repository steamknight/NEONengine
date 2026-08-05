# VM bridge contract for inventory operations

Type: grilling
Status: resolved
Blocked by: 01

## Question

How should Noir VM inventory opcodes map to engine behavior once the inventory store exists, including:

- exact bridge call semantics for `INV_ADD`, `INV_REMOVE`, `INV_HAS`, `INV_COUNT`, `INV_CLEAR`
- return-value/flag behavior consistency with existing VM expectations
- failure handling and bounds behavior for invalid item ids
- where bridge logic stops and where scene/engine logic begins

This ticket resolves the VM-to-engine contract without implementing it.

## Answer

Decision: **VM bridge contract for inventory operations** is locked as follows.

- `INV_COUNT` is redefined as per-item: `D0=item_id -> D0=count(item_id)`.
- Bridge hook changes from `inv_count()` to `u16 inv_count(u16 item_id)`.
- `INV_CLEAR` is redefined as per-item clear: `D0=item_id -> count(item_id)=0`.
- Bridge hook is explicitly renamed to `inv_clear_item(u16 item_id)` to avoid ambiguity with clear-all semantics.
- There is no aggregate/no-arg count opcode and no clear-all opcode in v1.
- Invalid item ids are handled consistently across all inventory opcodes (`INV_ADD`, `INV_REMOVE`, `INV_HAS`, `INV_COUNT`, `INV_CLEAR_ITEM`) as:
  - state-safe behavior (no-op mutations, safe read results), and
  - surfaced through the same VM error reporting channel used today for script errors.
- Status-register behavior is unchanged: only `INV_HAS` writes condition flags; other inventory opcodes preserve SR.
