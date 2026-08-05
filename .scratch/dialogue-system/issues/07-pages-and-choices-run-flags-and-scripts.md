# 07 — Pages and choices run flags and scripts

**What to build:** A conversation can change the world. A page can set and clear
flags when it is shown and run a script; a reply can do the same when it is taken.
That is what lets a conversation record that it happened, unlock content elsewhere,
give the player an item, or end with walking out of the room.

The order matters and is load-bearing.

On arriving at a page: mark it read, apply its set and clear flags, run its display
script, check whether the VM has latched terminated, and only then work out which
replies are available and lay the page out. The script runs before the layout so a
script that unlocks one of that same page's replies unlocks it now rather than on
some later visit, and after the authored flags so the script has the last word.

On a reply being taken: draw the highlight, mark it taken, apply its set and clear
flags, run its click script, check terminated, and then follow its destination.

Scripts run through the VM's immediate path, which bypasses time slices. That path
refuses every deferred modal call — speaking a line, opening another conversation,
fading, saving — and kills the session with a message the player will see. ADR-0001
records that this is accepted rather than worked around: a page is already the
engine's way of saying something with a portrait, and a conversation inside a
conversation would be a modal on top of a modal. Nothing needs building to enforce
it; the VM already does. But a dying script must not take the conversation's screen
state with it.

Changing scene is the exception. That call is terminal rather than modal and carries
no such guard, so it does fire from a dialogue script. Whenever a script has been
run, check terminated afterwards and, if it is set, end the conversation at once —
abandoning any destination the reply carried, because the player would otherwise
read a page in a scene the script has already left. The scene then applies the
pending change through the path it already has.

**Blocked by:** 05 — The player can pick a reply.

**Status:** done

- [x] A page's set and clear flags are applied when it is displayed
- [x] A page's display script runs when it is displayed
- [x] A page's script runs after its authored flags and before its replies are worked out
- [x] A script that unlocks one of its own page's replies makes that reply available on the same visit
- [x] A reply's set and clear flags are applied when it is taken
- [x] A reply's click script runs when it is taken
- [x] A reply's script runs after its authored flags and before its destination is followed
- [x] A script that changes scene ends the conversation immediately, abandoning the reply's destination
- [x] The scene applies the pending change once the conversation has come down
- [x] A script killed for making an illegal call reports itself and leaves the conversation in a sane state
- [x] Skipped and unavailable pages and replies run no flags and no scripts
- [x] Tests assert the order of bridge calls, not just that each happened
