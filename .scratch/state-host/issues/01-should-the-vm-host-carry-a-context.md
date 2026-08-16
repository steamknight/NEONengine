# Should the VM's host carry a context rather than the engine?

Type: grilling
Status: closed - no

## The answer

No. The context has nowhere to stand.

ACE's `tState` is four argument-less callbacks with no user pointer, so whatever
writes the scene pointer into a context can only reach that context through a
global or through static storage. Put the context on the engine and the VM may
as well ask the engine, which is where we already were. Put it anywhere else and
it is a second `g_pEngine` with a better name.

Two of the three reasons this issue gave were already false when it was written:

- **The bridge reaches no global.** All 31 implementations take `engine&`, handed
  to them by `host_call` from `_host`. The one `g_pEngine` left in
  `noir_bridge.cpp` is a debug-only path that carries its own note.
- **A test can already hand the VM its own host.** The host type is a template
  parameter of the bridge table, and the tests pass `mock_bridge_log` with a
  table of their own (`noir_vm_test_utils.h`).

What remained was the prize option B already had: getting a nullable out of a
header every translation unit includes.

A third option was raised and rejected too - making the four calls queued
requests, the way `GOTO_SCENE` is. `pending_request` holds one request and is
sound only because every poster blocks the VM as it posts. `HOTSPOT_ON` is
immediate, so three in a row would overwrite each other. That route costs a real
queue and delays when a hotspot toggles.

## What landed instead

The scene state holds the runtime, and the four bridge callbacks reach it
through one free function, `current_scene_runtime()`, declared in
`core/scene_runtime.h`. `state_host<T>::instance()` stayed private; a state
reaches its own instance through `friend T`.

Said plainly, because it was tempting not to: that function is the same
process-global authority under a better name. It is narrower by exposure - one
header the bridge already includes, rather than `neonengine.h` - and by what it
answers for. It is not narrower in kind.

## Still true, and worth knowing

`assert` compiles to nothing without `ACE_DEBUG`, which had gone missing from
the cache. So "the invariant can be asserted" means checked in a debug build and
documented in every other.
