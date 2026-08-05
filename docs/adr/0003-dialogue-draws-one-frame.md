# A dialogue draws one frame, not two

`neonengine_spec.md` drew the dialogue as two nine-patch frames: page text beside the
portrait on the top row, and a separate full-width frame for the choices below it. The
engine draws one frame instead, pinned to the top-left beside the 96x96 portrait and
holding the speaker name, the page text and the choices together.

Two frames would give the choices the full screen width, which is a real gain when choice
text wraps. One frame was chosen anyway because the frame is resized constantly — on every
page, and again on every choice-set rotation as the fitting rule takes a different number
of choices — and a single box whose top-left is pinned and whose bottom edge alone moves is
far easier to erase and redraw correctly than two boxes where the lower one moves while the
upper one sits still.

The spec's diagram is superseded by this decision.
