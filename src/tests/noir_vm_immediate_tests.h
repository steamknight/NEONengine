#ifndef __NOIR_VM_IMMEDIATE_TESTS_H__INCLUDED__
#define __NOIR_VM_IMMEDIATE_TESTS_H__INCLUDED__

#ifdef ACE_TEST_RUNNER

#include "noir_vm_test_utils.h"

namespace NEONengine
{
    // ═════════════════════════════════════════════════════════════════════════════
    // execute_now
    //
    // Runs a whole script between updates, in the one reserved context, and hands
    // back its D0. It ignores the run list, the duplicate check, the session
    // budget and the frame pool, and answers to a cap of its own.
    //
    // Three things it will not do: run while anything else owns the register
    // window, honour a WAIT nothing can age, or let a modal request be recorded
    // where no frame will ever draw to show it.
    // ═════════════════════════════════════════════════════════════════════════════

    namespace
    {
        /// Emits MOV16 D<reg>, #value.
        void imm_mov(bytecode_builder& bc, u8 dst_mode, u16 value)
        {
            bc.emit(noir_op::MOV16);
            bc.emit_mode(dst_mode, noir_mode::IMM16);
            bc.emit_u16(value);
        }

        /// Emits MOV16 D0, #value.
        void imm_mov_d0(bytecode_builder& bc, u16 value) { imm_mov(bc, noir_mode::REG_D0, value); }

        // ── Re-entry ─────────────────────────────────────────────────────────────
        //
        // Nothing else in the suite reaches back into the VM from a bridge
        // function, so the VM under test has to be findable from a free function.

        noir_vm* s_vm      = nullptr;
        u16 s_target       = 0;
        bool s_tried       = false;
        bool s_refused     = false;
        noir_error s_error = noir_error::none;

        void imm_reset_reentry()
        {
            s_vm      = nullptr;
            s_target  = 0;
            s_tried   = false;
            s_refused = false;
            s_error   = noir_error::none;
        }

        /// Stands in for PLAY_MUSIC. Immediate, so it runs inside the opcode.
        void imm_reentrant_music(u16 /*music_id*/)
        {
            if (!s_vm) return;

            s_tried      = true;
            auto const r = s_vm->execute_now(s_target);
            s_refused    = !r.has_value();
            if (!r) s_error = r.error();
        }
    }  // namespace

    // ── What it hands back ───────────────────────────────────────────────────────

    TEST_IMPL(test_immediate_hands_back_d0)
    {
        bytecode_builder bc;
        u16 fn = bc.begin_func();
        imm_mov_d0(bc, 4242);
        bc.emit(noir_op::RET);

        auto vm      = bc.build_vm();
        auto const r = vm.execute_now(fn);

        TEST_ASSERT(r.has_value(), "the script ran");
        TEST_ASSERT(r == i16(4242), "and D0 came back with it");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_immediate_hands_back_a_negative_d0)
    {
        // D0 is 16 bits of whatever; the answer is signed. A script that says
        // "no" with -1 must not be read as 65535.
        bytecode_builder bc;
        u16 fn = bc.begin_func();
        imm_mov_d0(bc, 0xFFFB);
        bc.emit(noir_op::RET);

        auto vm      = bc.build_vm();
        auto const r = vm.execute_now(fn);

        TEST_ASSERT(r.has_value(), "the script ran");
        TEST_ASSERT(r == i16(-5), "0xFFFB read back as -5");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_immediate_runs_past_a_session_budget)
    {
        // Twenty times round a loop is far more than the ten instructions a
        // session gets in a frame, and no update runs at all.
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        imm_mov(bc, noir_mode::REG_D1, 20);
        imm_mov_d0(bc, 0);

        u16 const top = bc.mark();
        bc.emit(noir_op::INC);
        bc.emit_mode(noir_mode::REG_D0, 0);
        bc.emit(noir_op::DEC);
        bc.emit_mode(noir_mode::REG_D1, 0);
        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(0);

        u16 const bne = bc.mark();
        bc.emit(noir_op::BNE);
        bc.emit_s16(to<i16>(top - (bne + 3)));

        bc.emit(noir_op::RET);

        auto vm      = bc.build_vm();
        auto const r = vm.execute_now(fn);

        TEST_ASSERT(r == i16(20), "the whole loop ran in the one call");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_immediate_makes_immediate_bridge_calls)
    {
        // The case it exists for: the dialogue is up, the VM is stopped, and the
        // music has to change now.
        bytecode_builder bc;
        u16 fn = bc.begin_func();
        imm_mov_d0(bc, 7);
        bc.emit(noir_op::PLAY_MUSIC);
        bc.emit(noir_op::RET);

        auto vm      = bc.build_vm();
        auto const r = vm.execute_now(fn);

        TEST_ASSERT(r == i16(7), "the script ran to its RET");
        TEST_ASSERT(g_mock_log.play_music_id == 7, "and the music started");
        TEST_SUCCESS;
    }

    // ── It leaves the run list alone ─────────────────────────────────────────────

    TEST_IMPL(test_immediate_does_not_join_the_run_list)
    {
        bytecode_builder bc;
        u16 fn = bc.begin_func();
        imm_mov_d0(bc, 1);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute_now(fn);

        TEST_ASSERT(vm.session_count() == 0, "nothing was left running");

        auto const status = vm.update();
        TEST_ASSERT(status == noir_result::ok, "and the update had nothing to do");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_immediate_leaves_the_register_window_as_it_found_it)
    {
        // Between updates the window holds whichever session went last. An
        // immediate run borrows it, and whoever comes back to it must not be
        // able to tell.
        bytecode_builder bc;

        u16 const fn_session = bc.begin_func();
        imm_mov(bc, noir_mode::REG_D1, 0x1111);
        imm_mov(bc, noir_mode::REG_D2, 0x2222);
        imm_mov_d0(bc, 0x3333);
        bc.emit(noir_op::WAIT);
        bc.emit_u16(1);
        bc.emit(noir_op::RET);

        u16 const fn_now = bc.begin_func();
        imm_mov(bc, noir_mode::REG_D1, 0xDEAD);
        imm_mov(bc, noir_mode::REG_D2, 0xDEAD);
        imm_mov_d0(bc, 0xDEAD);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn_session);
        vm.update();

        vm.execute_now(fn_now);

        TEST_ASSERT(vm.reg_d(0) == 0x3333, "D0 belongs to the parked session again");
        TEST_ASSERT(vm.reg_d(1) == 0x1111, "so does D1");
        TEST_ASSERT(vm.reg_d(2) == 0x2222, "so does D2");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_immediate_runs_on_a_stack_of_its_own)
    {
        // Both start at the top of their stack and push the same first frame, so
        // a shared one would put the two locals at the same address. Slot 0 is
        // reserved for this and no session may take it.
        bytecode_builder bc;

        u16 const fn_session = bc.begin_func();
        imm_mov_d0(bc, 0x1234);
        bc.emit(noir_op::PUSH);
        bc.emit_mode(0, noir_mode::REG_D0);
        bc.emit(noir_op::WAIT);
        bc.emit_u16(1);
        bc.emit(noir_op::POP);
        bc.emit_mode(noir_mode::REG_D0, 0);
        bc.emit(noir_op::HOTSPOT_ON);
        bc.emit(noir_op::RET);

        u16 const fn_filler = bc.begin_func();
        for (u8 i = 0; i < 3; ++i)
        {
            bc.emit(noir_op::PUSH);
            bc.emit_mode(0, noir_mode::IMM16);
            bc.emit_u16(0xFFFF);
        }
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm(64);
        vm.execute(fn_session);
        vm.update();

        vm.execute_now(fn_filler);

        for (u8 i = 0; i < 4; ++i) vm.update();

        TEST_ASSERT(g_mock_log.hotspot_on_id == 0x1234, "the session got its own local back");
        TEST_SUCCESS;
    }

    // ── It will not run on top of anything ───────────────────────────────────────

    TEST_IMPL(test_immediate_refuses_to_run_inside_an_update)
    {
        // A bridge function is inside an opcode, and the window it would need is
        // the one that opcode is running out of.
        imm_reset_reentry();

        bytecode_builder bc;

        u16 const fn_session = bc.begin_func();
        imm_mov_d0(bc, 1);
        bc.emit(noir_op::PLAY_MUSIC);
        bc.emit(noir_op::RET);

        u16 const fn_now = bc.begin_func();
        imm_mov_d0(bc, 99);
        bc.emit(noir_op::RET);

        noir_bridge bridge = g_mock_bridge;
        bridge.play_music  = {imm_reentrant_music};

        auto vm   = bc.build_vm(256, &bridge);
        s_vm      = &vm;
        s_target  = fn_now;

        vm.update();
        vm.execute(fn_session);
        vm.update();

        bool const tried   = s_tried;
        bool const refused = s_refused;
        auto const reason  = s_error;
        imm_reset_reentry();

        TEST_ASSERT(tried, "the bridge function did reach for it");
        TEST_ASSERT(refused, "and was turned down");
        TEST_ASSERT(reason == noir_error::nested_immediate, "for re-entering");
        TEST_ASSERT(nothing_reported(), "and nothing died, so the player hears nothing");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_immediate_refuses_to_nest)
    {
        imm_reset_reentry();

        bytecode_builder bc;

        u16 const fn_outer = bc.begin_func();
        imm_mov_d0(bc, 1);
        bc.emit(noir_op::PLAY_MUSIC);
        imm_mov_d0(bc, 55);
        bc.emit(noir_op::RET);

        u16 const fn_inner = bc.begin_func();
        imm_mov_d0(bc, 99);
        bc.emit(noir_op::RET);

        noir_bridge bridge = g_mock_bridge;
        bridge.play_music  = {imm_reentrant_music};

        auto vm  = bc.build_vm(256, &bridge);
        s_vm     = &vm;
        s_target = fn_inner;

        auto const r = vm.execute_now(fn_outer);

        bool const tried   = s_tried;
        bool const refused = s_refused;
        auto const reason  = s_error;
        imm_reset_reentry();

        TEST_ASSERT(tried, "the bridge function did reach for it");
        TEST_ASSERT(refused, "and was turned down");
        TEST_ASSERT(reason == noir_error::nested_immediate, "for nesting");
        TEST_ASSERT(r == i16(55), "the outer script carried on regardless");
        TEST_ASSERT(nothing_reported(), "and nothing died, so the player hears nothing");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_immediate_refuses_an_unknown_script)
    {
        bytecode_builder bc;
        bc.begin_func();
        bc.emit(noir_op::RET);

        auto vm      = bc.build_vm();
        auto const r = vm.execute_now(99);

        TEST_ASSERT(failed_with(r, noir_error::invalid_func), "there is no script 99");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_immediate_dies_when_its_first_frame_will_not_fit)
    {
        bytecode_builder bc;
        u16 fn = bc.begin_func();
        imm_mov_d0(bc, 1);
        bc.emit(noir_op::RET);

        auto vm      = bc.build_vm(4);
        auto const r = vm.execute_now(fn);

        TEST_ASSERT(failed_with(r, noir_error::stack_overflow), "eight bytes will not fit in four");
        TEST_SUCCESS;
    }

    // ── The cap ──────────────────────────────────────────────────────────────────

    TEST_IMPL(test_immediate_kills_a_runaway)
    {
        // Nothing here can ever end, and nothing outside is going to draw a frame
        // to notice. The cap is the only way out.
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        u16 const jmp = bc.mark();
        bc.emit(noir_op::JMP);
        bc.emit_s16(to<i16>(jmp - (jmp + 3)));

        auto vm = bc.build_vm();
        vm.set_immediate_cap(50);

        auto const r = vm.execute_now(fn);

        TEST_ASSERT(failed_with(r, noir_error::runaway), "the cap stopped it");
        TEST_ASSERT(message_is("script 0: ran past 50 instructions"), "and said so");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_immediate_lets_a_script_spend_the_whole_cap)
    {
        // One under the cap is not a runaway. The loop below is four instructions
        // a turn plus two to set up and one to return.
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        imm_mov(bc, noir_mode::REG_D1, 3);
        imm_mov_d0(bc, 0);

        u16 const top = bc.mark();
        bc.emit(noir_op::INC);
        bc.emit_mode(noir_mode::REG_D0, 0);
        bc.emit(noir_op::DEC);
        bc.emit_mode(noir_mode::REG_D1, 0);
        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(0);

        u16 const bne = bc.mark();
        bc.emit(noir_op::BNE);
        bc.emit_s16(to<i16>(top - (bne + 3)));

        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.set_immediate_cap(15);

        auto const r = vm.execute_now(fn);

        TEST_ASSERT(r == i16(3), "fifteen instructions were exactly enough");
        TEST_SUCCESS;
    }

    // ── WAIT ─────────────────────────────────────────────────────────────────────

    TEST_IMPL(test_immediate_ignores_wait)
    {
        // No frame passes until this returns, so a wait here would never end. The
        // compiler bans it; the VM steps over one that got through.
        bytecode_builder bc;
        u16 fn = bc.begin_func();
        bc.emit(noir_op::WAIT);
        bc.emit_u16(100);
        imm_mov_d0(bc, 7);
        bc.emit(noir_op::RET);

        auto vm      = bc.build_vm();
        auto const r = vm.execute_now(fn);

        TEST_ASSERT(r == i16(7), "the script ran straight through the WAIT");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_wait_still_parks_a_session)
    {
        // The step over it belongs to the immediate context and nowhere else.
        bytecode_builder bc;
        u16 fn = bc.begin_func();
        bc.emit(noir_op::WAIT);
        bc.emit_u16(2);
        imm_mov_d0(bc, 7);
        bc.emit(noir_op::HOTSPOT_ON);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);

        vm.update();
        TEST_ASSERT(g_mock_log.hotspot_on_id != 7, "the session parked on the WAIT");

        for (u8 i = 0; i < 4; ++i) vm.update();
        TEST_ASSERT(g_mock_log.hotspot_on_id == 7, "and carried on once it aged out");
        TEST_SUCCESS;
    }

    // ── Modal calls ──────────────────────────────────────────────────────────────

    TEST_IMPL(test_immediate_kills_a_modal_call)
    {
        bytecode_builder bc;
        u16 fn = bc.begin_func();
        imm_mov_d0(bc, 12);
        bc.emit(noir_op::SAY);
        bc.emit(noir_op::RET);

        auto vm      = bc.build_vm();
        auto const r = vm.execute_now(fn);

        TEST_ASSERT(failed_with(r, noir_error::immediate_modal), "a modal cannot run here");
        TEST_ASSERT(message_is("script 0: a modal call under execute_now"), "and said so");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_immediate_records_no_modal_request)
    {
        // The point of turning it down before the call: a request recorded here
        // would be acted on next frame, by a scene that never asked for it.
        bytecode_builder bc;
        u16 fn = bc.begin_func();
        imm_mov_d0(bc, 12);
        bc.emit(noir_op::SAY);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute_now(fn);

        TEST_ASSERT(!g_mock_log.say_called, "nothing was recorded");
        TEST_ASSERT(g_mock_log.say_string_id == 0, "not even the string id");
        TEST_SUCCESS;
    }

    // ── It runs while the VM is blocked ──────────────────────────────────────────

    TEST_IMPL(test_immediate_runs_while_the_vm_is_blocked)
    {
        // The case a dialogue choice depends on: the box is up, the VM is
        // latched, and a script still has to run to answer what the player
        // picked. If it could not, no choice could ever act until the box came
        // down on its own first.
        bytecode_builder bc;

        u16 const fn_say = bc.begin_func();
        imm_mov_d0(bc, 1);
        bc.emit(noir_op::SAY);
        bc.emit(noir_op::RET);

        u16 const fn_now = bc.begin_func();
        imm_mov_d0(bc, 42);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn_say);
        vm.update();
        TEST_ASSERT(vm.blocked(), "the SAY latched the VM");

        auto const r = vm.execute_now(fn_now);

        TEST_ASSERT(r == i16(42), "the script ran to its RET regardless");
        TEST_ASSERT(vm.blocked(), "and the box is still up when it is done");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_a_death_here_does_not_lift_the_modal_latch)
    {
        // The dialogue is still on screen; the choice script it runs dies.
        // Losing the latch here would let the parked sessions carry on behind
        // a box that has not come down.
        bytecode_builder bc;

        u16 const fn_say = bc.begin_func();
        imm_mov_d0(bc, 1);
        bc.emit(noir_op::SAY);
        bc.emit(noir_op::RET);

        u16 const fn_dies = bc.begin_func();
        bc.emit(noir_op::HALT);

        auto vm = bc.build_vm();
        vm.execute(fn_say);
        vm.update();
        TEST_ASSERT(vm.blocked(), "the SAY latched the VM");

        auto const r = vm.execute_now(fn_dies);

        TEST_ASSERT(failed_with(r, noir_error::halt), "the immediate script died");
        TEST_ASSERT(vm.blocked(), "and the box is still owed an answer");
        TEST_SUCCESS;
    }

    // ── Terminal calls ───────────────────────────────────────────────────────────

    TEST_IMPL(test_immediate_allows_a_terminal_call)
    {
        // A conversation choice may leave the room. The request is recorded now
        // and the scene tears down at the top of the next frame.
        bytecode_builder bc;
        u16 fn = bc.begin_func();
        imm_mov_d0(bc, 5);
        bc.emit(noir_op::GOTO_SCENE);
        imm_mov_d0(bc, 99);
        bc.emit(noir_op::RET);

        auto vm      = bc.build_vm();
        auto const r = vm.execute_now(fn);

        TEST_ASSERT(g_mock_log.goto_scene_called, "the scene change was recorded");
        TEST_ASSERT(g_mock_log.goto_scene_id == 5, "for scene 5");
        TEST_ASSERT(vm.terminated(), "and the VM is owed a teardown");
        TEST_ASSERT(r == i16(5), "D0 came back as it stood");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_a_death_here_does_not_lift_the_teardown_latch)
    {
        // A session asked to leave the scene; the dialogue still on screen then
        // runs a script that dies. Losing the latch would let the VM go back to
        // running sessions in a scene that is over.
        bytecode_builder bc;

        u16 const fn_leaving = bc.begin_func();
        imm_mov_d0(bc, 5);
        bc.emit(noir_op::GOTO_SCENE);
        bc.emit(noir_op::RET);

        u16 const fn_dies = bc.begin_func();
        bc.emit(noir_op::HALT);

        auto vm = bc.build_vm();
        vm.execute(fn_leaving);
        vm.update();
        TEST_ASSERT(vm.terminated(), "the terminal call latched");

        auto const r = vm.execute_now(fn_dies);

        TEST_ASSERT(failed_with(r, noir_error::halt), "the immediate script died");
        TEST_ASSERT(vm.terminated(), "and the teardown is still owed");
        TEST_SUCCESS;
    }

    // ═════════════════════════════════════════════════════════════════════════════

    TEST_SUITE_BEGIN(noir_vm_immediate)
        TEST(test_immediate_hands_back_d0)
        TEST(test_immediate_hands_back_a_negative_d0)
        TEST(test_immediate_runs_past_a_session_budget)
        TEST(test_immediate_makes_immediate_bridge_calls)
        TEST(test_immediate_does_not_join_the_run_list)
        TEST(test_immediate_leaves_the_register_window_as_it_found_it)
        TEST(test_immediate_runs_on_a_stack_of_its_own)
        TEST(test_immediate_refuses_to_run_inside_an_update)
        TEST(test_immediate_refuses_to_nest)
        TEST(test_immediate_refuses_an_unknown_script)
        TEST(test_immediate_dies_when_its_first_frame_will_not_fit)
        TEST(test_immediate_kills_a_runaway)
        TEST(test_immediate_lets_a_script_spend_the_whole_cap)
        TEST(test_immediate_ignores_wait)
        TEST(test_wait_still_parks_a_session)
        TEST(test_immediate_kills_a_modal_call)
        TEST(test_immediate_records_no_modal_request)
        TEST(test_immediate_runs_while_the_vm_is_blocked)
        TEST(test_a_death_here_does_not_lift_the_modal_latch)
        TEST(test_immediate_allows_a_terminal_call)
        TEST(test_a_death_here_does_not_lift_the_teardown_latch)
    TEST_SUITE_END

}  // namespace NEONengine

#endif  // ACE_TEST_RUNNER
#endif  // __NOIR_VM_IMMEDIATE_TESTS_H__INCLUDED__
