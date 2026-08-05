# 05 — The player can pick a reply

**What to build:** A conversation becomes two-sided. Below what the speaker says,
the player sees their possible replies, numbered from 1 and indented slightly.
Moving the pointer over a reply highlights it and the cursor becomes the engine's
clickable one; clicking takes it. Pressing a reply's number does the same, holding
the highlight on it briefly so the player can see what they picked before the
conversation moves on.

A taken reply leads to the page it names, and the no-destination sentinel ends the
conversation — so a writer can end a conversation with "Goodbye". A reply that
points back at its own page is how a writer builds "ask about each topic, then
leave": as one-time replies burn away, the page eventually has none left.

A reply is available on the same three counts a page is: the author enabled it, its
check flag is absent or true, and it is not already burnt. The difference is when a
one-time reply burns — when it is **taken**, not when it is merely displayed.
Burning on display would retire the reply the first time the player laid eyes on it,
so it could never be picked.

A page whose replies are all unavailable — because it was written with none, or
because every one is gated off or already taken — behaves exactly like a page with
no replies at all: click or space advances it to its next page. That is what lets a
character show the player out once there is nothing left to ask.

This ticket assumes every available reply fits on the screen; the case where they do
not is the next ticket.

`neonengine_spec.md` describes both pages and choices as "read once" in its
persistence note, which is wrong for choices — a choice is taken. Correct the
wording as part of this work.

**Blocked by:** 03 — Pages honour availability and the sequential skip.

**Status:** done

- [x] Available replies are drawn under the page text, indented and numbered from 1
- [x] Hovering a reply highlights it and switches the cursor to the clickable role
- [x] Moving off a reply removes the highlight and restores the cursor
- [x] Clicking a highlighted reply takes it
- [x] Pressing a reply's number takes it, moving the highlight there and holding it briefly first
- [x] Input is ignored while that highlight is held
- [x] The highlight always marks the reply about to be acted on, whichever device chose it
- [x] A taken reply goes to the page it names; the sentinel ends the conversation
- [x] A reply pointing at its own page returns there with itself now gone
- [x] A one-time reply is burnt when taken, not when displayed, and is gone on the next visit
- [x] A reply disabled by its author, or whose check flag is false, is not offered
- [x] A page with no available reply advances on click or space to its next page
- [x] A reply whose text wraps is drawn over as many lines as it needs
- [x] Tests cover availability, taking, both endings, the self-referencing reply and the fall-through, through the mock bridge
- [x] The specification's persistence note distinguishes a page being read from a choice being taken
