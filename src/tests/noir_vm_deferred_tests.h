#ifndef __NOIR_VM_DEFERRED_TESTS_H__INCLUDED__
#define __NOIR_VM_DEFERRED_TESTS_H__INCLUDED__

#ifdef ACE_TEST_RUNNER

#include "noir_vm_test_utils.h"

namespace NEONengine
{
    // ═════════════════════════════════════════════════════════════════════════════
    // Deferred bridge calls
    //
    // A deferred call records what the script asked for, returns, and drains the
    // frame pool. The walk ends there: no session runs again in that update, the
    // caller included. The session stays runnable with its program counter on the
    // opcode after the call.
    //
    // The deferred opcodes are SAY, SHOW_DIALOG, SAVE_GAME, FADE_OUT, FADE_IN and
    // GOTO_SCENE. The rest of the bridge is immediate and costs a session nothing
    // but an instruction.
    // ═════════════════════════════════════════════════════════════════════════════

    namespace
    {
        /// Emits MOV16 D0, #value.
        void def_mov_d0(bytecode_builder& bc, u16 value)
        {
            bc.emit(noir_op::MOV16);
            bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
            bc.emit_u16(value);
        }

        /// Emits MOV16 D<reg>, #value.
        void def_mov_reg(bytecode_builder& bc, u8 dst_mode, u16 value)
        {
            bc.emit(noir_op::MOV16);
            bc.emit_mode(dst_mode, noir_mode::IMM16);
            bc.emit_u16(value);
        }

        /**
         * @brief Emits a whole function that leaves @p mark on the bridge.
         *
         * Registers cannot say which session ran: reg_d() reads the window, and
         * the window holds whichever session went last. HOTSPOT_ON is immediate
         * and no deferred opcode touches its slot, so the mock's record of it is
         * a mark that survives whatever the scheduler does afterwards.
         */
        u16 def_marker_func(bytecode_builder& bc, u16 mark)
        {
            u16 const fn = bc.begin_func();
            def_mov_d0(bc, mark);
            bc.emit(noir_op::HOTSPOT_ON);
            bc.emit(noir_op::RET);
            return fn;
        }

        /// A second marker, for when one update has to tell two sessions apart.
        u16 def_marker_func_b(bytecode_builder& bc, u16 mark)
        {
            u16 const fn = bc.begin_func();
            def_mov_d0(bc, mark);
            bc.emit(noir_op::HOTSPOT_OFF);
            bc.emit(noir_op::RET);
            return fn;
        }

        /// Whether the marker function carrying @p mark has run.
        bool def_marked(u16 mark)
        {
            return g_mock_log.hotspot_on_id == mark;
        }

        /// Whether the second marker function carrying @p mark has run.
        bool def_marked_b(u16 mark)
        {
            return g_mock_log.hotspot_off_id == mark;
        }
    }  // namespace

    // ── One call ends the walk ───────────────────────────────────────────────────

    TEST_IMPL(test_deferred_stops_the_caller_mid_budget)
    {
        // The caller has budget left over and must not spend it. Ten NOPs would
        // fit; the deferred call is the third opcode, so eight instructions of
        // the session's own budget go unused.
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        def_mov_d0(bc, 1);
        bc.emit(noir_op::SAY);

        def_mov_reg(bc, noir_mode::REG_D3, 4321);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);

        auto status = vm.update();

        TEST_ASSERT(status == noir_result::yielded, "the session did not reach its RET");
        TEST_ASSERT(vm.reg_d(3) != 4321, "nor the opcode straight after the call");
        TEST_ASSERT(vm.session_count() == 1, "and it is still in the run list");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_deferred_resumes_on_the_next_opcode)
    {
        // The call itself has already run: its PC moved past it. A session that
        // resumed one opcode early would call the bridge twice, and the player
        // would hear the same line again.
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        def_mov_d0(bc, 77);
        bc.emit(noir_op::SAY);
        def_mov_reg(bc, noir_mode::REG_D2, 1234);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        g_mock_log.reset();
        vm.execute(fn);

        vm.update();
        TEST_ASSERT(g_mock_log.say_count == 1, "the line went out once");

        auto status = vm.update();
        TEST_ASSERT(status == noir_result::completed, "the rest of the script ran");
        TEST_ASSERT(vm.reg_d(2) == 1234, "starting at the opcode after the call");
        TEST_ASSERT(g_mock_log.say_count == 1, "and the line did not go out twice");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_deferred_waits_its_turn_for_the_budget)
    {
        // A deferred call is an opcode like any other and does not jump the
        // queue: a session whose budget ran out before reaching it makes the
        // call on its next slice, not this one.
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        def_mov_d0(bc, 1);   // 1
        bc.emit(noir_op::NOP);  // 2 - budget is now spent
        bc.emit(noir_op::SAY);  // must not run this update
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.set_session_budget(2);
        g_mock_log.reset();
        vm.execute(fn);

        vm.update();
        TEST_ASSERT(g_mock_log.say_count == 0, "the budget ran out before the call");

        vm.update();
        TEST_ASSERT(g_mock_log.say_count == 1, "which is where the next update began");
        TEST_SUCCESS;
    }

    // ── The walk ends for everyone ───────────────────────────────────────────────

    TEST_IMPL(test_deferred_stops_the_session_behind_it)
    {
        // The session after the caller in the run list gets nothing, though the
        // pool has room for it. This is what holds a conversation still.
        bytecode_builder bc;

        u16 fn_first = bc.begin_func();
        def_mov_d0(bc, 3);
        bc.emit(noir_op::SAY);
        bc.emit(noir_op::RET);

        u16 fn_second = def_marker_func(bc, 0x0B01);

        auto vm = bc.build_vm();
        g_mock_log.reset();
        vm.execute(fn_first);
        vm.execute(fn_second);

        vm.update();

        TEST_ASSERT(!def_marked(0x0B01), "the second session did not run");
        TEST_ASSERT(vm.session_count() == 2, "both are still live");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_deferred_spares_the_session_in_front_of_it)
    {
        // Only what comes after is stopped. A session that already had its slice
        // this update keeps what it did - the walk ends, it does not roll back.
        bytecode_builder bc;

        u16 fn_first = def_marker_func(bc, 0x0B02);

        u16 fn_second = bc.begin_func();
        def_mov_d0(bc, 3);
        bc.emit(noir_op::SAY);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        g_mock_log.reset();
        vm.execute(fn_first);
        vm.execute(fn_second);

        vm.update();

        TEST_ASSERT(def_marked(0x0B02), "the session in front ran to its end");
        TEST_ASSERT(g_mock_log.say_count == 1, "and the deferred call still happened");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_deferred_pool_is_drained_not_decremented)
    {
        // A pool big enough for everyone still leaves the second session with
        // nothing: the walk ends on the call, whatever room was left behind it.
        bytecode_builder bc;

        u16 fn_first = bc.begin_func();
        def_mov_d0(bc, 3);
        bc.emit(noir_op::SAY);
        bc.emit(noir_op::RET);

        u16 fn_second = def_marker_func(bc, 0x0B03);

        auto vm = bc.build_vm();
        vm.set_frame_budget(1000);
        g_mock_log.reset();
        vm.execute(fn_first);
        vm.execute(fn_second);

        vm.update();

        TEST_ASSERT(!def_marked(0x0B03), "998 instructions left in the pool and nobody ran");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_deferred_walk_end_does_not_outlive_its_update)
    {
        // The drain lasts one update. If the flag survived into the next one,
        // every session after the first would starve for the rest of the game.
        //
        // Three sessions, because with two the rotation swaps them and the
        // marker would run before a stale drain could stop it. With three, one
        // marker is still behind the drain on the second update.
        bytecode_builder bc;

        u16 fn_caller = bc.begin_func();
        def_mov_d0(bc, 3);
        bc.emit(noir_op::SAY);
        bc.emit(noir_op::RET);

        u16 fn_mark_a = def_marker_func(bc, 0x0B04);
        u16 fn_mark_b = def_marker_func_b(bc, 0x0B05);

        auto vm = bc.build_vm();
        g_mock_log.reset();
        vm.execute(fn_caller);
        vm.execute(fn_mark_a);
        vm.execute(fn_mark_b);

        vm.update();
        TEST_ASSERT(!def_marked(0x0B04), "both held on the update the call was made");
        TEST_ASSERT(!def_marked_b(0x0B05), "including the one two places back");

        vm.update();
        TEST_ASSERT(def_marked(0x0B04), "the first is free on the update after it");
        TEST_ASSERT(def_marked_b(0x0B05), "and so is the one behind that");
        TEST_SUCCESS;
    }

    // ── Which calls defer ────────────────────────────────────────────────────────

    TEST_IMPL(test_immediate_calls_do_not_end_the_walk)
    {
        // PLAY_MUSIC is immediate. A script full of them runs its whole slice,
        // and the session behind it still gets one. Were every bridge call
        // treated as deferred, this would take four updates instead of one.
        bytecode_builder bc;

        u16 fn_first = bc.begin_func();
        def_mov_d0(bc, 5);
        bc.emit(noir_op::PLAY_MUSIC);
        bc.emit(noir_op::STOP_MUSIC);
        def_mov_d0(bc, 6);
        bc.emit(noir_op::PLAY_SFX);
        bc.emit(noir_op::RET);

        u16 fn_second = def_marker_func(bc, 0x0B05);

        auto vm = bc.build_vm();
        g_mock_log.reset();
        vm.execute(fn_first);
        vm.execute(fn_second);

        auto status = vm.update();

        TEST_ASSERT(g_mock_log.play_sfx_id == 6, "every immediate call ran");
        TEST_ASSERT(def_marked(0x0B05), "and the session behind it ran too");
        TEST_ASSERT(status == noir_result::completed, "both finished in one update");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_every_deferred_opcode_ends_the_walk)
    {
        // One session per deferred opcode, each with a marker session behind it.
        // The marker must still be unset after a single update: naming them one
        // at a time would let a newly-immediate opcode slip through.
        u8 const deferred[] = {
            noir_op::SAY,
            noir_op::SHOW_DIALOG,
            noir_op::SAVE_GAME,
            noir_op::FADE_OUT,
            noir_op::FADE_IN,
            noir_op::GOTO_SCENE,
        };

        for (u8 i = 0; i < sizeof(deferred) / sizeof(deferred[0]); ++i)
        {
            bytecode_builder bc;

            u16 fn_call = bc.begin_func();
            def_mov_d0(bc, 1);
            bc.emit(deferred[i]);
            bc.emit(noir_op::RET);

            u16 fn_marker = def_marker_func(bc, 0x0B10);

            auto vm = bc.build_vm();
            g_mock_log.reset();
            vm.execute(fn_call);
            vm.execute(fn_marker);

            vm.update();

            TEST_ASSERT(!def_marked(0x0B10), "a deferred opcode let the walk carry on");
        }
        TEST_SUCCESS;
    }

    // ── Deferring from inside a call ─────────────────────────────────────────────

    TEST_IMPL(test_deferred_inside_call_returns_to_the_right_place)
    {
        // The park has to survive the call frame: the session resumes inside the
        // callee, finishes it, and comes back to the caller.
        bytecode_builder bc;

        u16 fn_main = bc.begin_func();
        bc.emit(noir_op::CALL);
        bc.emit_u16(1);
        def_mov_reg(bc, noir_mode::REG_D3, 0x0BAD);
        bc.emit(noir_op::RET);

        bc.begin_func();  // fn 1
        def_mov_d0(bc, 9);
        bc.emit(noir_op::SAY);
        def_mov_reg(bc, noir_mode::REG_D2, 0x0F00);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn_main);

        vm.update();
        TEST_ASSERT(vm.reg_d(2) != 0x0F00, "parked inside the callee");

        auto status = vm.update();
        TEST_ASSERT(vm.reg_d(2) == 0x0F00, "the callee finished");
        TEST_ASSERT(vm.reg_d(3) == 0x0BAD, "and the caller carried on after it");
        TEST_ASSERT(status == noir_result::completed, "the script ran out");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_deferred_leaves_registers_and_flags_alone)
    {
        // Nothing about a deferred call touches the session's state beyond its
        // program counter, and it is stored and reloaded across the update
        // boundary like any other parked session.
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        def_mov_reg(bc, noir_mode::REG_D1, 0x1111);
        def_mov_reg(bc, noir_mode::REG_D2, 0x2222);
        def_mov_reg(bc, noir_mode::REG_D3, 0x3333);
        def_mov_d0(bc, 0x4444);
        bc.emit(noir_op::SAY);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);

        vm.update();
        vm.update();

        TEST_ASSERT(vm.reg_d(1) == 0x1111, "D1 survived");
        TEST_ASSERT(vm.reg_d(2) == 0x2222, "D2 survived");
        TEST_ASSERT(vm.reg_d(3) == 0x3333, "D3 survived");
        TEST_ASSERT(vm.exit_code() == 0x4444, "D0 survived");
        TEST_SUCCESS;
    }

    // ── Waiting still works alongside it ─────────────────────────────────────────

    TEST_IMPL(test_deferred_does_not_disturb_a_waiting_session)
    {
        // A session parked on WAIT ages one update per update, and an update cut
        // short by someone else's deferred call still counts. A deferred call
        // that skipped the ageing would strand every WAIT behind it.
        bytecode_builder bc;

        u16 fn_waiter = bc.begin_func();
        bc.emit(noir_op::WAIT);
        bc.emit_u16(2);
        def_mov_d0(bc, 0x0B06);
        bc.emit(noir_op::HOTSPOT_ON);
        bc.emit(noir_op::RET);

        u16 fn_talker = bc.begin_func();
        def_mov_d0(bc, 1);
        bc.emit(noir_op::SAY);
        def_mov_d0(bc, 2);
        bc.emit(noir_op::SAY);
        def_mov_d0(bc, 3);
        bc.emit(noir_op::SAY);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        g_mock_log.reset();
        vm.execute(fn_waiter);
        vm.execute(fn_talker);

        // Update 1: the waiter runs WAIT 2 and parks; the talker says its first
        // line and ends the walk. Updates 2 and 3 age the wait out.
        vm.update();
        TEST_ASSERT(!def_marked(0x0B06), "the wait has not aged out yet");

        vm.update();
        vm.update();
        vm.update();

        TEST_ASSERT(def_marked(0x0B06), "the wait aged out despite the interruptions");
        TEST_SUCCESS;
    }

    // ── Terminal calls stop the VM ───────────────────────────────────────────────

    TEST_IMPL(test_terminal_call_holds_every_session)
    {
        // A modal only ends the walk; a terminal one ends the VM. Three sessions,
        // so the rotation cannot hand the markers a turn ahead of the caller and
        // pass the test by accident.
        bytecode_builder bc;

        u16 fn_goto = bc.begin_func();
        def_mov_d0(bc, 7);
        bc.emit(noir_op::GOTO_SCENE);
        bc.emit(noir_op::RET);

        u16 fn_a = def_marker_func(bc, 0x0B20);
        u16 fn_b = def_marker_func_b(bc, 0x0B21);

        auto vm = bc.build_vm();
        g_mock_log.reset();
        vm.execute(fn_goto);
        vm.execute(fn_a);
        vm.execute(fn_b);

        for (u16 i = 0; i < 6; ++i) vm.update();

        TEST_ASSERT(!def_marked(0x0B20), "no session ran after the terminal call");
        TEST_ASSERT(!def_marked_b(0x0B21), "not one of them");
        TEST_ASSERT(vm.session_count() == 3, "and none were cleaned up");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_terminal_call_freezes_a_waiting_session)
    {
        // Waiting is the one thing that moves without a session running. It has
        // to stop too, or a WAIT would age out during a scene load and the
        // session would come back due rather than discarded.
        bytecode_builder bc;

        u16 fn_wait = bc.begin_func();
        bc.emit(noir_op::WAIT);
        bc.emit_u16(3);
        def_mov_d0(bc, 0x0B22);
        bc.emit(noir_op::HOTSPOT_ON);
        bc.emit(noir_op::RET);

        u16 fn_goto = bc.begin_func();
        def_mov_d0(bc, 7);
        bc.emit(noir_op::GOTO_SCENE);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        g_mock_log.reset();
        vm.execute(fn_wait);
        vm.execute(fn_goto);

        for (u16 i = 0; i < 10; ++i) vm.update();

        TEST_ASSERT(!def_marked(0x0B22), "the wait did not age out while the VM was stopped");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_every_terminal_opcode_stops_the_vm)
    {
        // Named one at a time, a newly-terminal opcode would go untested and a
        // no-longer-terminal one would go unnoticed.
        u8 const terminal[] = {
            noir_op::GOTO_SCENE,
            noir_op::LOAD_GAME,
            noir_op::NEW_GAME,
            noir_op::QUIT_GAME,
        };

        for (u8 i = 0; i < sizeof(terminal) / sizeof(terminal[0]); ++i)
        {
            bytecode_builder bc;

            u16 fn_call = bc.begin_func();
            def_mov_d0(bc, 1);
            bc.emit(terminal[i]);
            bc.emit(noir_op::RET);

            u16 fn_marker = def_marker_func(bc, 0x0B23);

            auto vm = bc.build_vm();
            g_mock_log.reset();
            vm.execute(fn_call);
            vm.execute(fn_marker);

            vm.update();
            auto status = vm.update();

            TEST_ASSERT(status == noir_result::terminated, "a terminal opcode did not stop the VM");
            TEST_ASSERT(!def_marked(0x0B23), "and nothing ran while it was stopped");
        }
        TEST_SUCCESS;
    }

    TEST_IMPL(test_modal_calls_do_not_stop_the_vm)
    {
        // The other half of the pair. Were terminal and modal still the same
        // thing, a conversation would stop the VM for good on its first line.
        u8 const modal[] = {
            noir_op::SAY,
            noir_op::SHOW_DIALOG,
            noir_op::SAVE_GAME,
            noir_op::FADE_OUT,
            noir_op::FADE_IN,
        };

        for (u8 i = 0; i < sizeof(modal) / sizeof(modal[0]); ++i)
        {
            bytecode_builder bc;

            u16 fn = bc.begin_func();
            def_mov_d0(bc, 1);
            bc.emit(modal[i]);
            def_mov_d0(bc, 0x0B24);
            bc.emit(noir_op::HOTSPOT_ON);
            bc.emit(noir_op::RET);

            auto vm = bc.build_vm();
            g_mock_log.reset();
            vm.execute(fn);

            vm.update();
            TEST_ASSERT(!vm.terminated(), "a modal call stopped the VM");

            vm.update();
            TEST_ASSERT(def_marked(0x0B24), "and the session did not carry on");
        }
        TEST_SUCCESS;
    }

    TEST_IMPL(test_terminal_latch_outlives_new_sessions)
    {
        // Only the teardown lifts it. Starting a script is not a teardown, and
        // a session queued behind one must wait for the reset like the rest.
        bytecode_builder bc;

        u16 fn_goto = bc.begin_func();
        def_mov_d0(bc, 7);
        bc.emit(noir_op::GOTO_SCENE);
        bc.emit(noir_op::RET);

        u16 fn_marker = def_marker_func(bc, 0x0B25);
        u16 fn_late   = def_marker_func_b(bc, 0x0B26);

        auto vm = bc.build_vm();
        g_mock_log.reset();
        vm.execute(fn_goto);
        vm.execute(fn_marker);

        vm.update();

        vm.execute(fn_late);
        auto status = vm.update();

        TEST_ASSERT(status == noir_result::terminated, "still stopped");
        TEST_ASSERT(!def_marked_b(0x0B26), "and the session started after it did not run");

        vm.reset_execution();

        TEST_ASSERT(vm.session_count() == 0, "the teardown discarded it along with the rest");
        TEST_SUCCESS;
    }

    // ── Suite registration ───────────────────────────────────────────────────────

    constexpr char const* noir_vm_deferred_name = "noir_vm_deferred";

    Test noir_vm_deferred_tests[] = {
        { "stops the caller mid-budget", test_deferred_stops_the_caller_mid_budget },
        { "resumes on the next opcode", test_deferred_resumes_on_the_next_opcode },
        { "waits its turn for the budget", test_deferred_waits_its_turn_for_the_budget },
        { "stops the session behind it", test_deferred_stops_the_session_behind_it },
        { "spares the session in front", test_deferred_spares_the_session_in_front_of_it },
        { "pool is drained not decremented", test_deferred_pool_is_drained_not_decremented },
        { "walk end lasts one update", test_deferred_walk_end_does_not_outlive_its_update },
        { "immediate calls do not defer", test_immediate_calls_do_not_end_the_walk },
        { "every deferred opcode defers", test_every_deferred_opcode_ends_the_walk },
        { "deferring inside a CALL", test_deferred_inside_call_returns_to_the_right_place },
        { "registers survive the park", test_deferred_leaves_registers_and_flags_alone },
        { "a waiting session still ages", test_deferred_does_not_disturb_a_waiting_session },
        { "terminal holds every session", test_terminal_call_holds_every_session },
        { "terminal freezes a waiting session", test_terminal_call_freezes_a_waiting_session },
        { "every terminal opcode stops the VM", test_every_terminal_opcode_stops_the_vm },
        { "modal calls do not stop the VM", test_modal_calls_do_not_stop_the_vm },
        { "the latch outlives new sessions", test_terminal_latch_outlives_new_sessions },
    };

}  // namespace NEONengine

#endif  // ACE_TEST_RUNNER
#endif  // __NOIR_VM_DEFERRED_TESTS_H__INCLUDED__
