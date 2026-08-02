#ifndef __NOIR_VM_MENU_TESTS_H__INCLUDED__
#define __NOIR_VM_MENU_TESTS_H__INCLUDED__

#ifdef ACE_TEST_RUNNER

#include "types.h"

#include "tests/noir_vm_test_utils.h"

namespace NEONengine
{
    // ═════════════════════════════════════════════════════════════════════════════
    // Game Calls — Menu / Settings opcodes (0xC0–0xCF)
    //   FADE_OUT, FADE_IN, SCREEN_CLR, SET_LANG, SAVE_GAME, LOAD_GAME,
    //   NEW_GAME, QUIT_GAME
    //
    // All menu/settings opcodes are 1-byte (opcode only). Arguments in D0.
    // Bridge function pointers are called.
    //
    // FADE_OUT    (0xC0): D0=frames → bridge->fade_out(D0)     — deferred modal
    // FADE_IN     (0xC1): D0=frames → bridge->fade_in(D0)      — deferred modal
    // SCREEN_CLR  (0xC2): D0=color  → bridge->screen_clear(D0) — immediate
    // SET_LANG    (0xC3): D0=lang_id → bridge->set_lang(D0)    — immediate
    // SAVE_GAME   (0xC4): D0=slot   → bridge->save_game(D0)    — deferred modal
    // LOAD_GAME   (0xC5): D0=slot   → bridge->load_game(D0)    — deferred terminal
    // NEW_GAME    (0xC6): bridge->new_game()                   — deferred terminal
    // QUIT_GAME   (0xC7): bridge->quit_game()                  — deferred terminal
    // 0xC8–0xCF: Reserved → err_invalid_opcode
    //
    // SR: All menu/settings opcodes leave SR unchanged.
    // Registers: D0–D3 preserved (no opcode writes to registers).
    // ═════════════════════════════════════════════════════════════════════════════

    // ── FADE_OUT ────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_fade_out_basic)
    {
        // FADE_OUT with D0=25 (frames)
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(25);

        bc.emit(noir_op::FADE_OUT);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);
        vm.update();

        TEST_ASSERT(g_mock_log.fade_out_called, "fade_out bridge called");
        TEST_ASSERT(g_mock_log.fade_out_frames == 25, "fade_out received 25 frames");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_fade_out_parks_the_session)
    {
        // FADE_OUT is deferred: the walk ends on it, before the RET
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::FADE_OUT);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);
        auto status = vm.update();

        TEST_ASSERT(vm.session_count() == 1, "parked at the deferred call");
        TEST_ASSERT(status == noir_result::yielded, "sessions remain, so the update yields");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_fade_out_then_goto_scene_take_an_update_each)
    {
        // The pattern every scene transition is written in. Each call ends its
        // own update, so the scene change cannot be recorded in the same one the
        // fade was asked for - which is what stops a scene tearing down while
        // its own fade-out is still running.
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(30);
        bc.emit(noir_op::FADE_OUT);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(4);
        bc.emit(noir_op::GOTO_SCENE);

        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        g_mock_log.reset();
        vm.execute(fn);

        vm.update();
        TEST_ASSERT(g_mock_log.fade_out_called, "the fade was asked for");
        TEST_ASSERT(!g_mock_log.goto_scene_called, "and the scene change was not");

        vm.update();
        TEST_ASSERT(g_mock_log.goto_scene_called, "which waited for its own update");
        TEST_ASSERT(g_mock_log.goto_scene_id == 4, "and carried the right scene");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_fade_out_resumes_next_update)
    {
        // The session resumes on the next update with no help from the engine
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(15);

        bc.emit(noir_op::FADE_OUT);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(999);

        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);
        vm.update();

        TEST_ASSERT(vm.session_count() == 1, "parked at FADE_OUT");

        auto status = vm.update();

        TEST_ASSERT(status == noir_result::completed, "completes on the next update");
        TEST_ASSERT(vm.exit_code() == 999, "code after FADE_OUT executed");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_fade_out_preserves_all_regs)
    {
        // All data registers preserved across FADE_OUT
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

        bc.emit(noir_op::FADE_OUT);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);
        vm.update();

        vm.update();

        TEST_ASSERT(vm.reg_d(0) == 11, "D0 preserved");
        TEST_ASSERT(vm.reg_d(1) == 22, "D1 preserved");
        TEST_ASSERT(vm.reg_d(2) == 33, "D2 preserved");
        TEST_ASSERT(vm.reg_d(3) == 44, "D3 preserved");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_fade_out_preserves_sr)
    {
        // SR preserved across FADE_OUT
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::CLR);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D0);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);  // sets N

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(20);

        bc.emit(noir_op::FADE_OUT);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);
        vm.update();

        vm.update();

        TEST_ASSERT((vm.sr() & noir_sr::N) != 0, "N flag preserved across block");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_fade_out_zero_frames)
    {
        // FADE_OUT with D0=0
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::CLR);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D0);

        bc.emit(noir_op::FADE_OUT);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);
        vm.update();

        TEST_ASSERT(g_mock_log.fade_out_called, "fade_out called");
        TEST_ASSERT(g_mock_log.fade_out_frames == 0, "fade_out received 0 frames");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_fade_out_max_frames)
    {
        // FADE_OUT with D0=0xFFFF
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0xFFFF);

        bc.emit(noir_op::FADE_OUT);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);
        vm.update();

        TEST_ASSERT(g_mock_log.fade_out_called, "fade_out called");
        TEST_ASSERT(g_mock_log.fade_out_frames == 0xFFFF, "fade_out received max frames");
        TEST_SUCCESS;
    }

    // ── FADE_IN ─────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_fade_in_basic)
    {
        // FADE_IN with D0=25
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(25);

        bc.emit(noir_op::FADE_IN);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);
        vm.update();

        TEST_ASSERT(g_mock_log.fade_in_called, "fade_in bridge called");
        TEST_ASSERT(g_mock_log.fade_in_frames == 25, "fade_in received 25 frames");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_fade_in_parks_the_session)
    {
        // FADE_IN is deferred: the walk ends on it, before the RET
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::FADE_IN);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);
        auto status = vm.update();

        TEST_ASSERT(vm.session_count() == 1, "parked at the deferred call");
        TEST_ASSERT(status == noir_result::yielded, "sessions remain, so the update yields");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_fade_in_leaves_the_pool_empty)
    {
        // The pool is drained, not merely nudged: raising it changes nothing,
        // because the walk ends on the deferred call whatever room was left.
        //
        // The second session marks itself on the bridge rather than in a
        // register: reg_d() reads the window, which holds whichever session went
        // last, so it cannot say whether a particular one ran.
        bytecode_builder bc;

        u16 fn_fade = bc.begin_func();
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(20);
        bc.emit(noir_op::FADE_IN);
        bc.emit(noir_op::RET);

        u16 fn_other = bc.begin_func();
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(555);
        bc.emit(noir_op::HOTSPOT_ON);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.set_frame_pool(400);
        g_mock_log.reset();
        vm.execute(fn_fade);
        vm.execute(fn_other);

        vm.update();
        TEST_ASSERT(g_mock_log.hotspot_on_id != 555, "the pool had 398 left and still nobody ran");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_fade_in_resumes_next_update)
    {
        // The session resumes on the next update with no help from the engine
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(15);

        bc.emit(noir_op::FADE_IN);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(888);

        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);
        vm.update();

        TEST_ASSERT(vm.session_count() == 1, "parked at FADE_IN");

        auto status = vm.update();

        TEST_ASSERT(status == noir_result::completed, "completes on the next update");
        TEST_ASSERT(vm.exit_code() == 888, "code after FADE_IN executed");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_fade_in_preserves_all_regs)
    {
        // All data registers preserved across FADE_IN
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(55);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(66);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D2, noir_mode::IMM16);
        bc.emit_u16(77);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D3, noir_mode::IMM16);
        bc.emit_u16(88);

        bc.emit(noir_op::FADE_IN);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);
        vm.update();

        vm.update();

        TEST_ASSERT(vm.reg_d(0) == 55, "D0 preserved");
        TEST_ASSERT(vm.reg_d(1) == 66, "D1 preserved");
        TEST_ASSERT(vm.reg_d(2) == 77, "D2 preserved");
        TEST_ASSERT(vm.reg_d(3) == 88, "D3 preserved");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_fade_in_preserves_sr)
    {
        // SR preserved across FADE_IN
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::CLR);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D0);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);  // sets N

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(20);

        bc.emit(noir_op::FADE_IN);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);
        vm.update();

        vm.update();

        TEST_ASSERT((vm.sr() & noir_sr::N) != 0, "N flag preserved across block");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_fade_in_zero_frames)
    {
        // FADE_IN with D0=0
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::CLR);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D0);

        bc.emit(noir_op::FADE_IN);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);
        vm.update();

        TEST_ASSERT(g_mock_log.fade_in_called, "fade_in called");
        TEST_ASSERT(g_mock_log.fade_in_frames == 0, "fade_in received 0 frames");
        TEST_SUCCESS;
    }

    // ── SCREEN_CLR ──────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_screen_clr_basic)
    {
        // SCREEN_CLR with D0=5
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(5);

        bc.emit(noir_op::SCREEN_CLR);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(g_mock_log.screen_clr_color == 5, "screen_clear received color 5");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_screen_clr_does_not_block)
    {
        // SCREEN_CLR is immediate
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);

        bc.emit(noir_op::SCREEN_CLR);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(777);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 777, "the session carried on past SCREEN_CLR");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_screen_clr_preserves_d0)
    {
        // D0 preserved after SCREEN_CLR
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(42);

        bc.emit(noir_op::SCREEN_CLR);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 42, "D0 preserved");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_screen_clr_preserves_other_regs)
    {
        // D1–D3 preserved after SCREEN_CLR
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(100);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D2, noir_mode::IMM16);
        bc.emit_u16(200);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D3, noir_mode::IMM16);
        bc.emit_u16(300);

        bc.emit(noir_op::SCREEN_CLR);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.reg_d(1) == 100, "D1 preserved");
        TEST_ASSERT(vm.reg_d(2) == 200, "D2 preserved");
        TEST_ASSERT(vm.reg_d(3) == 300, "D3 preserved");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_screen_clr_preserves_sr)
    {
        // SR unchanged after SCREEN_CLR
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

        bc.emit(noir_op::SCREEN_CLR);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT((vm.sr() & noir_sr::N) != 0, "N flag preserved");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_screen_clr_zero)
    {
        // SCREEN_CLR with D0=0 (black)
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::CLR);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D0);

        bc.emit(noir_op::SCREEN_CLR);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(g_mock_log.screen_clr_color == 0, "screen_clear received color 0");
        TEST_SUCCESS;
    }

    // ── SET_LANG ────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_set_lang_basic)
    {
        // SET_LANG with D0=2
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(2);

        bc.emit(noir_op::SET_LANG);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(g_mock_log.set_lang_id == 2, "set_lang received lang_id 2");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_set_lang_does_not_block)
    {
        // SET_LANG is immediate
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);

        bc.emit(noir_op::SET_LANG);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(555);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 555, "the session carried on past SET_LANG");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_set_lang_preserves_regs)
    {
        // All data registers preserved after SET_LANG
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(3);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D2, noir_mode::IMM16);
        bc.emit_u16(20);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D3, noir_mode::IMM16);
        bc.emit_u16(30);

        bc.emit(noir_op::SET_LANG);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.reg_d(0) == 3, "D0 preserved");
        TEST_ASSERT(vm.reg_d(1) == 10, "D1 preserved");
        TEST_ASSERT(vm.reg_d(2) == 20, "D2 preserved");
        TEST_ASSERT(vm.reg_d(3) == 30, "D3 preserved");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_set_lang_preserves_sr)
    {
        // SR unchanged after SET_LANG
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::CLR);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D0);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);

        bc.emit(noir_op::SET_LANG);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT((vm.sr() & noir_sr::N) != 0, "N flag preserved");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_set_lang_zero)
    {
        // SET_LANG with D0=0
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::CLR);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D0);

        bc.emit(noir_op::SET_LANG);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(g_mock_log.set_lang_id == 0, "set_lang received lang_id 0");
        TEST_SUCCESS;
    }

    // ── SAVE_GAME ───────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_save_game_basic)
    {
        // SAVE_GAME with D0=1
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);

        bc.emit(noir_op::SAVE_GAME);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(g_mock_log.save_slot == 1, "save_game received slot 1");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_save_game_defers_then_carries_on)
    {
        // SAVE_GAME is deferred modal: it ends the update it runs in, and the
        // script picks up on the opcode after it once the save is done with.
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(2);

        bc.emit(noir_op::SAVE_GAME);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(444);

        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        g_mock_log.reset();
        vm.execute(fn);

        auto first = vm.update();
        TEST_ASSERT(g_mock_log.save_slot == 2, "the save was asked for");
        TEST_ASSERT(first == noir_result::yielded, "and the update ended there");
        TEST_ASSERT(vm.reg_d(0) != 444, "the opcode after it did not run");

        auto status = vm.update();

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 444, "the session carried on past SAVE_GAME");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_save_game_preserves_regs)
    {
        // All data registers preserved after SAVE_GAME
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(3);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(50);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D2, noir_mode::IMM16);
        bc.emit_u16(60);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D3, noir_mode::IMM16);
        bc.emit_u16(70);

        bc.emit(noir_op::SAVE_GAME);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.reg_d(0) == 3, "D0 preserved");
        TEST_ASSERT(vm.reg_d(1) == 50, "D1 preserved");
        TEST_ASSERT(vm.reg_d(2) == 60, "D2 preserved");
        TEST_ASSERT(vm.reg_d(3) == 70, "D3 preserved");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_save_game_preserves_sr)
    {
        // SR unchanged after SAVE_GAME
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::CLR);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D0);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);

        bc.emit(noir_op::SAVE_GAME);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT((vm.sr() & noir_sr::N) != 0, "N flag preserved");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_save_game_zero_slot)
    {
        // SAVE_GAME with D0=0
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::CLR);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D0);

        bc.emit(noir_op::SAVE_GAME);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(g_mock_log.save_slot == 0, "save_game received slot 0");
        TEST_SUCCESS;
    }

    // ── LOAD_GAME, NEW_GAME, QUIT_GAME ──────────────────────────────────────────
    //
    // All three are deferred terminal. They record what was asked for and stop
    // the VM: whoever answers the request tears the run list down, so the parked
    // session is never resumed and what it was holding does not matter.

    TEST_IMPL(test_noir_load_game_records_the_slot)
    {
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(2);

        bc.emit(noir_op::LOAD_GAME);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        g_mock_log.reset();
        vm.execute(fn);

        auto status = vm.update();

        TEST_ASSERT(status == noir_result::terminated, "the VM is owed a teardown");
        TEST_ASSERT(g_mock_log.load_slot == 2, "load_game received slot 2");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_load_game_zero_slot)
    {
        // Slot 0 is a slot like any other and must not read as "no slot".
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::CLR);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D0);

        bc.emit(noir_op::LOAD_GAME);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        g_mock_log.reset();
        g_mock_log.load_slot = 0xFFFF;  // reset() leaves 0, which the call also writes
        vm.execute(fn);

        vm.update();

        TEST_ASSERT(g_mock_log.load_slot == 0, "load_game received slot 0");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_new_game_records_the_call)
    {
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::NEW_GAME);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        g_mock_log.reset();
        vm.execute(fn);

        auto status = vm.update();

        TEST_ASSERT(status == noir_result::terminated, "the VM is owed a teardown");
        TEST_ASSERT(g_mock_log.new_game_called, "new_game bridge called");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_quit_game_records_the_call)
    {
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::QUIT_GAME);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        g_mock_log.reset();
        vm.execute(fn);

        auto status = vm.update();

        TEST_ASSERT(status == noir_result::terminated, "the VM is owed a teardown");
        TEST_ASSERT(g_mock_log.quit_game_called, "quit_game bridge called");
        TEST_SUCCESS;
    }

    // ── Combined patterns ───────────────────────────────────────────────────────

    TEST_IMPL(test_noir_fade_out_then_fade_in)
    {
        // Common pattern: fade out, then fade in - one update each
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(25);

        bc.emit(noir_op::FADE_OUT);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(30);

        bc.emit(noir_op::FADE_IN);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(666);

        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);
        vm.update();

        TEST_ASSERT(vm.session_count() == 1, "parked at FADE_OUT");
        TEST_ASSERT(g_mock_log.fade_out_called, "fade_out called");
        TEST_ASSERT(g_mock_log.fade_out_frames == 25, "fade_out(25)");

        // Second update — should defer on FADE_IN
        vm.update();

        TEST_ASSERT(vm.session_count() == 1, "parked at FADE_IN");
        TEST_ASSERT(g_mock_log.fade_in_called, "fade_in called");
        TEST_ASSERT(g_mock_log.fade_in_frames == 30, "fade_in(30)");

        // Third update — should complete
        auto status = vm.update();

        TEST_ASSERT(status == noir_result::completed, "completes after both");
        TEST_ASSERT(vm.exit_code() == 666, "final D0=666");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_fade_out_screen_clr_fade_in)
    {
        // Scene transition: fade_out → screen_clr → fade_in
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(20);
        bc.emit(noir_op::FADE_OUT);

        // After fade out: clear screen to black
        bc.emit(noir_op::CLR);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D0);
        bc.emit(noir_op::SCREEN_CLR);

        // Fade back in
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(20);
        bc.emit(noir_op::FADE_IN);

        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);
        vm.update();

        TEST_ASSERT(vm.session_count() == 1, "parked at FADE_OUT");
        TEST_ASSERT(g_mock_log.fade_out_frames == 20, "fade_out(20)");

        // Second update → SCREEN_CLR runs, then defers on FADE_IN
        vm.update();

        TEST_ASSERT(g_mock_log.screen_clr_color == 0, "screen_clear(0)");
        TEST_ASSERT(vm.session_count() == 1, "parked at FADE_IN");
        TEST_ASSERT(g_mock_log.fade_in_frames == 20, "fade_in(20)");

        // Third update → completes
        auto status = vm.update();

        TEST_ASSERT(status == noir_result::completed, "completes");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_save_then_load)
    {
        // Save, then load a different slot. Two deferred calls, so an update
        // each, and the second stops the VM where the first only paused it.
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);
        bc.emit(noir_op::SAVE_GAME);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(2);
        bc.emit(noir_op::LOAD_GAME);

        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        g_mock_log.reset();
        vm.execute(fn);

        auto first = vm.update();
        TEST_ASSERT(first == noir_result::yielded, "the save only paused the session");
        TEST_ASSERT(g_mock_log.save_slot == 1, "save_game(1)");

        auto second = vm.update();
        TEST_ASSERT(second == noir_result::terminated, "the load stopped the VM");
        TEST_ASSERT(g_mock_log.load_slot == 2, "load_game(2)");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_set_lang_then_new_game)
    {
        // SET_LANG is immediate, so both land in the same update.
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(3);
        bc.emit(noir_op::SET_LANG);

        bc.emit(noir_op::NEW_GAME);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        g_mock_log.reset();
        vm.execute(fn);

        auto status = vm.update();

        TEST_ASSERT(status == noir_result::terminated, "the new game stopped the VM");
        TEST_ASSERT(g_mock_log.set_lang_id == 3, "set_lang(3)");
        TEST_ASSERT(g_mock_log.new_game_called, "new_game called");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_full_menu_sequence)
    {
        // Full menu flow: fade_out → screen_clr → set_lang → save_game → fade_in
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        // Fade out
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(15);
        bc.emit(noir_op::FADE_OUT);

        // Clear screen
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(7);
        bc.emit(noir_op::SCREEN_CLR);

        // Set language
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(2);
        bc.emit(noir_op::SET_LANG);

        // Save game
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0);
        bc.emit(noir_op::SAVE_GAME);

        // Fade in
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(15);
        bc.emit(noir_op::FADE_IN);

        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        g_mock_log.reset();
        vm.execute(fn);

        // Each deferred call ends the update it runs in, so the flow takes one
        // update per deferred call plus one for the RET. The immediate calls
        // between them ride along in the same update as the deferred one that
        // follows.
        vm.update();
        TEST_ASSERT(g_mock_log.fade_out_frames == 15, "fade_out(15)");
        TEST_ASSERT(g_mock_log.screen_clr_color != 7, "and nothing after it ran");

        vm.update();
        TEST_ASSERT(g_mock_log.screen_clr_color == 7, "screen_clear(7)");
        TEST_ASSERT(g_mock_log.set_lang_id == 2, "set_lang(2)");
        TEST_ASSERT(g_mock_log.save_slot == 0, "save_game(0)");
        TEST_ASSERT(!g_mock_log.fade_in_called, "save_game stopped it before the fade in");

        vm.update();
        TEST_ASSERT(g_mock_log.fade_in_frames == 15, "fade_in(15)");
        TEST_ASSERT(vm.session_count() == 1, "parked at the RET");

        auto status = vm.update();

        TEST_ASSERT(status == noir_result::completed, "completes");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_fade_out_inside_call)
    {
        // FADE_OUT inside a called function defers, then returns correctly
        bytecode_builder bc;

        // fn0: main — calls fn1
        u16 fn0 = bc.begin_func();
        bc.emit(noir_op::CALL);
        bc.emit_u16(1);
        bc.emit(noir_op::RET);

        // fn1: FADE_OUT then sets D0
        u16 fn1 = bc.begin_func();
        (void)fn1;
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::FADE_OUT);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(77);

        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn0);
        vm.update();

        TEST_ASSERT(vm.session_count() == 1, "parked inside fn1");
        TEST_ASSERT(g_mock_log.fade_out_frames == 10, "fade_out(10)");

        auto status = vm.update();

        TEST_ASSERT(status == noir_result::completed, "completes on the next update");
        TEST_ASSERT(vm.exit_code() == 77, "D0=77 returned from fn1");
        TEST_SUCCESS;
    }

    // ── Invalid opcodes ─────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_menu_invalid_0xC8)
    {
        // 0xC8 is not a valid menu opcode
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(0xC8);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(failed_with(vm, status, noir_error::invalid_opcode), "0xC8 is invalid");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_menu_invalid_0xCF)
    {
        // 0xCF is not a valid menu opcode
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(0xCF);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(failed_with(vm, status, noir_error::invalid_opcode), "0xCF is invalid");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_menu_invalid_0xCB)
    {
        // mid-range invalid opcode
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(0xCB);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(failed_with(vm, status, noir_error::invalid_opcode), "0xCB is invalid");
        TEST_SUCCESS;
    }

    // ══════════════════════════════════════════════════════════════════════════════
    // Test suite registration
    // ══════════════════════════════════════════════════════════════════════════════

    constexpr char const* noir_vm_menu_name = "NoirVM Menu/Settings (0xC_)";

    Test noir_vm_menu_tests[] = {
        // FADE_OUT
        { "fade_out basic", test_noir_fade_out_basic },
        { "fade_out parks the session", test_noir_fade_out_parks_the_session },
        { "fade_out then goto_scene: an update each", test_noir_fade_out_then_goto_scene_take_an_update_each },
        { "fade_out resumes", test_noir_fade_out_resumes_next_update },
        { "fade_out preserves regs", test_noir_fade_out_preserves_all_regs },
        { "fade_out preserves SR", test_noir_fade_out_preserves_sr },
        { "fade_out zero frames", test_noir_fade_out_zero_frames },
        { "fade_out max frames", test_noir_fade_out_max_frames },

        // FADE_IN
        { "fade_in basic", test_noir_fade_in_basic },
        { "fade_in parks the session", test_noir_fade_in_parks_the_session },
        { "fade_in drains the pool", test_noir_fade_in_leaves_the_pool_empty },
        { "fade_in resumes", test_noir_fade_in_resumes_next_update },
        { "fade_in preserves regs", test_noir_fade_in_preserves_all_regs },
        { "fade_in preserves SR", test_noir_fade_in_preserves_sr },
        { "fade_in zero frames", test_noir_fade_in_zero_frames },

        // SCREEN_CLR
        { "screen_clr basic", test_noir_screen_clr_basic },
        { "screen_clr does not block", test_noir_screen_clr_does_not_block },
        { "screen_clr preserves D0", test_noir_screen_clr_preserves_d0 },
        { "screen_clr preserves regs", test_noir_screen_clr_preserves_other_regs },
        { "screen_clr preserves SR", test_noir_screen_clr_preserves_sr },
        { "screen_clr zero", test_noir_screen_clr_zero },

        // SET_LANG
        { "set_lang basic", test_noir_set_lang_basic },
        { "set_lang does not block", test_noir_set_lang_does_not_block },
        { "set_lang preserves regs", test_noir_set_lang_preserves_regs },
        { "set_lang preserves SR", test_noir_set_lang_preserves_sr },
        { "set_lang zero", test_noir_set_lang_zero },

        // SAVE_GAME
        { "save_game basic", test_noir_save_game_basic },
        { "save_game defers then carries on", test_noir_save_game_defers_then_carries_on },
        { "save_game preserves regs", test_noir_save_game_preserves_regs },
        { "save_game preserves SR", test_noir_save_game_preserves_sr },
        { "save_game zero slot", test_noir_save_game_zero_slot },

        // LOAD_GAME
        { "load_game records the slot", test_noir_load_game_records_the_slot },
        { "load_game zero slot", test_noir_load_game_zero_slot },

        // NEW_GAME
        { "new_game records the call", test_noir_new_game_records_the_call },

        // QUIT_GAME
        { "quit_game records the call", test_noir_quit_game_records_the_call },

        // Combined patterns
        { "fade_out then fade_in", test_noir_fade_out_then_fade_in },
        { "fade_out clr fade_in", test_noir_fade_out_screen_clr_fade_in },
        { "save then load", test_noir_save_then_load },
        { "set_lang then new_game", test_noir_set_lang_then_new_game },
        { "full menu sequence", test_noir_full_menu_sequence },
        { "fade_out inside CALL", test_noir_fade_out_inside_call },

        // Invalid opcodes
        { "invalid 0xC8", test_noir_menu_invalid_0xC8 },
        { "invalid 0xCF", test_noir_menu_invalid_0xCF },
        { "invalid 0xCB", test_noir_menu_invalid_0xCB },
    };

}  // namespace NEONengine

#endif  // ACE_TEST_RUNNER
#endif  // __NOIR_VM_MENU_TESTS_H__INCLUDED__
