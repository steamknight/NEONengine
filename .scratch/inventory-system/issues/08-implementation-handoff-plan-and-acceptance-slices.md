# Implementation handoff plan and acceptance slices

Type: grilling
Status: resolved
Blocked by: 02, 03, 05, 06, 07

## Question

Once core decisions are closed, what is the minimum-risk implementation handoff plan for engineering execution, including:

- recommended implementation slice order across engine and editor
- acceptance criteria per slice
- integration risks and mitigations
- explicit non-goals for the first implementation pass

This ticket closes the map by turning decisions into a build-ready execution handoff.

## Answer

Decision: **Implementation handoff plan and acceptance slices** are locked.

- Execution order:
  1. `game_session` inventory store + VM bridge/opcode updates
  2. inventory modal UI skeleton + layout/pagination
  3. popup/hover/focus interactions
  4. palette 32–255 composition ownership
  5. avNEONedit export/validation updates
  6. integration tests + serialization-contract documentation
- Save/load pipeline implementation is out of scope for this effort; only serialization contract documentation is in scope.
- Explicit non-goals:
  - item-combination mechanics
  - script-initiated inventory open
  - save/load pipeline implementation

Handoff artifact:
- [Inventory System Implementation Spec](../spec.md)
