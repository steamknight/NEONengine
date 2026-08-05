# 11 — Inventory modal UI skeleton — layout and pagination

**What to build:** The player can open the inventory from the game UI and see
everything they own laid out in a grid. They can page through their items with
Prev and Next controls, and close the inventory with Esc or the Close button.
Nothing else is interactive yet — clicking an item does nothing and there is no
selection effect.

The inventory is a full-screen ACE state, modelled on the dialogue and message
states. The scene loop pauses while it is active and resumes unchanged when it
closes. The inventory cannot be opened while dialogue or another modal is active.

The screen is divided into four regions from top to bottom:
- A title row.
- A one-line name strip (blank for now; filled by the next ticket).
- A grid of item cells. Each cell draws the item's frame from the shared inventory
  spritesheet at its authored size; cells have fixed padding around the art. The
  number of cells per page is computed from the grid region's dimensions and the
  cell size — it is never hard-coded. Items enumerate in authored order.
- A bottom strip with a Prev button, a page indicator ("Page N / M"), a Next button,
  and a Close button.

Prev and Next are non-wrapping: Prev is disabled on page one, Next is disabled on
the last page. The 8px/4px/8px spacing rhythm matches the existing dialogue and
message frame conventions.

**Blocked by:** 09 — `game_session` inventory store.

**Status:** ready-for-agent

- [ ] Opening the inventory from the game UI pushes the inventory state
- [ ] The inventory covers the full screen
- [ ] Items are drawn in the grid in authored order, one frame per cell, unscaled
- [ ] Items-per-page is derived from grid geometry and cell size, not hard-coded
- [ ] Only owned items (count > 0) appear in the grid
- [ ] The page indicator shows the current page and the total
- [ ] Prev moves to the previous page and is disabled on page one
- [ ] Next moves to the next page and is disabled on the last page
- [ ] Esc closes the inventory; the Close button does the same
- [ ] The scene is unchanged after the inventory closes
- [ ] The inventory cannot be opened while dialogue or another modal is active
- [ ] The input press that opened the inventory does not immediately close it
