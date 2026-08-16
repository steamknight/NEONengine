# Hotspots invent a press edge after a span they did not run

Status: needs-triage

## What happens

`scene_hotspots::update()` reads the button and derives press and release edges
from `_was_down`, last frame's reading. Any span where the update does not run
leaves `_was_down` describing a frame that may be many frames old, so the first
update after the span reads a button the player has since moved as a fresh
press on whatever lies under the pointer.

`sceneProcess()` skips the update whenever `g_pEngine->vm().blocked()` is true
(`src/states/scene.cpp:514`). A fade holds that latch for its whole run, and
`scene_fade_done()` hands the scene back without anything resyncing. Press the
button during a fade-in and hold it, and the hotspot under the pointer takes
the press when the fade lands.

## Why it survives

Two of the three spans that skip the update already handle it, each in its own
place and for its own stated reason:

- `scene_hotspots::load()` sets the flag itself (`src/core/scene_hotspots.cpp:92`).
- `sceneResume()` sets it when a state pushed over the scene comes down
  (`src/states/scene.cpp:548`).

Nothing sets it for a VM-blocked span. Every new span that skips the update
has to remember, and this one did not.

## The shape of a fix

`scene_hotspots` owns `_was_down`, so it is the thing that can tell whether its
own last reading is still worth an edge. A fix there — adopt rather than edge
when the previous update was not the previous frame — would cover all three
spans and every later one, and would likely subsume both call sites above.

Weigh that against the redundancy it removes. The two existing call sites are
cheap and honest; a self-detecting rule inside `scene_hotspots` costs a frame
counter and moves the rule somewhere a reader of `sceneProcess` will not see it.

## Not caused by

Found while inverting `resync_button()` into the scene's `cbResume`. That
change neither caused this nor made it worse.
