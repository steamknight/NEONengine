# 09 — `game_session` inventory store

**What to build:** The game session tracks how many of each item the player owns.
Scripts can add an item and remove one, ask whether the player has it, ask exactly
how many they own, and wipe one item's count to zero. None of these have any visible
effect yet — this ticket is the pure data layer that everything else builds on.

Count arithmetic saturates at the bounds rather than wrapping: adding past 65535
stays at 65535, removing below zero stays at zero. Calling any of these with an
item id the game data does not know about is not a crash or an assert — the operation
is ignored and the player sees the same VM error box that a bad script call produces
today (debug only; release ignores it silently). All items start at zero and
reset to zero when the session is created again.

**Blocked by:** None — can start immediately.

**Status:** ready-for-agent

- [ ] The game session holds a count for every item in the loaded game data, all starting at zero
- [ ] Adding an item increments its count by one
- [ ] Adding past the maximum (65535) keeps the count at 65535
- [ ] Removing an item decrements its count by one
- [ ] Removing below zero keeps the count at zero
- [ ] `has(id)` returns true when the item's count is greater than zero, false otherwise
- [ ] `count(id)` returns the exact count for that item
- [ ] `clear_item(id)` sets that item's count to zero
- [ ] An out-of-range id leaves every other count unchanged; in debug builds the error channel reports it
- [ ] Creating the session a second time resets all counts to zero
- [ ] A new test suite covers all of the above, using the same test pattern as `game_session_tests.h`
