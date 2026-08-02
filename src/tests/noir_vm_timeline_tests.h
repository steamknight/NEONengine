#ifndef __NOIR_VM_TIMELINE_TESTS_H__INCLUDED__
#define __NOIR_VM_TIMELINE_TESTS_H__INCLUDED__

#ifdef ACE_TEST_RUNNER

#include "types.h"

#include "tests/noir_vm_test_utils.h"

namespace NEONengine
{
    // ═════════════════════════════════════════════════════════════════════════════
    // Game Calls — Timeline opcodes (0x90–0x9F)
    //   ANIM_PLAY, ANIM_STOP
    //
    // All timeline opcodes are 1-byte (opcode only). Arguments passed in D0.
    // Bridge function pointers are called.
    //
    // ANIM_PLAY (0x90): D0=anim_id → bridge->anim_play(D0)     — immediate
    // ANIM_STOP (0x91): D0=anim_id → bridge->anim_stop(D0)     — immediate
    // 0x92–0x9F: Reserved → err_invalid_opcode
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

        TEST_ASSERT(status == noir_result::completed, "should complete");
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

        TEST_ASSERT(status == noir_result::completed, "should complete");
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

        TEST_ASSERT(status == noir_result::completed, "should complete");
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

        TEST_ASSERT(status == noir_result::completed, "should complete");
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

        TEST_ASSERT(status == noir_result::completed, "should complete in one update");
        TEST_ASSERT(vm.exit_code() == 999, "execution continued past ANIM_PLAY");
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

        TEST_ASSERT(status == noir_result::completed, "should complete");
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

        TEST_ASSERT(status == noir_result::completed, "should complete");
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

        TEST_ASSERT(status == noir_result::completed, "should complete");
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

        TEST_ASSERT(status == noir_result::completed, "should complete");
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

        TEST_ASSERT(status == noir_result::completed, "should complete");
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

        TEST_ASSERT(status == noir_result::completed, "should complete");
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

        TEST_ASSERT(status == noir_result::completed, "should complete in one update");
        TEST_ASSERT(vm.exit_code() == 777, "execution continued past ANIM_STOP");
        TEST_SUCCESS;
    }
    TEST_IMPL(test_noir_anim_play_stop_sequence)
    {
        // Play then stop — both immediate, both bridge calls logged
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

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(g_mock_log.anim_play_id == 15, "play called with 15");
        TEST_ASSERT(g_mock_log.anim_stop_id == 15, "stop called with 15");
        TEST_SUCCESS;
    }
    TEST_IMPL(test_noir_anim_play_different_ids)
    {
        // Play two different animations (both immediate, second overwrites log)
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

        TEST_ASSERT(status == noir_result::completed, "should complete");
        // Last play overwrites
        TEST_ASSERT(g_mock_log.anim_play_id == 200, "second anim_play logged");
        TEST_SUCCESS;
    }
    // ── Invalid timeline opcodes ────────────────────────────────────────────────

    TEST_IMPL(test_noir_timeline_anim_wait_opcode_is_gone)
    {
        // 0x92 was ANIM_WAIT. A script compiled against the old opcode set must
        // be rejected rather than quietly doing nothing, so that the failure
        // names itself instead of turning into a script that waits for ever.
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(0x92);
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(4242);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(failed_with(status, noir_error::invalid_opcode), "0x92 is invalid");
        TEST_ASSERT(vm.exit_code() != 4242, "the session died rather than running on");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_timeline_invalid_0x93)
    {
        // 0x93 is not a valid timeline opcode
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(0x93);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(failed_with(status, noir_error::invalid_opcode), "0x93 is invalid");
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

        TEST_ASSERT(failed_with(status, noir_error::invalid_opcode), "0x9F is invalid");
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

        TEST_ASSERT(failed_with(status, noir_error::invalid_opcode), "0x95 is invalid");
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

        // Combined patterns
        { "play stop sequence", test_noir_anim_play_stop_sequence },
        { "play different ids", test_noir_anim_play_different_ids },

        // Invalid opcodes
        { "anim_wait opcode is gone", test_noir_timeline_anim_wait_opcode_is_gone },
        { "invalid 0x93", test_noir_timeline_invalid_0x93 },
        { "invalid 0x9F", test_noir_timeline_invalid_0x9F },
        { "invalid 0x95", test_noir_timeline_invalid_0x95 },
    };

}  // namespace NEONengine

#endif  // ACE_TEST_RUNNER
#endif  // __NOIR_VM_TIMELINE_TESTS_H__INCLUDED__
