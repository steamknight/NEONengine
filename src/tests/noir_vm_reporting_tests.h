#ifndef __NOIR_VM_REPORTING_TESTS_H__INCLUDED__
#define __NOIR_VM_REPORTING_TESTS_H__INCLUDED__

#ifdef ACE_TEST_RUNNER

#include "types.h"

#include "tests/noir_vm_test_utils.h"

namespace NEONengine
{
    // ═════════════════════════════════════════════════════════════════════════════
    // What the VM reports, and to whom
    //
    // The player is told what killed a script. The message leaves through the
    // bridge, from the one place a session can die, so a request the VM merely
    // turns down says nothing: no session died to report.
    //
    // Whoever is tuning the game is told what the stacks cost and when the frame
    // budget runs out. Those go to the log in debug builds and nowhere else.
    // ═════════════════════════════════════════════════════════════════════════════

    /// A script that pushes @p pushes words and returns.
    inline u16 rep_pusher(bytecode_builder& bc, u16 pushes)
    {
        u16 fn = bc.begin_func();
        for (u16 i = 0; i < pushes; ++i)
        {
            bc.emit(noir_op::PUSH);
            bc.emit_mode(0, noir_mode::IMM16);
            bc.emit_u16(to<u16>(0xB000 + i));
        }
        bc.emit(noir_op::RET);
        return fn;
    }

    /// A script that spends @p nops instructions doing nothing, then returns.
    inline u16 rep_idler(bytecode_builder& bc, u16 nops)
    {
        u16 fn = bc.begin_func();
        for (u16 i = 0; i < nops; ++i) bc.emit(noir_op::NOP);
        bc.emit(noir_op::RET);
        return fn;
    }

    // ── A death reaches the player ──────────────────────────────────────────────

    TEST_IMPL(test_report_a_death_reaches_the_bridge)
    {
        bytecode_builder bc;
        u16 fn = bc.begin_func();
        bc.emit(noir_op::HALT);

        auto vm = bc.build_vm();
        TEST_ASSERT(nothing_reported(), "nothing has died yet");

        bc.run_to_completion(vm, fn);

        TEST_ASSERT(message_is("script 0: HALT"), "the bridge got the line the log gets");
        TEST_SUCCESS;
    }

    // ── A refusal is not a death ────────────────────────────────────────────────

    TEST_IMPL(test_report_a_refused_duplicate_says_nothing)
    {
        bytecode_builder bc;
        u16 fn = rep_idler(bc, 4);

        auto vm = bc.build_vm();
        TEST_ASSERT(vm.execute(fn), "the first request stands");
        TEST_ASSERT(failed_with(vm.execute(fn), noir_error::duplicate_script),
            "the second is turned down");
        TEST_ASSERT(nothing_reported(), "nothing died, so the player hears nothing");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_report_a_refused_execute_says_nothing)
    {
        bytecode_builder bc;
        bc.begin_func();
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        TEST_ASSERT(failed_with(vm.execute(99), noir_error::invalid_func),
            "there is no script 99");
        TEST_ASSERT(nothing_reported(), "the request was turned down, not run and killed");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_report_a_refused_execute_now_says_nothing)
    {
        bytecode_builder bc;
        bc.begin_func();
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        TEST_ASSERT(failed_with(vm.execute_now(99), noir_error::invalid_func),
            "there is no script 99");
        TEST_ASSERT(nothing_reported(), "and this one is turned down the same way");
        TEST_SUCCESS;
    }

#ifdef GAME_DEBUG

    // ── The stack record names the script that set it ───────────────────────────

    TEST_IMPL(test_stats_stack_record_names_the_script)
    {
        // A session's frame costs 8 bytes and every PUSH another 2. Script 0 is
        // there so neither of the other two has the id an untouched record holds.
        bytecode_builder bc;
        bc.begin_func();
        bc.emit(noir_op::RET);

        u16 const light = rep_pusher(bc, 2);
        u16 const heavy = rep_pusher(bc, 6);

        auto vm = bc.build_vm();

        bc.run_to_completion(vm, light);
        TEST_ASSERT(vm.debug_stats().max_stack_used == 12, "twelve bytes at its deepest");
        TEST_ASSERT(vm.debug_stats().max_stack_script == light, "and the light script owns them");

        bc.run_to_completion(vm, heavy);
        TEST_ASSERT(vm.debug_stats().max_stack_used == 20, "the heavy script took the record");
        TEST_ASSERT(vm.debug_stats().max_stack_script == heavy, "and its name went with it");

        bc.run_to_completion(vm, light);
        TEST_ASSERT(vm.debug_stats().max_stack_used == 20, "a lighter run does not lower it");
        TEST_ASSERT(vm.debug_stats().max_stack_script == heavy, "nor take the name back");
        TEST_SUCCESS;
    }

    // ── The frame budget running out ────────────────────────────────────────────

    TEST_IMPL(test_saturation_is_silent_when_demand_fits)
    {
        bytecode_builder bc;
        u16 const a = rep_idler(bc, 2);
        u16 const b = rep_idler(bc, 2);

        auto vm = bc.build_vm();
        vm.execute(a);
        vm.execute(b);
        vm.update();

        TEST_ASSERT(vm.debug_stats().consecutive_saturated_updates == 0,
            "six instructions against a budget of forty");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_saturation_needs_someone_turned_down)
    {
        // Three instructions and a budget of three. The budget is empty at the
        // end of the walk and nobody went short, the one session having reached
        // its RET on the last instruction the frame could pay for.
        bytecode_builder bc;
        u16 const a = rep_idler(bc, 2);

        auto vm = bc.build_vm();
        vm.set_session_budget(3);
        vm.set_frame_budget(3);
        vm.execute(a);

        vm.update();

        TEST_ASSERT(vm.session_count() == 0, "the session ran to its end");
        TEST_ASSERT(vm.debug_stats().consecutive_saturated_updates == 0,
            "an empty budget is not saturation on its own");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_saturation_counts_a_session_the_walk_never_reached)
    {
        bytecode_builder bc;
        u16 const a = rep_idler(bc, 8);
        u16 const b = rep_idler(bc, 8);
        u16 const c = rep_idler(bc, 8);

        auto vm = bc.build_vm();
        vm.set_session_budget(1);
        vm.set_frame_budget(2);
        vm.execute(a);
        vm.execute(b);
        vm.execute(c);

        vm.update();

        TEST_ASSERT(vm.debug_stats().consecutive_saturated_updates == 1,
            "two of the three ran and the budget was gone");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_saturation_counts_a_session_cut_short)
    {
        bytecode_builder bc;
        u16 const a = rep_idler(bc, 8);
        u16 const b = rep_idler(bc, 8);

        auto vm = bc.build_vm();
        vm.set_session_budget(2);
        vm.set_frame_budget(3);
        vm.execute(a);
        vm.execute(b);

        vm.update();

        TEST_ASSERT(vm.debug_stats().consecutive_saturated_updates == 1,
            "the walk reached both, and gave the second one instruction of two");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_saturation_counts_the_updates_it_lasts)
    {
        bytecode_builder bc;
        u16 const a = rep_idler(bc, 20);
        u16 const b = rep_idler(bc, 20);
        u16 const c = rep_idler(bc, 20);

        auto vm = bc.build_vm();
        vm.set_session_budget(1);
        vm.set_frame_budget(2);
        vm.execute(a);
        vm.execute(b);
        vm.execute(c);

        vm.update();
        vm.update();
        vm.update();

        TEST_ASSERT(vm.debug_stats().consecutive_saturated_updates == 3, "three updates in a row");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_saturation_ends_when_demand_fits_again)
    {
        bytecode_builder bc;
        u16 const a = rep_idler(bc, 20);
        u16 const b = rep_idler(bc, 20);
        u16 const c = rep_idler(bc, 20);

        auto vm = bc.build_vm();
        vm.set_session_budget(1);
        vm.set_frame_budget(2);
        vm.execute(a);
        vm.execute(b);
        vm.execute(c);

        vm.update();
        vm.update();
        TEST_ASSERT(vm.debug_stats().consecutive_saturated_updates == 2, "the budget ran out twice");

        vm.set_frame_budget(40);
        vm.update();

        TEST_ASSERT(vm.debug_stats().consecutive_saturated_updates == 0,
            "and the count starts over once demand fits");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_saturation_ignores_a_walk_a_deferred_call_ended)
    {
        // The SAY is the only instruction the frame can afford, so the budget is
        // empty when the walk breaks on it. Nobody was turned down: the walk
        // stopped because the scene has something to put up.
        bytecode_builder bc;

        u16 const talker = bc.begin_func();
        bc.emit(noir_op::SAY);
        bc.emit(noir_op::RET);

        u16 const idler = rep_idler(bc, 8);

        auto vm = bc.build_vm();
        vm.set_session_budget(1);
        vm.set_frame_budget(1);
        vm.execute(talker);
        vm.execute(idler);

        vm.update();

        TEST_ASSERT(g_mock_log.say_count == 1, "the line went out");
        TEST_ASSERT(vm.debug_stats().consecutive_saturated_updates == 0,
            "and the walk ended on it, not on an empty budget");
        TEST_SUCCESS;
    }

#endif  // GAME_DEBUG

    // ─── Test suite registration ────────────────────────────────────────────────

    TEST_SUITE_BEGIN(noir_vm_reporting)
    TEST(test_report_a_death_reaches_the_bridge)
    TEST(test_report_a_refused_duplicate_says_nothing)
    TEST(test_report_a_refused_execute_says_nothing)
    TEST(test_report_a_refused_execute_now_says_nothing)
#ifdef GAME_DEBUG
    TEST(test_stats_stack_record_names_the_script)
    TEST(test_saturation_is_silent_when_demand_fits)
    TEST(test_saturation_needs_someone_turned_down)
    TEST(test_saturation_counts_a_session_the_walk_never_reached)
    TEST(test_saturation_counts_a_session_cut_short)
    TEST(test_saturation_counts_the_updates_it_lasts)
    TEST(test_saturation_ends_when_demand_fits_again)
    TEST(test_saturation_ignores_a_walk_a_deferred_call_ended)
#endif
    TEST_SUITE_END

}  // namespace NEONengine

#endif  // ACE_TEST_RUNNER
#endif  // __NOIR_VM_REPORTING_TESTS_H__INCLUDED__
