#ifndef __DIALOGUE_SESSION_TESTS_H__INCLUDED__
#define __DIALOGUE_SESSION_TESTS_H__INCLUDED__

#ifdef ACE_TEST_RUNNER

#include "types.h"

#include "core/dialogue_bridge.h"
#include "core/dialogue_session.h"
#include "core/game_data_format.h"

#include "test_macros.h"

namespace NEONengine
{
    namespace
    {
        // ── Mock bridge ─────────────────────────────────────────────────────────
        //
        // An ordered list of calls rather than a flat last-call-wins record, so a
        // test can assert on the whole sequence a conversation produced and, as
        // the table grows, on what ran before what.

        /// Which bridge function a recorded call was.
        enum class dlg_call_kind : u8
        {
            mark_page_read,
        };

        struct dlg_call
        {
            dlg_call_kind kind;
            u16 arg;
        };

        struct dlg_mock_log
        {
            static constexpr u8 MAX_CALLS = 32;

            dlg_call calls[MAX_CALLS];
            u8 count;

            void reset() { count = 0; }

            void record(dlg_call_kind kind, u16 arg)
            {
                if (count >= MAX_CALLS) return;
                calls[count++] = dlg_call{ kind, arg };
            }
        };

        dlg_mock_log g_dlg_log;

        void dlg_mock_mark_page_read(u16 page_index)
        {
            g_dlg_log.record(dlg_call_kind::mark_page_read, page_index);
        }

        dialogue_bridge const g_dlg_mock_bridge = {
            .mark_page_read = dlg_mock_mark_page_read,
        };

        /// True when the pages read, in the order they were read, are @p expected.
        bool pages_read_were(u16 const* expected, u8 expected_count)
        {
            if (g_dlg_log.count != expected_count) return false;
            for (u8 i = 0; i < expected_count; ++i)
            {
                if (g_dlg_log.calls[i].kind != dlg_call_kind::mark_page_read) return false;
                if (g_dlg_log.calls[i].arg != expected[i]) return false;
            }
            return true;
        }

        // ── Record builders ─────────────────────────────────────────────────────

        /// A page that says string @p text_string_id and then goes to @p next.
        data::page a_page(u16 text_string_id, u16 next)
        {
            data::page page{};
            page.speaker_id          = data::NO_ID;
            page.text_string_id      = text_string_id;
            page.set_flag_id         = data::NO_ID;
            page.clear_flag_id       = data::NO_ID;
            page.check_flag_id       = data::NO_ID;
            page.next_page_id        = next;
            page.display_function_id = data::NO_ID;
            page.flags               = 1;  // enabled
            page.choice_range        = data::range{ 0, 0 };
            return page;
        }

        /// A dialogue over pages [@p start, @p start + @p count), entered at @p first.
        data::dialogue a_dialogue(u16 start, u16 count, u16 first)
        {
            data::dialogue dialogue{};
            dialogue.page_range    = data::range{ start, count };
            dialogue.first_page_id = first;
            return dialogue;
        }

        /// A session over @p dialogues and @p pages, wired to the mock bridge.
        void wire(dialogue_session& session,
            data::dialogue const* dialogues,
            u32 dialogue_count,
            data::page const* pages,
            u32 page_count)
        {
            g_dlg_log.reset();
            session.create(mtl::span<data::dialogue>(dialogues, dialogue_count),
                mtl::span<data::page>(pages, page_count),
                &g_dlg_mock_bridge);
        }
    }  // namespace

    // ── Starting a dialogue ─────────────────────────────────────────────────────

    TEST_IMPL(test_dialogue_starts_ended)
    {
        dialogue_session session;

        TEST_ASSERT(session.has_ended(), "a session with no dialogue has ended");
        TEST_ASSERT(session.current_page() == nullptr, "an ended session shows no page");
        TEST_ASSERT(session.current_page_index() == data::NO_ID,
            "an ended session names no page index");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_opens_on_its_first_page)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 3, 1) };
        data::page pages[]
            = { a_page(10, data::NO_ID), a_page(11, data::NO_ID), a_page(12, data::NO_ID) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 3);

        TEST_ASSERT(session.begin(0), "a dialogue with a first page should start");
        TEST_ASSERT(!session.has_ended(), "a started dialogue has not ended");
        TEST_ASSERT(session.current_page_index() == 1, "the entry page is the one named first");
        TEST_ASSERT(session.current_page()->text_string_id == 11,
            "the page on show is the one at the entry index");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_marks_its_entry_page_read)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 2, 0) };
        data::page pages[] = { a_page(10, data::NO_ID), a_page(11, data::NO_ID) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 2);
        session.begin(0);

        u16 const expected[] = { 0 };
        TEST_ASSERT(pages_read_were(expected, 1), "the entry page should be marked read, once");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_id_naming_nothing_does_not_start)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 1, 0) };
        data::page pages[] = { a_page(10, data::NO_ID) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 1);

        TEST_ASSERT(!session.begin(1), "a dialogue id past the last dialogue names nothing");
        TEST_ASSERT(session.has_ended(), "a dialogue that could not start has ended");
        TEST_ASSERT(g_dlg_log.count == 0, "a dialogue that could not start reads no page");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_with_no_first_page_does_not_start)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 1, data::NO_ID) };
        data::page pages[] = { a_page(10, data::NO_ID) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 1);

        TEST_ASSERT(!session.begin(0), "a dialogue naming no first page cannot start");
        TEST_ASSERT(g_dlg_log.count == 0, "a dialogue that could not start reads no page");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_with_a_first_page_outside_its_range_does_not_start)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 2, 5) };
        data::page pages[] = { a_page(10, data::NO_ID), a_page(11, data::NO_ID) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 2);

        TEST_ASSERT(!session.begin(0), "a first page outside the dialogue's pages cannot start");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_with_no_pages_does_not_start)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 0, 0) };
        data::page pages[] = { a_page(10, data::NO_ID) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 1);

        TEST_ASSERT(!session.begin(0), "a dialogue holding no pages cannot start");
        TEST_SUCCESS;
    }

    /// A range that runs past the loaded pages must not be read off the end.
    TEST_IMPL(test_dialogue_range_past_the_pages_is_truncated)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 9, 0) };
        data::page pages[] = { a_page(10, 1), a_page(11, 2) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 2);

        TEST_ASSERT(session.begin(0), "the pages that are there should still play");

        session.advance();
        TEST_ASSERT(session.current_page_index() == 1, "the second page is still in range");

        session.advance();
        TEST_ASSERT(session.has_ended(), "a next page past the loaded pages ends the dialogue");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_starting_over_forgets_the_previous_one)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 2, 0), a_dialogue(2, 1, 2) };
        data::page pages[]
            = { a_page(10, 1), a_page(11, data::NO_ID), a_page(12, data::NO_ID) };

        dialogue_session session;
        wire(session, dialogues, 2, pages, 3);
        session.begin(0);
        session.advance();

        TEST_ASSERT(session.begin(1), "the second dialogue should start");
        TEST_ASSERT(session.current_page_index() == 2, "it should open on its own first page");

        session.advance();
        TEST_ASSERT(session.has_ended(), "it should end on its own terms, not the first one's");
        TEST_SUCCESS;
    }

    // ── Advancing ───────────────────────────────────────────────────────────────

    TEST_IMPL(test_dialogue_advances_to_the_named_next_page)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 3, 0) };
        data::page pages[] = { a_page(10, 2), a_page(11, data::NO_ID), a_page(12, 1) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 3);
        session.begin(0);

        session.advance();
        TEST_ASSERT(session.current_page_index() == 2, "advancing follows the next-page field");

        session.advance();
        TEST_ASSERT(session.current_page_index() == 1, "and follows it again from there");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_marks_every_page_read_as_it_arrives)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 3, 0) };
        data::page pages[] = { a_page(10, 2), a_page(11, data::NO_ID), a_page(12, 1) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 3);
        session.begin(0);
        session.advance();
        session.advance();
        session.advance();

        u16 const expected[] = { 0, 2, 1 };
        TEST_ASSERT(pages_read_were(expected, 3),
            "every page reached should be marked read, in the order it was shown");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_ends_on_the_no_destination_sentinel)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 2, 0) };
        data::page pages[] = { a_page(10, data::NO_ID), a_page(11, data::NO_ID) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 2);
        session.begin(0);

        session.advance();

        TEST_ASSERT(session.has_ended(), "a page with no destination ends the conversation");
        TEST_ASSERT(session.current_page() == nullptr, "an ended conversation shows no page");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_ends_running_past_its_last_page)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 2, 0) };
        data::page pages[] = { a_page(10, 2), a_page(11, data::NO_ID), a_page(12, data::NO_ID) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 3);
        session.begin(0);

        session.advance();

        TEST_ASSERT(session.has_ended(), "a next page outside the dialogue ends the conversation");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_ending_reads_no_further_page)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 2, 0) };
        data::page pages[] = { a_page(10, data::NO_ID), a_page(11, data::NO_ID) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 2);
        session.begin(0);
        session.advance();

        u16 const expected[] = { 0 };
        TEST_ASSERT(pages_read_were(expected, 1),
            "the page the conversation never reached should not be read");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_advancing_after_the_end_does_nothing)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 1, 0) };
        data::page pages[] = { a_page(10, data::NO_ID) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 1);
        session.begin(0);
        session.advance();

        session.advance();
        session.advance();

        u16 const expected[] = { 0 };
        TEST_ASSERT(session.has_ended(), "an ended conversation stays ended");
        TEST_ASSERT(pages_read_were(expected, 1), "advancing past the end reads nothing");
        TEST_SUCCESS;
    }

    /// A page pointing at itself is the tightest loop the data can express; it
    /// must go round rather than end or stall.
    TEST_IMPL(test_dialogue_follows_a_page_pointing_at_itself)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 1, 0) };
        data::page pages[] = { a_page(10, 0) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 1);
        session.begin(0);

        session.advance();
        session.advance();

        u16 const expected[] = { 0, 0, 0 };
        TEST_ASSERT(session.current_page_index() == 0, "the page should still be on show");
        TEST_ASSERT(pages_read_were(expected, 3), "each display should be marked read");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_pages_are_addressed_globally)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 2, 0), a_dialogue(2, 2, 3) };
        data::page pages[] = { a_page(10, 1),
            a_page(11, data::NO_ID),
            a_page(12, data::NO_ID),
            a_page(13, 2) };

        dialogue_session session;
        wire(session, dialogues, 2, pages, 4);

        TEST_ASSERT(session.begin(1), "the second dialogue should start");
        TEST_ASSERT(session.current_page()->text_string_id == 13,
            "the entry page id is a global index, not one within the dialogue");

        session.advance();
        TEST_ASSERT(session.current_page()->text_string_id == 12,
            "the next-page id is a global index too");

        u16 const expected[] = { 3, 2 };
        TEST_ASSERT(pages_read_were(expected, 2), "pages are marked read by their global index");
        TEST_SUCCESS;
    }

    TEST_SUITE_BEGIN(dialogue_session)
        TEST(test_dialogue_starts_ended)
        TEST(test_dialogue_opens_on_its_first_page)
        TEST(test_dialogue_marks_its_entry_page_read)
        TEST(test_dialogue_id_naming_nothing_does_not_start)
        TEST(test_dialogue_with_no_first_page_does_not_start)
        TEST(test_dialogue_with_a_first_page_outside_its_range_does_not_start)
        TEST(test_dialogue_with_no_pages_does_not_start)
        TEST(test_dialogue_range_past_the_pages_is_truncated)
        TEST(test_dialogue_starting_over_forgets_the_previous_one)

        TEST(test_dialogue_advances_to_the_named_next_page)
        TEST(test_dialogue_marks_every_page_read_as_it_arrives)
        TEST(test_dialogue_ends_on_the_no_destination_sentinel)
        TEST(test_dialogue_ends_running_past_its_last_page)
        TEST(test_dialogue_ending_reads_no_further_page)
        TEST(test_dialogue_advancing_after_the_end_does_nothing)
        TEST(test_dialogue_follows_a_page_pointing_at_itself)
        TEST(test_dialogue_pages_are_addressed_globally)
    TEST_SUITE_END
}  // namespace NEONengine

#endif  // ACE_TEST_RUNNER

#endif  // __DIALOGUE_SESSION_TESTS_H__INCLUDED__
