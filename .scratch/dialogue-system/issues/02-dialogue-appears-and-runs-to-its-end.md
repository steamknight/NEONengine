# 02 — A dialogue appears on screen and runs to its end

**What to build:** A script that runs `SHOW_DIALOG` now puts a conversation on the
screen instead of logging that there is no box yet. The player sees what the
speaker is saying in a frame, clicks or presses space to move to the next thing
they say, and when the conversation runs out the box comes down, the scene is
exactly as it was, and the script that started the conversation carries on from
where it parked.

This is the tracer bullet: the whole path, end to end, at its simplest. Pages carry
text only. Each page is marked read as it is displayed. Advancing follows the
page's next-page field, and the no-destination sentinel ends the conversation. No
portraits, no speaker names, no choices, no flags and no scripts — those come
later.

The conversation's rules live in a dialogue session module that decides which page
is current and when the conversation is over, and knows nothing about drawing. It
reaches the engine through a small bridge of function pointers, in the same shape
and for the same reason as the script VM's bridge, so tests can substitute a mock.
The state around it owns the screen and the input.

Follow the message box for how a modal state behaves: freeze the display on entry
so the held-back buffer keeps the scene as it was, restore and resume on exit,
spend the press that opened the box so it does not immediately close itself, and
resynchronise the scene's view of the mouse button on the way out so the closing
press does not read as a fresh click in the scene.

The dialogue's entry page is whatever the dialogue names as its first. A dialogue
id that names nothing loadable should leave the scene untouched rather than push an
empty box.

There is an existing scratch state used to try dialogue rendering by hand; it is
scaffolding, and this is the point at which it can go.

**Blocked by:** 01 — Game session holds what the player has read and taken.

**Status:** done

- [x] `SHOW_DIALOG` pushes the dialogue state instead of logging
- [x] The current page's text is drawn in a frame sized to what it holds
- [x] A click or space advances to the page named by the current page's next-page field
- [x] The no-destination sentinel ends the conversation, as does running past the last page
- [x] Each page is marked read in the game session as it is displayed
- [x] The box comes down leaving the scene as it was, with no damage where the frame stood
- [x] The parked script resumes from the instruction after `SHOW_DIALOG`
- [x] The press that opened the box does not close it; the press that closed it does not click the scene
- [x] A dialogue id naming nothing loadable is logged and leaves the scene alone
- [x] The dialogue session decides the page sequence and performs no drawing
- [x] The dialogue session reaches the engine only through a bridge of function pointers
- [x] Tests drive the session with a mock bridge and assert on the page sequence and the end condition
- [x] The mock records an ordered list of calls rather than a last-call-wins record
- [x] The obsolete dialogue rendering scratch state is removed
