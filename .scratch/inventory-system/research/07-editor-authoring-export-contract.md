# 07 — Editor authoring and export contract for shared inventory sheet

## Summary

avNEONedit already implements an engine-first-compatible inventory authoring model built around one shared sheet (`inventory.image`) with per-item frame references, and exports directly to `INVC` + `INVT` in the shape NEONengine expects. `INVC`/`INVT` emission includes disabled-state handling, frame geometry, and item range wiring that matches both editor docs and engine loader contracts. No mandatory schema change is required for the shared-sheet direction itself. The main contract risk is ID stability: item IDs are positional (`INVT` index), so insert/delete/reorder renumbers IDs and could affect save compatibility once runtime inventory persistence (`INVN`) is active. Existing legacy project migration for inventory image references (name -> Adler key) is already implemented.

## Repositories discovered

- `avNEONedit` — editor data model, authoring UI, build/export pipeline, and format docs for inventory (`InventorySettings`, `GameDataBuilder`, `NeonWriter`, docs).
- `NEONengine` — runtime format contract (`inventory_config`, `inventory_item`), VM bridge/API consumption, current runtime implementation status.

## Current-state summary

1. **Authoring model is already one shared inventory sheet for all items.**
   `InventorySettings` has `image`, `frame_width`, `frame_height`, and ordered `items`; each item stores `frame`, `name`, `description`.

2. **Editor stores canonical image references as Adler32 keys and tolerates legacy names.**
   Inventory sheet selection resolves from stored reference to display name on load and persists back as canonical key on modify.

3. **Build/export contract is explicit and already aligned.**
   - `INVT` rows are built from project item order (`id=i`) with required `name` and `description` strings.
   - `INVC` is collapsed to disabled state when no items exist.

4. **Validation enforces inventory-sheet constraints only when inventory has items.**
   Non-empty inventory requires sheet reference; frame dimensions must fit nonzero `uint16`; item frames must be within row-major capacity.

5. **Palette contract is already implemented for the shared sheet.**
   Inventory role max colors = 32; conversion/runtime entries are clamped and padded to 32.

6. **Neon writer emits `INVC`/`INVT` exactly in expected forms.**
   Disabled inventory writes `0xFFFF` sheet ID + zeroed geometry/range; enabled writes sheet BMPS ID, frame dimensions, and item range.

7. **Engine contract side is compatible and strict on chunk shape.**
   Engine defines `inventory_config`/`inventory_item` layouts and requires full known-chunk table/valid strides.

## Minimal viable editor changes (required now)

**Required changes: none** for the shared-sheet engine-first direction.

Why:
- Data model already centers one shared sheet + per-item frame mapping.
- Export already emits `INVC`/`INVT` in contract-compliant shapes (including disabled inventory sentinel state).
- Palette and frame constraints are already validated/padded as needed.

## Optional follow-on improvements

1. **Make ID-stability policy explicit in editor UX/docs** (pre-1.0 save compatibility safeguard): warn that item order controls runtime IDs.
2. **If reordering is introduced/expanded, gate it behind warnings or migration tooling** because IDs are positional.
3. **Add a build-time/info diagnostic summarizing potential save-impact when inventory IDs change** (future-friendly once `INVN` saves are active).

## Compatibility and migration risks

1. **Legacy image-reference migration is covered.**
   Name-based inventory sheet refs are migrated to canonical Adler keys on load.

2. **Positional item IDs are the key long-term risk.**
   `INVT` ID = list index; changing order/insert/delete renumbers IDs, which can affect persisted inventory-state semantics once save inventories (`INVN`) are relied upon.

3. **Null `name`/`description` on items are model-allowed during editing but build-required.**
   Existing partial data can fail build until strings are filled.

## Recommended contract shape between editor and engine

### Editor output contract (authoring/build side)

- `inventory.image`: canonical image asset key (Adler32 string), required only when `items` is non-empty.
- `inventory.frame_width`, `inventory.frame_height`: nonzero `uint16` representable when enabled; zeroed in disabled state.
- `inventory.items`: ordered list; runtime item ID is ordinal index.
- Each item exports `{frame, nameStringId, descriptionStringId}` into `INVT`.
- Export always emits `INVC` and `INVT`; empty inventory => disabled `INVC` + empty `INVT`.

### Engine runtime contract

- Consume `INVC` + `INVT` exactly by schema (`sheet_bitmap_id`, frame geometry, item range, item frame/name/description).
- Treat inventory-op item IDs as `uint16` indices into `INVT`.
- Runtime ownership/count store and save semantics are engine-owned, not editor-owned.

## Gaps and uncertainties

- Runtime inventory implementation in engine is still stubbed (bridge methods no-op), so this analysis covers data/export ABI compatibility, not final runtime behavior/perf.
- Save migration policy for inventory ID renumbering is not yet specified in ticket set; only save-chunk intent (`INVN`) is documented.

## Key references

- `..\avNEONedit\NEONedit\Models\InventoryData.cs`
- `..\avNEONedit\NEONedit\Build\GameDataBuilder.Content.cs`
- `..\avNEONedit\NEONedit\Build\GameDataBuilder.Catalogs.cs`
- `..\avNEONedit\NEONedit\Build\GameDataBuilder.AssetChecks.cs`
- `..\avNEONedit\NEONedit\Build\NeonWriter.cs`
- `..\avNEONedit\docs\game_data_record_layouts.md`
- `..\avNEONedit\docs\game_data_source_mapping.md`
- `..\avNEONedit\docs\game_data_validation.md`
- `src\core\game_data_format.h`
- `src\core\game_data.cpp`
- `src\core\noir_vm.cpp`
- `src\core\noir_bridge.cpp`
- `neonengine_spec.md`
