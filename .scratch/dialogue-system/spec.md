# Dialogue System

Status: ready-for-agent

## Problem Statement

The game's characters cannot hold a conversation. `SHOW_DIALOG` (opcode 0xB1) is
authored into scripts, the `.neon` format already carries every record a
conversation needs — `DLGS` dialogues, `PAGS` pages, `CHOC` choices, `SPKR`
speakers — and the VM parks a session correctly when the opcode runs. But the
scene answers the request by logging "no dialogue box yet" and carrying on, so a
player never sees anything and the authored content is unreachable.

Everything that would let a writer build a conversation — a speaker saying
something over several pages, the player picking a reply, a greeting that only
happens the first time, a topic that can only be asked once — exists as data with
nothing to run it.

## Solution

A dialogue state, pushed over the scene the way the message box is, that owns the
screen until the conversation ends.

The player sees the speaker's portrait, their name, what they are saying, and a
numbered list of replies. They pick one with the mouse or the number keys, the
conversation moves to the next page, and when it ends the scene resumes exactly
where it left off with the script that started the conversation carrying on from
the instruction after `SHOW_DIALOG`.

Writers get pages and choices that can set and check flags, run scripts, and burn
themselves out after a single use — so a character greets the player differently
the second time, and a topic already asked about stops being offered.

## User Stories

1. As a player, I want a conversation to fill the screen when it starts, so that I
   know the game is waiting for me rather than continuing behind the box.
2. As a player, I want to see the portrait of whoever is talking, so that I know
   who is addressing me without being told.
3. As a player, I want to see the speaker's name above what they say, so that I can
   identify a character whose portrait I do not recognise yet.
4. As a player, I want the portrait to change when a different character speaks, so
   that I can follow a conversation between two people.
5. As a player, I want a page with no replies to advance when I click or press
   space, so that a speaker can say several things in a row without me choosing
   anything.
6. As a player, I want my replies numbered, so that I can see how many options I
   have at a glance.
7. As a player, I want to pick a reply by clicking it, so that I can play with the
   mouse alone.
8. As a player, I want to pick a reply by pressing its number, so that I can play
   without moving my hand to the mouse.
9. As a player, I want the reply under my pointer to be highlighted, so that I know
   which one I am about to pick before I commit.
10. As a player, I want the cursor to become the engine's clickable cursor over a
    reply, so that I can tell replies apart from the rest of the box.
11. As a player, I want a reply I picked with the keyboard to highlight briefly
    before the conversation moves on, so that I can see what I chose.
12. As a player, I want long replies to wrap onto several lines, so that a reply is
    never cut off mid-sentence.
13. As a player, I want a "More..." option when my replies do not all fit, so that I
    can reach the ones that were not shown.
14. As a player, I want "More..." to cycle back round to the first set of replies, so
    that I cannot get stuck having passed the one I wanted.
15. As a player, I want the box to be no bigger than what it holds, so that I can see
    as much of the scene behind it as possible.
16. As a player, I want a narrator page with no speaker to use the full width of the
    screen, so that longer prose is comfortable to read.
17. As a player, I want the scene to be exactly as I left it when the conversation
    ends, so that nothing appears to have moved while I was talking.
18. As a player, I want a character to greet me differently the second time I talk to
    them, so that the world remembers me.
19. As a player, I want a topic I have already asked about to stop being offered, so
    that my list of replies reflects what I still have left to ask.
20. As a player, I want a reply to be able to take me to a new scene, so that a
    conversation can end with going somewhere.
21. As a writer, I want a page to check a flag before it is shown, so that I can
    gate content on what the player has done.
22. As a writer, I want a page to set and clear flags when it is shown, so that a
    conversation can record that it happened.
23. As a writer, I want a page to run a script when it is shown, so that a
    conversation can have effects beyond flags.
24. As a writer, I want a page's script to be able to unlock one of that same page's
    replies, so that I can reveal an option at the moment it becomes relevant.
25. As a writer, I want a choice to check a flag before it is offered, so that a
    reply only appears once the player could plausibly say it.
26. As a writer, I want a choice to set and clear flags when it is taken, so that
    picking a reply records itself.
27. As a writer, I want a choice to run a script when it is taken, so that a reply
    can have effects in the world.
28. As a writer, I want a page marked one-time to be skipped on a later visit, so
    that I can write a first-meeting greeting.
29. As a writer, I want a one-time page's replacement to be the next page in order,
    so that writing a greeting variant needs no extra wiring.
30. As a writer, I want a choice marked one-time to disappear after it is taken, not
    after it is shown, so that the option can actually be picked.
31. As a writer, I want a choice to point back at its own page, so that the player
    can ask about each topic in turn and the list shrinks as they do.
32. As a writer, I want a page whose replies are all used up to advance on its own,
    so that a character shows the player out once there is nothing left to ask.
33. As a writer, I want a choice with no destination to end the conversation, so that
    I can write a goodbye.
34. As a writer, I want a page with no destination to end the conversation, so that a
    conversation can simply run out.
35. As a writer, I want a conversation to survive being started when its first page
    is used up, so that a one-time opening does not break the second visit.
36. As a writer, I want the conversation to end rather than hang when every page is
    unavailable, so that a mistake costs a short conversation and not a locked game.
37. As a writer, I want what the player has read and taken to survive a save and
    load, so that the world's memory of the player is part of their progress.
38. As a developer, I want the conversation's rules to be testable without hardware,
    so that navigation and availability can be verified in the test runner.
39. As a developer, I want a script that makes an illegal call from a conversation to
    report itself, so that the failure is diagnosable rather than silent.
40. As a developer, I want the conversation to give the screen back cleanly, so that
    the scene beneath is not left damaged.

## Implementation Decisions

### Vocabulary

Terms follow `CONTEXT.md`. A **dialogue** is the conversation, never the frame it
is drawn in. A **page** becomes **read** when displayed; a **choice** becomes
**taken** when selected. "Seen" is not used. A page or choice is **available**
when its authored enable bit is set, its check flag is unset or currently true,
and it is not already burnt by its one-time bit. A **choice set** is the run of
choices shown together when they do not all fit at once.

The authored enable bit is static: the loaded game data is read-only, so nothing
at runtime can flip it.

### Modules

**Dialogue session** — new. Owns everything decidable without drawing:
availability, the sequential-index skip, navigation, burn ordering, the terminal
check, choice-set rotation, and the two-pass fit. Pure with respect to the
engine: it holds no screen state and performs no I/O.

Its interface: begin a dialogue by id; report the current page and the choices in
the current set; accept "advance", "take choice N" and "rotate to next set";
report that the dialogue has ended. Measurement is injected — the caller supplies
the height of each available choice and the space available, and the session
answers which slice to show.

**Dialogue bridge** — new, mirroring `noir_bridge` in shape and purpose. A struct
of function pointers covering the four things the session needs from the engine:
read a flag, write a flag, run a script through the VM's immediate path, and ask
whether the VM has latched terminated. Production wires it to the engine; tests
supply a mock.

**Dialogue state** — new ACE state, modelled on the message state. Owns the
screen: freezes the display, draws the portrait and the frame, measures text,
handles mouse and keyboard, and drives the session. Pushed and popped like the
message state.

**Game session** — new, created at boot. Declared the home for all persistent
game state, but populated now with only two packed bit arrays: pages read and
choices taken, indexed by global record index and sized from the record counts at
creation. Following the timeline's existing fired-keyframe bit array. Flags and
values stay where the VM has them; moving those touches the VM's borrowed-state
invariant and does not belong in this change.

**Scene state** — modified. The `show_dialog` modal case, currently a log line,
pushes the dialogue state.

**Engine** — modified. Owns the game session and exposes it.

### Navigation

Entry is at the dialogue's first page. Landing on an unavailable page scans
forward by index through the dialogue's page range until an available page is
found; running off the end ends the dialogue. Skipped pages run no flags and no
scripts, and their own next-page field is never consulted. The same rule governs
the entry page, next-page navigation and choice destinations alike — see
ADR-0002, which records why this is by index rather than by following next-page
links.

A page presents its available choices. A page with none available — whether it
was authored with no choices, or every choice is gated off or already taken —
advances on click or space to its next page, where the no-destination sentinel
ends the dialogue. A taken choice goes to its destination; the sentinel there
ends the dialogue. A choice pointing back at its own page is how a writer builds
"ask about each topic, then leave", and it composes with the fallback: as
one-time choices burn away the page eventually has none left and advances on its
own.

### Order of work

On page arrival, in order: burn the read bit; apply the page's set and clear
flags; run its display script; check terminated; then compute which choices are
available and lay the page out. The script runs before layout so that a script
unlocking one of the page's own choices unlocks it now rather than on a later
visit, and after the authored flags so the script has the last word.

On a choice being taken, in order: draw the highlight; burn the taken bit; apply
the choice's set and clear flags; run its click script; check terminated; then
follow its destination.

### Script execution

Page display scripts and choice click scripts run through the VM's immediate
path, which bypasses time slices. That path refuses every deferred modal call and
kills the session — see ADR-0001, which records that this is accepted rather than
worked around, and that dialogue scripts are therefore non-modal.

Scene changes are the exception: that call is terminal rather than modal and
carries no such guard, so it fires. The session checks terminated after every
script and, when set, ends the dialogue at once, abandoning any destination the
choice carried. The scene then applies the pending request through the path it
already has.

The parked session that ran `SHOW_DIALOG` needs nothing done to it: a pushed state
stops the scene's loop entirely, so the session resumes on its own when the
dialogue pops.

### Layout

One frame, not the two the previous specification drew — see ADR-0003. The frame
is pinned to the top-left beside the portrait and holds the speaker name, the page
text and the choices together. Portraits are a fixed 96x96 with a corresponding
mask, drawn in the top-left corner with a margin between them and the frame. The
frame takes the remaining space with a small margin at the top and right, sized to
its content, so its height changes on every page and its width changes when a
portrait comes or goes.

The speaker's name is drawn as a heading line above the page text, in the heading
colour, mirroring the message box's caption. When the page has no speaker, or the
speaker has no portrait, the frame reclaims the portrait's column and starts at
the left margin.

Choices are indented slightly and numbered from 1. Fitting is two passes: first
try to fit every available choice in the space under the page text, capped at 9.
If they all fit, they are drawn and there is no "More..." at all. If they do not,
refit against the space less one line, cap at 8, and put "More..." in the last
slot. Selecting it clears the current set and shows the next, wrapping back to the
first. Two passes rather than backtracking by one, because a choice may be several
lines tall while "More..." is always one, so dropping the last fitted choice can
leave a gap or fail to free enough room.

A page with any available choice reserves room for one choice plus "More..."
before the text is placed, clipping the page text if it would otherwise take
everything, and logs when it does. Without that floor, a page with very long text
would present no choices and fall through to its next page, making an
unreadable-text bug indistinguishable from a page the author gave nothing to pick.

### Input

Hovering a choice highlights it and switches the cursor to the engine's clickable
role. Clicking a highlighted choice takes it. Pressing a choice's number moves the
highlight to it, holds it for a short fixed interval so the player sees what they
picked, and then proceeds; input is ignored during that interval. The highlight
always marks the choice about to be acted on, whichever device chose it. On a page
with no choices, a click anywhere or space advances.

The press that opened the dialogue must be spent on entry, and the scene's view of
the mouse button resynchronised on exit, as the message state does — otherwise the
opening press closes the box on the frame it appears, and the closing press reads
as a fresh click in the scene.

### Assets and redraw

Every distinct speaker portrait in the dialogue's page range is loaded at dialogue
create, under one system-use guard, mirroring how the scene preloads its sound
effects. The set is knowable from the page range, and this puts the cost at the
moment the box appears, where a pause reads as normal rather than as a stall
mid-conversation.

The display is frozen on entry as the message state does, so the held-back buffer
keeps the scene as it was and provides a clean plate to restore from. Two
rectangles are tracked: the portrait's, restored and redrawn only when the speaker
changes, and the frame's, restored and redrawn on every page and every choice-set
rotation. Both are restored before either is drawn, so a frame that reclaims the
portrait column leaves no ghost behind. Redrawing the whole background would be
simpler but is far slower in the common case, where only the frame changed.

### Specification document

`neonengine_spec.md` is updated as part of this work: its Display Layout diagram
shows the superseded two-frame arrangement, and its Persistence line describes
both pages and choices as "read once", which is wrong for choices.

## Testing Decisions

A good test here asserts on what the dialogue does as seen from outside it —
which page is current, which choices are offered, in what order the engine was
called — and never on how it stores that. Tests should survive the session's
internals being rewritten. This follows the VM's tests, which drive bytecode
through a mock bridge and assert on the bridge's record, not on the VM's fields.

**Tested: the dialogue session, through its own interface, with a mock bridge.**
One seam. The bridge is a struct of function pointers exactly like `noir_bridge`,
so tests substitute a mock the way the VM's tests do. Measurement is injected, so
fitting and rotation are exercised by handing the session arrays of heights rather
than by rendering anything.

The mock records an ordered list of calls rather than a flat last-call-wins
struct, so tests can assert that flags were applied before the script ran, and
that a script ran before the choices were computed. This is the shape the
architecture review recommended retrofitting onto the VM's mock; new code should
start there.

Coverage should include: availability across all three conditions; the sequential
skip, including the greeting-variant pattern and the all-unavailable case; every
navigation ending; the burn ordering on both paths; the terminal check abandoning
a destination; the two-pass fit at its boundaries — everything fits, one too many,
more than nine, a multi-line choice, and the reserved floor; and rotation wrapping.

**Not tested: everything that draws or reads input.** Layout, portrait loading,
the erase rectangles, mouse and keyboard handling, and the scene wire-up all need
the blitter or real input. The test runner has no host build — it calls into the
screen itself and runs only on Amiga or in UAE — so there is no seam at which
these could be exercised. Giving the screen a host-side adapter is a separate
piece of work.

Prior art to follow: the VM's test suites for the mock-bridge pattern and for
asserting on a call record; the track clock's tests for a pure module tested
exhaustively through a small interface; the game data tests for pure record
walking.

## Out of Scope

- **Serialization.** The game session holds the bits and is declared the home for
  persistent state, but nothing writes it to disk. The save format is its own work.
- **Moving flags and values into the game session.** They stay with the VM.
- **A host-side test build.** Rendering and input stay untested here.
- **A dedicated hand cursor.** The cursor roles are a closed set matching the
  authored cursor sheet; the existing clickable role is reused rather than adding
  one, which would touch the data format, the sheet asset and the editor.
- **Nested dialogues, and `SAY` from within a dialogue.** Both are refused by the
  VM's immediate path; see ADR-0001.
- **Voice, portrait animation, and text reveal effects.** Text is drawn complete.
- **Rewording the dialogue records in the `.neon` format.** The existing records
  carry everything needed.

## Further Notes

The `.neon` format already carries every field this needs, so no format version
change is required and existing exported data stays loadable.

Three decisions from the design conversation are recorded as ADRs because a future
reader would otherwise assume the opposite: dialogue scripts cannot make modal
calls (0001), unavailable pages are skipped by index rather than by following
next-page links (0002), and the dialogue draws one frame rather than the two the
specification drew (0003).

The dialogue state is the second modal state after the message box, and the first
that is not dismissed by a single click. If a third appears, the freeze-restore-
resume sequence the two share is the obvious thing to lift into one place — but
two instances is not yet enough to justify it.
