# A speaker's colour is never handed back

Status: needs-triage

## What happens

`apply_speaker_color()` in `src/states/dialogue.cpp` points
`DIALOGUE_TEXT_COLOR` at whatever the page's speaker is authored to speak in.
Its own doc says a page with no speaker "takes the normal text color". It does
not. It keeps the last speaker's.

```cpp
ULONG color = palette[DIALOGUE_TEXT_COLOR];
if (speaker_id != data::NO_ID && speaker_id < speakers.size())
{
    color = to<ULONG>(speakers[speaker_id].dialogue_color >> 8);
}
palette[DIALOGUE_TEXT_COLOR] = color;
```

With `NO_ID` the function reads the entry and writes the same value back. It
cannot do otherwise: nothing anywhere holds the colour the entry started at.

Two things follow.

**Narrator prose wears the last speaker's colour.** A page with no speaker,
reached after one with an authored colour, renders in that colour.

**The teardown restores nothing.** The destructor's
`apply_speaker_color(data::NO_ID)` reads the tinted entry and writes it back,
so the tint outlives the conversation.

## Why it sticks

`screenGetPalette()` returns the fade's *reference* palette
(`src/core/screen.cpp:285`) — the pristine one the scene loaded. The
conversation writes the speaker's colour into it, so the scene's own copy is
what gets overwritten. The entry only returns to its authored value when
`draw_backdrop()` loads the scene palette again, which happens on a scene change
or a language repaint.

Inside one scene, everything drawn in palette index `DIALOGUE_TEXT_COLOR` after
a conversation shows the last speaker's colour.

## The shape of a fix

The conversation borrows an entry it does not own, so it has to give it back.
Take a copy of `palette[DIALOGUE_TEXT_COLOR]` when the box goes up and write
that copy back when it comes down. The natural home is the dialogue state, which
now has somewhere to keep it.

Worth checking at the same time whether the borrow should be stated on `Screen`
rather than done by reaching into its reference palette — item 5's territory.

## Not caused by

Found while moving `apply_speaker_color()` verbatim into the hosted dialogue
state. That change neither caused this nor altered it.
