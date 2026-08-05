# 06 — Replies that do not fit rotate through "More..."

**What to build:** A page can offer more replies than the screen can hold, and the
player can still reach all of them. When the replies do not all fit, the last slot
becomes "More..."; picking it clears the current set and shows the next, and picking
it enough times comes back round to the first — so the player cannot get stuck
having passed the one they wanted.

Two things cause this: more than nine replies, which is as many as the numbering
allows, and replies that simply do not fit in the space left under the page text.
They are the same case and are handled by one rule. Note that a reply may wrap onto
several lines, so the space each takes has to be measured, not assumed.

Fit in two passes. First try to fit every available reply in the space under the
page text, capped at nine. If they all fit, draw them — there is no "More..." at
all. If they do not, fit again against the space less one line, cap at eight, and
put "More..." in the last slot. Two passes rather than fitting greedily and then
dropping the last one to make room: a dropped reply may be several lines tall while
"More..." is always one, so backtracking by one can leave a visible gap, or fail to
free enough room at all.

The frame is sized to what it holds, so moving between sets of different heights
resizes it. It is pinned at the top-left, so only its bottom edge moves — erase and
redraw its rectangle on every rotation.

One page can defeat this: text so tall that not even one reply plus "More..." fits
beneath it. Left alone that page would present no replies and fall through to its
next page, making an unreadable-text bug indistinguishable from a page the author
gave nothing to pick. So when a page has any available reply, reserve room for one
reply plus "More..." before placing the text, clipping the page text if it comes to
that, and log it.

Measurement stays outside the dialogue session: the state measures each available
reply and hands the session the heights and the space available, and the session
answers which slice to show. That keeps the fitting rule testable without drawing.

**Blocked by:** 05 — The player can pick a reply.

**Status:** done

- [x] Replies that all fit are drawn with no "More..." shown
- [x] More than nine available replies produces sets with "More..." in the last slot
- [x] Replies that fit in number but not in the space available produce the same rotation
- [x] A multi-line reply is measured at its true height when fitting
- [x] Picking "More..." shows the next set, and eventually wraps back to the first
- [x] The frame is resized and redrawn cleanly on every rotation
- [x] A page whose text leaves too little room still offers a reply, clipping the text and logging it
- [x] The session decides the slice from injected heights and measures nothing itself
- [x] Tests cover the boundaries: everything fits, one too many, more than nine, a multi-line reply, the reserved floor, and wrapping
