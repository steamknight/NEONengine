#ifndef __NOIR_VM_DIALOGUE_TESTS_H__INCLUDED__
#define __NOIR_VM_DIALOGUE_TESTS_H__INCLUDED__

#ifdef ACE_TEST_RUNNER

#include "types.h"

#include "tests/noir_vm_test_utils.h"

namespace NEONengine
{
    // ═════════════════════════════════════════════════════════════════════════════
    // Game Calls — Dialogue / Audio opcodes (0xB0–0xBF)
    //   SAY, SHOW_DIALOG, SHOW_TEXT, CLEAR_TEXT, PLAY_MUSIC, STOP_MUSIC, PLAY_SFX
    //
    // All dialogue/audio opcodes are 1-byte (opcode only). Arguments in D0–D2.
    // Bridge function pointers are called.
    //
    // SAY         (0xB0): D0=string_id, D1=speaker_id → bridge->say(D0,D1) — deferred modal
    // SHOW_DIALOG (0xB1): D0=dialogue_id → bridge->show_dialog(D0)         — deferred modal
    // SHOW_TEXT   (0xB2): D0=string_id, D1=x, D2=y → bridge->show_text     — immediate
    // CLEAR_TEXT  (0xB3): bridge->clear_text()                              — immediate
    // PLAY_MUSIC  (0xB4): D0=music_id → bridge->play_music(D0)             — immediate
    // STOP_MUSIC  (0xB5): bridge->stop_music()                              — immediate
    // PLAY_SFX    (0xB6): D0=sfx_id → bridge->play_sfx(D0)                 — immediate
    // 0xB7–0xBF: Reserved → err_invalid_opcode
    //
    // SR: All dialogue/audio opcodes leave SR unchanged.
    // Registers: D0–D3 preserved (no opcode writes to registers).
    // ═════════════════════════════════════════════════════════════════════════════

    // ── SAY ─────────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_say_basic)
    {
        // SAY with D0=10 (string), D1=3 (speaker)
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(3);

        bc.emit(noir_op::SAY);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);
        vm.update();

        TEST_ASSERT(g_mock_log.say_called, "say bridge called");
        TEST_ASSERT(g_mock_log.say_string_id == 10, "say received string_id 10");
        TEST_ASSERT(g_mock_log.say_speaker_id == 3, "say received speaker_id 3");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_say_parks_the_session)
    {
        // SAY is deferred: the walk ends on it and the RET waits for the next update
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(2);

        bc.emit(noir_op::SAY);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);
        auto status = vm.update();

        TEST_ASSERT(vm.session_count() == 1, "parked at the deferred call");
        TEST_ASSERT(status == noir_result::blocked, "the modal call latches the VM");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_say_one_line_per_update)
    {
        // The spec's headline case. Two SAYs in a row must not both run in one
        // slice, or the player would only ever see the second line. Draining the
        // pool is what puts one update between them.
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(12);
        bc.emit(noir_op::SAY);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(13);
        bc.emit(noir_op::SAY);

        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        g_mock_log.reset();
        vm.execute(fn);

        vm.update();
        TEST_ASSERT(g_mock_log.say_string_id == 12, "the first line went out");

        vm.resume_execution();
        vm.update();
        TEST_ASSERT(g_mock_log.say_string_id == 13, "the second line waited a whole update");

        vm.resume_execution();
        auto last = vm.update();
        TEST_ASSERT(last == noir_result::completed, "then the script ran out");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_say_resumes_next_update)
    {
        // The session resumes on the next update with no help from the engine
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(20);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(4);

        bc.emit(noir_op::SAY);

        // Runs in the update after the SAY
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(999);

        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);
        vm.update();

        TEST_ASSERT(vm.session_count() == 1, "parked at SAY");

        vm.resume_execution();
        auto status = vm.update();

        TEST_ASSERT(status == noir_result::completed, "completes on the next update");
        TEST_ASSERT(vm.exit_code() == 999, "code after SAY executed");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_say_preserves_d0_d1)
    {
        // D0 and D1 should be preserved across the park
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(42);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(7);

        bc.emit(noir_op::SAY);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);
        vm.update();

        vm.resume_execution();
        vm.update();

        TEST_ASSERT(vm.reg_d(0) == 42, "D0 preserved across SAY block");
        TEST_ASSERT(vm.reg_d(1) == 7, "D1 preserved across SAY block");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_say_preserves_all_regs)
    {
        // All data registers preserved across SAY
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(11);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(22);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D2, noir_mode::IMM16);
        bc.emit_u16(33);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D3, noir_mode::IMM16);
        bc.emit_u16(44);

        bc.emit(noir_op::SAY);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);
        vm.update();

        vm.resume_execution();
        vm.update();

        TEST_ASSERT(vm.reg_d(0) == 11, "D0 preserved");
        TEST_ASSERT(vm.reg_d(1) == 22, "D1 preserved");
        TEST_ASSERT(vm.reg_d(2) == 33, "D2 preserved");
        TEST_ASSERT(vm.reg_d(3) == 44, "D3 preserved");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_say_preserves_sr)
    {
        // SR preserved across SAY
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::CLR);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D0);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);  // 0 - 1 = -1, sets N

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(5);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(1);

        bc.emit(noir_op::SAY);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);
        vm.update();

        vm.resume_execution();
        vm.update();

        TEST_ASSERT((vm.sr() & noir_sr::N) != 0, "N flag preserved across SAY block");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_say_zero_args)
    {
        // SAY with D0=0, D1=0
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::CLR);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D0);

        bc.emit(noir_op::CLR);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::REG_D1);

        bc.emit(noir_op::SAY);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);
        vm.update();

        TEST_ASSERT(g_mock_log.say_called, "say called");
        TEST_ASSERT(g_mock_log.say_string_id == 0, "say received string_id 0");
        TEST_ASSERT(g_mock_log.say_speaker_id == 0, "say received speaker_id 0");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_say_max_args)
    {
        // SAY with D0=0xFFFF, D1=0xFFFF
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0xFFFF);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(0xFFFF);

        bc.emit(noir_op::SAY);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);
        vm.update();

        TEST_ASSERT(g_mock_log.say_called, "say called");
        TEST_ASSERT(g_mock_log.say_string_id == 0xFFFF, "say received max string_id");
        TEST_ASSERT(g_mock_log.say_speaker_id == 0xFFFF, "say received max speaker_id");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_say_inside_call)
    {
        // SAY inside a called function defers, then returns correctly
        bytecode_builder bc;

        // fn0: main — calls fn1
        u16 fn0 = bc.begin_func();
        bc.emit(noir_op::CALL);
        bc.emit_u16(1);

        bc.emit(noir_op::RET);

        // fn1: does SAY then sets D0=77
        u16 fn1 = bc.begin_func();
        (void)fn1;
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(100);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(2);

        bc.emit(noir_op::SAY);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(77);

        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn0);
        vm.update();

        TEST_ASSERT(vm.session_count() == 1, "parked inside fn1");
        TEST_ASSERT(g_mock_log.say_string_id == 100, "say string_id 100");
        TEST_ASSERT(g_mock_log.say_speaker_id == 2, "say speaker_id 2");

        vm.resume_execution();
        auto status = vm.update();

        TEST_ASSERT(status == noir_result::completed, "completes on the next update");
        TEST_ASSERT(vm.exit_code() == 77, "D0=77 returned from fn1");
        TEST_SUCCESS;
    }

    // ── SHOW_DIALOG ─────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_show_dialog_basic)
    {
        // SHOW_DIALOG with D0=5
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(5);

        bc.emit(noir_op::SHOW_DIALOG);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);
        vm.update();

        TEST_ASSERT(g_mock_log.show_dialog_called, "show_dialog bridge called");
        TEST_ASSERT(g_mock_log.show_dialog_id == 5, "show_dialog received id 5");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_show_dialog_parks_the_session)
    {
        // SHOW_DIALOG is deferred: the walk ends on it, before the RET
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);

        bc.emit(noir_op::SHOW_DIALOG);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);
        auto status = vm.update();

        TEST_ASSERT(vm.session_count() == 1, "parked at the deferred call");
        TEST_ASSERT(status == noir_result::blocked, "the modal call latches the VM");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_show_dialog_ends_the_walk_for_everyone)
    {
        // A deferred call takes the rest of the frame pool with it, so a session
        // further down the run list gets nothing - even though the pool had
        // plenty left. Without that, a conversation would open while the script
        // behind it carried on rearranging the room.
        //
        // The second session marks itself on the bridge rather than in a
        // register: reg_d() reads the window, which holds whichever session went
        // last, so it cannot say whether a particular one ran.
        bytecode_builder bc;

        u16 fn_dialog = bc.begin_func();
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);
        bc.emit(noir_op::SHOW_DIALOG);
        bc.emit(noir_op::RET);

        u16 fn_other = bc.begin_func();
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(4242);
        bc.emit(noir_op::HOTSPOT_ON);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        g_mock_log.reset();
        vm.execute(fn_dialog);
        vm.execute(fn_other);

        vm.update();

        TEST_ASSERT(g_mock_log.hotspot_on_id != 4242, "the session behind it did not run");
        TEST_ASSERT(vm.session_count() == 2, "and it is still there to run later");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_show_dialog_resumes_next_update)
    {
        // The session resumes on the next update with no help from the engine
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::SHOW_DIALOG);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(888);

        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);
        vm.update();

        TEST_ASSERT(vm.session_count() == 1, "parked at SHOW_DIALOG");

        vm.resume_execution();
        auto status = vm.update();

        TEST_ASSERT(status == noir_result::completed, "completes on the next update");
        TEST_ASSERT(vm.exit_code() == 888, "code after SHOW_DIALOG executed");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_show_dialog_preserves_all_regs)
    {
        // All data registers preserved across SHOW_DIALOG
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(11);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(22);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D2, noir_mode::IMM16);
        bc.emit_u16(33);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D3, noir_mode::IMM16);
        bc.emit_u16(44);

        bc.emit(noir_op::SHOW_DIALOG);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);
        vm.update();

        vm.resume_execution();
        vm.update();

        TEST_ASSERT(vm.reg_d(0) == 11, "D0 preserved");
        TEST_ASSERT(vm.reg_d(1) == 22, "D1 preserved");
        TEST_ASSERT(vm.reg_d(2) == 33, "D2 preserved");
        TEST_ASSERT(vm.reg_d(3) == 44, "D3 preserved");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_show_dialog_preserves_sr)
    {
        // SR preserved across SHOW_DIALOG
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::CLR);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D0);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);  // sets N

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(5);

        bc.emit(noir_op::SHOW_DIALOG);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);
        vm.update();

        vm.resume_execution();
        vm.update();

        TEST_ASSERT((vm.sr() & noir_sr::N) != 0, "N flag preserved across block");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_show_dialog_zero_id)
    {
        // SHOW_DIALOG with D0=0
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::CLR);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D0);

        bc.emit(noir_op::SHOW_DIALOG);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);
        vm.update();

        TEST_ASSERT(g_mock_log.show_dialog_called, "show_dialog called");
        TEST_ASSERT(g_mock_log.show_dialog_id == 0, "show_dialog received id 0");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_show_dialog_max_id)
    {
        // SHOW_DIALOG with D0=0xFFFF
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0xFFFF);

        bc.emit(noir_op::SHOW_DIALOG);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);
        vm.update();

        TEST_ASSERT(g_mock_log.show_dialog_called, "show_dialog called");
        TEST_ASSERT(g_mock_log.show_dialog_id == 0xFFFF, "show_dialog received max id");
        TEST_SUCCESS;
    }

    // ── SHOW_TEXT ────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_show_text_basic)
    {
        // SHOW_TEXT with D0=string, D1=x, D2=y
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(100);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(50);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D2, noir_mode::IMM16);
        bc.emit_u16(75);

        bc.emit(noir_op::SHOW_TEXT);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(g_mock_log.show_text_id == 100, "show_text received string 100");
        TEST_ASSERT(g_mock_log.show_text_x == 50, "show_text received x 50");
        TEST_ASSERT(g_mock_log.show_text_y == 75, "show_text received y 75");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_show_text_does_not_block)
    {
        // SHOW_TEXT is immediate
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D2, noir_mode::IMM16);
        bc.emit_u16(20);

        bc.emit(noir_op::SHOW_TEXT);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(555);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete in one update");
        TEST_ASSERT(vm.exit_code() == 555, "execution continued past SHOW_TEXT");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_show_text_preserves_regs)
    {
        // All data registers preserved after SHOW_TEXT
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(2);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D2, noir_mode::IMM16);
        bc.emit_u16(3);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D3, noir_mode::IMM16);
        bc.emit_u16(4);

        bc.emit(noir_op::SHOW_TEXT);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.reg_d(0) == 1, "D0 preserved");
        TEST_ASSERT(vm.reg_d(1) == 2, "D1 preserved");
        TEST_ASSERT(vm.reg_d(2) == 3, "D2 preserved");
        TEST_ASSERT(vm.reg_d(3) == 4, "D3 preserved");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_show_text_preserves_sr)
    {
        // SR unchanged after SHOW_TEXT
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::CLR);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D0);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);  // sets N

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(5);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D2, noir_mode::IMM16);
        bc.emit_u16(20);

        bc.emit(noir_op::SHOW_TEXT);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT((vm.sr() & noir_sr::N) != 0, "N flag preserved");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_show_text_zero_coords)
    {
        // SHOW_TEXT at 0,0
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(42);

        bc.emit(noir_op::CLR);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::REG_D1);

        bc.emit(noir_op::CLR);
        bc.emit_mode(noir_mode::REG_D2, noir_mode::REG_D2);

        bc.emit(noir_op::SHOW_TEXT);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(g_mock_log.show_text_id == 42, "show_text string 42");
        TEST_ASSERT(g_mock_log.show_text_x == 0, "show_text x=0");
        TEST_ASSERT(g_mock_log.show_text_y == 0, "show_text y=0");
        TEST_SUCCESS;
    }

    // ── CLEAR_TEXT ───────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_clear_text_basic)
    {
        // CLEAR_TEXT calls bridge->clear_text()
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::CLEAR_TEXT);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(g_mock_log.clear_text_called, "clear_text bridge called");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_clear_text_does_not_block)
    {
        // CLEAR_TEXT is immediate
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::CLEAR_TEXT);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(444);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 444, "the session carried on past CLEAR_TEXT");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_clear_text_preserves_regs)
    {
        // All data registers preserved after CLEAR_TEXT
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(20);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D2, noir_mode::IMM16);
        bc.emit_u16(30);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D3, noir_mode::IMM16);
        bc.emit_u16(40);

        bc.emit(noir_op::CLEAR_TEXT);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.reg_d(0) == 10, "D0 preserved");
        TEST_ASSERT(vm.reg_d(1) == 20, "D1 preserved");
        TEST_ASSERT(vm.reg_d(2) == 30, "D2 preserved");
        TEST_ASSERT(vm.reg_d(3) == 40, "D3 preserved");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_clear_text_preserves_sr)
    {
        // SR unchanged after CLEAR_TEXT
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::CLR);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D0);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);  // sets N

        bc.emit(noir_op::CLEAR_TEXT);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT((vm.sr() & noir_sr::N) != 0, "N flag preserved");
        TEST_SUCCESS;
    }

    // ── PLAY_MUSIC ──────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_play_music_basic)
    {
        // PLAY_MUSIC with D0=3
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(3);

        bc.emit(noir_op::PLAY_MUSIC);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(g_mock_log.play_music_id == 3, "play_music received id 3");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_play_music_does_not_block)
    {
        // PLAY_MUSIC is immediate
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);

        bc.emit(noir_op::PLAY_MUSIC);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(777);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 777, "the session carried on past PLAY_MUSIC");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_play_music_preserves_d0)
    {
        // D0 preserved after PLAY_MUSIC
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(42);

        bc.emit(noir_op::PLAY_MUSIC);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 42, "D0 preserved");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_play_music_preserves_other_regs)
    {
        // D1–D3 preserved after PLAY_MUSIC
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(5);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(100);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D2, noir_mode::IMM16);
        bc.emit_u16(200);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D3, noir_mode::IMM16);
        bc.emit_u16(300);

        bc.emit(noir_op::PLAY_MUSIC);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.reg_d(1) == 100, "D1 preserved");
        TEST_ASSERT(vm.reg_d(2) == 200, "D2 preserved");
        TEST_ASSERT(vm.reg_d(3) == 300, "D3 preserved");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_play_music_preserves_sr)
    {
        // SR unchanged after PLAY_MUSIC
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::CLR);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D0);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(7);

        bc.emit(noir_op::PLAY_MUSIC);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT((vm.sr() & noir_sr::N) != 0, "N flag preserved");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_play_music_zero_id)
    {
        // PLAY_MUSIC with D0=0
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::CLR);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D0);

        bc.emit(noir_op::PLAY_MUSIC);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(g_mock_log.play_music_id == 0, "play_music received id 0");
        TEST_SUCCESS;
    }

    // ── STOP_MUSIC ──────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_stop_music_basic)
    {
        // STOP_MUSIC calls bridge->stop_music()
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::STOP_MUSIC);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(g_mock_log.stop_music_called, "stop_music bridge called");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_stop_music_does_not_block)
    {
        // STOP_MUSIC is immediate
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::STOP_MUSIC);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(333);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 333, "the session carried on past STOP_MUSIC");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_stop_music_preserves_regs)
    {
        // All data registers preserved after STOP_MUSIC
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(20);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D2, noir_mode::IMM16);
        bc.emit_u16(30);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D3, noir_mode::IMM16);
        bc.emit_u16(40);

        bc.emit(noir_op::STOP_MUSIC);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.reg_d(0) == 10, "D0 preserved");
        TEST_ASSERT(vm.reg_d(1) == 20, "D1 preserved");
        TEST_ASSERT(vm.reg_d(2) == 30, "D2 preserved");
        TEST_ASSERT(vm.reg_d(3) == 40, "D3 preserved");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_stop_music_preserves_sr)
    {
        // SR unchanged after STOP_MUSIC
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::CLR);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D0);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);

        bc.emit(noir_op::STOP_MUSIC);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT((vm.sr() & noir_sr::N) != 0, "N flag preserved");
        TEST_SUCCESS;
    }

    // ── PLAY_SFX ────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_play_sfx_basic)
    {
        // PLAY_SFX with D0=8
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(8);

        bc.emit(noir_op::PLAY_SFX);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(g_mock_log.play_sfx_id == 8, "play_sfx received id 8");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_play_sfx_does_not_block)
    {
        // PLAY_SFX is immediate
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);

        bc.emit(noir_op::PLAY_SFX);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(222);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 222, "the session carried on past PLAY_SFX");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_play_sfx_preserves_d0)
    {
        // D0 preserved after PLAY_SFX
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(55);

        bc.emit(noir_op::PLAY_SFX);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 55, "D0 preserved");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_play_sfx_preserves_other_regs)
    {
        // D1–D3 preserved after PLAY_SFX
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(111);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D2, noir_mode::IMM16);
        bc.emit_u16(222);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D3, noir_mode::IMM16);
        bc.emit_u16(333);

        bc.emit(noir_op::PLAY_SFX);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.reg_d(1) == 111, "D1 preserved");
        TEST_ASSERT(vm.reg_d(2) == 222, "D2 preserved");
        TEST_ASSERT(vm.reg_d(3) == 333, "D3 preserved");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_play_sfx_preserves_sr)
    {
        // SR unchanged after PLAY_SFX
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::CLR);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D0);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(2);

        bc.emit(noir_op::PLAY_SFX);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT((vm.sr() & noir_sr::N) != 0, "N flag preserved");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_play_sfx_zero_id)
    {
        // PLAY_SFX with D0=0
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::CLR);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D0);

        bc.emit(noir_op::PLAY_SFX);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(g_mock_log.play_sfx_id == 0, "play_sfx received id 0");
        TEST_SUCCESS;
    }

    // ── Combined patterns ───────────────────────────────────────────────────────

    TEST_IMPL(test_noir_show_text_then_clear)
    {
        // Common pattern: show text then clear it
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(50);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D2, noir_mode::IMM16);
        bc.emit_u16(20);

        bc.emit(noir_op::SHOW_TEXT);
        bc.emit(noir_op::CLEAR_TEXT);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(g_mock_log.show_text_id == 50, "show_text received string 50");
        TEST_ASSERT(g_mock_log.clear_text_called, "clear_text called after show_text");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_play_music_then_sfx)
    {
        // Start music then play a sound effect
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(7);

        bc.emit(noir_op::PLAY_MUSIC);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(15);

        bc.emit(noir_op::PLAY_SFX);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(g_mock_log.play_music_id == 7, "play_music(7)");
        TEST_ASSERT(g_mock_log.play_sfx_id == 15, "play_sfx(15)");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_play_music_then_stop)
    {
        // Start music then stop it
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(4);

        bc.emit(noir_op::PLAY_MUSIC);
        bc.emit(noir_op::STOP_MUSIC);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(g_mock_log.play_music_id == 4, "play_music(4)");
        TEST_ASSERT(g_mock_log.stop_music_called, "stop_music called");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_say_then_show_text)
    {
        // SAY defers; SHOW_TEXT runs in the update after it
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(2);

        bc.emit(noir_op::SAY);

        // Next update: show text at (30, 40)
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(99);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(30);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D2, noir_mode::IMM16);
        bc.emit_u16(40);

        bc.emit(noir_op::SHOW_TEXT);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);
        vm.update();

        TEST_ASSERT(vm.session_count() == 1, "parked at SAY");
        TEST_ASSERT(g_mock_log.say_called, "say called");

        vm.resume_execution();
        auto status = vm.update();

        TEST_ASSERT(status == noir_result::completed, "completes on the next update");
        TEST_ASSERT(g_mock_log.show_text_id == 99, "show_text string 99");
        TEST_ASSERT(g_mock_log.show_text_x == 30, "show_text x=30");
        TEST_ASSERT(g_mock_log.show_text_y == 40, "show_text y=40");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_sequential_say_then_show_dialog)
    {
        // Two deferred calls in sequence: SAY then SHOW_DIALOG, one update each
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(2);

        bc.emit(noir_op::SAY);

        // After the SAY's update, load dialog id and SHOW_DIALOG
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(50);

        bc.emit(noir_op::SHOW_DIALOG);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(666);

        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);
        vm.update();

        TEST_ASSERT(vm.session_count() == 1, "parked at SAY");
        TEST_ASSERT(g_mock_log.say_called, "say called");

        // Second update — should then defer on SHOW_DIALOG
        vm.resume_execution();
        vm.update();

        TEST_ASSERT(vm.session_count() == 1, "parked at SHOW_DIALOG");
        TEST_ASSERT(g_mock_log.show_dialog_called, "show_dialog called");
        TEST_ASSERT(g_mock_log.show_dialog_id == 50, "show_dialog(50)");

        // Third update — should complete
        vm.resume_execution();
        auto status = vm.update();

        TEST_ASSERT(status == noir_result::completed, "completes once both have had their update");
        TEST_ASSERT(vm.exit_code() == 666, "final D0=666");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_full_dialogue_sequence)
    {
        // Full cutscene pattern: play_music → say → show_text → clear_text → stop_music
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        // Start music
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(3);
        bc.emit(noir_op::PLAY_MUSIC);

        // Play SFX
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(12);
        bc.emit(noir_op::PLAY_SFX);

        // SAY (deferred)
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(100);
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(5);
        bc.emit(noir_op::SAY);

        // After SAY: show_text
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(200);
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(10);
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D2, noir_mode::IMM16);
        bc.emit_u16(20);
        bc.emit(noir_op::SHOW_TEXT);

        // clear_text + stop_music
        bc.emit(noir_op::CLEAR_TEXT);
        bc.emit(noir_op::STOP_MUSIC);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);
        vm.update();

        // Should be parked on the opcode after SAY
        TEST_ASSERT(vm.session_count() == 1, "parked at SAY");
        TEST_ASSERT(g_mock_log.play_music_id == 3, "play_music(3) called before SAY");
        TEST_ASSERT(g_mock_log.play_sfx_id == 12, "play_sfx(12) called before SAY");
        TEST_ASSERT(g_mock_log.say_called, "say called");

        // Next update — rest should run to completion
        vm.resume_execution();
        auto status = vm.update();

        TEST_ASSERT(status == noir_result::completed, "completes in the update after the SAY");
        TEST_ASSERT(g_mock_log.show_text_id == 200, "show_text(200)");
        TEST_ASSERT(g_mock_log.clear_text_called, "clear_text called");
        TEST_ASSERT(g_mock_log.stop_music_called, "stop_music called");
        TEST_SUCCESS;
    }

    // ── Invalid opcodes ─────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_dialogue_invalid_0xB7)
    {
        // 0xB7 is not a valid dialogue/audio opcode
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(0xB7);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(failed_with(status, noir_error::invalid_opcode), "0xB7 is invalid");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_dialogue_invalid_0xBF)
    {
        // 0xBF is not a valid dialogue/audio opcode
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(0xBF);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(failed_with(status, noir_error::invalid_opcode), "0xBF is invalid");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_dialogue_invalid_0xBA)
    {
        // mid-range invalid opcode
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(0xBA);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(failed_with(status, noir_error::invalid_opcode), "0xBA is invalid");
        TEST_SUCCESS;
    }

    // ══════════════════════════════════════════════════════════════════════════════
    // Test suite registration
    // ══════════════════════════════════════════════════════════════════════════════

    constexpr char const* noir_vm_dialogue_name = "NoirVM Dialogue/Audio (0xB_)";

    Test noir_vm_dialogue_tests[] = {
        // SAY
        { "say basic", test_noir_say_basic },
        { "say parks the session", test_noir_say_parks_the_session },
        { "say: one line per update", test_noir_say_one_line_per_update },
        { "say resumes", test_noir_say_resumes_next_update },
        { "say preserves D0,D1", test_noir_say_preserves_d0_d1 },
        { "say preserves all regs", test_noir_say_preserves_all_regs },
        { "say preserves SR", test_noir_say_preserves_sr },
        { "say zero args", test_noir_say_zero_args },
        { "say max args", test_noir_say_max_args },
        { "say inside CALL", test_noir_say_inside_call },

        // SHOW_DIALOG
        { "show_dialog basic", test_noir_show_dialog_basic },
        { "show_dialog parks the session", test_noir_show_dialog_parks_the_session },
        { "show_dialog ends the walk", test_noir_show_dialog_ends_the_walk_for_everyone },
        { "show_dialog resumes", test_noir_show_dialog_resumes_next_update },
        { "show_dialog preserves regs", test_noir_show_dialog_preserves_all_regs },
        { "show_dialog preserves SR", test_noir_show_dialog_preserves_sr },
        { "show_dialog zero id", test_noir_show_dialog_zero_id },
        { "show_dialog max id", test_noir_show_dialog_max_id },

        // SHOW_TEXT
        { "show_text basic", test_noir_show_text_basic },
        { "show_text does not block", test_noir_show_text_does_not_block },
        { "show_text preserves regs", test_noir_show_text_preserves_regs },
        { "show_text preserves SR", test_noir_show_text_preserves_sr },
        { "show_text zero coords", test_noir_show_text_zero_coords },

        // CLEAR_TEXT
        { "clear_text basic", test_noir_clear_text_basic },
        { "clear_text does not block", test_noir_clear_text_does_not_block },
        { "clear_text preserves regs", test_noir_clear_text_preserves_regs },
        { "clear_text preserves SR", test_noir_clear_text_preserves_sr },

        // PLAY_MUSIC
        { "play_music basic", test_noir_play_music_basic },
        { "play_music does not block", test_noir_play_music_does_not_block },
        { "play_music preserves D0", test_noir_play_music_preserves_d0 },
        { "play_music preserves regs", test_noir_play_music_preserves_other_regs },
        { "play_music preserves SR", test_noir_play_music_preserves_sr },
        { "play_music zero id", test_noir_play_music_zero_id },

        // STOP_MUSIC
        { "stop_music basic", test_noir_stop_music_basic },
        { "stop_music does not block", test_noir_stop_music_does_not_block },
        { "stop_music preserves regs", test_noir_stop_music_preserves_regs },
        { "stop_music preserves SR", test_noir_stop_music_preserves_sr },

        // PLAY_SFX
        { "play_sfx basic", test_noir_play_sfx_basic },
        { "play_sfx does not block", test_noir_play_sfx_does_not_block },
        { "play_sfx preserves D0", test_noir_play_sfx_preserves_d0 },
        { "play_sfx preserves regs", test_noir_play_sfx_preserves_other_regs },
        { "play_sfx preserves SR", test_noir_play_sfx_preserves_sr },
        { "play_sfx zero id", test_noir_play_sfx_zero_id },

        // Combined patterns
        { "show_text then clear", test_noir_show_text_then_clear },
        { "play_music then sfx", test_noir_play_music_then_sfx },
        { "play_music then stop", test_noir_play_music_then_stop },
        { "say then show_text", test_noir_say_then_show_text },
        { "say then show_dialog", test_noir_sequential_say_then_show_dialog },
        { "full dialogue sequence", test_noir_full_dialogue_sequence },

        // Invalid opcodes
        { "invalid 0xB7", test_noir_dialogue_invalid_0xB7 },
        { "invalid 0xBF", test_noir_dialogue_invalid_0xBF },
        { "invalid 0xBA", test_noir_dialogue_invalid_0xBA },
    };

}  // namespace NEONengine

#endif  // ACE_TEST_RUNNER
#endif  // __NOIR_VM_DIALOGUE_TESTS_H__INCLUDED__
