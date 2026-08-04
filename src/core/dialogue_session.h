/**
 * @file dialogue_session.h
 * @brief The rules of one running conversation.
 *
 * Owns everything about a dialogue that can be decided without drawing: which
 * page is current, where advancing goes, and when the conversation is over. It
 * holds no screen state and performs no I/O, so it can be driven whole from the
 * test runner.
 *
 * Everything it needs from the engine goes through @ref dialogue_bridge. The
 * records it navigates are handed to it as views over the loaded game data, so
 * a test can hand it a handful of structs instead.
 */

#ifndef __DIALOGUE_SESSION_H__INCLUDED__
#define __DIALOGUE_SESSION_H__INCLUDED__

#include "types.h"

#include <mtl/span.h>

#include "core/dialogue_bridge.h"
#include "core/game_data_format.h"

namespace NEONengine
{
    /**
     * @brief One conversation in progress: the page on show and where it goes.
     *
     * Pages are addressed by their global PAGS index, because that is what every
     * record pointing at one carries. A page id outside the dialogue's own page
     * range is not a page of this conversation and ends it, which is what makes
     * the conversation terminate on data that points anywhere at all.
     */
    class dialogue_session
    {
        public:  //////////////////////////////////////////////////////////////////////////////////

        /**
         * @brief Points the session at the records and the engine it will use.
         *
         * Neither span is copied and the bridge is not owned: all three have to
         * outlive the session. Nothing is on show until begin() succeeds.
         */
        void create(mtl::span<data::dialogue> dialogues,
            mtl::span<data::page> pages,
            dialogue_bridge const* bridge) noexcept;

        /**
         * @brief Starts the dialogue @p dialogue_id at its first page.
         *
         * @return True when a page is on show. False when @p dialogue_id names
         *         no dialogue, or names one whose first page is absent or falls
         *         outside its own page range - in which case there is nothing to
         *         put on screen and the caller should leave the scene alone.
         */
        bool begin(u16 dialogue_id) noexcept;

        /** @brief True when the conversation is over, or never started. */
        bool has_ended() const noexcept { return _current == data::NO_ID; }

        /** @brief The global PAGS index on show, or @c data::NO_ID when ended. */
        u16 current_page_index() const noexcept { return _current; }

        /** @brief The page on show, or null when the conversation has ended. */
        data::page const* current_page() const noexcept;

        /**
         * @brief Moves to the page the current one names as next.
         *
         * The no-destination sentinel ends the conversation, as does a next page
         * that falls outside the dialogue's page range - which is what running
         * past the last page looks like. Calling this after the end does
         * nothing.
         */
        void advance() noexcept;

        private:  /////////////////////////////////////////////////////////////////////////////////

        /** @brief Whether @p page_id addresses a page of the running dialogue. */
        bool is_own_page(u16 page_id) const noexcept;

        /**
         * @brief Puts @p page_id on show and marks it read, or ends the
         *        conversation when it is not a page of this dialogue.
         */
        void go_to(u16 page_id) noexcept;

        mtl::span<data::dialogue> _dialogues;  ///< All DLGS records.
        mtl::span<data::page> _pages;          ///< All PAGS records.
        dialogue_bridge const* _bridge{ nullptr };

        data::range _page_range{};      ///< The running dialogue's PAGS range.
        u16 _current{ data::NO_ID };    ///< Global index of the page on show.
    };
}  // namespace NEONengine

#endif  // __DIALOGUE_SESSION_H__INCLUDED__
