/**
 * @file dialogue_bridge.h
 * @brief The dialogue session's one way out to the engine.
 *
 * A struct of plain function pointers, in the same shape and for the same
 * reason as @ref noir_bridge: the session decides what a conversation does and
 * nothing else, so everything it has to reach outside itself goes through here
 * and tests substitute a mock.
 *
 * Each call uses the host bound to the session.
 */

#ifndef __DIALOGUE_BRIDGE_H__INCLUDED__
#define __DIALOGUE_BRIDGE_H__INCLUDED__

#include "types.h"

namespace NEONengine
{
    class game_session;
    class noir_vm;

    struct dialogue_context
    {
        game_session& _session;
        noir_vm& _vm;
    };

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
        void (*mark_page_read)(dialogue_context* host, u16 page_index);

        /**
         * @brief Whether the page at global PAGS @p page_index has been read.
         *
         * Asked of every page the conversation is about to land on, because a
         * one-time page that has been read is no longer available.
         */
        bool (*is_page_read)(dialogue_context* host, u16 page_index);

        /**
         * @brief Records that the choice at global CHOC @p choice_index was taken.
         *
         * Called as a choice is taken - when the player picks it, not when it
         * was merely put on screen. Burning it on display would retire the
         * choice the first time the player laid eyes on it, so it could never
         * be picked.
         */
        void (*mark_choice_taken)(dialogue_context* host, u16 choice_index);

        /**
         * @brief Whether the choice at global CHOC @p choice_index was taken.
         *
         * Asked of every choice the page on show could offer, because a
         * one-time choice that has been taken is no longer available.
         */
        bool (*is_choice_taken)(dialogue_context* host, u16 choice_index);

        /**
         * @brief The current value of game state flag @p flag_id.
         *
         * Asked of a page's or a choice's check flag before it is offered.
         * Never asked for the no-flag sentinel: a record with no check flag is
         * available on that count without the engine being consulted.
         */
        bool (*get_flag)(dialogue_context* host, u16 flag_id);

        /**
         * @brief Writes game state flag @p flag_id.
         *
         * How a page's or a choice's authored set and clear flags are applied,
         * as it is displayed or taken. Never called for the no-flag sentinel:
         * a record that sets or clears nothing asks the engine for nothing.
         */
        void (*set_flag)(dialogue_context* host, u16 flag_id, bool value);

        /**
         * @brief Runs script @p function_id here and now, to its end.
         *
         * The VM's immediate path, which bypasses time slices - the dialogue
         * owns the frame, so there is no slice for a script to be given. That
         * path refuses every deferred modal call and kills the session, which
         * reports itself; see ADR-0001. Never called for the no-function
         * sentinel.
         */
        void (*run_script)(dialogue_context* host, u16 function_id);

        /**
         * @brief Whether the VM has latched terminated.
         *
         * Asked after every script the conversation runs. A terminal call -
         * changing scene - carries no modal guard, so it does fire from a
         * dialogue script, and the conversation it was read in is over.
         */
        bool (*is_terminated)(dialogue_context* host);
    };

    /**
     * @brief The engine's shared dialogue bridge.
     *
     * Pass its address and a dialogue_context to dialogue_session::create().
     */
    dialogue_bridge const& engine_dialogue_bridge() noexcept;
}  // namespace NEONengine

#endif  // __DIALOGUE_BRIDGE_H__INCLUDED__
