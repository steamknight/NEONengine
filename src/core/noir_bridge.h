#ifndef __NOIR_BRIDGE_H__INCLUDED__
#define __NOIR_BRIDGE_H__INCLUDED__

#include "core/noir_vm.h"

namespace NEONengine
{
    /**
     * @brief The engine's shared NoirVM bridge.
     *
     * Routes VM game-call opcodes to engine systems. The @ref noir_bridge table
     * is plain function pointers with no context argument, so its functions reach
     * the running engine through the g_pEngine / g_mainScreen globals; they are
     * only invoked once a script runs, by which point the engine is live. Pass
     * its address to the noir_vm constructor.
     *
     * Blocking opcodes (FADE_IN/OUT, GOTO_SCENE, ...) are put into the blocked
     * state by the VM itself; the bridge only starts the work and arranges the
     * matching decrement_blocking() - a fade completion callback for the fades,
     * and a scene transition (which resets the VM) for goto_scene.
     */
    noir_bridge const& engine_bridge() noexcept;
}  // namespace NEONengine

#endif  // __NOIR_BRIDGE_H__INCLUDED__
