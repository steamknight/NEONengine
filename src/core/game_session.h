/**
 * @file game_session.h
 * @brief The persistent state of one playthrough.
 *
 * What the player has done, as opposed to what the game data says. The engine
 * creates one at boot and holds it for as long as the playthrough lasts.
 *
 * This is declared the home for all persistent game state, but holds only three
 * things for now: which pages have been read, which choices have been taken,
 * and how many of each authored inventory item the player owns. Flags and
 * values stay where the VM has them, because the VM borrows the engine's
 * buffers for its lifetime and moving them is not a change this can make on
 * its own.
 *
 * Nothing writes any of this to disk. The save format is its own work.
 */

#ifndef __GAME_SESSION_H__INCLUDED__
#define __GAME_SESSION_H__INCLUDED__

#include <mtl/bitset.h>
#include <mtl/span.h>
#include <mtl/vector.h>

#include "game_data.h"
#include "types.h"

namespace NEONengine
{
    class game_data;

    /// Offsets into the initial VALUEs set that contain engine provided data
    enum class engine_values : i32
    {
        ENGINE_BUILD_NUMBER = -1,
        GAME_DATA_VERSION   = -2,
        SELECTED_INV_ITEM   = -3,

        COUNT = 3,
    };

    constexpr u16 NO_SELECTED_ITEM = 0xFFFFu;

    /**
     * @brief Mutable game state for one playthrough. This is what gets saved and loaded from disk.
     */
    class game_session
    {
        public:  //////////////////////////////////////////////////////////////////////////////////
        /**
         * @brief Sizes the session for @p page_count pages, @p choice_count
         *        choices and @p item_count inventory items, with all state clear.
         *
         * Only used by unit tests; the engine creates a session from game data or from a save file.
         */
        void create_for_test(u32 page_count, u32 choice_count, u32 item_count = 0);

        /**
         * @brief Sizes the session from what @p data holds.
         *
         * The counts are the only thing taken from @p data; nothing is kept
         * pointing into it.
         */
        void create_from(game_data const& data);

        /** @brief How many pages this session can remember. */
        u32 page_count() const noexcept { return _pages_read.size(); }

        /** @brief How many choices this session can remember. */
        u32 choice_count() const noexcept { return _choices_taken.size(); }

        /** @brief How many inventory items this session can remember. */
        u32 item_count() const noexcept { return _item_counts.size(); }

        /**
         * @brief Whether the page at global @p index has been read.
         *
         * An index the session was not sized for is not read, and never becomes
         * read: a page that does not exist cannot have been displayed. Answering
         * rather than trapping keeps a malformed pak to a page that reads oddly
         * instead of a crash.
         */
        bool is_page_read(u32 index) const noexcept
        {
            if (index >= _pages_read.size()) return false;
            return _pages_read.test(index);
        }

        /** @brief Records that the page at global @p index has been read. */
        void mark_page_read(u32 index) noexcept
        {
            if (index >= _pages_read.size()) return;
            _pages_read.set(index);
        }

        /**
         * @brief Whether the choice at global @p index has been taken.
         *
         * Out of range is not taken, on the same terms as is_page_read().
         */
        bool is_choice_taken(u32 index) const noexcept
        {
            if (index >= _choices_taken.size()) return false;
            return _choices_taken.test(index);
        }

        /** @brief Records that the choice at global @p index has been taken. */
        void mark_choice_taken(u32 index) noexcept
        {
            if (index >= _choices_taken.size()) return;
            _choices_taken.set(index);
        }

        /** @brief Adds one of inventory item @p id, saturating at 65535. */
        void add_item(u16 id) noexcept
        {
            if (!validate_item_id(id)) return;

            u16& owned = _item_counts[to<u32>(id)];
            if (owned < 0xFFFFu) ++owned;
        }

        /** @brief Removes one of inventory item @p id, flooring at zero. */
        void remove_item(u16 id) noexcept
        {
            if (!validate_item_id(id)) return;

            u16& owned = _item_counts[to<u32>(id)];
            if (owned > 0) --owned;
        }

        /** @brief Whether inventory item @p id is currently owned. */
        bool has(u16 id) const noexcept { return count(id) > 0; }

        /** @brief Current owned count for inventory item @p id, or zero when unknown. */
        u16 count(u16 id) const noexcept
        {
            if (!validate_item_id(id)) return 0;
            return _item_counts[to<u32>(id)];
        }

        /** @brief Sets inventory item @p id to zero owned. */
        void clear_item(u16 id) noexcept
        {
            if (!validate_item_id(id)) return;
            _item_counts[to<u32>(id)] = 0;
        }

        using invalid_item_id_reporter = void (*)(u16 item_id);
        void set_invalid_item_id_reporter(invalid_item_id_reporter reporter) noexcept
        {
            _invalid_item_id_reporter = reporter;
        }

        mtl::vector<u16>& get_values() noexcept { return _values; }
        mtl::vector<u16> const& get_values() const noexcept { return _values; }

        mtl::bitset<u32>& get_flags() noexcept { return _flags; }
        mtl::bitset<u32> const& get_flags() const noexcept { return _flags; }

        u16 get_selected_inventory_item() const noexcept
        {
            return _values[engine_value_index(engine_values::SELECTED_INV_ITEM)];
        }

        private:  /////////////////////////////////////////////////////////////////////////////////

        /**
         * @brief Returns the index of the engine "value" for @p which.
         */
        u32 engine_value_index(engine_values which) const noexcept
        {
            u32 const base = to<u32>(_values.size()) - to<u32>(engine_values::COUNT);
            return base + to<u32>(-to<i32>(which) - 1);
        }

        bool validate_item_id(u16 id) const noexcept
        {
            if (to<u32>(id) < _item_counts.size()) return true;
            report_invalid_item_id(id);
            return false;
        }

        void report_invalid_item_id(u16 id) const noexcept
        {
#ifdef GAME_DEBUG
            if (_invalid_item_id_reporter) _invalid_item_id_reporter(id);
#else
            (void)id;
#endif
        }

        void create_initial_values(mtl::span<i16> initial_values,
            data::header const& header) noexcept;

        private:  /////////////////////////////////////////////////////////////////////////////////

        /// One bit per page, in PAGS order: whether the player has read it.
        mtl::bitset<u32> _pages_read;

        /// One bit per choice, in CHOC order: whether the player has taken it.
        mtl::bitset<u32> _choices_taken;

        /// One count per inventory item, in INVT order.
        mtl::vector<u16> _item_counts;

        mtl::vector<u16> _values;  ///< One signed value per declared value, in VALS order.
        mtl::bitset<u32> _flags;   ///< One bit per declared flag, in FLGS order.

        invalid_item_id_reporter _invalid_item_id_reporter{ nullptr };
    };
}  // namespace NEONengine

#endif  // __GAME_SESSION_H__INCLUDED__
