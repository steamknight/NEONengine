# NEONengine

A point-and-click adventure engine for the Commodore Amiga. This glossary covers
the concepts the engine and its authored game data share; it is vocabulary only,
not a specification.

## Language

### Scripting

**NoirScript**:
The scripting language the game's behavior is authored in, compiled to bytecode
the VM runs.
_Avoid_: script language, Noir

**Session**:
One running instance of a script, with its own stack and execution state.
Several run concurrently, each getting a slice per frame.
_Avoid_: thread, process, coroutine

**Slice**:
The run a session gets in one frame, bounded by an instruction budget.
_Avoid_: tick, quantum

**Immediate call**:
A bridge call that acts and returns inside the opcode.

**Deferred call**:
A bridge call that records a request and parks the session; the engine answers
it and the session resumes afterwards. A **modal** deferred call is answered by
something the player dismisses; a **terminal** one tears the VM down.

### Scenes

**Scene**:
One screen of the game: a backdrop, the hotspots on it, the graphics it owns,
and its timeline.
_Avoid_: room, level, location

**Hotspot**:
A region of a scene the player can point at and act on.
_Avoid_: interactive region, clickable

**Timeline**:
The set of tracks a scene owns. Only a container; everything playable lives on
the tracks.

**Track**:
A named, independently playable sequence of keyframes with its own clock, loop
mode and duration.
_Avoid_: animation, sequence

**Keyframe**:
One entry on a track: either an event the playhead fires as it crosses, or one
end of a span the playhead stands inside.

**Playhead**:
Where a track's clock currently stands, counted in frames processed.

### Dialogue

**Dialogue**:
A conversation with a speaker, made of pages. Hosted in a modal frame, but the
word always means the conversation, never the frame.
_Avoid_: dialog, conversation tree, chat

**Page**:
One thing a speaker says, with its optional choices. Pages form a graph within
their dialogue.
_Avoid_: node, line, beat

**Choice**:
A response the player may pick on a page.
_Avoid_: option, reply, answer

**Speaker**:
Who is talking: a display name and an optional portrait.
_Avoid_: character, actor

**Read**:
What a page becomes when it is displayed. A page marked one-time is burnt at
that moment and is not available again.
_Avoid_: seen, visited, shown

**Taken**:
What a choice becomes when the player selects it. A choice marked one-time is
burnt then, not when it is merely displayed.
_Avoid_: seen, chosen, used, clicked

**Available**:
Whether a page or choice may be presented at all, given how it was authored,
the flags it checks, and whether it is already burnt.
_Avoid_: enabled, visible, active

**Choice set**:
The run of choices shown together on a page when they do not all fit at once.
The last slot rotates to the next set.

### State

**Game session**:
The persistent state of one playthrough: what the player has done, as opposed
to what the game data says. Survives a save and load.
_Avoid_: save state, profile, progress

**Flag**:
A named boolean of game state that scripts and authored data both read and
write.

**Value**:
A named signed integer of game state, the numeric counterpart to a flag.
