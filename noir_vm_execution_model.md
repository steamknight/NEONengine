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
4. [Budgets and passes](#budgets-and-passes)
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
        │
        │ RET from the top frame, END, or an error
        ▼
    ┌──────────┐
    │   DEAD   │  removed at the end of the frame
    └──────────┘
```

Nothing suspends a session but `WAIT` and the scene choosing not to call the VM
at all.

### Stacks

Each session gets a 1 KB stack. The size is configurable.

Stacks live in a vector with room for four sessions. The vector grows
if a fifth session starts. It never shrinks, not even when the scene changes —
the scene resets the live count and keeps the memory.

A session that runs out of stack dies. See [Errors](#errors).

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

## Budgets and passes

Two limits, both configurable:

- **Session budget — 10 instructions.** The most one session may run in one
  frame, counted across every pass.
- **Frame pool — 40 instructions.** The most all sessions together may run in
  one frame.

A session that stops early hands its unused instructions back to the pool.

### Passes

The VM walks the run list, giving each session that can run up to what is left
of its own budget. Then it walks it again. It stops when either:

- the pool reaches zero, or
- no session can run — every one has spent its budget, is waiting, or is dead.

A second pass matters when a session stopped early on the first one and the pool
still has room.

### Order

Sessions run in the order the scripts were spawned. Each time the VM runs it
begins one place further along the run list, wrapping at the end.

Every pass within a single update begins at the same session. The move happens
between updates, never between passes.

The position advances only on updates the VM actually runs. A frame the scene
skips does not count, so the order depends on how many times the VM has run, not
on how much time has passed.

This prevents scripts from starving if there are more active sessions than there
is room in the global instruction pool.

### Reporting

Two things can end a pass:

- A script called a deferred bridge function. The VM drains the pool on purpose.
  This is normal and the VM says nothing.
- Demand ran the pool dry. Sessions still wanted instructions and did not get
  them.

The VM logs when that saturation starts, every 50 updates while it lasts, and
when it ends. Each line carries:

- how many sessions were live
- how many spent their whole budget or ended
- how many ran but were cut short
- how many got nothing at all

The closing line reports the worst update seen, not the last one.

---

## Starting a script

### `execute`

Puts a script at the back of the run list. It runs on the next VM update, which
may be the same frame if the VM has not run yet.

It returns nothing.

If a session for that script is already live, the VM drops the request and logs
a warning. The player never sees it. Debug builds may show it on screen.

This is what a hotspot click uses, and it is why clicking a door twice opens it
once.

### `execute_now`

Runs a whole script at once, in the reserved immediate context, and returns
whatever the script left in D0 as a signed 16-bit value.

It ignores the run list, the duplicate check, the session budget and the frame
pool. It runs until the script ends or hits its own cap of 2048 instructions.
That cap is configurable.

It exists for the case where the VM is stopped and something must happen anyway:
a conversation choice that changes the music, for example. The dialogue is on
screen, the VM is not running, and the music has to start now.

Rules:

- Only engine code may call it, and only from outside a VM update. A bridge
  function invoked from an opcode must never call it. That would be re-entry,
  and the VM has nothing to save a context with.
- There is exactly one immediate context, with its own stack. Nesting
  cannot happen: scripts cannot start scripts, and the compiler bans the calls
  that could open a second modal.
- A script that reaches the cap is a runaway. The VM stops it and reports an
  error. Nothing runs here that a script should not finish in a few thousand
  instructions; at 40 instructions an update, 2048 is what the whole VM would
  get through in a second of normal work.

---

## Bridge functions

Every bridge function falls into one of three kinds. The kind decides three
separate things, so it is one property, not three lists.

| Kind | What it does | Ends the pass | Allowed under `execute_now` |
| --- | --- | --- | --- |
| **Immediate** | Acts inside the opcode and returns | No | Yes |
| **Deferred modal** | Records a request; the scene shows it and holds the VM | Yes | No |
| **Deferred terminal** | Records a request; the scene tears down | Yes | Yes |

`PLAY_MUSIC` is immediate. `SAY`, `SHOW_DIALOG` and `SAVE` are deferred modal.
`GOTO_SCENE` is deferred terminal.

### Deferred requests

A deferred call does not act. It records what the script wants and returns. Then
it drains the frame pool, which ends the pass at once. No session runs again
this frame.

Draining the pool is what makes speech work in order:

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

---

## The frame

The scene decides what runs. The VM does not know whether anything is on screen.

```
0.  Act on requests recorded last frame
      scene change  → destroy everything, load the new scene
      modal         → put it on screen
1.  Update modals
2.  Update the timeline     skipped if a modal is up
                            or a scene change is waiting
3.  Update the UI
4.  Update the VM           skipped if a modal is up
                            or a scene change is waiting
```

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

The VM does not run in the frame a scene change is waiting, so no session gets
one more slice in a scene that is already gone.

Save games do not store VM state. Loading a save starts the scene fresh and runs
its entry script from the beginning.

---

## Errors

A script error kills one session. The rest keep running.

The VM logs it and sends it to the bridge, which puts it on screen with the same
nine-patch box `SAY` uses. It is a modal, so the game holds still until the
player dismisses it.

This happens in every build. A message naming the fault beats a game that stops
working for no visible reason.

Errors include a bad opcode, a stack overflow from deep `CALL` nesting, a bad
jump target, and an `execute_now` script that runs past its instruction cap.

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

---

## Compiler rules

The editor enforces these. The runtime does not check them.

1. **No modal calls under `execute_now`.** Any script the engine can reach
   through `execute_now`, following every `CALL`, may not use a deferred modal
   bridge function. Such a call would record a request while the engine sits
   inside a loop that never returns, so the frame would never draw and the
   player could never dismiss it.
2. **No `WAIT` under `execute_now`.** No frames pass, so the wait would never
   end.
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
                    bridge records "say 12", drains the pool
                    S is runnable, PC on SAY #13
frame  N+1  step 0  scene puts the box on screen
            step 2  timeline skipped
            step 4  VM skipped
frame  N+k  step 1  player clicks; box closes
            step 2  timeline runs
            step 4  VM runs; S resumes on SAY #13,
                    records "say 13", drains the pool
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

### The pool running dry

Five sessions, budget 10, pool 40.

```
pass 1   A 10   B 10   C 10   D 3 (deferred call)
         pool 7 left, but the pass ends here — D drained it
```

Without the request, D would spend its full 10, the pool would hit 40, and E
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

The summary in [neonengine_spec.md](neonengine_spec.md) under **NoirScript VM**
still describes the old model and needs updating when this is built.
