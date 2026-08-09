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
     * @brief What the player has read and taken, for the whole playthrough.
     *
     * Pages and choices are addressed by their global index - their position in
     * the loaded game data's PAGS and CHOC chunks - rather than by an index
     * within their dialogue, because that is what every record that points at
     * one carries.
     *
     * Both records are packed one bit per entry, following the timeline's
     * fired-keyframe array: a playthrough can hold thousands of pages, and a
     * byte apiece would be seven eighths waste in a machine measured in
     * megabytes.
     */
    class game_session
    {
        public:  //////////////////////////////////////////////////////////////////////////////////
        /**
         * @brief Sizes the session for @p page_count pages, @p choice_count
         *        choices and @p item_count inventory items, with all state clear.
         *
         * Sizing from the loaded game data means an index that came from a
         * record in that data is always addressable. Calling this again starts
         * the playthrough's memory over, which is what a new game wants.
         */
        void create(u32 page_count, u32 choice_count, u32 item_count = 0);

        /**
         * @brief Sizes the session from what @p data holds.
         *
         * The counts are the only thing taken from @p data; nothing is kept
         * pointing into it.
         */
        void create_from(game_data const& data);

        /** @brief How many pages this session can remember. */
        u32 page_count() const noexcept { return _page_count; }

        /** @brief How many choices this session can remember. */
        u32 choice_count() const noexcept { return _choice_count; }

        /** @brief How many inventory items this session can remember. */
        u32 item_count() const noexcept { return _item_count; }

        /**
         * @brief Whether the page at global @p index has been read.
         *
         * An index the session was not sized for is not read, and never becomes
         * read: a page that does not exist cannot have been displayed. Answering
         * rather than trapping keeps a malformed pak to a page that reads oddly
         * instead of a crash.
         */
        bool is_page_read(u32 index) const noexcept { return test(_pages_read, _page_count, index); }

        /** @brief Records that the page at global @p index has been read. */
        void mark_page_read(u32 index) noexcept { set(_pages_read, _page_count, index); }

        /**
         * @brief Whether the choice at global @p index has been taken.
         *
         * Out of range is not taken, on the same terms as is_page_read().
         */
        bool is_choice_taken(u32 index) const noexcept
        {
            return test(_choices_taken, _choice_count, index);
        }

        /** @brief Records that the choice at global @p index has been taken. */
        void mark_choice_taken(u32 index) noexcept { set(_choices_taken, _choice_count, index); }

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

        u16 get_selected_inventory_item() const noexcept
        {
            return _values[to<u32>(engine_values::SELECTED_INV_ITEM)];
        }

        private:  /////////////////////////////////////////////////////////////////////////////////
        /** @brief Bytes needed to hold @p count bits. */
        static u32 byte_count(u32 count) noexcept { return (count + 7) / 8; }

        /** @brief Whether bit @p index of @p bits is set; false past @p count. */
        static bool test(mtl::vector<u8> const& bits, u32 count, u32 index) noexcept
        {
            if (index >= count) return false;
            return (bits[index >> 3] & to<u8>(1 << (index & 7))) != 0;
        }

        /** @brief Sets bit @p index of @p bits, ignoring an index past @p count. */
        static void set(mtl::vector<u8>& bits, u32 count, u32 index) noexcept
        {
            if (index >= count) return;
            bits[index >> 3] |= to<u8>(1 << (index & 7));
        }

        bool validate_item_id(u16 id) const noexcept
        {
            if (to<u32>(id) < _item_count) return true;
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

        void create_initial_values(mtl::vector<u16> const& initial_values,
            data::header const& header) noexcept;

        /// One bit per page, in PAGS order: whether the player has read it.
        mtl::vector<u8> _pages_read;

        /// One bit per choice, in CHOC order: whether the player has taken it.
        mtl::vector<u8> _choices_taken;

        /// One count per inventory item, in INVT order.
        mtl::vector<u16> _item_counts;

        mtl::vector<u16> _values;  ///< One signed value per declared value, in VALS order.

        u32 _page_count{ 0 };    ///< Pages the bit array was sized for.
        u32 _choice_count{ 0 };  ///< Choices the bit array was sized for.
        u32 _item_count{ 0 };    ///< Inventory items the count array was sized for.

        invalid_item_id_reporter _invalid_item_id_reporter{ nullptr };
    };
}  // namespace NEONengine

#endif  // __GAME_SESSION_H__INCLUDED__
