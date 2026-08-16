# Should the VM's host carry a context rather than the engine?

Type: grilling
Status: open

## The question

Item 4a gave `noir_vm` a `void* _host`, passed as the first argument of every
bridge call, and every implementation in `src/core/noir_bridge.cpp` recovers
`engine&` from it through `on_engine<Fn>`.

Four of the thirty do not want the engine. `nb_hotspot_on`, `nb_hotspot_off`,
`nb_anim_play` and `nb_anim_stop` (`noir_bridge.cpp:45,46,108,109`) want the
scene runtime, and reach it through the engine only because that is what
arrives.

Option C: the host stops being the engine and becomes a context carrying both
the engine and the scene, set when the scene state starts. The bridge then
reaches nothing global, and a test can hand the VM its own.

## Why it is parked

Option B goes first: the scene state holds the runtime and the four callbacks
reach it through the state host. That leaves one narrow global where there had
been a wide one, and it can assert what it used to only claim.

C's prize is not these four calls. It is unwinding the engine global for all
thirty, and for the two hundred references the refactor plan counts elsewhere.
That is larger than item 3 should swallow.

## What would settle it

The architecture review planned after items 3 and 5. Grill before deciding.
The context needs somewhere to live, so C is downstream of `state_host<T>`
whichever way it goes.
