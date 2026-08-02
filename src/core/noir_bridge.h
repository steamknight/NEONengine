#ifndef __NOIR_BRIDGE_H__INCLUDED__
#define __NOIR_BRIDGE_H__INCLUDED__

#include "core/noir_vm.h"

namespace NEONengine
{
    /**
     * @brief The engine's shared NoirVM bridge.
     *
     * Routes VM game-call opcodes to engine systems. The @ref noir_bridge table
     * holds plain function pointers with no context argument, so its functions
     * reach the running engine through the g_pEngine / g_mainScreen globals;
     * they are only invoked once a script runs, by which point the engine is
     * live. Pass its address to the noir_vm constructor.
     *
     * Deferred calls do no work here. They record what the script asked for - a
     * modal request or a scene request - and the scene acts on it at the top of
     * the next frame, deciding for itself when the VM runs again. Which calls
     * those are is declared in the table, not listed here.
     */
    noir_bridge const& engine_bridge() noexcept;
}  // namespace NEONengine

#endif  // __NOIR_BRIDGE_H__INCLUDED__
