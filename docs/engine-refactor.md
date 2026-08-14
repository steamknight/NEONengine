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

4a, 4b and 1 are done. Item 2 is done.

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
doing what `_clear()` did. A `scene_runtime` holds them by value in teardown
order. No virtuals: same memory, same generated code.

**A correction to the list.** Nine modules were named here. Only four belong in
`scene_runtime`, because the other five have different lifetimes:

- **`scene_text` has no state at all** — zero `s_*` variables, one function over
  its argument. A class around it would be an abstraction earning nothing. Left
  as it is.
- **`dialogue_portraits` lives for one conversation**, not one scene, and only
  `dialogue.cpp` touches it. It belongs to item 3's `state_host<dialogue_state>`.
  Moving it here would only mean moving it again.
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
and says so at length about music — are classes owned by a `scene_runtime`. The
constructor takes the ACE bob manager where `scene_bobs_create()` had it; the
destructors do what the four `_clear` functions did.

`sceneDestroy` no longer calls them in a hand-maintained order. Member
declaration order does, and the reason is written where the members are:
`~scene_hotspots` takes its hover caption down through the bobs, so the bobs are
declared first and outlive it. That is the correctness this item was ranked for —
the ordering was already right, but nothing had been holding it there.

Dependencies are arguments now. `scene_hotspots` and `scene_timeline` take
`scene_bobs&` and `scene_sound_effects&`; every module takes `game_data&`, which
retired about twenty `g_pEngine->data()` reaches. The graph turned out to be
acyclic, so no back-pointers and no two-phase construction were needed.

`scene_runtime` hangs off the engine, built by the scene state and dropped by it,
because its parts want the screen and the pak and neither exists when the engine
is built. `engine::scene()` returns a reference and dereferences its pointer
unchecked, as `data()` and `vm()` do: every caller runs inside the scene state,
and a box of text or a conversation is pushed *over* that state rather than
swapped for it. The nullable accessor it replaced was guarding a case that cannot
happen.

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

That moves cursor creation ahead of `screenLoad()`, which holds: the view exists
from `screenCreate()`, and the sprite manager only has to be attached to it
before the view is published.

It also moved the pointer's teardown. `genericDestroy` now releases the engine
straight after `stateManagerDestroy`, because the pointer's destructor reads the
view and the OS, and both used to be gone by the time global destructors ran.
Anything the engine gains that touches ACE state depends on that ordering.

**Left for the second half.** Nothing. The runtime is built per scene: its
constructor takes the scene and does what `load()` did, its destructor does what
`_clear` did, and `load_scene` reads

```
drop_scene()  ->  draw_backdrop(sc)  ->  build_scene(sc)
```

The palette appears nowhere in that: `~scene_timeline` hands its bands back,
which is the one place that knows they were turned. The engine only holds the
runtime, so its pair is named for what it does to it.

Two things had to move first, because they were sitting in `scene_bobs` with the
wrong lifetime and per-scene construction would have churned both every scene
change. The ACE bob manager went to `screenCreate`/`screenDestroy`, since it
draws into the screen's buffers and undraws from them. The hover caption ink went
to the engine: one Chip bitmap sized from the UI font, which is loaded once and
never swapped.

A scene is now always loaded once the scene state runs, so `engine::initialize()`
refuses a project whose boot scene does not exist rather than leaving the state
to dereference a runtime that was never built.

That last step had an ordering constraint. `draw_backdrop()` used to call the
timeline's `restore_palette()` itself, on the *outgoing* scene while loading the
incoming one, so the old runtime could not be dropped before the backdrop was
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

**Now.** ACE's `tState` is four `void(*)()` slots with no user pointer. That is
why `message.cpp` keeps `s_boxX`, `s_boxY`, `s_boxWidth` and `s_boxHeight` at
file scope, and why `dialogue.cpp` does far more of the same across 36 KB.
Overlay state is process-global and outlives the state it belongs to.

**Change.** A `state_host<T>` template owning a `T` and generating the four C
thunks from it: `tState g_stateMessage = state_host<message_state>::table();`.
The thunks inline to a direct call on a static member, so this costs nothing.

**Payoff.** Create and destroy become a constructor and a destructor, so the
freeze/restore-rect pairing in `message.cpp` turns into RAII instead of two
functions that have to agree. `dialogue.cpp`, the second-largest file in the
tree, is the main beneficiary.

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
`screen.freeze_display()`. Add a `frozen_display` guard so an overlay cannot leak
a freeze.

**Payoff.** Mostly mechanical and low risk, and the guard closes a real hole: a
state that pops early leaves the display locked on one buffer. Ranked below 1–3
because it buys clarity more than correctness.

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
