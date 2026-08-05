# Dialogue scripts cannot make modal calls

A page's display script and a choice's click script run through `noir_vm::execute_now`,
because the dialogue owns the frame and needs the script's effects before it lays the
page out. `execute_now` refuses every deferred modal call — `SAY`, `SHOW_DIALOG`,
`FADE_IN`, `FADE_OUT`, `SAVE_GAME` — and kills the session with `immediate_modal`,
since no frame draws until it returns and the box would never appear for the player to
dismiss.

We accept that rather than teaching the dialogue to host those calls itself. A page is
already the engine's way of saying something with a portrait, so `SAY` inside a dialogue
script is redundant, and `SHOW_DIALOG` would mean a modal on top of a modal for the state
stack to unwind in order.

## Consequences

`GOTO_SCENE` is a deferred *terminal* call and carries no such guard, so it does fire from
a dialogue script. The dialogue checks `terminated()` after every `execute_now` and ends
at once when it is set, abandoning the choice's `goto_page_id`, and the scene applies the
pending request through the path it already has.
