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

Item 4 splits in two, and the halves want different neighbours. 4a, the bridge
context, goes first: it is mechanical, self-contained, and it is what unlocks
every later reduction in global reach. 4b, the overlay slot, goes next because it
deletes the arbitration block in `main.cpp` that item 1 would otherwise have to
carry across intact. Item 1 goes before 2 because the frame's owner should exist
before we decide what the scene runtime is a member of.

---

## 1. The engine owns the frame

**Now.** `genericProcess()` pumps input, arbitrates which overlay to push, calls
`engine::update()`, then `stateProcess()` and `screenProcess()`, then handles the
debug keys. `sceneProcess()` runs a second ordering pass of its own: pending
requests, then modals, then the timeline, hotspots, VM, and bobs.
`engine::update()` applies a pending language and nothing else.

The overlay-push block in `main.cpp` is engine policy wearing a main-loop
disguise. It reads `g_gameStateManager->pCurrent`, compares it against
`g_stateMessage` and `g_stateDialogue`, and encodes the rule that two overlays
may not go up in one frame. That rule belongs with the code that posts the
requests.

**Change.** `engine::frame()` runs the ordered sequence. `genericProcess()`
becomes a call to it. The debug keys move to a `debug_console` the engine ticks
last, or compile out of a release build. Item 4b should land first, so that the
overlay arbitration has already collapsed to a `take` and a switch by the time
this moves it.

**Payoff.** One function becomes the frame order, readable top to bottom. Today
the question "does X run before Y" needs two files and trust in the comments.
Every hazard those comments guard against is an ordering hazard, so an explicit
order is what actually retires them.

## 2. The `scene_*` modules become objects

**Now.** `scene_bobs`, `scene_timeline`, `scene_hotspots`, `scene_audio`,
`scene_text`, `mouse_pointer`, `dialogue_portraits`, `asset_cache` and
`palette_cycle` are free functions over file-static state — around fifty `s_*`
variables, eleven in `scene_bobs.cpp` alone. Each carries a `_load`/`_clear` pair
that `sceneCreate` and `sceneDestroy` must call, in order, by hand.

**Change.** One class per module, the `s_*` state as members, the destructor
doing what `_clear()` does. A `scene_runtime` holds them by value in teardown
order. No virtuals and no change in where anything is allocated: same memory,
same generated code.

**Payoff.** This is the largest block of C left, and the one that costs
correctness rather than only readability — a paired-call lifetime is invisible to
the compiler, an object's is not. It also puts these modules within reach of the
test runner, which cannot construct them today. And it thins the scene state down
to an adapter: `sceneProcess()` becomes `_runtime.update()`.

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
`engine`, `nine_patch`, `noir_vm` and the scene modules go to Fast whatever the
call site says. Add `mtl::make_unique_in<MemF>` for the cases that need Chip.
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
