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
         * None of the spans is copied and the bridge is not owned: all four
         * have to outlive the session. Nothing is on show until begin()
         * succeeds.
         */
        void create(mtl::span<data::dialogue> dialogues,
            mtl::span<data::page> pages,
            mtl::span<data::choice> choices,
            dialogue_bridge const* bridge) noexcept;

        /**
         * @brief Starts the dialogue @p dialogue_id at its first available page.
         *
         * @return True when a page is on show. False when @p dialogue_id names
         *         no dialogue, or names one whose first page is absent, falls
         *         outside its own page range, or is followed by no available
         *         page at all - in which case there is nothing to put on screen
         *         and the caller should leave the scene alone.
         */
        bool begin(u16 dialogue_id) noexcept;

        /** @brief True when the conversation is over, or never started. */
        bool has_ended() const noexcept { return _current == data::NO_ID; }

        /** @brief The global PAGS index on show, or @c data::NO_ID when ended. */
        u16 current_page_index() const noexcept { return _current; }

        /** @brief The page on show, or null when the conversation has ended. */
        data::page const* current_page() const noexcept;

        /**
         * @brief The pages the running dialogue owns, as global PAGS indices.
         *
         * Truncated to the pages actually loaded, so walking it is safe on a
         * malformed pak. This is what a caller wanting everything the
         * conversation could name - the portraits its speakers use, say - reads
         * ahead over. Empty until begin() succeeds, and empty again once it
         * fails.
         */
        data::range page_range() const noexcept { return _page_range; }

        /**
         * @brief Moves to the page the current one names as next.
         *
         * The no-destination sentinel ends the conversation, as does a next page
         * that falls outside the dialogue's page range - which is what running
         * past the last page looks like. When the named page is not available it
         * is replaced by the next available page by index, not by its own
         * next-page field. Calling this after the end does nothing.
         */
        void advance() noexcept;

        /**
         * @brief How many of the current page's choices the player may pick.
         *
         * Worked out once as the page is arrived at, not on every ask, so the
         * engine is consulted once per choice per page and a caller may read
         * this as often as it likes. Zero on a page authored with no choices,
         * on a page whose every choice is gated off or already taken, and once
         * the conversation has ended - the three cases the drawing side treats
         * alike, because a page with nothing to pick advances instead.
         */
        u8 choice_count() const noexcept { return _choice_count; }

        /** @brief The global CHOC index in @p slot, or NO_ID past the end. */
        u16 choice_index(u8 slot) const noexcept;

        /** @brief The choice in @p slot, or null past the end. */
        data::choice const* choice_at(u8 slot) const noexcept;

        /**
         * @brief Takes the choice in @p slot and follows where it leads.
         *
         * The choice is marked taken first, so a one-time choice pointing back
         * at its own page is gone by the time that page is laid out again -
         * which is how "ask about each topic, then leave" is written. The
         * no-destination sentinel ends the conversation, as does a destination
         * outside the dialogue's page range; an unavailable destination is
         * replaced by the next available page by index, exactly as advance()
         * does. A slot past the last available choice does nothing.
         */
        void take_choice(u8 slot) noexcept;

        private:  /////////////////////////////////////////////////////////////////////////////////

        /**
         * @brief The most choices one page may offer the player.
         *
         * A fixed array rather than an allocation: the list is rebuilt on every
         * page arrival, and a conversation cannot be made to allocate by the
         * data it is given. It is a limit on what a page may be authored with,
         * not on what is shown at once - the choice sets that rotate through
         * "More..." take their slices out of this list - so it is set well past
         * what a page is written with in practice. A page offering more has the
         * rest dropped, and says so.
         */
        static constexpr u8 MAX_CHOICES = 32;

        /** @brief Whether @p page_id addresses a page of the running dialogue. */
        bool is_own_page(u16 page_id) const noexcept;

        /**
         * @brief Whether the page at global @p page_id may be presented.
         *
         * Three things have to hold: the author enabled it, its check flag is
         * either absent or currently true, and its one-time bit has not already
         * been burnt by a previous read. The enable bit is static - the loaded
         * game data is read-only - so only the last two can change during a
         * playthrough.
         */
        bool is_available(u16 page_id) const noexcept;

        /**
         * @brief Whether the choice at global @p choice_id may be offered.
         *
         * The same three counts a page answers to: the author enabled it, its
         * check flag is either absent or currently true, and its one-time bit
         * has not already been burnt. The difference is when the burn happens -
         * a choice is burnt when it is taken, never when it is displayed, or it
         * could never be picked.
         */
        bool is_choice_available(u16 choice_id) const noexcept;

        /**
         * @brief Rebuilds the list of choices the page on show may offer.
         *
         * Called once as each page is arrived at, and again when the player
         * comes back to the same page, so a choice that burnt itself on the way
         * round is gone the second time. Leaves the list empty when the
         * conversation has ended.
         */
        void collect_choices() noexcept;

        /**
         * @brief Puts @p page_id on show and marks it read, or ends the
         *        conversation when there is nothing available to show.
         *
         * An unavailable page is not shown; the search moves forward by index
         * through the dialogue's pages until it finds one that is available,
         * and ends the conversation on running off the end. Skipped pages run
         * nothing and their own next-page field is never consulted. See
         * ADR-0002 for why the scan is by index rather than along next-page
         * links.
         */
        void go_to(u16 page_id) noexcept;

        mtl::span<data::dialogue> _dialogues;  ///< All DLGS records.
        mtl::span<data::page> _pages;          ///< All PAGS records.
        mtl::span<data::choice> _choices;      ///< All CHOC records.
        dialogue_bridge const* _bridge{ nullptr };

        data::range _page_range{};      ///< The running dialogue's PAGS range.
        u16 _current{ data::NO_ID };    ///< Global index of the page on show.

        /// Global CHOC indices the page on show may offer, in authored order.
        u16 _available[MAX_CHOICES]{};
        u8 _choice_count{ 0 };
    };
}  // namespace NEONengine

#endif  // __DIALOGUE_SESSION_H__INCLUDED__
