#ifndef __NOIR_VM_TESTS_H__INCLUDED__
#define __NOIR_VM_TESTS_H__INCLUDED__

#ifdef ACE_TEST_RUNNER

#include "types.h"

#include "tests/noir_vm_test_utils.h"

namespace NEONengine
{
    // ═════════════════════════════════════════════════════════════════════════════
    // Script Execution opcodes (0x00–0x0F)
    //   NOP, WAIT, HALT, CALL, LOCALS, RET, RETZ, RETC
    // ═════════════════════════════════════════════════════════════════════════════

    // ── NOP ──────────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_nop_then_halt)
    {
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::NOP);
        bc.emit(noir_op::NOP);
        bc.emit(noir_op::NOP);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "NOPs followed by RET should complete");
        TEST_SUCCESS;
    }

    // ── HALT ─────────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_halt)
    {
        // HALT is an error, not an exit: it says the author reached a case that
        // cannot happen. A script that HALTs must not look like one that ended.
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::HALT);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(failed_with(status, noir_error::halt), "HALT reports err_halt");
        TEST_ASSERT(message_has("HALT"), "and says so in words");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_halt_stops_the_session_there)
    {
        // The opcodes after a HALT belong to a case the author ruled out, so
        // none of them may run.
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(11);

        bc.emit(noir_op::HALT);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(22);

        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        bc.run_to_completion(vm, fn);

        TEST_ASSERT(vm.reg_d(0) == 11, "the MOV before HALT ran");
        TEST_ASSERT(vm.session_count() == 0, "the session was removed");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_halt_spares_the_other_session)
    {
        // A script error kills one session. The rest keep running.
        bytecode_builder bc;

        u16 fn_halt = bc.begin_func();
        bc.emit(noir_op::HALT);

        u16 fn_live = bc.begin_func();
        bc.emit(noir_op::WAIT);
        bc.emit_u16(4);
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(777);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn_halt);
        vm.execute(fn_live);

        vm.update();

        TEST_ASSERT(vm.session_count() == 1, "only the halting session was removed");

        // Age out the wait, then let the survivor finish.
        for (u16 i = 0; i < 6; ++i) vm.update();

        TEST_ASSERT(vm.reg_d(0) == 777, "the survivor ran to its end");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_halt_does_not_poison_later_updates)
    {
        // An error belongs to the update it happened in. A defensive HALT in one
        // script must not make every later clean completion look like a failure.
        bytecode_builder bc;

        u16 fn_halt = bc.begin_func();
        bc.emit(noir_op::HALT);

        u16 fn_clean = bc.begin_func();
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();

        vm.execute(fn_halt);
        auto first = vm.update();
        TEST_ASSERT(failed_with(first, noir_error::halt), "the HALT was reported");

        vm.execute(fn_clean);
        auto second = vm.update();

        TEST_ASSERT(second == noir_result::completed, "the clean script completed cleanly");
        TEST_SUCCESS;
    }

    // ── WAIT ─────────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_wait_delays_frames)
    {
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        // WAIT 3 frames
        bc.emit(noir_op::WAIT);
        bc.emit_u16(3);

        // After wait: set D0 = 0xBEEF
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0xBEEF);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        TEST_ASSERT(vm.execute(fn), "execute should succeed");
        mtl::expected<noir_result, noir_error> status = noir_result::ok;

        // First update: executes WAIT, enters waiting state
        status = vm.update();
        TEST_ASSERT(status == noir_result::yielded, "update 1 should yield (waiting)");

        // Frames 2-4: still waiting (counter decrements each update)
        status = vm.update();
        TEST_ASSERT(status == noir_result::yielded, "update 2 should yield (waiting)");
        status = vm.update();
        TEST_ASSERT(status == noir_result::yielded, "update 3 should yield (waiting)");
        status = vm.update();
        TEST_ASSERT(status == noir_result::yielded, "update 4 should yield (waiting)");

        // Frame 5: counter reached 0 last frame, now resumes and runs MOV + HALT
        status = vm.update();
        TEST_ASSERT(status == noir_result::completed, "update 5 should complete");
        TEST_ASSERT(vm.reg_d(0) == 0xBEEF, "D0 should be set after wait completes");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_wait_zero_frames)
    {
        // WAIT 0 should still enter waiting state but resume immediately next update
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::WAIT);
        bc.emit_u16(0);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        TEST_ASSERT(vm.execute(fn), "execute should succeed");
        mtl::expected<noir_result, noir_error> status = noir_result::ok;

        // First update: executes WAIT(0), enters waiting
        status = vm.update();
        TEST_ASSERT(status == noir_result::yielded, "should yield on WAIT(0)");

        // Second update: counter is 0, resumes, runs HALT
        status = vm.update();
        TEST_ASSERT(status == noir_result::completed, "should complete after WAIT(0)");
        TEST_SUCCESS;
    }

    // ── RET ──────────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_ret_completes)
    {
        // RET from the entry function should complete with exit code in D0
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        // D0 = 7 (exit code)
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(7);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "RET should complete");
        TEST_ASSERT(vm.session_count() == 0, "the run list is empty after RET");
        TEST_ASSERT(vm.exit_code() == 7, "exit code should be D0 value");
        TEST_SUCCESS;
    }

    // ── CALL + RET ───────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_call_and_ret)
    {
        // Main calls sub-function, sub sets D1, returns, main reads D1
        bytecode_builder bc;

        // func 0: main
        u16 fn_main = bc.begin_func();
        // Set D0 = 10
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);
        // CALL func 1
        bc.emit(noir_op::CALL);
        bc.emit_u16(1);
        // After return, D1 should be 20 (set by callee)
        // Set D0 = D1 (copy result to exit code)
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D1);
        bc.emit(noir_op::RET);

        // func 1: subroutine
        bc.begin_func();
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(20);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn_main);

        TEST_ASSERT(status == noir_result::completed, "CALL+RET should complete");
        TEST_ASSERT(vm.exit_code() == 20, "exit code should be callee's D1 value");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_call_invalid_func)
    {
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        // CALL func_id 99 — out of range
        bc.emit(noir_op::CALL);
        bc.emit_u16(99);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(failed_with(status, noir_error::invalid_func), "invalid CALL should error");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_call_nested)
    {
        // main → func1 → func2, each adds to D0
        bytecode_builder bc;

        // func 0: main. D0=1, CALL 1, RET
        u16 fn_main = bc.begin_func();
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);
        bc.emit(noir_op::CALL);
        bc.emit_u16(1);
        bc.emit(noir_op::RET);

        // func 1: D0 += 10, CALL 2, RET
        bc.begin_func();
        bc.emit(noir_op::ADD);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);
        bc.emit(noir_op::CALL);
        bc.emit_u16(2);
        bc.emit(noir_op::RET);

        // func 2: D0 += 100, RET
        bc.begin_func();
        bc.emit(noir_op::ADD);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(100);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn_main);

        TEST_ASSERT(status == noir_result::completed, "nested calls should complete");
        TEST_ASSERT(vm.exit_code() == 111, "D0 should be 1+10+100=111");
        TEST_SUCCESS;
    }

    // ── LOCALS ───────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_locals_reserves_space)
    {
        // LOCALS allocates stack space for local variables. Verify we can
        // write and read a local via the LOCAL addressing mode.
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        // Reserve 4 bytes of locals (2 x 16-bit slots)
        bc.emit(noir_op::LOCALS);
        bc.emit(4);

        // Write 0x1234 to local at FP-4 (first local, offset = -4)
        // Inline data follows dst→src order (matching mode byte [dst:4][src:4])
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::LOCAL, noir_mode::IMM16);
        bc.emit(to<u8>(-4));  // dst offset (LOCAL inline data)
        bc.emit_u16(0x1234);                // src value  (IMM16 inline data)

        // Read it back into D0
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::LOCAL);
        bc.emit(to<u8>(-4));  // src offset

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "LOCALS test should complete");
        TEST_ASSERT(vm.exit_code() == 0x1234, "local variable round-trip failed");
        TEST_SUCCESS;
    }

    // ── RETZ ─────────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_retz_taken)
    {
        // RETZ returns when Z=1. Set Z via CMP equal values.
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        // D0 = 5
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(5);

        // CMP D0, 5 → result is 0 → Z=1
        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(5);

        // RETZ should take (Z=1)
        bc.emit(noir_op::RETZ);

        // Should NOT reach here
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(999);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "RETZ taken should complete");
        TEST_ASSERT(vm.session_count() == 0, "the run list is empty (RET path)");
        TEST_ASSERT(vm.exit_code() == 5, "D0 should still be 5");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_retz_not_taken)
    {
        // RETZ should NOT return when Z=0
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        // D0 = 5
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(5);

        // CMP D0, 3 → result != 0 → Z=0
        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(3);

        // RETZ should NOT take (Z=0)
        bc.emit(noir_op::RETZ);

        // Execution continues: set D0 = 77
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(77);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "RETZ not-taken should complete");
        TEST_ASSERT(vm.exit_code() == 77, "D0 should be 77 (continued past RETZ)");
        TEST_SUCCESS;
    }

    // ── RETC ─────────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_retc_taken)
    {
        // RETC returns when C=1. SUB where a < b sets C.
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        // D0 = 3, D1 = 10
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(3);
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(10);

        // CMP D0, D1 → 3 - 10, borrow → C=1
        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D1);

        // RETC should take (C=1)
        bc.emit(noir_op::RETC);

        // Should NOT reach here
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(999);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "RETC taken should complete");
        TEST_ASSERT(vm.session_count() == 0, "the run list is empty (RET path)");
        TEST_ASSERT(vm.exit_code() == 3, "D0 should still be 3");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_retc_not_taken)
    {
        // RETC should NOT return when C=0
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        // D0 = 10, D1 = 3
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(3);

        // CMP D0, D1 → 10 - 3 = 7, no borrow → C=0
        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D1);

        // RETC should NOT take (C=0)
        bc.emit(noir_op::RETC);

        // Execution continues
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(55);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "RETC not-taken should complete");
        TEST_ASSERT(vm.exit_code() == 55, "D0 should be 55 (continued past RETC)");
        TEST_SUCCESS;
    }

    // ── Invalid opcode ───────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_invalid_opcode)
    {
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        // 0x0F is in the script_exec category but not a valid opcode
        bc.emit(0x0F);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(failed_with(status, noir_error::invalid_opcode), "bad opcode should error");
        TEST_ASSERT(vm.state() == noir_session_state::died, "state should be dead on error");
        TEST_SUCCESS;
    }

    // ── execute() with invalid func_id ───────────────────────────────────────────

    TEST_IMPL(test_noir_execute_invalid_func)
    {
        bytecode_builder bc;
        bc.begin_func();
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = vm.execute(99);

        TEST_ASSERT(failed_with(status, noir_error::invalid_func), "execute bad func_id should error");
        TEST_SUCCESS;
    }

    // ── Opcodes-per-update budget ────────────────────────────────────────────────

    TEST_IMPL(test_noir_opcodes_per_update)
    {
        // With a session budget of 2, a 4-opcode function should take 2+ updates
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::NOP);
        bc.emit(noir_op::NOP);
        bc.emit(noir_op::NOP);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.set_session_budget(2);

        TEST_ASSERT(vm.execute(fn), "execute should succeed");
        mtl::expected<noir_result, noir_error> status = noir_result::ok;

        // First update: runs 2 NOPs, budget exhausted → yields
        status = vm.update();
        TEST_ASSERT(status == noir_result::yielded, "update 1 should yield (budget)");

        // Second update: runs NOP + HALT → completes
        status = vm.update();
        TEST_ASSERT(status == noir_result::completed, "update 2 should complete");
        TEST_SUCCESS;
    }

    // ── CALL preserves registers across nested calls ─────────────────────────────

    TEST_IMPL(test_noir_call_preserves_frame)
    {
        // Verify that CALL/RET correctly saves and restores FP,
        // allowing the caller to still access its own locals.
        bytecode_builder bc;

        // func 0: main
        u16 fn_main = bc.begin_func();
        // D0 = 0xAA
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0xAA);
        // CALL func 1 (callee clobbers D1)
        bc.emit(noir_op::CALL);
        bc.emit_u16(1);
        // After return: D0 should still be 0xAA, D1 should be callee's value
        bc.emit(noir_op::RET);

        // func 1: set D1 = 0xBB, return
        bc.begin_func();
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(0xBB);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn_main);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0xAA, "D0 should survive across CALL");
        TEST_ASSERT(vm.reg_d(1) == 0xBB, "D1 should have callee value");
        TEST_SUCCESS;
    }

    // ── reset_execution() abandons the run but keeps game state ─────────────────

    TEST_IMPL(test_noir_reset_execution_clears_run_keeps_state)
    {
        // Park a session mid-run with a dirty register and a mutated game value,
        // then reset_execution() and confirm the execution state is clean while
        // VALUES survive and the VM can run a fresh function afterwards.
        bytecode_builder bc;

        u16 fn0 = bc.begin_func();
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(42);
        bc.emit(noir_op::INCV);  // VALUES[0] += 1
        bc.emit_u16(0);
        bc.emit(noir_op::WAIT);  // park the session (non-idle) mid-run
        bc.emit_u16(5);
        bc.emit(noir_op::RET);

        u16 fn1 = bc.begin_func();
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(5);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        TEST_ASSERT(vm.execute(fn0), "fn0 starts");
        vm.update();
        TEST_ASSERT(vm.state() == noir_session_state::waiting, "session parked mid-run");
        TEST_ASSERT(vm.reg_d(0) == 42, "D0 dirty before reset");
        TEST_ASSERT(bc.values[0] == 1, "value mutated before reset");

        vm.reset_execution();
        TEST_ASSERT(vm.session_count() == 0, "run list emptied by reset");
        TEST_ASSERT(vm.state() == noir_session_state::ended, "and the register window with it");
        TEST_ASSERT(vm.reg_d(0) == 0 && vm.reg_d(1) == 0 && vm.reg_d(2) == 0 && vm.reg_d(3) == 0,
            "registers cleared by reset");
        TEST_ASSERT(vm.sr() == 0, "status register cleared by reset");
        TEST_ASSERT(vm.exit_code() == 0, "exit code cleared by reset");
        TEST_ASSERT(bc.values[0] == 1, "game state preserved across reset");

        auto status = bc.run_to_completion(vm, fn1);
        TEST_ASSERT(status == noir_result::completed, "fresh function runs after reset");
        TEST_ASSERT(vm.exit_code() == 5, "fresh function produced its own result");
        TEST_SUCCESS;
    }

    // ── Test suite registration ─────────────────────────────────────────────────

    TEST_SUITE_BEGIN(noir_vm)
    TEST(test_noir_nop_then_halt)
    TEST(test_noir_halt)
    TEST(test_noir_halt_stops_the_session_there)
    TEST(test_noir_halt_spares_the_other_session)
    TEST(test_noir_halt_does_not_poison_later_updates)
    TEST(test_noir_wait_delays_frames)
    TEST(test_noir_wait_zero_frames)
    TEST(test_noir_ret_completes)
    TEST(test_noir_call_and_ret)
    TEST(test_noir_call_invalid_func)
    TEST(test_noir_call_nested)
    TEST(test_noir_locals_reserves_space)
    TEST(test_noir_retz_taken)
    TEST(test_noir_retz_not_taken)
    TEST(test_noir_retc_taken)
    TEST(test_noir_retc_not_taken)
    TEST(test_noir_invalid_opcode)
    TEST(test_noir_execute_invalid_func)
    TEST(test_noir_opcodes_per_update)
    TEST(test_noir_call_preserves_frame)
    TEST(test_noir_reset_execution_clears_run_keeps_state)
    TEST_SUITE_END

}  // namespace NEONengine

#endif  // ACE_TEST_RUNNER
#endif  // __NOIR_VM_TESTS_H__INCLUDED__
