/**
 * @file dialogue_bridge.h
 * @brief The dialogue session's one way out to the engine.
 *
 * A struct of plain function pointers, in the same shape and for the same
 * reason as @ref noir_bridge: the session decides what a conversation does and
 * nothing else, so everything it has to reach outside itself goes through here
 * and tests substitute a mock.
 *
 * The functions take no context argument. Production wires them to the running
 * engine through the g_pEngine global, exactly as the VM's bridge does; nothing
 * calls them until a dialogue is on screen, and by then the engine is live.
 */

#ifndef __DIALOGUE_BRIDGE_H__INCLUDED__
#define __DIALOGUE_BRIDGE_H__INCLUDED__

#include "types.h"

namespace NEONengine
{
    /**
     * @brief Function pointer table bridging a dialogue session to the engine.
     *
     * Every function pointer must stay valid as long as the session does.
     */
    struct dialogue_bridge
    {
        /**
         * @brief Records that the page at global PAGS @p page_index was read.
         *
         * Called once as each page is displayed. What is done with that -
         * today, a bit in the game session - is the engine's business.
         */
        void (*mark_page_read)(u16 page_index);

        /**
         * @brief Whether the page at global PAGS @p page_index has been read.
         *
         * Asked of every page the conversation is about to land on, because a
         * one-time page that has been read is no longer available.
         */
        bool (*is_page_read)(u16 page_index);

        /**
         * @brief The current value of game state flag @p flag_id.
         *
         * Asked of a page's check flag before it is shown. Never asked for the
         * no-flag sentinel: a page with no check flag is available on that
         * count without the engine being consulted.
         */
        bool (*get_flag)(u16 flag_id);
    };

    /**
     * @brief The engine's shared dialogue bridge.
     *
     * Pass its address to dialogue_session::create().
     */
    dialogue_bridge const& engine_dialogue_bridge() noexcept;
}  // namespace NEONengine

#endif  // __DIALOGUE_BRIDGE_H__INCLUDED__
