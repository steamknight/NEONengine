# Engine refactor plan

Where the C-to-C++20 seams still cost us, and what to do about them. Ranked by
payoff, not by size. This is a direction, not a specification: each item is
worth its own issue under `.scratch/` when it comes up for work.

## Why

The engine grew out of a C codebase and the joins still show. Three of them
matter:

- **The frame has no owner.** `genericProcess()`, `engine::update()` and
  `sceneProcess()` each run part of the frame, and the order between them lives
  only in comments.
- **Modules are file-static state behind free functions.** The `scene_*` modules
  hold roughly fifty `s_*` variables at file scope, with `_load`/`_clear` pairs
  the caller has to match by hand.
- **Globals are load-bearing.** `g_pEngine`, `g_mainScreen` and
  `g_gameStateManager` are reached for about two hundred times, because the VM
  bridge has no way to carry a receiver.

The goals the work is measured against:

- Idiomatic C++20 over idiomatic C.
- Concerns split along lines the compiler can enforce, not lines a comment
  describes.
- Zero-overhead abstractions. `mtl::unique_ptr` is the standard: a `unique_ptr`'s
  guarantees for the size of a bare pointer.
- Fast RAM for everything Chip RAM does not demand.
- The engine composed of small systems, not grown into a god class.

## Sequence

**4a → 4b → 1 → 2 → 3**, with 5 and 6 folded in as they are passed.

4a, 4b and 1 are done. Items 2 and 3 are done. Items 5 and 6 are what remain.

**Current state model:** [item 3](#3-states-get-a-context) now describes heap-owned,
deferred states. It supersedes the static `state_host`, permanent scene state,
and engine-level overlay presentation described in the earlier work below.
Sections 1, 2 and 4b retain that work's history, not the current state API.

Item 4 split in two, and the halves wanted different neighbours. 4a, the bridge
context, went first: mechanical, self-contained, and what unlocks every later
reduction in global reach. 4b, the overlay slot, went next because it deletes the
arbitration block in `main.cpp` that item 1 would otherwise have had to carry
across intact. Item 1 comes before 2 because the frame's owner should exist
before we decide what the scene runtime is a member of.

---

## 1. The engine owns the frame

**Done.** The rest of this section records what the change was and why.

**Now.** `genericProcess()` pumps input, presents the pending overlay, calls
`engine::update()`, then `stateProcess()` and `screenProcess()`, then the debug
keys. `sceneProcess()` runs a second ordering pass of its own: pending requests,
then modals, then the timeline, hotspots, VM, and bobs. `engine::update()`
applies a pending language and nothing else — four lines with a grand name.

**A correction to what this item first claimed.** It said one function would
become *the* frame order. It cannot, and should not. There are legitimately two
orders: the frame's, which holds whatever is on top, and the scene's, which
applies only while a scene is presented. When a message box is up the timeline
must not tick and hotspots must not update. Collapsing the two would mean the
engine deciding scene business, which is the god class this plan exists to avoid.

So the goal, stated properly: **the frame's order lives in one function, that
function contains only calls, and the scene's order stops being reachable from
`main.cpp`.**

**Change, in four steps.**

*Step 1 — evict the debug keys.* **Done.** F1 through F5 was a third of
`main.cpp` and related to nothing around it. Now `debug_keys_process()` in
`core/debug_keys.cpp`. They sit outside `frame()` rather than inside a
`debug_console` the engine ticks, as this section first proposed: they belong to
whoever is working on the engine, not to the game, and none of them is part of a
frame. They still run after the frame they report on.

*Step 2 — `overlay_system`.* **Done.** The slot lived in the engine and the rule
for when it may go up lived in `main.cpp`, so neither half could be read without
the other. Both now sit in `core/overlay_system.h`, with the message vocabulary
they work on. Which states draw an overlay is told to the system at boot rather
than known by it, so the engine keeps no knowledge of which states a build has —
the test runner has neither and binds nothing. The engine hands the system out
rather than forwarding to it: five engine methods went for one, and
`neonengine.h` lost 175 lines against 20 gained.

*Step 3 — `engine::frame()`.* **Done.** Six lines that are the frame order:

```cpp
void engine::frame() noexcept
{
    update_input();
    update_audio();
    settle_language();
    _overlays.present(g_gameStateManager);
    stateProcess(g_gameStateManager);
    screenProcess(g_mainScreen);
}
```

`genericProcess()` is now `g_pEngine->frame();` then `debug_keys_process();`.
`engine::update()` retired into it, and the language swap became
`settle_language()` — a phase with a name rather than a four-line function with
a grand one. Input and audio are pumped separately because they are separate
things; an `update_input()` that also drove the music player would be a small lie
in the one place the frame is meant to be read literally.

The invariant, written on the function: **`frame()` holds calls and no logic.** A
test in it would be a rule about the frame hiding where nobody thinks to look.

That invariant nearly bought a wrapper it did not need. `settle_language()` was
first written around `apply_pending_language()`, whose only job was to reset a
flag and discard a `request_result` that its one caller tested for truthiness and
never read — while both failure paths already logged their own reason. The two
merged into one function, and `engine::request_failure` and
`engine::request_result` went with them, having no other user. The rule is that
`frame()` holds no logic, not that every phase is owed a function.

*Step 4 — record the drain order.* **Done**, as the doc comment on `frame()`,
which is 4b's unfinished half closed. It states why each phase sits where it
does, and says plainly that modal and system requests are *not* drained there:
they mean something only while a scene is on top, so the scene drains them in its
own pass. The second ordering is named as deliberate rather than left to be
discovered.

**One ordering change, deliberate.** The overlay push used to run *before*
`engine::update()`; in `frame()` it runs after the language settles. That
inversion was unsafe until 4b — `statePush` calls `cbCreate` synchronously, so
`messageCreate` rendered the text before the swap freed the table under it. 4b's
copy is what makes the order free to choose. Settling engine state before drawing
is the more defensible order, and the text is identical either way because the
copy is taken as the message is posted.

**Out of scope, deliberately.** `g_mainScreen` has about eighty call sites across
twelve files and `g_gameStateManager` about twenty-four, nearly all `statePop`
inside states. Those are items 5 and 3. An item 1 that absorbs either stops being
a frame refactor and becomes a rewrite. `sceneProcess()` keeps its own pass; at
most its request-drain block gets a name, and that is really item 2's opening
move.

**Payoff.** The frame order becomes readable top to bottom in one place. Today
the question "does X run before Y" needs two files and trust in the comments.
Every hazard those comments guard against is an ordering hazard, so an explicit
order is what actually retires them.

## 2. The `scene_*` modules become objects

**Done.**

**Was.** `scene_bobs`, `scene_timeline`, `scene_hotspots`, `scene_audio`,
`scene_text`, `mouse_pointer`, `dialogue_portraits`, `asset_cache` and
`palette_cycle` were free functions over file-static state — around fifty `s_*`
variables, eleven in `scene_bobs.cpp` alone. Each carried a `_load`/`_clear` pair
that `sceneCreate` and `sceneDestroy` had to call, in order, by hand.

**Change.** One class per module, the `s_*` state as members, the destructor
doing what `_clear()` did. `scene_state` now holds the four helpers by value,
in dependency order. The former `scene_runtime` allocation and getter-only
wrapper are gone.

**A correction to the list.** Nine modules were named here. Only four belong in
the scene state, because the other five have different lifetimes:

- **Static scene text has no state.** It now lives inside `scene_backdrop`,
  together with background drawing and palette reading. One free function takes
  the engine and scene; it needs no class or resource owner.
- **`dialogue_portraits` lives for one conversation**, not one scene. It belongs
  inside the `dialogue_presentation` the dialogue state owns.
- **`asset_cache` lives for the whole run**, opened and cleared in `main.cpp`.
  **It belongs on the engine**, not on `game_data`, whose one job is to open the
  pak, read the .neon and hand back its tables. What is worth keeping after a
  read is not part of that. `game_data::open_asset()` is gone;
  `engine::assets().open()` serves cached reads and `open_asset_uncached()`
  stays behind for music, because a raw pak read *is* `game_data`'s job.
- **`mouse_pointer` belongs on the engine.** An earlier draft called it
  scene-*state*-scoped, because `mousePointerCreate()` reads the pak once and
  every later scene reuses it. True, but not the whole picture: `scene.cpp`,
  `message.cpp`, `dialogue.cpp` and `debug_view.cpp` all call
  `mousePointerUpdate()`, and `noir_bridge.cpp` and `scene_hotspots.cpp` switch
  the role. It outlives and serves every state.
- **`palette_cycle` was folded into `scene_timeline`** rather than made a peer.
  It owns no resources and has no paired lifetime — two dirty-band variables over
  the screen palette, with one caller. Its own header already said which band
  turns is the timeline's business.

**What landed.** `scene_bobs`, `scene_hotspots`, `scene_timeline` and
`scene_audio` — renamed **`scene_sound_effects`**, because it holds only effects
and says so at length about music — are direct members of `scene_state`.
Constructors only wire dependencies. Entry loads bobs, effects, hotspots and
timeline, in that order, after drawing the backdrop.

`sceneDestroy` no longer calls them in a hand-maintained order. Member
declaration order does, and the reason is written where the members are:
`~scene_hotspots` takes its hover caption down through the bobs, so the bobs are
declared first and outlive it. That is the correctness this item was ranked for —
the ordering was already right, but nothing had been holding it there.

Dependencies are arguments now. `scene_hotspots` and `scene_timeline` take
`scene_bobs&` and `scene_sound_effects&`; every module takes `game_data&`, which
retired about twenty `g_pEngine->data()` reaches. The graph turned out to be
acyclic, so no back-pointers and no two-phase construction were needed.

The engine borrows the entered `scene_state`. `engine::scene()` returns that
state so the bridge can reach its hotspots and timeline, including while a
dialogue suspends it. The state exposes the helpers, not forwarding methods
for each operation.

**`asset_cache` landed on the engine.** `engine::initialize()` builds it before
the first UI asset is read, and the engine's destructor releases it, replacing
the call in `genericDestroy`. It takes the pak, not the `game_data` around it,
so a test can build one over a bare pak. Its reporting no longer reaches
`g_pEngine`: `log()` takes the game data as an argument. `scene_bobs` and
`scene_sound_effects` take an `asset_cache&` beside their `game_data&`.

**`mouse_pointer` landed on the engine.** The enum keeping the name became
`pointer_role`, and the class holding the cursors took it. `engine::initialize()`
builds it and the engine's destructor frees it, so `sceneCreate` and
`sceneDestroy` lose their create/destroy pair. `scene_hotspots` takes a
`mouse_pointer&`; the states and the bridge reach it through the engine, as they
already did for everything else.

The engine owns normal pointer publication: `engine::process()` updates the
pointer once after successful state handling and before screen processing.
States select the role and visibility only. Scene loading may publish the wait
pointer early before blocking disk access.

That moves cursor creation ahead of `screenLoad()`, which holds: the view exists
from `screenCreate()`, and the sprite manager only has to be attached to it
before the view is published.

It also moved the pointer's teardown. `genericDestroy` now releases the engine
straight after `stateManagerDestroy`, because the pointer's destructor reads the
view and the OS, and both used to be gone by the time global destructors ran.
Anything the engine gains that touches ACE state depends on that ordering.

**Current lifetime.** One state owns one scene. The manager destroys the old
state before entering its replacement:

```
destroy old state -> draw backdrop -> load bobs, effects, hotspots, timeline
```

The palette appears nowhere in that: `~scene_timeline` hands its bands back,
which is the one place that knows they were turned. The engine binds and
unbinds the state by identity.

Two things had to move first, because they were sitting in `scene_bobs` with the
wrong lifetime and per-scene construction would have churned both every scene
change. The ACE bob manager went to `screenCreate`/`screenDestroy`, since it
draws into the screen's buffers and undraws from them. The hover caption ink went
to the engine: one Chip bitmap sized from the UI font, which is loaded once and
never swapped.

A scene is now always loaded once the scene state runs, so `engine::initialize()`
refuses a project whose boot scene does not exist rather than leaving the state
to run a scene that never loaded.

That last step had an ordering constraint. `draw_backdrop()` used to call the
timeline's `restore_palette()` itself, on the *outgoing* scene while loading the
incoming one, so the old helpers could not be dropped before the backdrop was
painted: the order has to be restore, destroy, paint, construct. That reads as an
argument against putting the restore in `~scene_timeline`, and it was, for as
long as `draw_backdrop()` served both the scene load and the language repaint.
Hoisting the call out to those two callers ended it. The destructor restores for
a scene change; the repaint calls `restore_palette()` directly, because it
reloads the same palette with the timeline still running.

**Payoff.** This is the largest block of C left, and the one that costs
correctness rather than only readability — a paired-call lifetime is invisible to
the compiler, an object's is not. It also puts these modules within reach of the
test runner, which cannot construct them today; injecting `game_data` is the
first step of that, and the screen and the VM are the two reaches still in the
way. And it thins the scene state down to an adapter.

## 3. States get a context

**The current implementation replaces `state_host`.** `state_manager` owns
Fast RAM state objects through `state_ptr`. There is one `scene_state` per loaded
scene, not a permanent object with placement-new storage for changing resources.
The earlier static-storage and private-host-access rationale no longer applies.

### Lifetime and transitions

`start`, `push` and `replace` construct the pending object at request time.
They do not call `pushed()` then. `apply_transition()` applies a selected
transition without running gameplay; `update_current()` runs only the top
state. `update()` remains the combined operation for standalone manager users.

- Push suspends the current top before entering the new state.
- Replace destroys the outgoing top **before** entering its replacement.
- Pop destroys the top, then resumes the state below it without updating it.
- A state may request only one transition in an update. Control is valid only
  during that callback.
- The host can select `push(state_ptr&)` or `replace(state_ptr&)` only while the
  manager is idle. A transition already selected by a callback wins: selection
  returns false and leaves the host's pointer intact. It never overwrites the
  callback's transition. Calling `update_current()` with a pending transition
  is fatal, as is using host selection from a state callback.
- Constructors store inputs and copy owned payloads; resource loading belongs
  in `pushed()`. A pending state can die without ever entering.
- Allocation or entry failure is fatal. The manager retains the error text and
  does not resume a suspended parent. Later updates run no callbacks.
- `clear()` destroys pending and stacked states without resuming any of them.

`engine::process()` runs input, audio, pending language, `update_states()`, then
the screen. `update_states(state_manager&)` owns request selection; production
passes the engine's manager, and display-backed tests pass their isolated stack.
It retains neither the manager nor a borrowed `state_control`.
A finished or failed manager makes the engine clear states and exit
before rendering; failure also logs the error. Engine shutdown clears states
before ACE shutdown. Startup establishes the OS-use baseline and cursor palette
exemption once, before the first state enters. The test-runner state stays up
after its report; F9 exits.

### Scene ownership and errors

`scene_state(engine&, id)` owns bobs, sound effects, hotspots and timeline as
ordinary members, declared in that order. Their constructors acquire no scene
resources. `load_scene()` loads them after the backdrop under its OS-use guard.
The engine holds a borrowed pointer for `engine::scene()` and the VM bridge.
Binding asserts that no scene is already registered; unbinding checks identity.
The registered state stays alive while an overlay suspends the scene.

Only destruction of the state that owns that registration clears its engine
requests and VM execution. An unentered or failed state must not reset a different
scene's VM or unbind its state. Bobs record registration during load and reset
the shared manager only when they registered a set. Empty hotspots only hide
their own bobs' caption; empty timelines have no palette cycles to restore.
Reverse member destruction restores timeline bands and releases hotspots before
bobs.

A guard in the state destructor body would end before member destruction.
Instead, cleanup uses the guards already at the release sites: ACE's
`bobDeallocBuffers()`, `bitmapDestroy()` and `ptplayerSfxDestroy()` guard their
frees. `ace::bitmap_ptr` calls `bitmapDestroy()`; `mtl::vector` frees its storage
through global delete, which calls ACE's guarded `memFree` in debug and release.
No extra guard or resource wrapper is needed. Scene destruction does not
free music: `MUSIC_KEEP` carries it across a replacement, and `MUSIC_SILENCE`
stops it when the incoming scene loads.

An invalid scene id or required backdrop resource failure returns an explicit
entry error. Palette loads validate their header and read lengths. Missing
optional assets keep their previous skip behavior. A missing entry script is
not an entry failure; it starts the configured default fade.

### Scene update order

The engine applies at most one stack transition per `update_states()`:

1. Apply a transition already selected by the previous state callback, including
   startup or a modal's pop. This completes destruction, display restoration and
   the parent's `resumed()` callback before request selection.
2. Answer skipped modal calls, then examine system work. A scene replacement
   takes priority over a pending display state. While the scene is suspended,
   wait for the state above it to close; do not replace that state with a scene.
3. If a stack transition already ran, keep the engine request in its slot for
   the next call and skip resumed scene gameplay. Otherwise select and apply
   the request now. Pending display states also wait while the saved background
   is occupied. No intermediate state kind or dispatcher selects their type.
4. Start pending fades, then update only the current state. Calls made during
   that update remain deferred. After the callback returns, answer skipped
   waits and start its fade before `screen::process()`. A terminal request
   suppresses that fade and stays pending until the next safe selection.

`request_state<T>(waiting_vm, args...)` constructs an ordinary state immediately,
passing the engine followed by its arguments. The waiting VM records identity
metadata for rejection; it does not assign completion to the state.
`request_modal_state<T>(vm, args...)` uses that path and supplies the VM followed
by the arguments and an explicit `modal_wait(&vm)`. Both modal state constructors
require that final token. Non-script callers use `request_state` with a null
waiting VM and an explicit `modal_wait{}`. The one display-request slot is
first-wins until entry. `request_message()` handles empty text, uses the modal
helper for SAY and supplies an empty token for diagnostics. `SHOW_DIALOG` uses
the modal helper for `dialogue_state`. F1 requests
`debug_view_state`; it still comes after scene work. System placeholders retain
their previous behavior, but the engine now handles them.

The scene checks only whether engine work interrupts it, before producers and
after timeline, hotspot and VM work. It never interprets a request kind or
selects a stack transition. A timeline terminal would skip hotspots; a hotspot
terminal skips the VM; a VM terminal skips the remaining outgoing drawing and
language work. Current timeline script events enqueue VM sessions rather than
executing them synchronously. The VM runs at most once per scene update.

Before selecting a display state, the engine calls the scene's
`prepare_suspend()` to reconcile a changed language unless a fade-out owns or
is about to own the palette. Preparation failure is fatal and prevents entry.
If static text redraws the backdrop, preparation also draws the current bob pose
into the front buffer. `scene_bobs::draw(target)` selects that physical buffer's
ACE queue and saves fresh undraw records without advancing animation. A pixel
copy cannot replace this draw: the backdrop redraw discarded both buffers'
undraw records, so copied bobs would leave trails when they move.
The manager then calls `suspended()` to draw the displayed bob pose into the
alternate buffer. Later blits wait for that draw before using
the buffer. Message, dialogue and debug states restore from it. `resumed()`
resynchronizes hotspot button edges so a dismissal press cannot become a scene
click. Engine arbitration may follow resume without running scene gameplay.

`scene_backdrop_draw()` takes the engine and scene, obtaining drawing dependencies
from the supplied engine. Palette reading and static text layout stay private
to that module. It keeps the drawing order: background palette and bitmap, UI
colors, cursor palette exemption, text, then back-to-front copy. No background
bitmap means no clear or scene-palette load; existing pixels remain. Text stays
below bobs, with stable region order and clipping, and adds no retained resources.
The state sets pointer visibility after a successful draw, before any pointer
update; drawing itself cannot change it.

The scene records its rendered language. A change during suspension therefore
triggers rebuilding when the scene returns. `update()` owns both gameplay
checks: rebuild only on a language mismatch, never during fade-out. Before
scripts, also skip a black screen when no fade owns its palette. After scripts
and their requests, rebuild even while black if the scene remains. This lets a
terminal request stop the update before the old scene reloads. Timeline and
hotspots wait until the rendered language matches.

`rebuild_for_language()` does the work, with no scheduling parameter: reload
captions; for static text, restore timeline colors, draw the backdrop and discard
saved bob backgrounds; then record the rendered language. Caption reload receives
the owning scene. A required backdrop failure still logs and calls `gameExit()`.

### Message and dialogue overlays

Both states render their full UI. They borrow the engine for display resources
and an explicit VM for script work. They are ordinary states, with no overlay
base class or dispatcher. Constructors store dependencies and copy message
payloads once; `pushed()` prepares resources before acquiring `freeze_display`.

A message builds wrapped body and caption glyphs and an optional nine-patch
frame. Width stays within the screen, and tall text clips to the frame's lower
edge. Entry consumes opening mouse and space presses. Idle updates allocate
nothing; a fresh click or space requests pop. Empty content or lines with no
measured glyph width close normally without freezing. A failed allocation for
nonempty content returns an entry error instead of silently dismissing the
message.

A dialogue owns a `dialogue_session` and a `dialogue_presentation`, both direct
members of the state. The session holds the rules; the presentation holds the
whole of the UI — the frame, heading and body glyphs, the drawn choice slots and
their stable choice mapping, the hit regions, the visual highlight, the pointer
role while the dialogue is up, raw input reading and consumption, the keyboard
hold and its pending selection, the speaker's portraits and palette colors, and
the `freeze_display` itself. The state keeps the VM, the `dialogue_context`, the
modal wait, session navigation and choice actions, and the terminal checks. It
reads no input and never touches a bitmap, a palette band or an erase order.

The presentation takes the engine, not a list of engine-owned parts, and is
built inert: `open()` loads portraits, lays the page out, freezes the display,
draws, and only then consumes the opening click, space and number keys and puts
the default pointer back; `refresh()` builds a new page; `redraw()` draws a rotated
choice set from retained glyphs; `update()` reads one frame of input and answers what the
player asked for; `close()` restores everything. It exposes no rectangle, glyph,
slot or resource, and holds no callback.

`update()` returns a small value, not a callback or a queued event: a kind of
`none`, `advance`, `take_choice` or `rotate_choices`, and for `take_choice` the
available-choice index — never the drawn slot, which is the presentation's own
business. Keyboard input beats the mouse. A number key highlights its slot and
returns `none` for `DIALOGUE_CHOICE_HOLD_FRAMES`, consuming every input during
the hold including the frame that answers. A page with no drawn slot advances on
a click or space even when the session still holds choices nothing could render.
The state runs what it is handed against the session, then refreshes or redraws. The
presentation runs no conversation action or script and owns no VM or modal wait.
Layout fitting runs against the session as the page is laid out; the fitting
algorithm itself stays in `dialogue_session`.

Page entry renders the name, body, every drawable choice and the localized
control. The engine caches number labels `1.` through `9.` for the UI font's
lifetime. The portrait sets the width limit; all page glyphs determine the
content width, which stays fixed across choice sets.

The name and body take their space first, clipped to whole font lines at the
screen limit. Choices never shorten them. The session fits choices into the
remaining height. Oversized choices clip to whole lines, leaving room for the
full control when needed, and are fitted again only if clipping changed their
heights. A layout with no room for one choice line and its required control
logs an authoring error with `NE_LOG` and returns an error without drawing it.
Missing or undrawable choices consume no space.

Rotation keeps every page glyph and rebuilds only the slot mappings and frame.
The frame height follows the current set. Redraw still erases the whole frame;
partial erasure below the body remains a later optimization.

`dialogue_context` holds the game session and the supplied VM.
`dialogue_session::create()` binds that context as the bridge's `void*` host;
scripts must run on that VM, not a global one. No available entry page, empty
page text or a terminal VM ends entry normally without acquiring the display.
Other page-build errors return an explicit error. A refresh failure logs a fatal
error; reporting that failure to the state manager remains a TODO.

The dialogue draws one frame beside the optional portrait, as
[ADR 0003](adr/0003-dialogue-draws-one-frame.md) requires. It includes the
speaker, page, numbered choices and localized choice-set control. Missing
control text is an error when choices need rotation; the engine does not
substitute English. Mouse hover changes choice colors; clicking selects at
once. Number keys hold the selection highlighted for the configured frame
count, consuming other input during that hold. Rotation does not take a choice
or rerun the page's display script.

Page glyphs survive a string-table swap, including choices in other sets.
Rotation keeps that page's strings and width; page navigation uses the current
strings. No displayed choice retains a borrowed string-table view.
Scene language reconciliation runs after the overlay returns.

`dialogue_presentation::close()` erases the overlay's pixels before the state
completes its script wait. The call is idempotent and the presentation's own
destructor repeats it, so a failed entry is safe too. Presentation waits for
erasure before publishing page colors and, when frozen, before restoring palette
colors on exit. Bitmap deleters already wait before freeing their memory;
buffer-pointer changes need no separate wait. Cleanup frees portraits and
restores their palette band, restores the previous text color, unfreezes the
display, puts the default pointer role back — and only when the display was
held — and frees remaining glyphs; the state then completes its wait. A portrait
takes its palette band only when shown, not while loading. Unentered states,
empty entries and entries that failed before acquisition own no display changes,
release nobody else's and touch no other state's pointer role.
Clearing the manager uses the same cleanup, before destroying the suspended
scene.

The engine slot owns a `state_ptr`, not a second copy of its payload.
`message_request::modal_wait` distinguishes SAY from diagnostics. The composed
`modal_wait` records the supplied VM and its wait identity. The engine explicitly
assigns completion to the accepted state; constructors do not infer ownership
from the VM or message request. Token copies express that assignment, and token
destruction alone does nothing. The engine's `_state_wait` is identity metadata
for rejection, not a completing owner.

A new modal, execution reset or successful resume invalidates the old identity.
State destruction completes the assigned wait after display and resource
cleanup, including accepted pending cancellation, empty entry and failed entry.
Construction and pop requests do not complete it. Stale cleanup cannot answer a
later fade or execution. Empty tokens, including diagnostics, complete nothing,
and completion never clears a terminal latch.

The engine associates pending display states, system work, fades and skipped
waits with the scene that posted them. Scene teardown discards that work.
Terminal replacement cancels the outgoing display request before loading the
new scene. Cancellation may clear its current modal block before scene teardown
resets execution, but no VM update runs between those steps. Clearing the request
also discards skipped-wait metadata so it cannot answer later work.
A scene constructor or failed, unbound entry cannot cancel the
active scene's work.

Empty SAY and rejected modal calls still need an answer. The engine records
their wait identity and completes it only after the VM callback returns.
The VM sets its latch and wait identity *before* the bridge callback; this refactor
keeps that behavior. No posting call clears it synchronously. Rejection constructs
no state and its temporary token completes nothing. A duplicate request for the
same wait cannot release the accepted owner. The bounded slot does not become
an event queue.

`freeze_display` construction is inert. Acquisition rejects a second owner:
the screen has only one saved background, not nested snapshots. The engine
defers display requests while that background is held. Cleanup restores pixels
and palette before release. Display ownership has no effect on script waits.
The engine keeps pending and active fade waits distinct: an old screen
completion cannot answer a new fade before the engine starts it.

[ADR 0001](adr/0001-dialogue-scripts-cannot-make-modal-calls.md) governs page
and choice scripts: `execute_now` permits terminal requests and forbids modal
calls. Dialogue navigation checks termination after each script and abandons
any page destination. The engine handles the terminal request after dialogue
cleanup, before any queued diagnostic. Script errors can queue a diagnostic,
but only the engine selects it; a visible dialogue never hosts another modal.
Unavailable pages still scan forward by index under
[ADR 0002](adr/0002-unavailable-pages-are-skipped-by-index.md).

### Regression scope

`overlay_state_tests.h` runs real VM bytecode, engine request selection and
rendered states through the real manager, using the initialized engine's display assets.
It checks pending destruction, first-wins behavior, entry/pop latch timing,
diagnostic/fade separation, terminal latch preservation, and empty or dropped
modal requests. Both states have checks for accepted pending cancellation,
failed-entry completion, explicit empty tokens and stale cancellation during a
new fade. Cancellation also checks terminal preservation and skipped-wait
discard. Direct-state tests assign one owner instead of creating a second owner
for an engine request. The fixture keeps its VM alive until pending-state
cleanup finishes, including early test returns.
Pixel checks cover owned body and caption bytes, slot reuse,
alignment, wrapping, colors, width boundaries, tall messages and restoration.
Dialogue checks cover opening input, page reads, mouse hover and selection,
keyboard hold, choice rotation, speaker palette changes and cleanup. An inert
`dialogue_presentation` case checks that constructing, closing twice and
destroying one that never opened releases no other owner's display, asks the
conversation for nothing and paints nothing. Independent
long-choice string tables exercise layout without changing authored assets.
Language tests release the original table while choices remain drawn and check
the next page against the new language's glyphs.

New cases cover separate manager phases, host/callback selection conflicts,
stale wait identities after execution reset, and inert/nested display guards.
The request fixture uses the engine's scheduling seam, not a test dispatcher.

`scene_state_tests.h` checks real scene entry errors, pending scene/debug
destruction and borrowed-state access. Most loaded-scene cases use read-only,
scriptless authored scenes and observe the real scene callbacks. They cover
dialogue-to-message-to-scene restoration, bob buffer preparation, clearing an
active overlay, terminal request priority and successful scene replacement.
Language cases cover fade-out deferral, rebuilding while black when the scene
stays, skipping an outgoing scene's rebuild after a script requests replacement,
and rebuilding before an overlay when fade-in owns a still-black palette.
The mixed text-and-bob case borrows the authored graphic ranges for the text
scene under a restoring test guard. It checks both buffers at suspension, visible
bobs above a message, restoration on close, and trail-free movement and hiding
across both buffers. Reference images use the normal scene drawing path.
Pending cancellation and invalid entry leave an active scene's registration,
requests, VM, pixels, palette and Chip RAM intact. Clearing a queued replacement
checks that pending destruction leaves the outgoing bob buffers alive until
the outgoing state's destructor. A new scene then checks that cleanup allows
fresh bob registration. A scene update counter checks that terminal selection
after dialogue restoration runs no resumed gameplay. A real screen fade case
checks dispatch before display and diagnostic cleanup without fade completion.
A read-only hotspot fixture checks that its terminal request skips a queued VM
session; it requires a scriptless scene with a scene-changing hotspot.
The bridge test runs with a real suspended scene.
A bound dialogue-session display script uses the engine bridge and VM to
request that replacement while a real dialogue is visible. This tests the
request and closure path, not an authored displayed page containing GOTO_SCENE.

Tests snapshot and restore game progress, strings, VM, input and report pixels.
Missing required fixtures fail assertions rather than silently skipping tests.
Language tests load through the public string-table API and replace the table
directly; they cover glyph lifetime and scene reconciliation, not the deferred
language-request phase of `engine::process()`. They never reenter that function
from the test runner's entry callback.

Amiga runtime checks still need to cover deterministic allocation failures,
required-resource read failures, fade completion and fade-out language timing,
F1 restoration and music continuity. The tests do not exhaust shared memory to
force allocation errors. Building the suites is not evidence that they ran.

## 4a. A context on the bridge

**Done.** The rest of this section records what the change was and why.

**Now.** `noir_bridge` is a table of plain function pointers wrapped in
`immediate`, `deferred_modal` and `deferred_terminal`, which encode what each
call does to the walk. That part of the design is good and stays: the kind lives
on the function, `noir_vm::call()` overloads on it, and a new deferred call
cannot be added as though it were immediate.

The problem is underneath it. None of the pointers carry a receiver, so all
thirty implementations in `noir_bridge.cpp` reach the engine through
`g_pEngine` — twenty-one references in that file alone — and each one repeats a
null check that can never fire:

```cpp
void nb_inv_add(u16 item_id)
{
    if (g_pEngine) g_pEngine->session().add_item(item_id);
}
u16 nb_inv_count(u16 item_id)
{
    return g_pEngine ? g_pEngine->session().count(item_id) : u16(0);
}
```

The guard exists because nothing in the type system says the engine is live. It
is dead weight on every call and it turns a wiring mistake into a silently wrong
answer — `nb_inv_count` returning 0 — instead of a crash a test would catch.

The same shape appears in the test bridge. `g_mock_bridge`'s thirty mocks all
write to `g_mock_log`, a second global, because they have nowhere else to put
their results.

**Change.** The receiver travels with the VM, not with the table:

- `noir_vm` gains a `void* _host`, taken in the constructor beside the bridge
  pointer, and passes it as the first argument of every bridge call. The VM never
  dereferences it.
- Bridge signatures become `immediate<void(void*, u16 item_id)>` and so on.
  `report_error` takes it too — it is called directly at `noir_vm.cpp:106`,
  outside the `call()` helpers, and needs the same treatment.
- The implementations cast once through a helper, so the cast is written in one
  place rather than thirty:

```cpp
template<auto Fn>
constexpr auto on_engine = [](void* host, auto... args) {
    return Fn(*static_cast<engine*>(host), args...);
};
```

The table stays `static const`, which matters: putting the context *in* the table
would force one table per receiver and make it mutable. Keeping it on the VM lets
the real bridge and the mock bridge each stay a single shared constant.

**Why `void*` and not a typed receiver.** `noir_vm.h` deliberately includes no
engine headers, and a template parameter — `noir_vm<Receiver>` — would turn a
66 KB translation unit into a header template for one pointer's worth of type
safety. The cast is confined to the helper above, so the type is recovered once
at the boundary and every implementation below it is typed.

**Cost.** One pointer argument per bridge call, against a global load and a
branch removed from each. Measured on the built object at `-O2`, the whole of
`GET_SCENE` is now three instructions:

```
movea.l 4(sp),a0      ; the host
move.w  138(a0),d0    ; _current_scene
rts
```

The wrapper is the entire function - the implementation inlines into it - so the
cast costs nothing at runtime.

**Payoff.** Nothing else can start unwinding the two hundred global references
until the bridge can carry a receiver, because the bridge is called from inside
`vm().update()` where no argument can reach it. This comes before any
global-removal work rather than after it. It also lets a test hand the VM its own
log instead of sharing `g_mock_log`.

**Blast radius.** Wide but shallow, and entirely mechanical: thirty
implementations, thirty mocks, and the thirty-four dispatch sites in
`noir_vm.cpp`. One commit, with the test runner as the proof.

## 4b. One overlay slot, and one drain order

**Historical; superseded by section 3.** The engine now owns typed pending
states and selection; `overlay_system` and `overlay_kind` no longer exist.
The following records the earlier slot design.

**Done, except for the drain order.** The overlay slot landed; stating the frame
order in one place waits on item 1, which is where a `engine::frame()` exists to
state it in. The rest of this section records what changed and why.

**Now.** The engine holds five `pending_request<T>` members. They are usually
described as one problem, but they are not one thing — they have three different
owners:

| Slot | Posted by | Drained by |
| --- | --- | --- |
| `_pending_language` | `SET_LANG`, immediate | `engine::update()` |
| `_pending_modal` | `SAY`, `FADE_*`, `SHOW_DIALOG` | `sceneProcess` → `start_pending_modal()` |
| `_pending_system` | `GOTO_SCENE`, `SAVE_GAME`, … | `sceneProcess` → `apply_system_request()`, peeked by the scene, cleared by `sceneDestroy` |
| `_pending_message` | `nb_say`, `nb_report_error` | `messageCreate()`, after `main.cpp` pushes the state |
| `_pending_dialogue` | `start_pending_modal()` | `dialogueCreate()`, after `main.cpp` pushes the state |

Collapsing all five into one sum type would be wrong: it would merge slots whose
consumers and lifetimes genuinely differ, and `_pending_system` is *peeked*
without being taken, which a queue does not do. The language slot is already
engine-owned and already drained in the right place.

The bottom two are the ones that want joining. They are mutually exclusive by
policy, and that policy is written as an if-else chain in `main.cpp`:

```cpp
tState const* const on_top = g_gameStateManager->pCurrent;
bool const overlay_up = on_top == &g_stateMessage || on_top == &g_stateDialogue;

if (!overlay_up && g_pEngine->has_pending_message())      { statePush(…, &g_stateMessage); }
else if (!overlay_up && g_pEngine->has_pending_dialogue()) { statePush(…, &g_stateDialogue); }
```

Both are consumed by pushing a state, both resume the VM when that state comes
down, and the rule that only one may go up per frame is enforced by an `else`
that a later edit can quietly break.

**Change.** One slot holding a sum type — `pending_overlay`, with `message`,
`dialogue` and `none` — replacing both. "Only one overlay per frame" stops being
an `else` and becomes the fact that a slot holds one value. The push moves to the
engine as part of item 1, and shrinks to a `take` and a switch. `_pending_modal`,
`_pending_system` and `_pending_language` stay as they are; what they need is the
drain *order* stated once, in `engine::frame()`, instead of in comments in two
files.

**A lifetime hazard to defuse while we are here.** `message_request` holds
`bstr_view`, which owns nothing, and its comment says the poster must keep the
text alive until the box is drawn. `nb_say` posts a view straight into the string
table's `_payload`. A language change frees that payload:
`_pStrings = mtl::move(strings.value())`.

So `SET_LANG` followed by `SAY` in one slice leaves a pending message pointing
into a buffer that `engine::update()` is about to free. Today this is safe, but
only by accident of ordering: ACE's `statePush` calls `cbCreate` synchronously
(`state.c:102`), and the push block in `genericProcess` sits *above*
`g_pEngine->update()`, so `messageCreate` has already rendered the text into a
bitmap before the swap happens. Nothing states that dependency and no compiler
checks it — and item 1 moves exactly that code.

`nb_report_error` already works around the same hazard by copying into a
file-static `s_error_text`. The fix is to make the policy general: the overlay
slot owns its text, either by copying on post or by holding a fixed inline
buffer. That deletes `s_error_text`, and it means item 1 can reorder the frame
freely.

**A smaller tidy.** "First message wins" is currently enforced in three places —
`engine::show_message` checks `has_pending_message()`, `pending_request::
post_if_empty` checks `_valid` again, and `nb_report_error` checks a third time.
Only the third is load-bearing: it guards the `s_error_text` copy, per its own
comment. The first two are redundant with each other. Once the slot owns its
text, one `post_if_empty` at the slot is the whole policy.

**Payoff.** A rule the compiler enforces instead of an `else`, a dangling-view
hazard retired before item 1 can trip over it, and a frame order that exists in
one place. The `pending_request<T>` template itself is fine and stays; it is the
five-slots-polled-separately pattern, and the two slots that should have been
one, that need the work.

**What landed.** `pending_overlay` in `neonengine.h` holds a `message_request`,
a dialogue id, an `overlay_kind`, and the two buffers the message text is copied
into — 512 bytes for the text, 32 for the caption, both in Fast RAM. The longest
string in the shipped tables is 95 bytes, so the cut path logs and is not
expected to run. `post_message` and `post_dialogue` both refuse a full slot, so
"first wins" is stated once, at the store. The engine's four accessors forward to
it; `message.cpp`, `dialogue.cpp` and `scene.cpp` needed no edits at all.

`s_error_text` and its copy loop are gone: `nb_report_error` now hands the VM's
own buffer straight to `show_message`, because the slot is what copies. The
`main.cpp` if-else is a `switch` on `pending_overlay_kind()`, so a new overlay
kind must be answered or the build warns. `pending_request::post_if_empty` had no
callers left and went with it.

**One behaviour change, deliberate.** Two slots could both be full: the message
went up and the conversation followed a frame later. One slot drops the second
post instead. The only path there is a script death, whose report is posted from
the same walk that a later session could run `SHOW_DIALOG` from. Dropping the
conversation after a script error is the better of the two behaviours — the
report is the thing worth reading — but it is a change, and this is the record of
it.

**Cost.** One `bstr_view` copy of up to 512 bytes per message box, against a box
render that blits a nine-patch frame and rasterises the text. Nothing measurable,
and no allocation.

## 5. `Screen` becomes a class

**Now.** An opaque `struct Screen` and about twenty-five free functions taking a
`Screen*`. It is already an object with invariants: freeze and resume must pair,
the reference and display palette layers must agree, the cursor band is exempt
from the fade or it is not.

**Change.** Methods. `screenFreezeDisplay(g_mainScreen)` becomes
`screen.freeze_display()`.

**Payoff.** Mostly mechanical and low risk. Ranked below 1–3 because it buys
clarity more than correctness.

**The `frozen_display` guard this section used to propose is gone.** It was
justified as closing "a real hole: a state that pops early leaves the display
locked on one buffer". Re-derived, there was no hole: ACE runs `cbDestroy` on
every pop including `statePopAll` (`state.c:114,134`), every `cbDestroy`
resumed, and both screen calls are idempotent. Item 3 then made the state object
itself the guard, and `freezes_display` in `screen.h` is what came of it. Check
whether the caption ink should follow the bob manager onto `Screen` rather than
staying on the engine.

## 6. Fast RAM by type, not by call site

**Now.** Every allocation names its arena — `new (mtl::MemF::Fast) T(...)` — and
`mtl::make_unique` uses plain `new`, so it quietly follows `GLOBAL_MEM_FLAGS`
instead of choosing. Landing in Fast RAM depends on each author remembering to
ask for it.

**Change.** A `fast_allocated` mixin supplying a class-level `operator new`, so
`engine`, `nine_patch`, `noir_vm`, the asset cache and the scene modules go to
Fast whatever the call site says. Add `mtl::make_unique_in<MemF>` for the cases that need Chip.
Chip stays explicit — bitmaps, audio, bob buffers — which is the right way round,
because Chip is the scarce one.

**Payoff.** A convention becomes a compile-time guarantee, and every Chip
allocation becomes greppable. Cheap, and best done alongside item 2, when the
constructors are open anyway.

---

## Left alone deliberately

**`noir_vm` (66 KB).** Large, but a self-contained interpreter with a clean
boundary and real test coverage. Splitting it buys navigability, not
correctness. Do it when a feature forces the issue.

**`game_data_format.h`.** A wire format. It should be POD-shaped, and C is the
right shape for that.
