#ifndef __NOIR_VM_TIMELINE_TESTS_H__INCLUDED__
#define __NOIR_VM_TIMELINE_TESTS_H__INCLUDED__

#ifdef ACE_TEST_RUNNER

#include "types.h"

#include "tests/noir_vm_test_utils.h"

namespace NEONengine
{
    // ═════════════════════════════════════════════════════════════════════════════
    // Game Calls — Timeline opcodes (0x90–0x9F)
    //   ANIM_PLAY, ANIM_STOP, ANIM_WAIT
    //
    // All timeline opcodes are 1-byte (opcode only). Arguments passed in D0.
    // Bridge function pointers are called.
    //
    // ANIM_PLAY (0x90): D0=anim_id → bridge->anim_play(D0)     — non-blocking
    // ANIM_STOP (0x91): D0=anim_id → bridge->anim_stop(D0)     — non-blocking
    // ANIM_WAIT (0x92): D0=anim_id → bridge->anim_wait(D0)     — blocking
    // 0x93–0x9F: Reserved → err_invalid_opcode
    //
    // SR: All timeline opcodes leave SR unchanged.
    // Registers: D0–D3 are preserved across calls.
    // ═════════════════════════════════════════════════════════════════════════════

    // ── ANIM_PLAY ───────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_anim_play_basic)
    {
        // ANIM_PLAY with D0=7 → bridge->anim_play(7)
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(7);

        bc.emit(noir_op::ANIM_PLAY);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(g_mock_log.anim_play_id == 7, "anim_play received anim_id 7");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_anim_play_preserves_d0)
    {
        // D0 should not be modified by ANIM_PLAY
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(42);

        bc.emit(noir_op::ANIM_PLAY);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 42, "D0 unchanged after ANIM_PLAY");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_anim_play_preserves_other_regs)
    {
        // D1, D2, D3 should not be modified by ANIM_PLAY
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

        bc.emit(noir_op::ANIM_PLAY);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.reg_d(0) == 10, "D0 preserved");
        TEST_ASSERT(vm.reg_d(1) == 20, "D1 preserved");
        TEST_ASSERT(vm.reg_d(2) == 30, "D2 preserved");
        TEST_ASSERT(vm.reg_d(3) == 40, "D3 preserved");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_anim_play_preserves_sr)
    {
        // SR should not be modified by ANIM_PLAY
        // Set some flags via CMP, then call ANIM_PLAY and verify unchanged
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        // CMP D0 (0), IMM16 (1) → sets N flag (0 - 1 = -1)
        bc.emit(noir_op::CLR);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D0);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);

        // Set D0 to anim_id for ANIM_PLAY
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(5);

        bc.emit(noir_op::ANIM_PLAY);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        // N flag should still be set from CMP
        TEST_ASSERT((vm.sr() & noir_sr::N) != 0, "N flag preserved");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_anim_play_does_not_block)
    {
        // ANIM_PLAY should not block — VM should remain running and reach RET
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);

        bc.emit(noir_op::ANIM_PLAY);

        // If we reach here, it did not block
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(999);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete without blocking");
        TEST_ASSERT(vm.exit_code() == 999, "execution continued past ANIM_PLAY");
        TEST_ASSERT(vm.state() != noir_state::blocked, "not blocked");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_anim_play_zero_id)
    {
        // ANIM_PLAY with D0=0 should still call bridge with 0
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::CLR);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D0);

        bc.emit(noir_op::ANIM_PLAY);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(g_mock_log.anim_play_id == 0, "anim_play received anim_id 0");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_anim_play_max_id)
    {
        // ANIM_PLAY with D0=0xFFFF (max u16)
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0xFFFF);

        bc.emit(noir_op::ANIM_PLAY);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(g_mock_log.anim_play_id == 0xFFFF, "anim_play received max id");
        TEST_SUCCESS;
    }

    // ── ANIM_STOP ───────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_anim_stop_basic)
    {
        // ANIM_STOP with D0=3 → bridge->anim_stop(3)
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(3);

        bc.emit(noir_op::ANIM_STOP);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(g_mock_log.anim_stop_id == 3, "anim_stop received anim_id 3");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_anim_stop_preserves_d0)
    {
        // D0 should not be modified by ANIM_STOP
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(55);

        bc.emit(noir_op::ANIM_STOP);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 55, "D0 unchanged after ANIM_STOP");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_anim_stop_preserves_other_regs)
    {
        // D1, D2, D3 unchanged by ANIM_STOP
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(100);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(200);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D2, noir_mode::IMM16);
        bc.emit_u16(300);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D3, noir_mode::IMM16);
        bc.emit_u16(400);

        bc.emit(noir_op::ANIM_STOP);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.reg_d(0) == 100, "D0 preserved");
        TEST_ASSERT(vm.reg_d(1) == 200, "D1 preserved");
        TEST_ASSERT(vm.reg_d(2) == 300, "D2 preserved");
        TEST_ASSERT(vm.reg_d(3) == 400, "D3 preserved");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_anim_stop_preserves_sr)
    {
        // SR should not be modified
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::CLR);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D0);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(5);

        bc.emit(noir_op::ANIM_STOP);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT((vm.sr() & noir_sr::N) != 0, "N flag preserved");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_anim_stop_does_not_block)
    {
        // ANIM_STOP should not block
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(2);

        bc.emit(noir_op::ANIM_STOP);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(777);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete without blocking");
        TEST_ASSERT(vm.exit_code() == 777, "execution continued past ANIM_STOP");
        TEST_SUCCESS;
    }

    // ── ANIM_WAIT ───────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_anim_wait_calls_bridge)
    {
        // ANIM_WAIT with D0=12 → bridge->anim_wait(12) is called
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(12);

        bc.emit(noir_op::ANIM_WAIT);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);
        vm.update();  // executes up to ANIM_WAIT, which blocks

        TEST_ASSERT(g_mock_log.anim_wait_called, "anim_wait bridge called");
        TEST_ASSERT(g_mock_log.anim_wait_id == 12, "anim_wait received anim_id 12");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_anim_wait_blocks_vm)
    {
        // ANIM_WAIT should enter blocked state
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(5);

        bc.emit(noir_op::ANIM_WAIT);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);
        auto status = vm.update();

        TEST_ASSERT(vm.state() == noir_state::blocked, "VM should be blocked");
        TEST_ASSERT(status == noir_status::yielded, "update returns yielded while blocked");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_anim_wait_stays_blocked_until_decremented)
    {
        // VM should stay blocked across multiple update() calls until
        // decrement_blocking() is called
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(5);

        bc.emit(noir_op::ANIM_WAIT);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);
        vm.update();  // execute until blocked

        // Should stay blocked for multiple updates
        auto s1 = vm.update();
        TEST_ASSERT(s1 == noir_status::yielded, "still yielded after 2nd update");
        TEST_ASSERT(vm.state() == noir_state::blocked, "still blocked after 2nd update");

        auto s2 = vm.update();
        TEST_ASSERT(s2 == noir_status::yielded, "still yielded after 3rd update");
        TEST_ASSERT(vm.state() == noir_state::blocked, "still blocked after 3rd update");

        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_anim_wait_resumes_after_unblock)
    {
        // After decrement_blocking(), VM should resume and complete
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(5);

        bc.emit(noir_op::ANIM_WAIT);

        // After unblocking, this code should execute
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(888);

        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);
        vm.update();  // execute until blocked

        TEST_ASSERT(vm.state() == noir_state::blocked, "VM should be blocked");

        // Simulate engine completing the animation
        vm.decrement_blocking();
        auto status = vm.update();

        TEST_ASSERT(status == noir_status::completed, "should complete after unblock");
        TEST_ASSERT(vm.exit_code() == 888, "code after ANIM_WAIT executed");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_anim_wait_preserves_d0)
    {
        // D0 should be preserved across the blocking period
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(42);

        bc.emit(noir_op::ANIM_WAIT);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);
        vm.update();  // blocks

        vm.decrement_blocking();
        vm.update();  // resumes, executes RET

        TEST_ASSERT(vm.exit_code() == 42, "D0 preserved across ANIM_WAIT block");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_anim_wait_preserves_all_regs)
    {
        // All data registers should be preserved across the blocking period
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

        bc.emit(noir_op::ANIM_WAIT);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);
        vm.update();

        vm.decrement_blocking();
        vm.update();

        TEST_ASSERT(vm.reg_d(0) == 11, "D0 preserved");
        TEST_ASSERT(vm.reg_d(1) == 22, "D1 preserved");
        TEST_ASSERT(vm.reg_d(2) == 33, "D2 preserved");
        TEST_ASSERT(vm.reg_d(3) == 44, "D3 preserved");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_anim_wait_preserves_sr)
    {
        // SR should be preserved across the blocking period
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        // Set up N flag via CMP
        bc.emit(noir_op::CLR);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D0);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);  // 0 - 1 = -1, sets N

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(5);

        bc.emit(noir_op::ANIM_WAIT);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);
        vm.update();

        vm.decrement_blocking();
        vm.update();

        TEST_ASSERT((vm.sr() & noir_sr::N) != 0, "N flag preserved across block");
        TEST_SUCCESS;
    }

    // ── ANIM_WAIT — Sequential blocking ────────────────────────────────────────

    TEST_IMPL(test_noir_anim_wait_two_sequential)
    {
        // Two ANIM_WAIT calls in sequence — each blocks and resumes independently
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        // First anim_wait with D0=1
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);
        bc.emit(noir_op::ANIM_WAIT);

        // Between waits — set D0=2 for second anim
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(2);
        bc.emit(noir_op::ANIM_WAIT);

        // After both waits
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(999);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);

        // First update: runs to first ANIM_WAIT, blocks
        vm.update();
        TEST_ASSERT(vm.state() == noir_state::blocked, "blocked on first ANIM_WAIT");
        TEST_ASSERT(g_mock_log.anim_wait_id == 1, "first wait anim_id=1");

        // Unblock first wait
        vm.decrement_blocking();
        vm.update();
        // Should now be blocked on second ANIM_WAIT
        TEST_ASSERT(vm.state() == noir_state::blocked, "blocked on second ANIM_WAIT");
        TEST_ASSERT(g_mock_log.anim_wait_id == 2, "second wait anim_id=2");

        // Unblock second wait
        vm.decrement_blocking();
        auto status = vm.update();
        TEST_ASSERT(status == noir_status::completed, "completes after both waits");
        TEST_ASSERT(vm.exit_code() == 999, "final D0 set after both waits");

        TEST_SUCCESS;
    }

    // ── ANIM_PLAY + ANIM_WAIT combined ──────────────────────────────────────────

    TEST_IMPL(test_noir_anim_play_then_wait)
    {
        // Typical pattern: play an animation, then wait for it
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::ANIM_PLAY);
        bc.emit(noir_op::ANIM_WAIT);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);
        vm.update();  // executes ANIM_PLAY (non-blocking) then ANIM_WAIT (blocks)

        TEST_ASSERT(g_mock_log.anim_play_id == 10, "anim_play called with id 10");
        TEST_ASSERT(g_mock_log.anim_wait_id == 10, "anim_wait called with id 10");
        TEST_ASSERT(vm.state() == noir_state::blocked, "blocked on ANIM_WAIT");

        vm.decrement_blocking();
        auto status = vm.update();
        TEST_ASSERT(status == noir_status::completed, "completes after unblock");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_anim_play_stop_sequence)
    {
        // Play then stop — both non-blocking, both bridge calls logged
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(15);

        bc.emit(noir_op::ANIM_PLAY);

        bc.emit(noir_op::ANIM_STOP);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(g_mock_log.anim_play_id == 15, "play called with 15");
        TEST_ASSERT(g_mock_log.anim_stop_id == 15, "stop called with 15");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_anim_play_wait_stop)
    {
        // Full lifecycle: play → wait → stop
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(20);

        bc.emit(noir_op::ANIM_PLAY);
        bc.emit(noir_op::ANIM_WAIT);

        // After wait completes, stop the animation
        bc.emit(noir_op::ANIM_STOP);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);
        vm.update();

        TEST_ASSERT(g_mock_log.anim_play_id == 20, "play called");
        TEST_ASSERT(g_mock_log.anim_wait_id == 20, "wait called");
        TEST_ASSERT(vm.state() == noir_state::blocked, "blocked");

        vm.decrement_blocking();
        auto status = vm.update();

        TEST_ASSERT(status == noir_status::completed, "completes");
        TEST_ASSERT(g_mock_log.anim_stop_id == 20, "stop called after unblock");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_anim_play_different_ids)
    {
        // Play two different animations (both non-blocking, second overwrites log)
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(100);
        bc.emit(noir_op::ANIM_PLAY);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(200);
        bc.emit(noir_op::ANIM_PLAY);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        // Last play overwrites
        TEST_ASSERT(g_mock_log.anim_play_id == 200, "second anim_play logged");
        TEST_SUCCESS;
    }

    // ── ANIM_WAIT with other opcodes after blocking ─────────────────────────────

    TEST_IMPL(test_noir_anim_wait_code_after_unblock)
    {
        // Arithmetic after ANIM_WAIT unblocks to verify VM fully resumes
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(3);

        bc.emit(noir_op::ANIM_WAIT);

        // After unblock: D0 = D0 + 10
        bc.emit(noir_op::ADD);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);
        vm.update();

        vm.decrement_blocking();
        auto status = vm.update();

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 13, "D0=3+10=13 after unblock");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_anim_wait_branch_after_unblock)
    {
        // Branch after ANIM_WAIT to verify PC is correct
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);

        bc.emit(noir_op::ANIM_WAIT);

        // After unblock: CMP D0 with 1 → Z=1
        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);

        // BEQ to success
        bc.emit(noir_op::BEQ);
        bc.emit_s16(3);  // skip over the next MOV16+mode+u16

        // Not taken: D0=0
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0);

        // Taken: D0=555
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(555);

        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);
        vm.update();

        vm.decrement_blocking();
        auto status = vm.update();

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 555, "branch taken after unblock");
        TEST_SUCCESS;
    }

    // ── ANIM_WAIT with CALL ─────────────────────────────────────────────────────

    TEST_IMPL(test_noir_anim_wait_inside_call)
    {
        // ANIM_WAIT inside a called function should block, then return correctly
        bytecode_builder bc;

        // fn0: main — calls fn1
        u16 fn0 = bc.begin_func();
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(50);

        bc.emit(noir_op::CALL);
        bc.emit_u16(1);  // call fn1

        // After call returns, D0 should have value from fn1
        bc.emit(noir_op::RET);

        // fn1: called function — does ANIM_WAIT
        u16 fn1 = bc.begin_func();
        (void)fn1;
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(7);

        bc.emit(noir_op::ANIM_WAIT);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(77);

        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn0);
        vm.update();

        TEST_ASSERT(vm.state() == noir_state::blocked, "blocked inside fn1");
        TEST_ASSERT(g_mock_log.anim_wait_id == 7, "anim_wait called with id 7");

        vm.decrement_blocking();
        auto status = vm.update();

        TEST_ASSERT(status == noir_status::completed, "completes after unblock");
        TEST_ASSERT(vm.exit_code() == 77, "D0=77 returned from fn1");
        TEST_SUCCESS;
    }

    // ── Invalid timeline opcodes ────────────────────────────────────────────────

    TEST_IMPL(test_noir_timeline_invalid_0x93)
    {
        // 0x93 is not a valid timeline opcode
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(0x93);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::err_invalid_opcode, "0x93 is invalid");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_timeline_invalid_0x9F)
    {
        // 0x9F is not a valid timeline opcode
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(0x9F);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::err_invalid_opcode, "0x9F is invalid");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_timeline_invalid_0x95)
    {
        // mid-range invalid opcode
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(0x95);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::err_invalid_opcode, "0x95 is invalid");
        TEST_SUCCESS;
    }

    // ══════════════════════════════════════════════════════════════════════════════
    // Test suite registration
    // ══════════════════════════════════════════════════════════════════════════════

    constexpr char const* noir_vm_timeline_name = "NoirVM Timeline (0x9_)";

    Test noir_vm_timeline_tests[] = {
        // ANIM_PLAY
        { "anim_play basic", test_noir_anim_play_basic },
        { "anim_play preserves D0", test_noir_anim_play_preserves_d0 },
        { "anim_play preserves regs", test_noir_anim_play_preserves_other_regs },
        { "anim_play preserves SR", test_noir_anim_play_preserves_sr },
        { "anim_play does not block", test_noir_anim_play_does_not_block },
        { "anim_play zero id", test_noir_anim_play_zero_id },
        { "anim_play max id", test_noir_anim_play_max_id },

        // ANIM_STOP
        { "anim_stop basic", test_noir_anim_stop_basic },
        { "anim_stop preserves D0", test_noir_anim_stop_preserves_d0 },
        { "anim_stop preserves regs", test_noir_anim_stop_preserves_other_regs },
        { "anim_stop preserves SR", test_noir_anim_stop_preserves_sr },
        { "anim_stop does not block", test_noir_anim_stop_does_not_block },

        // ANIM_WAIT — basic
        { "anim_wait calls bridge", test_noir_anim_wait_calls_bridge },
        { "anim_wait blocks VM", test_noir_anim_wait_blocks_vm },
        { "anim_wait stays blocked", test_noir_anim_wait_stays_blocked_until_decremented },
        { "anim_wait resumes", test_noir_anim_wait_resumes_after_unblock },
        { "anim_wait preserves D0", test_noir_anim_wait_preserves_d0 },
        { "anim_wait preserves regs", test_noir_anim_wait_preserves_all_regs },
        { "anim_wait preserves SR", test_noir_anim_wait_preserves_sr },

        // ANIM_WAIT — sequential
        { "anim_wait two sequential", test_noir_anim_wait_two_sequential },

        // Combined patterns
        { "play then wait", test_noir_anim_play_then_wait },
        { "play stop sequence", test_noir_anim_play_stop_sequence },
        { "play wait stop", test_noir_anim_play_wait_stop },
        { "play different ids", test_noir_anim_play_different_ids },

        // ANIM_WAIT — post-unblock execution
        { "wait code after unblock", test_noir_anim_wait_code_after_unblock },
        { "wait branch after unblock", test_noir_anim_wait_branch_after_unblock },
        { "wait inside CALL", test_noir_anim_wait_inside_call },

        // Invalid opcodes
        { "invalid 0x93", test_noir_timeline_invalid_0x93 },
        { "invalid 0x9F", test_noir_timeline_invalid_0x9F },
        { "invalid 0x95", test_noir_timeline_invalid_0x95 },
    };

}  // namespace NEONengine

#endif  // ACE_TEST_RUNNER
#endif  // __NOIR_VM_TIMELINE_TESTS_H__INCLUDED__
