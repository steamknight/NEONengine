# Palette budget and render composition for inventory screen

Branch suggestion: `research/inv-palette-composition` (research-only, do not merge)

## Summary

A feasible strategy is to treat inventory as a dedicated modal palette owner that temporarily owns indices 32–255, while leaving 0–31 untouched for existing UI/cursor behavior. This aligns with current scene/dialogue modal patterns and the engine’s reference/live palette model, including fade-safe publishing. The main compatibility challenge is that current spec/editor contracts assume inventory visuals are 32-color content mapped to 32–63; moving item visuals to 64–255 requires an export contract change. The least invasive runtime direction is to keep "baked absolute indices" and avoid introducing runtime per-pixel remapping.

## Existing constraints in current code/spec

- Current palette layout reserves 0–31 for fixed UI, 32–79 for portrait/BOB slots, and 80–255 for scene background. `neonengine_spec.md`
- Scene composition currently loads background palette, then overlays UI (0–31), then overlays portrait/BOB (32–79). `neonengine_spec.md`
- Inventory is currently specified as modal and 32-color at 32–63 (not 64–255). `neonengine_spec.md`
- Portrait system already snapshots/restores a modal palette band (32 entries at base 32) and republishes via `screenPublishPalette`. `src/core/dialogue_portraits.h`, `src/core/dialogue_portraits.cpp`
- Scene backgrounds are loaded as full 256 entries, then UI 0–31 is re-overlaid for UI scenes. `src/states/scene.cpp`
- Palette updates should use publish path to respect current fade ratio (reference vs live palette). `src/core/screen.h`, `src/core/screen.cpp`
- Cursor uses fixed UI sub-band 24–31; this should remain undisturbed by inventory composition. `src/core/screen.h`
- Overlay behavior is modal and non-nested today (message/dialogue); scene work is held while modal is active. `src/main.cpp`, `src/states/scene.cpp`
- There is no inventory UI state/modal kind implemented yet; bridge inventory ops are currently stubs. `src/neonengine.h`, `src/core/noir_bridge.cpp`

## Editor/export compatibility constraints (avNEONedit)

- Current inventory contract assumes 32-color inventory palette mapped to 32–63 from shared sheet local 0–31. `..\avNEONedit\docs\neonedit_spec.md`
- Build pipeline deduplicates runtime `.plt` assets and stores hashes in `.neon`. `..\avNEONedit\docs\neonedit_spec.md`
- BOB slot logic in editor docs allows slot bleeding/capacity up to 48 colors at slot 5 (important consistency risk vs engine spec wording). `..\avNEONedit\docs\neonedit_spec.md`

## Viable approaches

### Approach A (recommended): Dedicated inventory modal compositor owning 32–255

- On inventory-open:
  1. Pause scene updates (modal overlay model).
  2. Snapshot palette reference entries 32–255.
  3. Load inventory chrome palette into 32–63.
  4. Load inventory-item palette into 64–255.
  5. Publish changed range once (`32..255`) through `screenPublishPalette`.
- On inventory-close:
  - Restore snapshot 32–255 and publish.
- Render paths keep existing baked absolute-index strategy (no runtime index shift).

**Pros**
- Closest to existing dialogue band handoff pattern and modal lifecycle.
- Fade-safe if using publish API.
- Keeps 0–31/UI/cursor compatibility intact.

**Cons / risks**
- Requires editor/export contract change from current inventory 32-color assumption to a 192-color item-visual budget (64–255).
- Requires clearly defined asset split (chrome vs items) and deterministic palette packing for 64–255.

### Approach B: Treat inventory as pseudo-scene with full inventory backdrop/palette

- Build inventory screen as full-screen bitmap + palette composition loaded similarly to scene backdrop, with UI 0–31 preserved/re-overlaid.

**Pros**
- Reuses established backdrop/palette flow.
- Simple authoring mental model: inventory screen is a scene-like static composition + overlays.

**Cons / risks**
- Coarser ownership (likely replace much of 32–255 at once).
- Less flexible for dynamic item highlighting/states unless additional sprite layers are introduced.

### Approach C: Runtime remap of legacy item indices into 64–255

- Keep legacy 0–31 item source and dynamically remap at draw time into 64+ range.

**Pros**
- Potentially smaller editor changes initially.

**Cons / risks**
- Breaks with current engine/export philosophy (baked absolute indices).
- Higher runtime complexity and blit-path risk on target hardware.
- Harder to keep consistent with existing BOB/portrait/inventory palette mechanics.

## Recommended direction

Choose **Approach A**.

Rationale:
1. It matches existing modal ownership patterns (dialogue already snapshots and restores a borrowed palette band). `src/core/dialogue_portraits.cpp`
2. It preserves core engine palette semantics (reference/live palette, fade-aware publish). `src/core/screen.cpp`
3. It minimizes renderer risk by keeping baked absolute palette indices, consistent with current BOB/portrait approach. `src/core/scene_bobs.cpp`
4. It cleanly isolates inventory as modal and avoids scene/runtime interference.

## Tradeoffs and risks to track

- **Spec/editor contract drift**: current inventory docs assume 32-color 32–63 mapping; must be revised for 64–255 item visuals. `..\avNEONedit\docs\neonedit_spec.md`
- **Palette validation rules**: need explicit validation for inventory 64–255 occupancy and collision policy with inventory chrome in 32–63.
- **Asset memory/size pressure**: full-screen inventory assets + high-color item visuals may increase CHIP/Fast pressure.
- **BOB slot assumptions**: editor slot-bleeding rules and engine spec wording should be reconciled before final palette allocator policy is locked.

## Unknowns for follow-up decision tickets

1. Should inventory chrome in 32–63 come from a dedicated chrome `.plt`, or from a shared inventory screen palette asset with deterministic slicing?
2. How should item visuals be authored/exported for 64–255: one global 192-color inventory-items palette, or constrained per-page/per-category subsets?
3. Exact runtime layering model: full-screen static inventory bitmap plus item overlays, or fully sprite/tile-driven composition?
4. Should inventory opening snapshot/restore only 32–255, or the whole 0–255 with explicit 0–31 guard?
5. What validation policy becomes blocking vs warning in avNEONedit for inventory palette budget violations?
