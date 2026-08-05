# 08 — The editor bakes a portrait's palette base in, and pins "More..."

## Portraits

**What to build:** The exporter writes a portrait as the artist drew it: 96x96, five
bitplanes, local indices 0..31, with a matching mask at the same depth. Nothing in
the file says where those indices are meant to land, so the engine works it out at
runtime — it reads each portrait, allocates a second bitmap at the screen's depth,
copies the five planes across, and fills the planes above them according to the bits
of the band's base index, so local index n comes out as 32 + n. The mask is rebuilt
the same way, because the blitter reads a mask with the artwork's own stride.

That is the aligned masked draw the specification's Bitplane Shifting section
describes, and doing it at load leaves the drawing itself a plain cookie-cut. But it
is work the export already has every fact for. Baking it there would mean the pak
carries portraits in the shape the blitter wants, exactly as it already does for bob
strips, and the engine would only have to read them.

What it costs today: a portrait is read into Fast RAM and rebuilt into Chip rather
than read straight into Chip, and the rebuilt pair is nearly twice the size on the
wire it would have been — 96x96 at eight planes rather than five, for the artwork and
the mask both. That is CPU and Chip RAM spent on every conversation, in a place
where the pause is already the thing the player notices.

Bob strips are the precedent: the editor exports those at the screen's depth with
their palette slot's base already in the pixels, which is why the engine only has to
overlay their colors and blit. Portraits should be the same, with the difference that
their base is fixed at 32 rather than chosen per graphic — the portrait band is the
same 32 entries for everything, since the portrait and the inventory share it and are
never on screen together.

This is deliberately deferred, not forgotten: the runtime conversion works and is
tested against the exported assets as they stand today. When the editor changes, the
engine's `bake_to_band` and `bake_mask` in `src/core/dialogue_portraits.cpp` come out
and the loader validates the depth instead of converting it — and the old assets stop
loading, so the two have to change together.

## The "More..." string

**What to build:** A second pinned string, alongside the one the editor already
pins. String id 0 is the language's own name written in that language, which is
what lets a language menu label every choice without the engine carrying any
localized text of its own. The replies that rotate need exactly the same thing:
the last slot reads "More...", and that word has to be translated with the rest
of the game rather than compiled into the engine.

So the editor pins a second entry, keyed `more_choices`, holding "More..." — at a
fixed id the engine can name as a constant, the way it names the language name's,
and present in every language's table whether or not the writer has translated it
yet. Pinning it rather than letting a writer author it means the engine can read
it unconditionally: a dialogue that needs "More..." cannot be made to fall back on
nothing by a missing string.

Ticket 06 draws it. Until this lands the engine has no string to draw there.

**Blocked by:** nothing. Needs the editor, not the engine.

**Status:** done

- [x] The editor exports a portrait at the screen's bitplane depth, with palette index 32 folded into the pixels
- [x] The editor exports a portrait's mask at that same depth
- [x] The engine loads a portrait straight into Chip RAM and validates its depth rather than converting it
- [x] `bake_to_band` and `bake_mask` are removed from `dialogue_portraits.cpp`
- [x] The specification's Portrait and Inventory Palettes section says where the baking happens
- [x] The editor pins a second string, keyed `more_choices` and reading "More...", at a fixed id
- [x] Every language's exported table carries it, translated or not
- [x] The engine names that id as a constant beside `LANGUAGE_NAME_ID`
