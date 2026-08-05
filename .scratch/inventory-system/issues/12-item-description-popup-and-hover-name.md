# 12 — Item description popup and hover name

**What to build:** Moving the pointer over an item shows its name in the one-line
strip at the top of the grid. Clicking an item opens a small dismissible popup that
shows the item's full description. The popup closes when the player clicks or presses
Space, matching how other popups in the engine behave.

When the pointer leaves all items the name strip clears. When the popup is open the
name strip continues to show the selected item's name.

**Blocked by:** 11 — Inventory modal UI skeleton — layout and pagination.

**Status:** ready-for-agent

- [ ] Moving the pointer over a grid cell fills the name strip with that item's name
- [ ] Moving the pointer off all cells clears the name strip
- [ ] Clicking a grid cell opens a popup showing that item's description
- [ ] The popup closes on a mouse click
- [ ] The popup closes on Space
- [ ] While the popup is open the name strip continues to show the item's name
- [ ] Esc and the Close button still close the whole inventory (not just the popup)
