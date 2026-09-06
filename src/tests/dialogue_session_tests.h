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
            mark_choice_taken,
            set_flag,
            clear_flag,
            run_script,
        };

        struct dlg_call
        {
            dlg_call_kind kind;
            u16 arg;
        };

        struct dlg_mock_log
        {
            static constexpr u8 MAX_CALLS = 32;
            static constexpr u16 MAX_PAGES = 64;
            static constexpr u16 MAX_CHOICES = 64;
            static constexpr u16 MAX_FLAGS = 16;

            dlg_call calls[MAX_CALLS];
            u8 count;

            /// What the engine would remember: one bit per page, choice and flag.
            bool page_read[MAX_PAGES];
            bool choice_taken[MAX_CHOICES];
            bool flags[MAX_FLAGS];

            /// How many times the check flag of a page was asked for.
            u8 flag_reads;

            /// The script whose run latches the VM terminated, as GOTO_SCENE does.
            u16 terminating_script;

            /// A flag this script sets as it runs, standing in for a script's effects.
            u16 script_sets_flag;
            u16 script_that_sets;

            void reset()
            {
                count              = 0;
                flag_reads         = 0;
                terminating_script = data::NO_ID;
                script_sets_flag   = data::NO_ID;
                script_that_sets   = data::NO_ID;
                for (u16 i = 0; i < MAX_PAGES; ++i) page_read[i] = false;
                for (u16 i = 0; i < MAX_CHOICES; ++i) choice_taken[i] = false;
                for (u16 i = 0; i < MAX_FLAGS; ++i) flags[i] = false;
            }

            void record(dlg_call_kind kind, u16 arg)
            {
                if (count >= MAX_CALLS) return;
                calls[count++] = dlg_call{ kind, arg };
            }
        };

        dlg_mock_log g_dlg_log;

        void dlg_mock_mark_page_read(dialogue_context*, u16 page_index)
        {
            g_dlg_log.record(dlg_call_kind::mark_page_read, page_index);
            if (page_index < dlg_mock_log::MAX_PAGES) g_dlg_log.page_read[page_index] = true;
        }

        bool dlg_mock_is_page_read(dialogue_context*, u16 page_index)
        {
            if (page_index >= dlg_mock_log::MAX_PAGES) return false;
            return g_dlg_log.page_read[page_index];
        }

        void dlg_mock_mark_choice_taken(dialogue_context*, u16 choice_index)
        {
            g_dlg_log.record(dlg_call_kind::mark_choice_taken, choice_index);
            if (choice_index < dlg_mock_log::MAX_CHOICES)
            {
                g_dlg_log.choice_taken[choice_index] = true;
            }
        }

        bool dlg_mock_is_choice_taken(dialogue_context*, u16 choice_index)
        {
            if (choice_index >= dlg_mock_log::MAX_CHOICES) return false;
            return g_dlg_log.choice_taken[choice_index];
        }

        bool dlg_mock_get_flag(dialogue_context*, u16 flag_id)
        {
            ++g_dlg_log.flag_reads;
            if (flag_id >= dlg_mock_log::MAX_FLAGS) return false;
            return g_dlg_log.flags[flag_id];
        }

        void dlg_mock_set_flag(dialogue_context*, u16 flag_id, bool value)
        {
            g_dlg_log.record(value ? dlg_call_kind::set_flag : dlg_call_kind::clear_flag, flag_id);
            if (flag_id < dlg_mock_log::MAX_FLAGS) g_dlg_log.flags[flag_id] = value;
        }

        void dlg_mock_run_script(dialogue_context*, u16 function_id)
        {
            g_dlg_log.record(dlg_call_kind::run_script, function_id);

            // What a script's effects look like from out here: a flag the
            // authored records never name, set by the script itself.
            if (function_id == g_dlg_log.script_that_sets
                && g_dlg_log.script_sets_flag < dlg_mock_log::MAX_FLAGS)
            {
                g_dlg_log.flags[g_dlg_log.script_sets_flag] = true;
            }
        }

        bool dlg_mock_is_terminated(dialogue_context*)
        {
            if (g_dlg_log.terminating_script == data::NO_ID) return false;

            // Latched, as the VM's is: once the terminal call has run, every
            // later ask says so too.
            for (u8 i = 0; i < g_dlg_log.count; ++i)
            {
                if (g_dlg_log.calls[i].kind != dlg_call_kind::run_script) continue;
                if (g_dlg_log.calls[i].arg == g_dlg_log.terminating_script) return true;
            }
            return false;
        }

        dialogue_bridge const g_dlg_mock_bridge = {
            .mark_page_read    = dlg_mock_mark_page_read,
            .is_page_read      = dlg_mock_is_page_read,
            .mark_choice_taken = dlg_mock_mark_choice_taken,
            .is_choice_taken   = dlg_mock_is_choice_taken,
            .get_flag          = dlg_mock_get_flag,
            .set_flag          = dlg_mock_set_flag,
            .run_script        = dlg_mock_run_script,
            .is_terminated     = dlg_mock_is_terminated,
        };

        /// True when the pages read, in the order they were read, are @p expected.
        bool pages_read_were(u16 const* expected, u8 expected_count)
        {
            u8 seen = 0;
            for (u8 i = 0; i < g_dlg_log.count; ++i)
            {
                if (g_dlg_log.calls[i].kind != dlg_call_kind::mark_page_read) continue;
                if (seen >= expected_count) return false;
                if (g_dlg_log.calls[i].arg != expected[seen]) return false;
                ++seen;
            }
            return seen == expected_count;
        }

        /// True when every call the conversation made, in order, is @p expected.
        bool calls_were(dlg_call const* expected, u8 expected_count)
        {
            if (g_dlg_log.count != expected_count) return false;
            for (u8 i = 0; i < expected_count; ++i)
            {
                if (g_dlg_log.calls[i].kind != expected[i].kind) return false;
                if (g_dlg_log.calls[i].arg != expected[i].arg) return false;
            }
            return true;
        }

        /// How many calls of @p kind the conversation made.
        u8 times_called(dlg_call_kind kind)
        {
            u8 total = 0;
            for (u8 i = 0; i < g_dlg_log.count; ++i)
            {
                if (g_dlg_log.calls[i].kind == kind) ++total;
            }
            return total;
        }

        /// Where @p kind with @p arg first appears in the log, or MAX_CALLS.
        u8 index_of(dlg_call_kind kind, u16 arg)
        {
            for (u8 i = 0; i < g_dlg_log.count; ++i)
            {
                if (g_dlg_log.calls[i].kind == kind && g_dlg_log.calls[i].arg == arg) return i;
            }
            return dlg_mock_log::MAX_CALLS;
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
            page.flags               = data::PAGE_FLAG_ENABLED;
            page.choice_range        = data::range{ 0, 0 };
            return page;
        }

        /// @p page with the authored enable bit its author never set.
        data::page disabled(data::page page)
        {
            page.flags = to<u16>(page.flags & ~data::PAGE_FLAG_ENABLED);
            return page;
        }

        /// @p page burnt by its one-time bit once it has been read.
        data::page one_time(data::page page)
        {
            page.flags = to<u16>(page.flags | data::PAGE_FLAG_ONE_TIME);
            return page;
        }

        /// @p page shown only while flag @p flag_id is true.
        data::page gated_on(data::page page, u16 flag_id)
        {
            page.check_flag_id = flag_id;
            return page;
        }

        /// @p page that sets @p set_id and clears @p clear_id as it is displayed.
        data::page changing_flags(data::page page, u16 set_id, u16 clear_id)
        {
            page.set_flag_id   = set_id;
            page.clear_flag_id = clear_id;
            return page;
        }

        /// @p page that runs script @p function_id as it is displayed.
        data::page running(data::page page, u16 function_id)
        {
            page.display_function_id = function_id;
            return page;
        }

        /// @p page offering the choices [@p start, @p start + @p count).
        data::page with_choices(data::page page, u16 start, u16 count)
        {
            page.choice_range = data::range{ start, count };
            return page;
        }

        /// A choice reading string @p text_string_id that leads to @p goto_page.
        data::choice a_choice(u16 text_string_id, u16 goto_page)
        {
            data::choice choice{};
            choice.text_string_id    = text_string_id;
            choice.set_flag_id       = data::NO_ID;
            choice.clear_flag_id     = data::NO_ID;
            choice.check_flag_id     = data::NO_ID;
            choice.goto_page_id      = goto_page;
            choice.click_function_id = data::NO_ID;
            choice.flags             = data::CHOICE_FLAG_ENABLED;
            return choice;
        }

        /// @p choice with the authored enable bit its author never set.
        data::choice disabled(data::choice choice)
        {
            choice.flags = to<u16>(choice.flags & ~data::CHOICE_FLAG_ENABLED);
            return choice;
        }

        /// @p choice burnt by its one-time bit once it has been taken.
        data::choice one_time(data::choice choice)
        {
            choice.flags = to<u16>(choice.flags | data::CHOICE_FLAG_ONE_TIME);
            return choice;
        }

        /// @p choice offered only while flag @p flag_id is true.
        data::choice gated_on(data::choice choice, u16 flag_id)
        {
            choice.check_flag_id = flag_id;
            return choice;
        }

        /// @p choice that sets @p set_id and clears @p clear_id when it is taken.
        data::choice changing_flags(data::choice choice, u16 set_id, u16 clear_id)
        {
            choice.set_flag_id   = set_id;
            choice.clear_flag_id = clear_id;
            return choice;
        }

        /// @p choice that runs script @p function_id when it is taken.
        data::choice running(data::choice choice, u16 function_id)
        {
            choice.click_function_id = function_id;
            return choice;
        }

        /// A dialogue over pages [@p start, @p start + @p count), entered at @p first.
        data::dialogue a_dialogue(u16 start, u16 count, u16 first)
        {
            data::dialogue dialogue{};
            dialogue.page_range    = data::range{ start, count };
            dialogue.first_page_id = first;
            return dialogue;
        }

        /// A session over @p dialogues, @p pages and @p choices, on the mock bridge.
        void wire(dialogue_session& session,
            data::dialogue const* dialogues,
            u32 dialogue_count,
            data::page const* pages,
            u32 page_count,
            data::choice const* choices = nullptr,
            u32 choice_count            = 0)
        {
            g_dlg_log.reset();
            session.create(mtl::span<data::dialogue>(dialogues, dialogue_count),
                mtl::span<data::page>(pages, page_count),
                mtl::span<data::choice>(choices, choice_count),
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

    /// What the caller loads portraits for: the pages the conversation can show.
    TEST_IMPL(test_dialogue_reports_the_pages_it_can_show)
    {
        data::dialogue dialogues[] = { a_dialogue(2, 3, 2) };
        data::page pages[] = { a_page(10, data::NO_ID),
            a_page(11, data::NO_ID),
            a_page(12, data::NO_ID),
            a_page(13, data::NO_ID),
            a_page(14, data::NO_ID) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 5);
        session.begin(0);

        TEST_ASSERT(session.page_range().start == 2, "the range starts where the dialogue's does");
        TEST_ASSERT(session.page_range().count == 3, "and holds every page the dialogue owns");
        TEST_SUCCESS;
    }

    /// The truncation begin() applies is what the caller has to see, or it would
    /// read records past the end of the chunk looking for portraits.
    TEST_IMPL(test_dialogue_reports_its_range_truncated_to_the_loaded_pages)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 9, 0) };
        data::page pages[] = { a_page(10, 1), a_page(11, data::NO_ID) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 2);
        session.begin(0);

        TEST_ASSERT(session.page_range().count == 2, "the range stops at the last loaded page");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_that_never_started_reports_no_pages)
    {
        dialogue_session session;

        TEST_ASSERT(session.page_range().count == 0, "a session with no dialogue owns no pages");
        TEST_SUCCESS;
    }

    /// A conversation that could not start owns nothing, or its caller would go
    /// on to load the portraits of a dialogue that is never put on screen.
    TEST_IMPL(test_dialogue_that_could_not_start_reports_no_pages)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 2, 5) };
        data::page pages[] = { a_page(10, data::NO_ID), a_page(11, data::NO_ID) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 2);

        TEST_ASSERT(!session.begin(0), "a first page outside the dialogue's pages cannot start");
        TEST_ASSERT(session.page_range().count == 0, "and the pages it named are not on offer");
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

    // ── Availability and the sequential skip ────────────────────────────────────

    TEST_IMPL(test_dialogue_never_shows_a_page_its_author_disabled)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 3, 0) };
        data::page pages[] = { a_page(10, 1), disabled(a_page(11, 2)), a_page(12, data::NO_ID) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 3);
        session.begin(0);

        session.advance();

        u16 const expected[] = { 0, 2 };
        TEST_ASSERT(session.current_page_index() == 2, "a disabled page is passed over");
        TEST_ASSERT(pages_read_were(expected, 2), "and is never marked read");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_skips_a_page_whose_check_flag_is_false)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 3, 0) };
        data::page pages[]
            = { a_page(10, 1), gated_on(a_page(11, 2), 3), a_page(12, data::NO_ID) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 3);
        session.begin(0);

        session.advance();

        TEST_ASSERT(session.current_page_index() == 2, "a page gated on a false flag is skipped");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_shows_a_gated_page_once_its_flag_is_true)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 3, 0) };
        data::page pages[]
            = { a_page(10, 1), gated_on(a_page(11, 2), 3), a_page(12, data::NO_ID) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 3);
        g_dlg_log.flags[3] = true;
        session.begin(0);

        session.advance();

        TEST_ASSERT(session.current_page_index() == 1, "the same page is shown once its flag is true");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_page_with_no_check_flag_needs_no_flag)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 2, 0) };
        data::page pages[] = { a_page(10, 1), a_page(11, data::NO_ID) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 2);
        session.begin(0);
        session.advance();

        TEST_ASSERT(session.current_page_index() == 1,
            "a page with no check flag is available with every flag false");
        TEST_ASSERT(g_dlg_log.flag_reads == 0, "and the engine is never asked about a flag");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_one_time_page_is_shown_once_then_skipped)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 2, 0) };
        data::page pages[] = { one_time(a_page(10, 0)), a_page(11, data::NO_ID) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 2);
        session.begin(0);

        TEST_ASSERT(session.current_page_index() == 0, "a one-time page is shown the first time");

        session.advance();

        u16 const expected[] = { 0, 1 };
        TEST_ASSERT(session.current_page_index() == 1,
            "coming back to it a second time skips past it");
        TEST_ASSERT(pages_read_were(expected, 2), "and it is read exactly once");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_skips_a_one_time_page_burnt_before_it_started)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 2, 0) };
        data::page pages[] = { one_time(a_page(10, data::NO_ID)), a_page(11, data::NO_ID) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 2);
        g_dlg_log.page_read[0] = true;

        TEST_ASSERT(session.begin(0), "a burnt opening page does not stop the dialogue starting");
        TEST_ASSERT(session.current_page_index() == 1, "it opens on the page after it instead");
        TEST_SUCCESS;
    }

    /// The replacement is the next page by index, never the skipped page's own
    /// next-page field - ADR-0002.
    TEST_IMPL(test_dialogue_replaces_an_unavailable_page_by_index)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 5, 0) };
        data::page pages[] = { a_page(10, 1),
            disabled(a_page(11, 4)),
            a_page(12, data::NO_ID),
            a_page(13, data::NO_ID),
            a_page(14, data::NO_ID) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 5);
        session.begin(0);

        session.advance();

        TEST_ASSERT(session.current_page_index() == 2,
            "the page after the skipped one is shown, not the one it named as next");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_skips_a_run_of_unavailable_pages)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 4, 0) };
        data::page pages[] = { a_page(10, 1),
            disabled(a_page(11, data::NO_ID)),
            gated_on(a_page(12, data::NO_ID), 1),
            a_page(13, data::NO_ID) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 4);
        session.begin(0);

        session.advance();

        u16 const expected[] = { 0, 3 };
        TEST_ASSERT(session.current_page_index() == 3, "the scan carries on until a page is available");
        TEST_ASSERT(pages_read_were(expected, 2), "no page it passed over is marked read");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_ends_when_nothing_after_an_unavailable_page_is_available)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 3, 0) };
        data::page pages[] = { a_page(10, 1),
            disabled(a_page(11, 0)),
            disabled(a_page(12, 0)) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 3);
        session.begin(0);

        session.advance();

        TEST_ASSERT(session.has_ended(), "running off the end of the scan ends the conversation");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_with_every_page_unavailable_does_not_start)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 2, 0) };
        data::page pages[]
            = { disabled(a_page(10, 1)), gated_on(a_page(11, data::NO_ID), 2) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 2);

        TEST_ASSERT(!session.begin(0), "a dialogue with nothing available ends rather than hangs");
        TEST_ASSERT(session.has_ended(), "and shows no page");
        TEST_ASSERT(g_dlg_log.count == 0, "and reads none of them");
        TEST_SUCCESS;
    }

    /// The greeting-variant pattern: a one-time opening, its replacement next by
    /// index, both leading to the same hub page.
    TEST_IMPL(test_dialogue_greeting_variant_needs_no_extra_wiring)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 3, 0) };
        data::page pages[]
            = { one_time(a_page(10, 2)), a_page(11, 2), a_page(12, data::NO_ID) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 3);

        session.begin(0);
        TEST_ASSERT(session.current_page_index() == 0, "the first visit gets the opening greeting");
        session.advance();
        TEST_ASSERT(session.current_page_index() == 2, "which leads to the hub page");
        session.advance();
        TEST_ASSERT(session.has_ended(), "and the first conversation ends");

        session.begin(0);
        TEST_ASSERT(session.current_page_index() == 1, "the second visit gets the replacement");
        session.advance();
        TEST_ASSERT(session.current_page_index() == 2, "which leads to the same hub page");

        u16 const expected[] = { 0, 2, 1, 2 };
        TEST_ASSERT(pages_read_were(expected, 4), "and the burnt greeting is never read again");
        TEST_SUCCESS;
    }

    /// All three conditions on one page: only the combination lets it through.
    TEST_IMPL(test_dialogue_availability_conditions_combine)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 2, 0) };
        data::page pages[]
            = { one_time(gated_on(a_page(10, data::NO_ID), 4)), a_page(11, data::NO_ID) };

        dialogue_session session;

        wire(session, dialogues, 1, pages, 2);
        session.begin(0);
        TEST_ASSERT(session.current_page_index() == 1, "a false check flag alone keeps it off");

        wire(session, dialogues, 1, pages, 2);
        g_dlg_log.flags[4]     = true;
        g_dlg_log.page_read[0] = true;
        session.begin(0);
        TEST_ASSERT(session.current_page_index() == 1, "being already burnt alone keeps it off");

        wire(session, dialogues, 1, pages, 2);
        g_dlg_log.flags[4] = true;
        session.begin(0);
        TEST_ASSERT(session.current_page_index() == 0,
            "enabled, flagged true and unburnt together let it through");
        TEST_SUCCESS;
    }

    // ── Choices ─────────────────────────────────────────────────────────────────

    TEST_IMPL(test_dialogue_page_authored_with_no_choices_offers_none)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 1, 0) };
        data::page pages[] = { a_page(10, data::NO_ID) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 1);
        session.begin(0);

        TEST_ASSERT(session.choice_count() == 0, "a page written with no choices offers none");
        TEST_ASSERT(session.choice_at(0) == nullptr, "and holds nothing in its first slot");
        TEST_ASSERT(session.choice_index(0) == data::NO_ID, "which names no choice either");
        TEST_SUCCESS;
    }

    /// Choices are addressed globally, as pages are: the page names a range into
    /// the whole CHOC chunk, not into a list of its own.
    TEST_IMPL(test_dialogue_offers_the_choices_its_page_names)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 2, 0) };
        data::page pages[] = { with_choices(a_page(10, data::NO_ID), 1, 2),
            a_page(11, data::NO_ID) };
        data::choice choices[] = { a_choice(20, 1), a_choice(21, 1), a_choice(22, 1) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 2, choices, 3);
        session.begin(0);

        TEST_ASSERT(session.choice_count() == 2, "both choices in the page's range are offered");
        TEST_ASSERT(session.choice_index(0) == 1, "the first slot holds the range's first choice");
        TEST_ASSERT(session.choice_index(1) == 2, "and the second the one after it");
        TEST_ASSERT(session.choice_at(0)->text_string_id == 21,
            "the record in a slot is the one that index names");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_never_offers_a_choice_its_author_disabled)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 2, 0) };
        data::page pages[] = { with_choices(a_page(10, data::NO_ID), 0, 2),
            a_page(11, data::NO_ID) };
        data::choice choices[] = { disabled(a_choice(20, 1)), a_choice(21, 1) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 2, choices, 2);
        session.begin(0);

        TEST_ASSERT(session.choice_count() == 1, "the disabled choice is not offered");
        TEST_ASSERT(session.choice_index(0) == 1, "and the one after it takes the first slot");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_skips_a_choice_whose_check_flag_is_false)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 2, 0) };
        data::page pages[] = { with_choices(a_page(10, data::NO_ID), 0, 2),
            a_page(11, data::NO_ID) };
        data::choice choices[] = { gated_on(a_choice(20, 1), 3), a_choice(21, 1) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 2, choices, 2);
        session.begin(0);

        TEST_ASSERT(session.choice_count() == 1, "a choice gated on a false flag is not offered");
        TEST_ASSERT(session.choice_index(0) == 1, "only the ungated one is");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_offers_a_gated_choice_once_its_flag_is_true)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 2, 0) };
        data::page pages[] = { with_choices(a_page(10, data::NO_ID), 0, 2),
            a_page(11, data::NO_ID) };
        data::choice choices[] = { gated_on(a_choice(20, 1), 3), a_choice(21, 1) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 2, choices, 2);
        g_dlg_log.flags[3] = true;
        session.begin(0);

        TEST_ASSERT(session.choice_count() == 2, "the same choice is offered once its flag is true");
        TEST_ASSERT(session.choice_index(0) == 0, "in the place it was authored in");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_choice_with_no_check_flag_needs_no_flag)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 2, 0) };
        data::page pages[] = { with_choices(a_page(10, data::NO_ID), 0, 2),
            a_page(11, data::NO_ID) };
        data::choice choices[] = { a_choice(20, 1), a_choice(21, 1) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 2, choices, 2);
        session.begin(0);

        TEST_ASSERT(session.choice_count() == 2, "a choice with no check flag is offered");
        TEST_ASSERT(g_dlg_log.flag_reads == 0, "without the engine being asked about any flag");
        TEST_SUCCESS;
    }

    /// Burning on display would retire the choice the first time the player laid
    /// eyes on it, so it could never be picked.
    TEST_IMPL(test_dialogue_showing_a_one_time_choice_does_not_burn_it)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 2, 0) };
        data::page pages[] = { with_choices(a_page(10, 1), 0, 1), a_page(11, 0) };
        data::choice choices[] = { one_time(a_choice(20, 1)) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 2, choices, 1);
        session.begin(0);
        TEST_ASSERT(session.choice_count() == 1, "the one-time choice is offered on arrival");

        session.advance();
        session.advance();

        TEST_ASSERT(session.current_page_index() == 0, "the conversation comes back to the page");
        TEST_ASSERT(session.choice_count() == 1, "and the choice merely displayed is offered again");
        u16 const expected[] = { 0, 1, 0 };
        TEST_ASSERT(pages_read_were(expected, 3), "nothing was ever taken");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_burns_a_one_time_choice_when_it_is_taken)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 2, 0) };
        data::page pages[] = { with_choices(a_page(10, 1), 0, 1), a_page(11, 0) };
        data::choice choices[] = { one_time(a_choice(20, 1)) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 2, choices, 1);
        session.begin(0);

        session.take_choice(0);
        TEST_ASSERT(session.current_page_index() == 1, "taking it leads to the page it names");

        session.advance();
        TEST_ASSERT(session.current_page_index() == 0, "the conversation comes back to the page");
        TEST_ASSERT(session.choice_count() == 0, "and the choice taken is gone on the next visit");
        TEST_SUCCESS;
    }

    /// The bit is burnt before the destination is followed, so a choice pointing
    /// at its own page finds itself already gone when that page is laid out.
    TEST_IMPL(test_dialogue_marks_a_choice_taken_before_it_moves_on)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 2, 0) };
        data::page pages[] = { with_choices(a_page(10, data::NO_ID), 0, 1),
            a_page(11, data::NO_ID) };
        data::choice choices[] = { a_choice(20, 1) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 2, choices, 1);
        session.begin(0);

        session.take_choice(0);

        dlg_call const expected[] = { { dlg_call_kind::mark_page_read, 0 },
            { dlg_call_kind::mark_choice_taken, 0 },
            { dlg_call_kind::mark_page_read, 1 } };
        TEST_ASSERT(calls_were(expected, 3),
            "the choice is taken after its page was read and before the next one is");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_taking_a_choice_goes_to_the_page_it_names)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 3, 0) };
        data::page pages[] = { with_choices(a_page(10, data::NO_ID), 0, 2),
            a_page(11, data::NO_ID),
            a_page(12, data::NO_ID) };
        data::choice choices[] = { a_choice(20, 2), a_choice(21, 1) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 3, choices, 2);
        session.begin(0);

        session.take_choice(1);

        TEST_ASSERT(session.current_page_index() == 1,
            "the destination is the one the chosen slot names, not the first");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_choice_with_no_destination_ends_the_conversation)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 2, 0) };
        data::page pages[] = { with_choices(a_page(10, 1), 0, 1), a_page(11, data::NO_ID) };
        data::choice choices[] = { a_choice(20, data::NO_ID) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 2, choices, 1);
        session.begin(0);

        session.take_choice(0);

        TEST_ASSERT(session.has_ended(), "a goodbye ends the conversation");
        TEST_ASSERT(session.choice_count() == 0, "and offers nothing further");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_choice_pointing_outside_the_dialogue_ends_it)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 2, 0) };
        data::page pages[] = { with_choices(a_page(10, 1), 0, 1),
            a_page(11, data::NO_ID),
            a_page(12, data::NO_ID) };
        data::choice choices[] = { a_choice(20, 2) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 3, choices, 1);
        session.begin(0);

        session.take_choice(0);

        TEST_ASSERT(session.has_ended(),
            "a destination outside the dialogue's pages ends the conversation");
        TEST_SUCCESS;
    }

    /// How "ask about each topic, then leave" is written: one-time choices that
    /// point back at their own page, burning away until none is left.
    TEST_IMPL(test_dialogue_choice_pointing_at_its_own_page_returns_with_itself_gone)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 2, 0) };
        data::page pages[] = { with_choices(a_page(10, 1), 0, 2), a_page(11, data::NO_ID) };
        data::choice choices[] = { one_time(a_choice(20, 0)), one_time(a_choice(21, 0)) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 2, choices, 2);
        session.begin(0);

        session.take_choice(0);
        TEST_ASSERT(session.current_page_index() == 0, "the page it points at is its own");
        TEST_ASSERT(session.choice_count() == 1, "and the topic just asked about is gone");
        TEST_ASSERT(session.choice_index(0) == 1, "leaving the one still unasked");

        session.take_choice(0);
        TEST_ASSERT(session.current_page_index() == 0, "the last topic returns to the page too");
        TEST_ASSERT(session.choice_count() == 0, "with nothing left to ask");
        TEST_SUCCESS;
    }

    /// A choice's destination obeys the same skip a next-page field does.
    TEST_IMPL(test_dialogue_replaces_an_unavailable_choice_destination_by_index)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 3, 0) };
        data::page pages[] = { with_choices(a_page(10, data::NO_ID), 0, 1),
            disabled(a_page(11, data::NO_ID)),
            a_page(12, data::NO_ID) };
        data::choice choices[] = { a_choice(20, 1) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 3, choices, 1);
        session.begin(0);

        session.take_choice(0);

        TEST_ASSERT(session.current_page_index() == 2,
            "an unavailable destination is replaced by the next available page");
        TEST_SUCCESS;
    }

    /// What lets a character show the player out: a page whose every choice is
    /// used up is a page with no choices, and advances on its own.
    TEST_IMPL(test_dialogue_page_whose_choices_are_all_unavailable_falls_through)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 2, 0) };
        data::page pages[] = { with_choices(a_page(10, 1), 0, 2), a_page(11, data::NO_ID) };
        data::choice choices[] = { disabled(a_choice(20, 0)), gated_on(a_choice(21, 0), 3) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 2, choices, 2);
        session.begin(0);

        TEST_ASSERT(session.choice_count() == 0, "a page with nothing available offers nothing");

        session.advance();

        TEST_ASSERT(session.current_page_index() == 1,
            "and behaves exactly like a page written with no choices at all");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_taking_a_slot_past_the_end_does_nothing)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 2, 0) };
        data::page pages[] = { with_choices(a_page(10, data::NO_ID), 0, 1),
            a_page(11, data::NO_ID) };
        data::choice choices[] = { a_choice(20, 1) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 2, choices, 1);
        session.begin(0);

        session.take_choice(1);
        session.take_choice(200);

        u16 const expected[] = { 0 };
        TEST_ASSERT(session.current_page_index() == 0, "the conversation has not moved");
        TEST_ASSERT(pages_read_were(expected, 1), "and nothing was taken");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_that_has_ended_offers_no_choices)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 1, 0) };
        data::page pages[] = { with_choices(a_page(10, data::NO_ID), 0, 1) };
        data::choice choices[] = { a_choice(20, data::NO_ID) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 1, choices, 1);
        session.begin(0);
        session.advance();

        TEST_ASSERT(session.has_ended(), "the conversation is over");
        TEST_ASSERT(session.choice_count() == 0, "so it offers nothing");

        session.take_choice(0);
        TEST_ASSERT(session.has_ended(), "and taking a choice after the end does nothing");
        TEST_SUCCESS;
    }

    /// A range running past the loaded choices must not be read off the end.
    TEST_IMPL(test_dialogue_choice_range_past_the_choices_is_truncated)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 2, 0) };
        data::page pages[] = { with_choices(a_page(10, data::NO_ID), 1, 9),
            a_page(11, data::NO_ID) };
        data::choice choices[] = { a_choice(20, 1), a_choice(21, 1) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 2, choices, 2);
        session.begin(0);

        TEST_ASSERT(session.choice_count() == 1, "only the choices that are there are offered");
        TEST_ASSERT(session.choice_index(0) == 1, "and they are the ones the range reaches");
        TEST_SUCCESS;
    }

    /// All three conditions on one choice: only the combination lets it through.
    TEST_IMPL(test_dialogue_choice_availability_conditions_combine)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 2, 0) };
        data::page pages[] = { with_choices(a_page(10, data::NO_ID), 0, 1),
            a_page(11, data::NO_ID) };
        data::choice choices[] = { one_time(gated_on(a_choice(20, 1), 4)) };

        dialogue_session session;

        wire(session, dialogues, 1, pages, 2, choices, 1);
        session.begin(0);
        TEST_ASSERT(session.choice_count() == 0, "a false check flag alone keeps it off");

        wire(session, dialogues, 1, pages, 2, choices, 1);
        g_dlg_log.flags[4]        = true;
        g_dlg_log.choice_taken[0] = true;
        session.begin(0);
        TEST_ASSERT(session.choice_count() == 0, "being already taken alone keeps it off");

        wire(session, dialogues, 1, pages, 2, choices, 1);
        g_dlg_log.flags[4] = true;
        session.begin(0);
        TEST_ASSERT(session.choice_count() == 1,
            "enabled, flagged true and untaken together let it through");
        TEST_SUCCESS;
    }

    // ── Choice sets ─────────────────────────────────────────────────────────────

    namespace
    {
        /// A page offering @p count choices, each leading back to the page itself.
        void a_page_of_choices(data::page* pages, data::choice* choices, u16 count)
        {
            pages[0] = with_choices(a_page(10, data::NO_ID), 0, count);
            pages[1] = a_page(11, data::NO_ID);

            for (u16 slot = 0; slot < count; ++slot)
            {
                choices[slot] = a_choice(to<u16>(20 + slot), 0);
            }
        }

        /// True when the current set draws exactly @p expected, in that order.
        bool set_was(dialogue_session const& session, u8 const* expected, u8 expected_count)
        {
            if (session.slot_count() != expected_count) return false;
            for (u8 slot = 0; slot < expected_count; ++slot)
            {
                if (session.slot_choice(slot) != expected[slot]) return false;
            }
            return true;
        }
    }  // namespace

    /// Nothing is fitted until the drawing side has measured, so a caller that
    /// never fits - every test above this one - sees the whole list.
    TEST_IMPL(test_dialogue_shows_every_choice_until_they_are_fitted)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 2, 0) };
        data::page pages[2];
        data::choice choices[3];
        a_page_of_choices(pages, choices, 3);

        dialogue_session session;
        wire(session, dialogues, 1, pages, 2, choices, 3);
        session.begin(0);

        u8 const expected[] = { 0, 1, 2 };
        TEST_ASSERT(set_was(session, expected, 3), "every available choice is in the set");
        TEST_ASSERT(!session.has_more(), "and there is nothing to rotate to");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_choices_that_all_fit_offer_no_more)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 2, 0) };
        data::page pages[2];
        data::choice choices[3];
        a_page_of_choices(pages, choices, 3);

        dialogue_session session;
        wire(session, dialogues, 1, pages, 2, choices, 3);
        session.begin(0);

        u16 heights[] = { 10, 10, 10 };
        session.fit_choices(mtl::span<u16>(heights, 3), 100, 2, 10);

        u8 const expected[] = { 0, 1, 2 };
        TEST_ASSERT(set_was(session, expected, 3), "all three are shown at once");
        TEST_ASSERT(!session.has_more(), "so no slot is given to More...");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_slot_past_the_set_names_nothing)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 2, 0) };
        data::page pages[2];
        data::choice choices[2];
        a_page_of_choices(pages, choices, 2);

        dialogue_session session;
        wire(session, dialogues, 1, pages, 2, choices, 2);
        session.begin(0);

        u16 heights[] = { 10, 10 };
        session.fit_choices(mtl::span<u16>(heights, 2), 100, 2, 10);

        TEST_ASSERT(session.slot_choice(2) == dialogue_session::NO_CHOICE,
            "a slot past the set names no choice");
        TEST_SUCCESS;
    }

    /// One choice too many for the space is the whole of what starts a rotation:
    /// the room "More..." needs comes off the top, so the first set is shorter
    /// than what would have fitted without it.
    TEST_IMPL(test_dialogue_one_choice_too_many_starts_a_rotation)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 2, 0) };
        data::page pages[2];
        data::choice choices[3];
        a_page_of_choices(pages, choices, 3);

        dialogue_session session;
        wire(session, dialogues, 1, pages, 2, choices, 3);
        session.begin(0);

        // Two of the three fit; the second pass gives one of those two to
        // "More...", so only the first choice is left on screen.
        u16 heights[] = { 10, 10, 10 };
        session.fit_choices(mtl::span<u16>(heights, 3), 22, 2, 10);

        u8 const first[] = { 0 };
        TEST_ASSERT(set_was(session, first, 1), "the first set holds what is left after More...");
        TEST_ASSERT(session.has_more(), "and offers More... to reach the rest");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_rotating_shows_the_next_set_then_wraps)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 2, 0) };
        data::page pages[2];
        data::choice choices[3];
        a_page_of_choices(pages, choices, 3);

        dialogue_session session;
        wire(session, dialogues, 1, pages, 2, choices, 3);
        session.begin(0);

        u16 heights[] = { 10, 10, 10 };
        session.fit_choices(mtl::span<u16>(heights, 3), 22, 2, 10);

        u8 const second[] = { 1 };
        session.rotate_choices();
        TEST_ASSERT(set_was(session, second, 1), "More... moves on to the next choice");
        TEST_ASSERT(session.has_more(), "which offers More... in its turn");

        u8 const third[] = { 2 };
        session.rotate_choices();
        TEST_ASSERT(set_was(session, third, 1), "and on again to the last");

        u8 const first[] = { 0 };
        session.rotate_choices();
        TEST_ASSERT(set_was(session, first, 1),
            "the last set comes back round to the first, so nothing is out of reach");
        TEST_SUCCESS;
    }

    /// The numbering runs 1 to 9, so a tenth choice rotates however much room
    /// there is for it.
    TEST_IMPL(test_dialogue_more_than_nine_choices_rotate)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 2, 0) };
        data::page pages[2];
        data::choice choices[10];
        a_page_of_choices(pages, choices, 10);

        dialogue_session session;
        wire(session, dialogues, 1, pages, 2, choices, 10);
        session.begin(0);

        u16 heights[] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
        session.fit_choices(mtl::span<u16>(heights, 10), 1000, 0, 1);

        u8 const first[] = { 0, 1, 2, 3, 4, 5, 6, 7 };
        TEST_ASSERT(set_was(session, first, 8),
            "eight are shown, the ninth slot going to More...");
        TEST_ASSERT(session.has_more(), "which the tenth choice is reached through");

        u8 const second[] = { 8, 9 };
        session.rotate_choices();
        TEST_ASSERT(set_was(session, second, 2), "the rest follow in the next set");

        session.rotate_choices();
        TEST_ASSERT(set_was(session, first, 8), "and it comes back round to the first eight");
        TEST_SUCCESS;
    }

    /// A wrapped choice takes several lines, and the fit has to believe what it
    /// was told about it rather than assume every choice is one line tall.
    TEST_IMPL(test_dialogue_fits_a_multi_line_choice_at_its_true_height)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 2, 0) };
        data::page pages[2];
        data::choice choices[3];
        a_page_of_choices(pages, choices, 3);

        dialogue_session session;
        wire(session, dialogues, 1, pages, 2, choices, 3);
        session.begin(0);

        // Three one-line choices would all have fitted in 42; the middle one
        // running to three lines is what pushes the last out of the set.
        u16 heights[] = { 10, 30, 10 };
        session.fit_choices(mtl::span<u16>(heights, 3), 42, 2, 10);

        u8 const first[] = { 0 };
        TEST_ASSERT(set_was(session, first, 1), "the tall choice does not fit beside the first");
        TEST_ASSERT(session.has_more(), "so the page rotates");

        u8 const second[] = { 1 };
        session.rotate_choices();
        TEST_ASSERT(set_was(session, second, 1), "and the tall one has the next set to itself");
        TEST_SUCCESS;
    }

    /// The page reserves room for one choice and a "More..." before its text is
    /// placed. Should that ever fail, showing nothing would rotate through empty
    /// sets forever, so one choice goes up regardless.
    TEST_IMPL(test_dialogue_shows_one_choice_even_with_no_room_for_it)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 2, 0) };
        data::page pages[2];
        data::choice choices[2];
        a_page_of_choices(pages, choices, 2);

        dialogue_session session;
        wire(session, dialogues, 1, pages, 2, choices, 2);
        session.begin(0);

        u16 heights[] = { 10, 10 };
        session.fit_choices(mtl::span<u16>(heights, 2), 0, 2, 10);

        u8 const first[] = { 0 };
        TEST_ASSERT(set_was(session, first, 1), "the first choice is shown anyway");
        TEST_ASSERT(session.has_more(), "with More... to reach the other");

        u8 const second[] = { 1 };
        session.rotate_choices();
        TEST_ASSERT(set_was(session, second, 1), "which the rotation does reach");
        TEST_SUCCESS;
    }

    /// A choice the drawing side could not measure - its text is missing from
    /// the string table - is passed as zero and belongs to no set at all.
    TEST_IMPL(test_dialogue_leaves_out_a_choice_that_could_not_be_measured)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 2, 0) };
        data::page pages[2];
        data::choice choices[3];
        a_page_of_choices(pages, choices, 3);

        dialogue_session session;
        wire(session, dialogues, 1, pages, 2, choices, 3);
        session.begin(0);

        u16 heights[] = { 10, 0, 10 };
        session.fit_choices(mtl::span<u16>(heights, 3), 100, 2, 10);

        u8 const expected[] = { 0, 2 };
        TEST_ASSERT(set_was(session, expected, 2), "the unmeasurable choice is not drawn");
        TEST_ASSERT(!session.has_more(),
            "and does not make a page that otherwise fits rotate");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_rotating_a_page_that_fits_does_nothing)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 2, 0) };
        data::page pages[2];
        data::choice choices[2];
        a_page_of_choices(pages, choices, 2);

        dialogue_session session;
        wire(session, dialogues, 1, pages, 2, choices, 2);
        session.begin(0);

        u16 heights[] = { 10, 10 };
        session.fit_choices(mtl::span<u16>(heights, 2), 100, 2, 10);
        session.rotate_choices();

        u8 const expected[] = { 0, 1 };
        TEST_ASSERT(set_was(session, expected, 2), "the set is the one it always was");
        TEST_SUCCESS;
    }

    /// Fitting the same page again - a redraw after a rotation - must not send
    /// the player back to the first set.
    TEST_IMPL(test_dialogue_fitting_again_keeps_the_set_the_player_is_on)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 2, 0) };
        data::page pages[2];
        data::choice choices[3];
        a_page_of_choices(pages, choices, 3);

        dialogue_session session;
        wire(session, dialogues, 1, pages, 2, choices, 3);
        session.begin(0);

        u16 heights[] = { 10, 10, 10 };
        session.fit_choices(mtl::span<u16>(heights, 3), 22, 2, 10);
        session.rotate_choices();
        session.fit_choices(mtl::span<u16>(heights, 3), 22, 2, 10);

        u8 const second[] = { 1 };
        TEST_ASSERT(set_was(session, second, 1), "the set is still the rotated one");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_arriving_at_a_page_starts_on_its_first_set)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 2, 0) };
        data::page pages[2];
        data::choice choices[3];
        a_page_of_choices(pages, choices, 3);
        pages[0].next_page_id = 1;
        pages[1].next_page_id = 0;

        dialogue_session session;
        wire(session, dialogues, 1, pages, 2, choices, 3);
        session.begin(0);

        u16 heights[] = { 10, 10, 10 };
        session.fit_choices(mtl::span<u16>(heights, 3), 22, 2, 10);
        session.rotate_choices();

        session.advance();
        session.advance();
        TEST_ASSERT(session.current_page_index() == 0, "the conversation is back on the page");

        u8 const everything[] = { 0, 1, 2 };
        TEST_ASSERT(set_was(session, everything, 3),
            "which offers the whole list again until it is fitted");

        session.fit_choices(mtl::span<u16>(heights, 3), 22, 2, 10);
        u8 const first[] = { 0 };
        TEST_ASSERT(set_was(session, first, 1), "and fits from the first set, not where it left");
        TEST_SUCCESS;
    }

    /// The set says which of the page's choices it draws, so the choice the
    /// player picked in a rotated set is the one that gets taken.
    TEST_IMPL(test_dialogue_a_rotated_set_names_the_choices_it_draws)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 3, 0) };
        data::page pages[] = { with_choices(a_page(10, data::NO_ID), 0, 3),
            a_page(11, data::NO_ID),
            a_page(12, data::NO_ID) };
        data::choice choices[] = { a_choice(20, 0), a_choice(21, 2), a_choice(22, 0) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 3, choices, 3);
        session.begin(0);

        u16 heights[] = { 10, 10, 10 };
        session.fit_choices(mtl::span<u16>(heights, 3), 22, 2, 10);
        session.rotate_choices();

        session.take_choice(session.slot_choice(0));

        TEST_ASSERT(session.current_page_index() == 2,
            "the choice taken is the one the rotated set drew");
        TEST_SUCCESS;
    }

    // ── Flags and scripts ───────────────────────────────────────────────────────

    TEST_IMPL(test_dialogue_page_applies_its_flags_when_it_is_displayed)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 1, 0) };
        data::page pages[] = { changing_flags(a_page(10, data::NO_ID), 3, 4) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 1);
        g_dlg_log.flags[4] = true;
        session.begin(0);

        TEST_ASSERT(g_dlg_log.flags[3], "the page's set flag is set");
        TEST_ASSERT(!g_dlg_log.flags[4], "and its clear flag is cleared");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_page_with_no_flags_writes_none)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 1, 0) };
        data::page pages[] = { a_page(10, data::NO_ID) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 1);
        session.begin(0);

        TEST_ASSERT(times_called(dlg_call_kind::set_flag) == 0, "no flag is set");
        TEST_ASSERT(times_called(dlg_call_kind::clear_flag) == 0, "and none is cleared");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_page_runs_its_display_script)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 1, 0) };
        data::page pages[] = { running(a_page(10, data::NO_ID), 7) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 1);
        session.begin(0);

        TEST_ASSERT(index_of(dlg_call_kind::run_script, 7) < dlg_mock_log::MAX_CALLS,
            "the page's display script runs");
        TEST_ASSERT(times_called(dlg_call_kind::run_script) == 1, "once");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_page_with_no_script_runs_none)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 1, 0) };
        data::page pages[] = { a_page(10, data::NO_ID) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 1);
        session.begin(0);

        TEST_ASSERT(times_called(dlg_call_kind::run_script) == 0,
            "a page naming no script runs nothing");
        TEST_SUCCESS;
    }

    /// The script has the last word over the flags the author wrote, so it runs
    /// after them and never before.
    TEST_IMPL(test_dialogue_page_runs_its_script_after_its_flags)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 1, 0) };
        data::page pages[] = { running(changing_flags(a_page(10, data::NO_ID), 3, 4), 7) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 1);
        session.begin(0);

        dlg_call const expected[] = {
            { dlg_call_kind::mark_page_read, 0 },
            { dlg_call_kind::set_flag, 3 },
            { dlg_call_kind::clear_flag, 4 },
            { dlg_call_kind::run_script, 7 },
        };
        TEST_ASSERT(calls_were(expected, 4), "read, then the flags, then the script");
        TEST_SUCCESS;
    }

    /// The whole reason the script runs before the page is laid out: a choice the
    /// script unlocks is there on this visit, not on some later one.
    TEST_IMPL(test_dialogue_page_script_unlocks_one_of_its_own_choices)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 2, 0) };
        data::page pages[] = { with_choices(running(a_page(10, data::NO_ID), 7), 0, 1),
            a_page(11, data::NO_ID) };
        data::choice choices[] = { gated_on(a_choice(20, 1), 5) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 2, choices, 1);
        g_dlg_log.script_that_sets = 7;
        g_dlg_log.script_sets_flag = 5;
        session.begin(0);

        TEST_ASSERT(session.choice_count() == 1,
            "the choice the display script unlocked is offered on this visit");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_choice_applies_its_flags_when_it_is_taken)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 2, 0) };
        data::page pages[]
            = { with_choices(a_page(10, data::NO_ID), 0, 1), a_page(11, data::NO_ID) };
        data::choice choices[] = { changing_flags(a_choice(20, 1), 3, 4) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 2, choices, 1);
        g_dlg_log.flags[4] = true;
        session.begin(0);

        TEST_ASSERT(!g_dlg_log.flags[3], "showing a choice applies nothing");

        session.take_choice(0);

        TEST_ASSERT(g_dlg_log.flags[3], "the choice's set flag is set once it is taken");
        TEST_ASSERT(!g_dlg_log.flags[4], "and its clear flag is cleared");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_choice_runs_its_click_script)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 2, 0) };
        data::page pages[]
            = { with_choices(a_page(10, data::NO_ID), 0, 1), a_page(11, data::NO_ID) };
        data::choice choices[] = { running(a_choice(20, 1), 9) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 2, choices, 1);
        session.begin(0);

        TEST_ASSERT(times_called(dlg_call_kind::run_script) == 0,
            "showing a choice runs nothing");

        session.take_choice(0);

        TEST_ASSERT(index_of(dlg_call_kind::run_script, 9) < dlg_mock_log::MAX_CALLS,
            "the choice's click script runs when it is taken");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_choice_runs_its_script_after_its_flags)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 2, 0) };
        data::page pages[]
            = { with_choices(a_page(10, data::NO_ID), 0, 1), a_page(11, data::NO_ID) };
        data::choice choices[] = { running(changing_flags(a_choice(20, 1), 3, 4), 9) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 2, choices, 1);
        session.begin(0);
        session.take_choice(0);

        dlg_call const expected[] = {
            { dlg_call_kind::mark_page_read, 0 },
            { dlg_call_kind::mark_choice_taken, 0 },
            { dlg_call_kind::set_flag, 3 },
            { dlg_call_kind::clear_flag, 4 },
            { dlg_call_kind::run_script, 9 },
            { dlg_call_kind::mark_page_read, 1 },
        };
        TEST_ASSERT(calls_were(expected, 6),
            "taken, then the flags, then the script, then the destination");
        TEST_SUCCESS;
    }

    /// A script that changed scene has left the scene the conversation was read
    /// in, so the page the choice named belongs somewhere the player no longer is.
    TEST_IMPL(test_dialogue_choice_script_that_changes_scene_abandons_its_destination)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 2, 0) };
        data::page pages[]
            = { with_choices(a_page(10, data::NO_ID), 0, 1), a_page(11, data::NO_ID) };
        data::choice choices[] = { running(a_choice(20, 1), 9) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 2, choices, 1);
        g_dlg_log.terminating_script = 9;
        session.begin(0);
        session.take_choice(0);

        TEST_ASSERT(session.has_ended(), "the conversation is over at once");
        TEST_ASSERT(!g_dlg_log.page_read[1], "the destination the choice named is abandoned");
        TEST_ASSERT(session.choice_count() == 0, "and nothing is left to pick");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_page_script_that_changes_scene_ends_the_conversation)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 2, 0) };
        data::page pages[] = { with_choices(running(a_page(10, 1), 7), 0, 1),
            a_page(11, data::NO_ID) };
        data::choice choices[] = { a_choice(20, 1) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 2, choices, 1);
        g_dlg_log.terminating_script = 7;

        TEST_ASSERT(!session.begin(0),
            "a conversation whose opening page leaves the scene never opens");
        TEST_ASSERT(session.has_ended(), "and has ended");
        TEST_ASSERT(session.choice_count() == 0, "with no choices worked out for it");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_page_script_that_changes_scene_stops_mid_conversation)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 2, 0) };
        data::page pages[] = { a_page(10, 1), running(a_page(11, data::NO_ID), 7) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 2);
        g_dlg_log.terminating_script = 7;
        session.begin(0);

        TEST_ASSERT(!session.has_ended(), "the first page has no script to leave on");

        session.advance();

        TEST_ASSERT(session.has_ended(), "the page whose script left the scene ends it");
        TEST_SUCCESS;
    }

    /// A script the VM killed for an illegal call reports itself through the VM's
    /// own error path; from here it is a script that ran and did not terminate,
    /// so the conversation goes exactly where it was going.
    TEST_IMPL(test_dialogue_a_script_that_did_not_terminate_changes_nothing)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 2, 0) };
        data::page pages[]
            = { with_choices(a_page(10, data::NO_ID), 0, 1), a_page(11, data::NO_ID) };
        data::choice choices[] = { running(a_choice(20, 1), 9) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 2, choices, 1);
        session.begin(0);
        session.take_choice(0);

        TEST_ASSERT(!session.has_ended(), "the conversation carries on");
        TEST_ASSERT(session.current_page_index() == 1, "at the page the choice named");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_skipped_pages_run_no_flags_and_no_scripts)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 2, 0) };
        data::page pages[] = { disabled(running(changing_flags(a_page(10, data::NO_ID), 3, 4), 7)),
            a_page(11, data::NO_ID) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 2);
        g_dlg_log.flags[4] = true;
        session.begin(0);

        TEST_ASSERT(session.current_page_index() == 1, "the skip lands on the page after it");
        TEST_ASSERT(!g_dlg_log.flags[3], "the skipped page sets nothing");
        TEST_ASSERT(g_dlg_log.flags[4], "clears nothing");
        TEST_ASSERT(times_called(dlg_call_kind::run_script) == 0, "and runs no script");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_unavailable_choices_run_no_flags_and_no_scripts)
    {
        data::dialogue dialogues[] = { a_dialogue(0, 2, 0) };
        data::page pages[]
            = { with_choices(a_page(10, data::NO_ID), 0, 2), a_page(11, data::NO_ID) };
        data::choice choices[] = { disabled(running(changing_flags(a_choice(20, 1), 3, 4), 9)),
            a_choice(21, 1) };

        dialogue_session session;
        wire(session, dialogues, 1, pages, 2, choices, 2);
        g_dlg_log.flags[4] = true;
        session.begin(0);

        TEST_ASSERT(session.choice_count() == 1, "only the enabled choice is offered");

        session.take_choice(0);

        TEST_ASSERT(!g_dlg_log.flags[3], "the choice that was never offered sets nothing");
        TEST_ASSERT(g_dlg_log.flags[4], "clears nothing");
        TEST_ASSERT(times_called(dlg_call_kind::run_script) == 0, "and runs no script");
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
        TEST(test_dialogue_reports_the_pages_it_can_show)
        TEST(test_dialogue_reports_its_range_truncated_to_the_loaded_pages)
        TEST(test_dialogue_that_never_started_reports_no_pages)
        TEST(test_dialogue_that_could_not_start_reports_no_pages)
        TEST(test_dialogue_starting_over_forgets_the_previous_one)

        TEST(test_dialogue_advances_to_the_named_next_page)
        TEST(test_dialogue_marks_every_page_read_as_it_arrives)
        TEST(test_dialogue_ends_on_the_no_destination_sentinel)
        TEST(test_dialogue_ends_running_past_its_last_page)
        TEST(test_dialogue_ending_reads_no_further_page)
        TEST(test_dialogue_advancing_after_the_end_does_nothing)
        TEST(test_dialogue_follows_a_page_pointing_at_itself)
        TEST(test_dialogue_pages_are_addressed_globally)

        TEST(test_dialogue_never_shows_a_page_its_author_disabled)
        TEST(test_dialogue_skips_a_page_whose_check_flag_is_false)
        TEST(test_dialogue_shows_a_gated_page_once_its_flag_is_true)
        TEST(test_dialogue_page_with_no_check_flag_needs_no_flag)
        TEST(test_dialogue_one_time_page_is_shown_once_then_skipped)
        TEST(test_dialogue_skips_a_one_time_page_burnt_before_it_started)
        TEST(test_dialogue_replaces_an_unavailable_page_by_index)
        TEST(test_dialogue_skips_a_run_of_unavailable_pages)
        TEST(test_dialogue_ends_when_nothing_after_an_unavailable_page_is_available)
        TEST(test_dialogue_with_every_page_unavailable_does_not_start)
        TEST(test_dialogue_greeting_variant_needs_no_extra_wiring)
        TEST(test_dialogue_availability_conditions_combine)

        TEST(test_dialogue_page_authored_with_no_choices_offers_none)
        TEST(test_dialogue_offers_the_choices_its_page_names)
        TEST(test_dialogue_never_offers_a_choice_its_author_disabled)
        TEST(test_dialogue_skips_a_choice_whose_check_flag_is_false)
        TEST(test_dialogue_offers_a_gated_choice_once_its_flag_is_true)
        TEST(test_dialogue_choice_with_no_check_flag_needs_no_flag)
        TEST(test_dialogue_showing_a_one_time_choice_does_not_burn_it)
        TEST(test_dialogue_burns_a_one_time_choice_when_it_is_taken)
        TEST(test_dialogue_marks_a_choice_taken_before_it_moves_on)
        TEST(test_dialogue_taking_a_choice_goes_to_the_page_it_names)
        TEST(test_dialogue_choice_with_no_destination_ends_the_conversation)
        TEST(test_dialogue_choice_pointing_outside_the_dialogue_ends_it)
        TEST(test_dialogue_choice_pointing_at_its_own_page_returns_with_itself_gone)
        TEST(test_dialogue_replaces_an_unavailable_choice_destination_by_index)
        TEST(test_dialogue_page_whose_choices_are_all_unavailable_falls_through)
        TEST(test_dialogue_taking_a_slot_past_the_end_does_nothing)
        TEST(test_dialogue_that_has_ended_offers_no_choices)
        TEST(test_dialogue_choice_range_past_the_choices_is_truncated)
        TEST(test_dialogue_choice_availability_conditions_combine)

        TEST(test_dialogue_shows_every_choice_until_they_are_fitted)
        TEST(test_dialogue_choices_that_all_fit_offer_no_more)
        TEST(test_dialogue_slot_past_the_set_names_nothing)
        TEST(test_dialogue_one_choice_too_many_starts_a_rotation)
        TEST(test_dialogue_rotating_shows_the_next_set_then_wraps)
        TEST(test_dialogue_more_than_nine_choices_rotate)
        TEST(test_dialogue_fits_a_multi_line_choice_at_its_true_height)
        TEST(test_dialogue_shows_one_choice_even_with_no_room_for_it)
        TEST(test_dialogue_leaves_out_a_choice_that_could_not_be_measured)
        TEST(test_dialogue_rotating_a_page_that_fits_does_nothing)
        TEST(test_dialogue_fitting_again_keeps_the_set_the_player_is_on)
        TEST(test_dialogue_arriving_at_a_page_starts_on_its_first_set)
        TEST(test_dialogue_a_rotated_set_names_the_choices_it_draws)

        TEST(test_dialogue_page_applies_its_flags_when_it_is_displayed)
        TEST(test_dialogue_page_with_no_flags_writes_none)
        TEST(test_dialogue_page_runs_its_display_script)
        TEST(test_dialogue_page_with_no_script_runs_none)
        TEST(test_dialogue_page_runs_its_script_after_its_flags)
        TEST(test_dialogue_page_script_unlocks_one_of_its_own_choices)
        TEST(test_dialogue_choice_applies_its_flags_when_it_is_taken)
        TEST(test_dialogue_choice_runs_its_click_script)
        TEST(test_dialogue_choice_runs_its_script_after_its_flags)
        TEST(test_dialogue_choice_script_that_changes_scene_abandons_its_destination)
        TEST(test_dialogue_page_script_that_changes_scene_ends_the_conversation)
        TEST(test_dialogue_page_script_that_changes_scene_stops_mid_conversation)
        TEST(test_dialogue_a_script_that_did_not_terminate_changes_nothing)
        TEST(test_dialogue_skipped_pages_run_no_flags_and_no_scripts)
        TEST(test_dialogue_unavailable_choices_run_no_flags_and_no_scripts)
    TEST_SUITE_END
}  // namespace NEONengine

#endif  // ACE_TEST_RUNNER

#endif  // __DIALOGUE_SESSION_TESTS_H__INCLUDED__
