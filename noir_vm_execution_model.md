# NoirScript VM — Execution Model

> How the VM runs scripts: sessions, scheduling, and the frame.

This document covers the machinery around the opcodes. It does not cover the
opcodes themselves, the bytecode format, or the addressing modes; those live in
[noir_script_reference.md](noir_script_reference.md).

---

## Table of Contents

1. [The model in short](#the-model-in-short)
2. [Sessions](#sessions)
3. [The run list](#the-run-list)
4. [Budgets and the walk](#budgets-and-the-walk)
5. [Starting a script](#starting-a-script)
6. [Bridge functions](#bridge-functions)
7. [The frame](#the-frame)
8. [Waiting](#waiting)
9. [Scene teardown](#scene-teardown)
10. [Errors](#errors)
11. [Debug stats](#debug-stats)
12. [Compiler rules](#compiler-rules)
13. [Worked examples](#worked-examples)
14. [What this replaces](#what-this-replaces)

---

## The model in short

Several scripts can run at once. Each one gets its own registers, its own stack
and its own program counter. The VM calls this a **session**.

Every frame the VM hands out small slices of time to the sessions that can run.
A session never blocks. It never waits on another session. It has no idea any
other session exists.

When a script asks for something that takes over the screen — a line of speech,
a conversation, a save — the bridge records the request and the script stops for
the frame. The next frame the scene acts on the request and stops calling the
timeline and the VM until the screen is free again. The script resumes where it
left off, because nothing moved it.

That is the whole blocking mechanism. It costs no per-session state.

---

## Sessions

A session is one running script.

`CALL` does not make a session. It pushes a frame onto the session's own stack
and moves the program counter. One session can run through many functions.

### What a session owns

| Offset | Field | Size | Notes |
| --- | --- | --- | --- |
| 0 | D0–D3 | 4 × `u16` | General-purpose data registers |
| 8 | SP | `u16` | Offset into the session's stack, not an address |
| 10 | FP | `u16` | Offset into the session's stack, not an address |
| 12 | PC | `u16` | Offset into the shared bytecode |
| 14 | Instructions left | `u16` | Reset each frame |
| 16 | Wait counter | `u16` | See [Waiting](#waiting) |
| 18 | Script id | `u16` | Used to reject duplicates |
| 20 | Stack index | `u16` | Index into the stack vector |
| 22 | SR | `u8` | N/Z/C/V flags |
| 23 | State | `u8` | Runnable, waiting, or dead |

24 bytes, plus the stack. The two byte-sized fields sit together at the end so
every wider field lands on an even address and the struct needs no padding.

Stack pointers are **offsets**, never raw addresses. The stack vector may move
its contents when it grows, and offsets survive that.

The program counter fits in a `u16` because a scene's bytecode caps at 64 KB.

### What sessions share

FLAGS, VALUES, the bytecode, the function table and the bridge belong to the
scene, not to any session.

The scratch fields that read-modify-write opcodes use to remember a destination
are also shared. An opcode is atomic: the VM never cuts a session off partway
through one, so nothing can read that scratch except the opcode that wrote it.

### Session states

```
    execute()
        │
        ▼
    ┌──────────┐   WAIT n    ┌──────────┐
    │ RUNNABLE │────────────▶│ WAITING  │
    │          │◀────────────│   (n)    │
    └──────────┘  n reaches 0└──────────┘
       │    │
       │    └─────────────────────┐
       │ RET from the top frame   │ an error
       ▼                          ▼
    ┌──────────┐            ┌──────────┐
    │  ENDED   │            │   DIED   │
    └──────────┘            └──────────┘
        both are removed at the end of the frame
```

The run list treats the two the same. Everyone else does not: only an error
produces DIED, and only the error funnel can raise one. See [Errors](#errors).

Nothing suspends a session but `WAIT` and the scene choosing not to call the VM
at all.

### Stacks

Each session gets a 1 KB stack. The size is configurable.

Stacks live in a vector with room for four sessions and the immediate context,
which holds the first stack and never gives it up. The vector grows if a fifth
session starts. It never shrinks, not even when the scene changes — the scene
resets the live count and keeps the memory.

A session that runs out of stack dies. See [Errors](#errors).

A session reaches only its own. The stacks lie end to end, so an address off
either end of one would land in a neighbour's; the VM kills the session instead.
That covers `LOCAL`, whose signed offset from the frame pointer can name an
address outside the stack it belongs to.

The VM records the most stack any one session has ever used. See
[Debug stats](#debug-stats).

---

## The run list

Sessions sit in a list in the order they started.

The list only grows between frames. Nothing adds to it while the VM is handing
out slices, because scripts cannot start scripts and every engine system that
can start one runs earlier in the frame.

Sessions that die keep their place until the VM has finished the frame. The VM
skips them and removes them in one pass at the end. The list therefore holds
still for a whole frame.

There is no cap on the number of sessions.

---

## Budgets and the walk

Two limits, both configurable:

- **Session budget — 10 instructions.** The most one session may run in one
  frame.
- **Frame budget — 40 instructions.** The most all sessions together may run
  in one frame.

A session that stops early hands its unused instructions back to the frame
budget, and whichever session comes after it in the walk may spend them.

### The walk

The VM walks the run list once, giving each session that can run the smaller of
what is left of its own budget and what is left of the frame's. It stops early
if the frame budget reaches zero.

One walk is enough. A session only stops short of its budget by parking itself
in a `WAIT`, by making a deferred bridge call, or by ending, and none of those
leave it able to run again in the same update. Walking a second time would find
the same sessions and turn all of them down.

### Order

Sessions run in the order the scripts were spawned. Each time the VM runs it
begins one place further along the run list, wrapping at the end.

The position advances only on updates the VM actually runs. A frame the scene
skips does not count, so the order depends on how many times the VM has run, not
on how much time has passed.

This prevents scripts from starving when there are more live sessions than the
frame budget has room for.

### Reporting

Two things can end a walk early:

- A script called a deferred bridge function. The VM spends the rest of the
  frame budget on purpose. This is normal and the VM says nothing.
- Demand used the whole frame budget. Sessions still wanted instructions and did
  not get them.

An empty budget on its own is not the second one. A walk whose last session
reached its `RET` on the last instruction the frame could pay for turned nobody
down. Somebody has to have gone short.

The VM logs when that saturation starts, every 50 updates while it lasts, and
when it ends. Each line carries:

- how many sessions were live
- how many got every instruction they asked for
- how many ran but were cut short
- how many the walk never reached

Only debug builds count this. A release build has no log to write it to.
- how many got nothing at all

The closing line reports the worst update seen, not the last one.

---

## Starting a script

### `execute`

Puts a script at the back of the run list. It runs on the next VM update, which
may be the same frame if the VM has not run yet.

It says whether the request was taken, and if not, why: no such function, no
memory for a stack, a session already running that script, or a stack too small
to hold the script's first frame.

If a session for that script is already live, the VM drops the request and logs
a warning. The player never sees it. Debug builds may show it on screen.

This is what a hotspot click uses, and it is why clicking a door twice opens it
once.

### `execute_now`

Runs a whole script at once, in the reserved immediate context, and returns
whatever the script left in D0 as a signed 16-bit value.

It ignores the run list, the duplicate check, the session budget and the frame
budget. It runs until the script ends or hits its own cap of 2048 instructions.
That cap is configurable.

It exists for the case where the VM is stopped and something must happen anyway:
a conversation choice that changes the music, for example. The dialogue is on
screen, the VM is not running, and the music has to start now.

Rules:

- Only engine code may call it, and only from outside a VM update. A bridge
  function invoked from an opcode must never call it. That would be re-entry,
  and the VM has nothing to save a context with. It is turned down when it
  happens: one thing at a time owns the register window, and both rules are
  that one rule.
- There is exactly one immediate context, with its own stack. Nesting
  cannot happen: scripts cannot start scripts, and the compiler bans the calls
  that could open a second modal.
- A script that reaches the cap is a runaway. The VM stops it and reports an
  error. Nothing runs here that a script should not finish in a few thousand
  instructions; at 40 instructions an update, 2048 is what the whole VM would
  get through in a second of normal work.
- A modal call kills the script, before the request is recorded. Nothing is
  going to draw a frame to put the box on screen, and a request left behind
  would be acted on next frame by a scene that never asked for it.
- A `WAIT` is stepped over. Nothing ages it, so honouring it would stop the
  script for good.

The last two are the compiler's job. See [Compiler rules](#compiler-rules).

A death here leaves the run list untouched, and a teardown a terminal call is
still owed stays owed.

---

## Bridge functions

Every bridge function falls into one of three kinds. The kind decides three
separate things, so it is one property, not three lists. It is declared with the
function in the bridge table, and the VM reads it from there: an opcode cannot
treat a call as anything other than what it is.

| Kind | What it does | Ends the walk | Allowed under `execute_now` |
| --- | --- | --- | --- |
| **Immediate** | Acts inside the opcode and returns | No | Yes |
| **Deferred modal** | Records a request; the scene shows it and holds the VM | Yes | No |
| **Deferred terminal** | Records a request; the VM stops until it is torn down | Yes | Yes |

`PLAY_MUSIC` is immediate. `SAY`, `SHOW_DIALOG` and `SAVE` are deferred modal.
`GOTO_SCENE`, `LOAD_GAME`, `NEW_GAME` and `QUIT_GAME` are deferred terminal.

### Deferred requests

A deferred call does not act. It records what the script wants and returns. Then
it spends the rest of the frame budget, which ends the walk at once. No session
runs again this frame.

Spending the frame budget is what makes speech work in order:

```
SAY #12
SAY #13
RET
```

Without it, both lines would run in the same slice and the player would only
ever see the second. With it, the session stops after `SAY #12` with its program
counter on `SAY #13`, and stays there until the first box closes.

`GOTO_SCENE` is safe from `execute_now` for the same reason it is deferred. A
conversation choice can leave the room: the request is recorded during the
dialogue's update, and the scene tears down at the top of the next frame, with
nothing on the call stack that is about to be destroyed.

### Terminal requests

A terminal call goes further than ending the walk: it stops the VM. Everything
in the run list belongs to a scene that is over, so nothing runs again — not
even a `WAIT` ageing towards its end — until the teardown it asked for has
happened. `reset_execution()` is that teardown, and the only thing that lifts
it.

Without this the VM would trust whoever answers the request to always answer it.
A scene that fails to load, or a request overwritten before anyone reads it,
would leave the session resuming on the opcode after `GOTO_SCENE`, in the scene
it believed it had left. Whatever a script writes after a terminal call is dead
either way: the next scene's own entry script sets that scene up.

---

## The frame

The scene decides what runs. The VM does not know whether anything is on screen.

```
0.  Act on requests recorded last frame
      system  → load the scene, save, start a new game, quit
      teardown the VM if a terminal call is still owed one
      modal   → put it on screen
1.  Update modals
2.  Update the timeline     skipped if a modal is up
                            or a scene change is waiting
3.  Update the UI
4.  Update the VM           skipped if a modal is up
                            or a scene change is waiting
```

A system request and a modal request are different things. A modal says what to
put on screen; a system request says which game to play. `SAVE` records a system
request from a modal call, because saving is not a scene change but the session
that asked for it lives on.

The teardown in step 0 is unconditional and belongs to the VM, not to any one
request. A terminal call stops the VM and is owed a `reset_execution()`; if
acting on its request did not deliver one - the scene failed to load, or nothing
implements that request yet - this is where it arrives. Without it a request
nobody answers would stop scripting for the rest of the session.

Modals update before the timeline so that a modal closing in step 1 lets both
the timeline and the VM run in the same frame. They stop together and start
together.

One effect worth knowing: between two lines of speech the timeline gets exactly
one tick, so on-screen animation advances by a single frame per line.

While a modal is up the whole scene holds still. Animation stops. Timeline
keyframes do not fire. Music and sound already playing keep playing, because
the replayer does not depend on the timeline.

---

## Waiting

`WAIT n` stops a session for `n` VM updates.

It counts VM updates, not frames. A frame where the scene skips the VM does not
count. A script that waits fifty and sits through a conversation resumes fifty
VM updates after the conversation ends, not fifty frames after it started.

`WAIT 0` is a mistake and the compiler rejects it.

---

## Scene teardown

`GOTO_SCENE` ends everything.

Every session dies. Every modal closes. Anything on screen belonged to the old
scene and goes with it. Nothing carries over and nothing resumes.

The stacks stay allocated. The scene resets the live session count and
reuses them.

The VM stops the moment the request is recorded and stays stopped until
`reset_execution()` is called, so no session gets one more slice in a scene that
is already gone — and none of them can quietly resume if the scene change is
dropped or the load fails. A scene that turns a load down owes the VM that
teardown anyway: the script asked to leave, and its sessions are over either
way.

Save games do not store VM state. Loading a save starts the scene fresh and runs
its entry script from the beginning.

---

## Errors

A script error kills one session. The rest keep running.

`update()` hands back the reason in place of its result for the update a session
died in — a death outranks sessions merely remaining. The verdict belongs to
that one update; the next is ordinary again.

Every death goes through one funnel. It records the reason, writes a line naming
the script and the fault, and marks the session dead. Nothing else may kill a
session, which is what stops the three from disagreeing.

The VM logs that line and sends it to the bridge, which puts it on screen with
the same nine-patch box `SAY` uses. It is a modal, so the game holds still until
the player dismisses it.

The VM keeps no copy. It builds the line, hands it over, and forgets it; the
engine holds the message until the scene has shown it. That slot is the error's
own, not the one a script's `SAY` uses, because a death does not end the walk: a
session further along it can ask for a modal, or die in turn, before the scene
looks. The first message stands until the scene takes it, the second death being
the likelier consequence of the two.

The line is written in every build, whether or not logging is compiled in. A
message naming the fault beats a game that stops working for no visible reason.

Errors include a bad opcode, a stack overflow from deep `CALL` nesting, a bad
jump target, a `LOCAL` naming an address outside the session's own stack, an
`execute_now` script that runs past its instruction cap, and a modal bridge call
under `execute_now`.

A script whose first frame will not fit died before it ran an instruction, and
it is reported like any other. How far it got does not come into it.

A script can raise one itself with `HALT`. It says the author reached a case
that should never happen, and it kills only the session that ran it.

A request the VM turns down is not one of these. `execute()` refusing a script
that is already running kills nothing and tells the player nothing; it is handed
back to whoever asked.

---

## Debug stats

`noir_debug_stats` is built only under `GAME_DEBUG`.

**`max_stack_used`** — the most stack any single session has used, in bytes. One
number for the whole run. It never resets, not on scene change, not on anything.

```
script A uses 128 bytes   max = 128
script B uses 340 bytes   max = 340
script C uses  54 bytes   max = 340
```

This is the number that sets the per-session stack size. It is a peak for one
session, not a total across live sessions; the stacks are all the same size, so
the hungriest script sets the figure for every one of them.

Alongside it the VM keeps the id of the script that set the record. Knowing a
game needs 340 bytes is useful. Knowing which script asked for them is what lets
you go and look.

The VM checks for a new record where it already checks for overflow — the paths
that lower SP. Nothing else pays for it.

**`opcode_histogram`** — how many times each opcode ran, counted across every
session.

**`consecutive_saturated_updates`** — updates in a row the frame budget has run
out on, back to 0 when one finishes with instructions to spare. It is what
decides which updates get a line and which stay quiet.

---

## Compiler rules

The editor enforces these. The runtime does not rely on it for the first two;
the third it does not check.

1. **No modal calls under `execute_now`.** Any script the engine can reach
   through `execute_now`, following every `CALL`, may not use a deferred modal
   bridge function. Such a call would record a request while the engine sits
   inside a loop that never returns, so the frame would never draw and the
   player could never dismiss it. One that gets through kills the script before
   the request is recorded.
2. **No `WAIT` under `execute_now`.** No frames pass, so the wait would never
   end. One that gets through is stepped over.
3. **No `WAIT 0`.**

Rules 1 and 2 need the bridge function table: which function is immediate, which
is deferred modal, which is deferred terminal. This document is the authority.
The editor follows it, the same way it follows the opcode list.

---

## Worked examples

### Two lines of speech

```
SAY #12          ; "He's been dead six hours."
SAY #13          ; "Maybe seven."
RET
```

```
frame  N    step 4  session S runs SAY #12
                    bridge records "say 12", spends the frame budget
                    S is runnable, PC on SAY #13
frame  N+1  step 0  scene puts the box on screen
            step 2  timeline skipped
            step 4  VM skipped
frame  N+k  step 1  player clicks; box closes
            step 2  timeline runs
            step 4  VM runs; S resumes on SAY #13,
                    records "say 13", spends the frame budget
frame  N+k+1        second box on screen
```

### A conversation choice that changes the music

```
dialogue on screen, VM stopped
player picks "So it was you."
    dialogue calls execute_now(reveal_villain)
        PLAY_MUSIC #7        immediate, plays now
        RET                  returns D0
    dialogue reads D0, carries on
VM still stopped; the script that opened the dialogue has not moved
```

### The frame budget running out

Five sessions, session budget 10, frame budget 40.

```
A 10   B 10   C 10   D 3 (deferred call)
7 left in the frame, but the walk ends here — D spent the rest
```

Without the request, D would spend its full 10, the frame budget would run out,
and E
would still get nothing. E misses out either way, which is why the next update
begins at B and moves E up the order.

---

## What this replaces

The earlier VM ran one script at a time and called itself again when the engine
needed a second one. That design is gone, along with:

- the **blocked** state and the counter behind it
- the blocking counter's underflow, which could stop the VM for good
- re-entry, saved register frames, and the nesting level
- signalling a script that it may continue
- `YIELD`, in every form it has taken
- the debug stats `max_reentry_level` and `max_call_depth`

Nothing takes their place. The scene deciding not to call the VM does the same
job, and a stopped session holds no state at all.
