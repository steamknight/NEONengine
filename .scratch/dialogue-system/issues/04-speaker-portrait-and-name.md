# 04 — The speaker's portrait and name are shown

**What to build:** The player can see who is talking. The speaker's portrait sits in
the top-left corner of the screen and their name is written above what they say, so
a conversation between two characters is easy to follow and a character the player
has not met yet is still identified.

Portraits are a fixed 96x96 with a matching mask, drawn in the corner with a margin
between them and the frame. The name is a heading line at the top of the frame,
above the page text, in the heading colour — the message box's caption is the same
shape and the same colour convention.

Some pages have no speaker at all, and some speakers have no portrait. When there is
no portrait to draw, the frame reclaims that column and starts at the left margin
instead, which gives narrator prose the full width of the screen. The name is still
drawn when there is one.

The portrait only changes when the speaker does, so it is only erased and redrawn
then — redrawing the whole background every page would be far slower for the common
case where only the frame changed. Track the portrait's rectangle and the frame's
rectangle separately, and restore both before drawing either, so a frame that
reclaims the portrait column does not leave the old portrait behind.

Loading a portrait reads the pak, which needs a system-use guard and blocks. Load
every distinct portrait the dialogue's pages name when the conversation starts,
under one guard — the set is knowable from the pages, and the pause reads as normal
when the box appears rather than as a stall mid-conversation. Be mindful of Chip RAM:
a portrait and its mask are a few kilobytes each and a talkative scene may name many.

`neonengine_spec.md` still draws the dialogue as two frames; ADR-0003 supersedes
that, and the diagram should be corrected as part of this work.

**Blocked by:** 02 — A dialogue appears on screen and runs to its end.

**Status:** ready-for-agent

- [x] The speaker's portrait is drawn in the top-left corner at its fixed size, with its mask applied
- [x] The speaker's name is drawn as a heading above the page text, in the heading colour
- [x] A page with no speaker draws neither, and the frame takes the full width
- [x] A speaker with no portrait still has their name drawn, and the frame takes the full width
- [x] The portrait is only erased and redrawn when the speaker changes between pages
- [x] Moving between a page with a portrait and one without leaves nothing behind
- [x] Every distinct portrait in the dialogue is loaded when the conversation starts, under one system-use guard
- [x] A speaker naming a portrait that cannot be loaded is logged and drawn without one
- [x] The specification's display layout diagram is corrected to one frame
