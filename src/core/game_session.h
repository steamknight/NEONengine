/**
 * @file game_session.h
 * @brief The persistent state of one playthrough.
 *
 * What the player has done, as opposed to what the game data says. The engine
 * creates one at boot and holds it for as long as the playthrough lasts.
 *
 * This is declared the home for all persistent game state, but holds only two
 * things for now: which pages have been read and which choices have been taken.
 * Flags and values stay where the VM has them, because the VM borrows the
 * engine's buffers for its lifetime and moving them is not a change this can
 * make on its own.
 *
 * Nothing writes any of this to disk. The save format is its own work.
 */

#ifndef __GAME_SESSION_H__INCLUDED__
#define __GAME_SESSION_H__INCLUDED__

#include "types.h"

#include <mtl/vector.h>

namespace NEONengine
{
    class game_data;

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
         * @brief Sizes the session for @p page_count pages and @p choice_count
         *        choices, with nothing read and nothing taken.
         *
         * Sizing from the loaded game data means an index that came from a
         * record in that data is always addressable. Calling this again starts
         * the playthrough's memory over, which is what a new game wants.
         */
        void create(u32 page_count, u32 choice_count);

        /**
         * @brief Sizes the session from what @p data holds.
         *
         * The counts are the only thing taken from @p data; nothing is kept
         * pointing into it.
         */
        void create_from(game_data const& data);

        /** @brief How many pages this session can remember. */
        u32 page_count() const noexcept { return m_page_count; }

        /** @brief How many choices this session can remember. */
        u32 choice_count() const noexcept { return m_choice_count; }

        /**
         * @brief Whether the page at global @p index has been read.
         *
         * An index the session was not sized for is not read, and never becomes
         * read: a page that does not exist cannot have been displayed. Answering
         * rather than trapping keeps a malformed pak to a page that reads oddly
         * instead of a crash.
         */
        bool is_page_read(u32 index) const noexcept { return test(m_pages_read, m_page_count, index); }

        /** @brief Records that the page at global @p index has been read. */
        void mark_page_read(u32 index) noexcept { set(m_pages_read, m_page_count, index); }

        /**
         * @brief Whether the choice at global @p index has been taken.
         *
         * Out of range is not taken, on the same terms as is_page_read().
         */
        bool is_choice_taken(u32 index) const noexcept
        {
            return test(m_choices_taken, m_choice_count, index);
        }

        /** @brief Records that the choice at global @p index has been taken. */
        void mark_choice_taken(u32 index) noexcept { set(m_choices_taken, m_choice_count, index); }

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

        /// One bit per page, in PAGS order: whether the player has read it.
        mtl::vector<u8> m_pages_read;

        /// One bit per choice, in CHOC order: whether the player has taken it.
        mtl::vector<u8> m_choices_taken;

        u32 m_page_count{ 0 };    ///< Pages the bit array was sized for.
        u32 m_choice_count{ 0 };  ///< Choices the bit array was sized for.
    };
}  // namespace NEONengine

#endif  // __GAME_SESSION_H__INCLUDED__
