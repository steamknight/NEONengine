#ifndef __NOIR_BRIDGE_H__INCLUDED__
#define __NOIR_BRIDGE_H__INCLUDED__

#include "core/noir_vm.h"

namespace NEONengine
{
    /**
     * @brief The engine's shared NoirVM bridge.
     *
     * Routes VM game-call opcodes to engine systems. Pass the table's address
     * and the engine as the host to the noir_vm constructor.
     *
     * Deferred calls do no work here. They record what the script asked for - a
     * modal request or a scene request. The scene can act after the VM returns
     * in the same update; state entry waits for the next manager update.
     * Whoever answers a modal releases its wait. The table declares which
     * calls defer.
     */
    noir_bridge const& engine_bridge() noexcept;

    /// Reports an out-of-range inventory item id through the VM error channel.
    void report_invalid_inventory_item_id(u16 item_id) noexcept;
}  // namespace NEONengine

#endif  // __NOIR_BRIDGE_H__INCLUDED__
