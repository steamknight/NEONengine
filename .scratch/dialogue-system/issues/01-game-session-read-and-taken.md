# 01 — Game session holds what the player has read and taken

**What to build:** The engine keeps a record, for the whole playthrough, of which
dialogue pages the player has read and which choices they have taken. Nothing yet
reads or writes it during play — this is the store the rest of the dialogue work
burns its one-time bits into, created once when the game boots and living as long
as the playthrough does.

Declared as the home for all persistent game state, but holding only these two
things for now. Flags and values stay where the VM has them; moving those touches
the VM's borrowed-state invariant and is not part of this work.

Each record is addressed by its global index, so the page and choice counts in the
loaded game data give the exact size needed. Store them packed as bits rather than
bytes — the timeline already does exactly this for its fired keyframes, and that is
the pattern to follow.

**Blocked by:** None — can start immediately.

**Status:** done

- [x] The engine owns a game session and creates it during boot
- [x] It is sized from the loaded game data's page and choice counts
- [x] A page's read state and a choice's taken state can each be asked for and set by global index
- [x] The bits are packed, following the timeline's fired-keyframe precedent
- [x] Every record starts out neither read nor taken
- [x] An out-of-range index is handled without reading or writing past the end
- [x] Nothing is written to disk — serialization is out of scope
