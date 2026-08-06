#ifndef __GAME_SESSION_TESTS_H__INCLUDED__
#define __GAME_SESSION_TESTS_H__INCLUDED__

#ifdef ACE_TEST_RUNNER

#include "types.h"

#include "core/game_session.h"

#include "test_macros.h"

namespace NEONengine
{
    namespace
    {
        /// A session sized for @p pages pages, @p choices choices and @p items items.
        game_session sized_session(u32 pages, u32 choices, u32 items = 0)
        {
            game_session session;
            session.create(pages, choices, items);
            return session;
        }

#ifdef GAME_DEBUG
        u16 s_last_invalid_item_id = 0;
        u32 s_invalid_item_reports = 0;

        void on_invalid_item_id(u16 item_id)
        {
            s_last_invalid_item_id = item_id;
            ++s_invalid_item_reports;
        }
#endif
    }  // namespace

    // ── Creation ────────────────────────────────────────────────────────────────

    TEST_IMPL(test_session_starts_empty)
    {
        game_session session;

        TEST_ASSERT(session.page_count() == 0, "a fresh session should hold no pages");
        TEST_ASSERT(session.choice_count() == 0, "a fresh session should hold no choices");
        TEST_ASSERT(!session.is_page_read(0), "a page of a fresh session cannot be read");
        TEST_ASSERT(!session.is_choice_taken(0), "a choice of a fresh session cannot be taken");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_session_takes_its_size_from_the_counts)
    {
        auto session = sized_session(37, 9);

        TEST_ASSERT(session.page_count() == 37, "the session should hold every page it was sized for");
        TEST_ASSERT(session.choice_count() == 9, "the session should hold every choice it was sized for");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_session_starts_nothing_read_or_taken)
    {
        auto session = sized_session(100, 100);

        for (u32 index = 0; index < 100; ++index)
        {
            TEST_ASSERT(!session.is_page_read(index), "no page should start out read");
            TEST_ASSERT(!session.is_choice_taken(index), "no choice should start out taken");
        }
        TEST_SUCCESS;
    }

    TEST_IMPL(test_session_created_again_forgets_everything)
    {
        auto session = sized_session(16, 16);
        session.mark_page_read(3);
        session.mark_choice_taken(4);

        session.create(16, 16);

        TEST_ASSERT(!session.is_page_read(3), "creating again should forget what was read");
        TEST_ASSERT(!session.is_choice_taken(4), "creating again should forget what was taken");
        TEST_SUCCESS;
    }

    // ── Reading and taking ──────────────────────────────────────────────────────

    TEST_IMPL(test_session_remembers_a_page_read)
    {
        auto session = sized_session(10, 10);

        session.mark_page_read(5);

        TEST_ASSERT(session.is_page_read(5), "the page marked read should be read");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_session_remembers_a_choice_taken)
    {
        auto session = sized_session(10, 10);

        session.mark_choice_taken(5);

        TEST_ASSERT(session.is_choice_taken(5), "the choice marked taken should be taken");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_session_marking_a_page_touches_no_other)
    {
        auto session = sized_session(24, 24);

        session.mark_page_read(9);

        for (u32 index = 0; index < 24; ++index)
        {
            bool const expected = (index == 9);
            TEST_ASSERT(session.is_page_read(index) == expected,
                "only the page that was read should be read");
        }
        TEST_SUCCESS;
    }

    TEST_IMPL(test_session_marking_a_choice_touches_no_other)
    {
        auto session = sized_session(24, 24);

        session.mark_choice_taken(9);

        for (u32 index = 0; index < 24; ++index)
        {
            bool const expected = (index == 9);
            TEST_ASSERT(session.is_choice_taken(index) == expected,
                "only the choice that was taken should be taken");
        }
        TEST_SUCCESS;
    }

    TEST_IMPL(test_session_keeps_pages_and_choices_apart)
    {
        auto session = sized_session(10, 10);

        session.mark_page_read(2);

        TEST_ASSERT(session.is_page_read(2), "the page should be read");
        TEST_ASSERT(!session.is_choice_taken(2), "the choice at the same index should not be taken");

        session.mark_choice_taken(7);

        TEST_ASSERT(session.is_choice_taken(7), "the choice should be taken");
        TEST_ASSERT(!session.is_page_read(7), "the page at the same index should not be read");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_session_marking_twice_is_harmless)
    {
        auto session = sized_session(10, 10);

        session.mark_page_read(1);
        session.mark_page_read(1);
        session.mark_choice_taken(1);
        session.mark_choice_taken(1);

        TEST_ASSERT(session.is_page_read(1), "the page should still be read");
        TEST_ASSERT(session.is_choice_taken(1), "the choice should still be taken");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_session_holds_every_index_it_was_sized_for)
    {
        auto session = sized_session(70, 70);

        for (u32 index = 0; index < 70; ++index)
        {
            session.mark_page_read(index);
            session.mark_choice_taken(index);
        }

        for (u32 index = 0; index < 70; ++index)
        {
            TEST_ASSERT(session.is_page_read(index), "every page marked should be read");
            TEST_ASSERT(session.is_choice_taken(index), "every choice marked should be taken");
        }
        TEST_SUCCESS;
    }

    // ── Out of range ────────────────────────────────────────────────────────────

    TEST_IMPL(test_session_reading_past_the_end_is_not_read)
    {
        auto session = sized_session(8, 8);

        TEST_ASSERT(!session.is_page_read(8), "the page one past the end cannot be read");
        TEST_ASSERT(!session.is_page_read(0xFFFFFFFF), "a wild page index cannot be read");
        TEST_ASSERT(!session.is_choice_taken(8), "the choice one past the end cannot be taken");
        TEST_ASSERT(!session.is_choice_taken(0xFFFFFFFF), "a wild choice index cannot be taken");
        TEST_SUCCESS;
    }

    /**
     * @brief An out-of-range write must not land on a neighbouring record.
     *
     * The counts here are not multiples of eight, so the bit array has spare
     * bits in its last byte: an unchecked write of index 12 would set one of
     * them, and index 20 would run off the array entirely.
     */
    TEST_IMPL(test_session_writing_past_the_end_changes_nothing)
    {
        auto session = sized_session(12, 12);

        session.mark_page_read(12);
        session.mark_page_read(20);
        session.mark_choice_taken(12);
        session.mark_choice_taken(20);

        for (u32 index = 0; index < 12; ++index)
        {
            TEST_ASSERT(!session.is_page_read(index), "a write past the end should reach no page");
            TEST_ASSERT(!session.is_choice_taken(index), "a write past the end should reach no choice");
        }

        TEST_ASSERT(!session.is_page_read(12), "the page past the end should still not be read");
        TEST_ASSERT(!session.is_choice_taken(12), "the choice past the end should still not be taken");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_session_of_no_records_answers_everything_no)
    {
        auto session = sized_session(0, 0);

        session.mark_page_read(0);
        session.mark_choice_taken(0);

        TEST_ASSERT(!session.is_page_read(0), "a session with no pages has nothing to read");
        TEST_ASSERT(!session.is_choice_taken(0), "a session with no choices has nothing to take");
        TEST_SUCCESS;
    }

    /// The last addressable index is the one most likely to be dropped by an
    /// off-by-one in the packing, so it gets its own test.
    TEST_IMPL(test_session_holds_the_last_index_of_a_ragged_size)
    {
        auto session = sized_session(11, 13);

        session.mark_page_read(10);
        session.mark_choice_taken(12);

        TEST_ASSERT(session.is_page_read(10), "the last page should be read");
        TEST_ASSERT(session.is_choice_taken(12), "the last choice should be taken");
        TEST_SUCCESS;
    }

    // ── Inventory ───────────────────────────────────────────────────────────────

    TEST_IMPL(test_session_inventory_starts_empty)
    {
        auto session = sized_session(0, 0, 6);

        for (u16 id = 0; id < 6; ++id)
        {
            TEST_ASSERT(session.count(id) == 0, "every item should start at zero");
            TEST_ASSERT(!session.has(id), "an item at zero should not be owned");
        }
        TEST_SUCCESS;
    }

    TEST_IMPL(test_session_inventory_add_increments_one_item)
    {
        auto session = sized_session(0, 0, 4);

        session.add_item(2);

        TEST_ASSERT(session.count(2) == 1, "add should increment the addressed item");
        TEST_ASSERT(session.has(2), "the addressed item should now be owned");
        TEST_ASSERT(session.count(1) == 0, "other items should stay unchanged");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_session_inventory_add_saturates_at_u16_max)
    {
        auto session = sized_session(0, 0, 1);

        for (u32 i = 0; i < 65535; ++i) session.add_item(0);
        session.add_item(0);

        TEST_ASSERT(session.count(0) == 65535, "add should cap at 65535");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_session_inventory_remove_decrements_one_item)
    {
        auto session = sized_session(0, 0, 3);
        session.add_item(1);
        session.add_item(1);

        session.remove_item(1);

        TEST_ASSERT(session.count(1) == 1, "remove should decrement the addressed item");
        TEST_ASSERT(session.count(0) == 0, "other items should stay unchanged");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_session_inventory_remove_floors_at_zero)
    {
        auto session = sized_session(0, 0, 1);

        session.remove_item(0);

        TEST_ASSERT(session.count(0) == 0, "remove should not underflow below zero");
        TEST_ASSERT(!session.has(0), "an item floored at zero should not be owned");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_session_inventory_clear_item_sets_count_to_zero)
    {
        auto session = sized_session(0, 0, 2);
        session.add_item(1);
        session.add_item(1);

        session.clear_item(1);

        TEST_ASSERT(session.count(1) == 0, "clear_item should reset that item to zero");
        TEST_ASSERT(!session.has(1), "a cleared item should not be owned");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_session_inventory_out_of_range_is_safe_and_unchanged)
    {
        auto session = sized_session(0, 0, 3);
        session.add_item(0);
        session.add_item(2);

        session.add_item(3);
        session.remove_item(3);
        session.clear_item(3);

        TEST_ASSERT(!session.has(3), "an unknown item id should never be owned");
        TEST_ASSERT(session.count(3) == 0, "an unknown item id should count as zero");
        TEST_ASSERT(session.count(0) == 1, "out-of-range operations should not touch other items");
        TEST_ASSERT(session.count(2) == 1, "out-of-range operations should not touch other items");
        TEST_SUCCESS;
    }

#ifdef GAME_DEBUG
    TEST_IMPL(test_session_inventory_reports_out_of_range_in_debug)
    {
        auto session = sized_session(0, 0, 2);
        s_last_invalid_item_id = 0;
        s_invalid_item_reports = 0;
        session.set_invalid_item_id_reporter(on_invalid_item_id);

        session.add_item(9);
        session.remove_item(9);
        session.clear_item(9);
        (void)session.has(9);
        (void)session.count(9);

        TEST_ASSERT(s_invalid_item_reports == 5,
            "every out-of-range inventory operation should report in debug");
        TEST_ASSERT(s_last_invalid_item_id == 9, "the reported id should match the invalid call");
        TEST_SUCCESS;
    }
#endif

    TEST_IMPL(test_session_inventory_create_again_resets_counts)
    {
        auto session = sized_session(0, 0, 2);
        session.add_item(0);
        session.add_item(1);

        session.create(0, 0, 2);

        TEST_ASSERT(session.count(0) == 0, "creating again should clear item 0");
        TEST_ASSERT(session.count(1) == 0, "creating again should clear item 1");
        TEST_SUCCESS;
    }

    TEST_SUITE_BEGIN(game_session)
        TEST(test_session_starts_empty)
        TEST(test_session_takes_its_size_from_the_counts)
        TEST(test_session_starts_nothing_read_or_taken)
        TEST(test_session_created_again_forgets_everything)

        TEST(test_session_remembers_a_page_read)
        TEST(test_session_remembers_a_choice_taken)
        TEST(test_session_marking_a_page_touches_no_other)
        TEST(test_session_marking_a_choice_touches_no_other)
        TEST(test_session_keeps_pages_and_choices_apart)
        TEST(test_session_marking_twice_is_harmless)
        TEST(test_session_holds_every_index_it_was_sized_for)

        TEST(test_session_reading_past_the_end_is_not_read)
        TEST(test_session_writing_past_the_end_changes_nothing)
        TEST(test_session_of_no_records_answers_everything_no)
        TEST(test_session_holds_the_last_index_of_a_ragged_size)

        TEST(test_session_inventory_starts_empty)
        TEST(test_session_inventory_add_increments_one_item)
        TEST(test_session_inventory_add_saturates_at_u16_max)
        TEST(test_session_inventory_remove_decrements_one_item)
        TEST(test_session_inventory_remove_floors_at_zero)
        TEST(test_session_inventory_clear_item_sets_count_to_zero)
        TEST(test_session_inventory_out_of_range_is_safe_and_unchanged)
#ifdef GAME_DEBUG
        TEST(test_session_inventory_reports_out_of_range_in_debug)
#endif
        TEST(test_session_inventory_create_again_resets_counts)
    TEST_SUITE_END
}  // namespace NEONengine

#endif  // ACE_TEST_RUNNER

#endif  // __GAME_SESSION_TESTS_H__INCLUDED__
