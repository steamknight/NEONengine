#ifndef __NOIR_VM_FLAG_OPS_TESTS_H__INCLUDED__
#define __NOIR_VM_FLAG_OPS_TESTS_H__INCLUDED__

#ifdef ACE_TEST_RUNNER

#include "tests/noir_vm_test_utils.h"

namespace NEONengine
{
    // ═════════════════════════════════════════════════════════════════════════════
    // Flag Operations opcodes (0x50–0x5F)
    //   SETF, CLRF, TOGF, TSTF     — immediate u16 index (3 bytes)
    //   SETFR, CLRFR, TOGFR, TSTFR — register-indexed (2 bytes)
    //   LDFR, STFR                  — flag ↔ register transfer (4 bytes)
    //
    // FLAGS is a borrowed bitfield array. Each flag is a single bit.
    // reg_id encoding: 0=D0, 1=D1, 2=D2, 3=D3.
    // TSTF/TSTFR: Z=1 when flag is clear, Z=0 when flag is set.
    // LDFR: Dn = 0 or 1, sets NZ flags.
    // STFR: flag = Dn bit 0, no flags affected.
    // Out-of-bounds → err_flag_oob, HALT.
    // ═════════════════════════════════════════════════════════════════════════════

    // ── SETF ────────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_setf_basic)
    {
        // Set flag 0, then test it — should be set (Z=0)
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::SETF);
        bc.emit_u16(0);

        bc.emit(noir_op::TSTF);
        bc.emit_u16(0);

        // After TSTF: flag is set → Z=0
        // Branch if Z=0 (BNE = "flag is set") → skip overwrite
        bc.emit(noir_op::BNE);
        bc.emit_s16(4);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(999);  // should NOT get here

        // Load flag into D0 to verify
        bc.emit(noir_op::LDFR);
        bc.emit(0);  // reg_id = D0
        bc.emit_u16(0);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 1, "flag 0 should be set → D0=1");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_setf_high_index)
    {
        // Set flag 127 (highest valid in 16-byte array = 128 flags)
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::SETF);
        bc.emit_u16(127);

        bc.emit(noir_op::LDFR);
        bc.emit(0);  // D0
        bc.emit_u16(127);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 1, "flag 127 should be set");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_setf_multiple_flags)
    {
        // Set flags 0, 5, and 10 — verify they are independent
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::SETF);
        bc.emit_u16(0);

        bc.emit(noir_op::SETF);
        bc.emit_u16(5);

        bc.emit(noir_op::SETF);
        bc.emit_u16(10);

        // Check flag 0
        bc.emit(noir_op::LDFR);
        bc.emit(0);  // D0
        bc.emit_u16(0);

        // Check flag 5
        bc.emit(noir_op::LDFR);
        bc.emit(1);  // D1
        bc.emit_u16(5);

        // Check flag 10
        bc.emit(noir_op::LDFR);
        bc.emit(2);  // D2
        bc.emit_u16(10);

        // Check flag 1 (should still be clear)
        bc.emit(noir_op::LDFR);
        bc.emit(3);  // D3
        bc.emit_u16(1);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.reg_d(0) == 1, "flag 0 should be set");
        TEST_ASSERT(vm.reg_d(1) == 1, "flag 5 should be set");
        TEST_ASSERT(vm.reg_d(2) == 1, "flag 10 should be set");
        TEST_ASSERT(vm.reg_d(3) == 0, "flag 1 should be clear");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_setf_oob)
    {
        // Flag index 128 is out of bounds for 16-byte (128-bit) array
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::SETF);
        bc.emit_u16(128);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::err_flag_oob, "should error on OOB flag");
        TEST_ASSERT(vm.state() == noir_state::halted, "should be halted");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_setf_idempotent)
    {
        // Setting an already-set flag should have no effect
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::SETF);
        bc.emit_u16(42);

        bc.emit(noir_op::SETF);
        bc.emit_u16(42);  // set again

        bc.emit(noir_op::LDFR);
        bc.emit(0);  // D0
        bc.emit_u16(42);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 1, "flag 42 still set after double SETF");
        TEST_SUCCESS;
    }

    // ── CLRF ────────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_clrf_basic)
    {
        // Set then clear a flag
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::SETF);
        bc.emit_u16(7);

        bc.emit(noir_op::CLRF);
        bc.emit_u16(7);

        bc.emit(noir_op::LDFR);
        bc.emit(0);  // D0
        bc.emit_u16(7);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0, "flag 7 should be cleared");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_clrf_already_clear)
    {
        // Clear a flag that's already clear — should be fine
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::CLRF);
        bc.emit_u16(3);

        bc.emit(noir_op::LDFR);
        bc.emit(0);  // D0
        bc.emit_u16(3);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0, "flag 3 still clear");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_clrf_does_not_affect_neighbors)
    {
        // Set flags 7, 8, 9 then clear 8 — verify 7 and 9 still set
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::SETF);
        bc.emit_u16(7);
        bc.emit(noir_op::SETF);
        bc.emit_u16(8);
        bc.emit(noir_op::SETF);
        bc.emit_u16(9);

        bc.emit(noir_op::CLRF);
        bc.emit_u16(8);

        bc.emit(noir_op::LDFR);
        bc.emit(0);  // D0 = flag[7]
        bc.emit_u16(7);

        bc.emit(noir_op::LDFR);
        bc.emit(1);  // D1 = flag[8]
        bc.emit_u16(8);

        bc.emit(noir_op::LDFR);
        bc.emit(2);  // D2 = flag[9]
        bc.emit_u16(9);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.reg_d(0) == 1, "flag 7 still set");
        TEST_ASSERT(vm.reg_d(1) == 0, "flag 8 cleared");
        TEST_ASSERT(vm.reg_d(2) == 1, "flag 9 still set");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_clrf_oob)
    {
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::CLRF);
        bc.emit_u16(200);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::err_flag_oob, "should error on OOB");
        TEST_ASSERT(vm.state() == noir_state::halted, "should be halted");
        TEST_SUCCESS;
    }

    // ── TOGF ────────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_togf_set_to_clear)
    {
        // Set a flag then toggle it → should be clear
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::SETF);
        bc.emit_u16(15);

        bc.emit(noir_op::TOGF);
        bc.emit_u16(15);

        bc.emit(noir_op::LDFR);
        bc.emit(0);
        bc.emit_u16(15);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0, "toggling set flag → clear");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_togf_clear_to_set)
    {
        // Toggle a clear flag → should be set
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::TOGF);
        bc.emit_u16(20);

        bc.emit(noir_op::LDFR);
        bc.emit(0);
        bc.emit_u16(20);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 1, "toggling clear flag → set");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_togf_double_toggle)
    {
        // Toggle twice → back to original
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::SETF);
        bc.emit_u16(30);

        bc.emit(noir_op::TOGF);
        bc.emit_u16(30);

        bc.emit(noir_op::TOGF);
        bc.emit_u16(30);

        bc.emit(noir_op::LDFR);
        bc.emit(0);
        bc.emit_u16(30);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 1, "double toggle restores original");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_togf_oob)
    {
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::TOGF);
        bc.emit_u16(128);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::err_flag_oob, "should error on OOB");
        TEST_ASSERT(vm.state() == noir_state::halted, "should be halted");
        TEST_SUCCESS;
    }

    // ── TSTF ────────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_tstf_set_flag_clears_z)
    {
        // TSTF on a set flag → Z=0, meaning BNE = "flag is set"
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::SETF);
        bc.emit_u16(50);

        bc.emit(noir_op::TSTF);
        bc.emit_u16(50);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT((vm.sr() & noir_sr::Z) == 0, "Z=0 when flag is set");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_tstf_clear_flag_sets_z)
    {
        // TSTF on a clear flag → Z=1, meaning BEQ = "flag is clear"
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::TSTF);
        bc.emit_u16(50);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT((vm.sr() & noir_sr::Z) != 0, "Z=1 when flag is clear");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_tstf_does_not_modify_flag)
    {
        // TSTF should test the flag without altering it
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::SETF);
        bc.emit_u16(60);

        bc.emit(noir_op::TSTF);
        bc.emit_u16(60);

        // Flag should still be set after test
        bc.emit(noir_op::LDFR);
        bc.emit(0);
        bc.emit_u16(60);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 1, "flag unchanged after TSTF");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_tstf_bne_pattern)
    {
        // Common pattern: TSTF + BNE = "if flag is set"
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::SETF);
        bc.emit_u16(42);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0);

        bc.emit(noir_op::TSTF);
        bc.emit_u16(42);

        // BNE = Z=0 = "flag is set"
        bc.emit(noir_op::BNE);
        bc.emit_s16(4);  // skip MOV16

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(999);

        // D0 = 1 (flag was set, skipped the 999 overwrite)
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 1, "TSTF+BNE: flag set → branch taken");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_tstf_beq_pattern)
    {
        // Common pattern: TSTF + BEQ = "if flag is clear"
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        // Flag 42 is clear (default)
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0);

        bc.emit(noir_op::TSTF);
        bc.emit_u16(42);

        // BEQ = Z=1 = "flag is clear"
        bc.emit(noir_op::BEQ);
        bc.emit_s16(4);  // skip MOV16

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(999);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 1, "TSTF+BEQ: flag clear → branch taken");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_tstf_oob)
    {
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::TSTF);
        bc.emit_u16(128);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::err_flag_oob, "should error on OOB");
        TEST_ASSERT(vm.state() == noir_state::halted, "should be halted");
        TEST_SUCCESS;
    }

    // ── SETFR ───────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_setfr_d0)
    {
        // D0 = 10, SETFR D0 → set flag[10]
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::SETFR);
        bc.emit(0);  // reg_id = D0

        bc.emit(noir_op::LDFR);
        bc.emit(1);  // D1 = flag[10]
        bc.emit_u16(10);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.reg_d(1) == 1, "flag[D0=10] should be set");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_setfr_d1)
    {
        // D1 = 25, SETFR D1 → set flag[25]
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(25);

        bc.emit(noir_op::SETFR);
        bc.emit(1);  // reg_id = D1

        bc.emit(noir_op::LDFR);
        bc.emit(0);  // D0 = flag[25]
        bc.emit_u16(25);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 1, "flag[D1=25] should be set");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_setfr_oob)
    {
        // D0 = 200 (OOB), SETFR D0 → err_flag_oob
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(200);

        bc.emit(noir_op::SETFR);
        bc.emit(0);  // reg_id = D0

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::err_flag_oob, "should error on OOB");
        TEST_ASSERT(vm.state() == noir_state::halted, "should be halted");
        TEST_SUCCESS;
    }

    // ── CLRFR ───────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_clrfr_basic)
    {
        // Set flag 15, then D0=15, CLRFR D0 → flag 15 cleared
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::SETF);
        bc.emit_u16(15);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(15);

        bc.emit(noir_op::CLRFR);
        bc.emit(0);  // reg_id = D0

        bc.emit(noir_op::LDFR);
        bc.emit(1);  // D1 = flag[15]
        bc.emit_u16(15);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.reg_d(1) == 0, "flag[D0=15] should be cleared");
        TEST_SUCCESS;
    }

    // ── TOGFR ───────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_togfr_basic)
    {
        // D0=30, TOGFR D0 → flag 30 toggled (clear→set)
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(30);

        bc.emit(noir_op::TOGFR);
        bc.emit(0);  // reg_id = D0

        bc.emit(noir_op::LDFR);
        bc.emit(1);  // D1 = flag[30]
        bc.emit_u16(30);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.reg_d(1) == 1, "flag[D0=30] toggled clear→set");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_togfr_d2)
    {
        // D2=50, set flag 50, TOGFR D2 → flag 50 toggled (set→clear)
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::SETF);
        bc.emit_u16(50);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D2, noir_mode::IMM16);
        bc.emit_u16(50);

        bc.emit(noir_op::TOGFR);
        bc.emit(2);  // reg_id = D2

        bc.emit(noir_op::LDFR);
        bc.emit(0);  // D0 = flag[50]
        bc.emit_u16(50);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0, "flag[D2=50] toggled set→clear");
        TEST_SUCCESS;
    }

    // ── TSTFR ───────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_tstfr_set_flag)
    {
        // Set flag 40, D0=40, TSTFR D0 → Z=0 (flag is set)
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::SETF);
        bc.emit_u16(40);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(40);

        bc.emit(noir_op::TSTFR);
        bc.emit(0);  // reg_id = D0

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT((vm.sr() & noir_sr::Z) == 0, "Z=0 when flag is set");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_tstfr_clear_flag)
    {
        // Flag 40 is clear, D0=40, TSTFR D0 → Z=1
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(40);

        bc.emit(noir_op::TSTFR);
        bc.emit(0);  // reg_id = D0

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT((vm.sr() & noir_sr::Z) != 0, "Z=1 when flag is clear");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_tstfr_d3)
    {
        // D3=100, set flag 100, TSTFR D3 → Z=0
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::SETF);
        bc.emit_u16(100);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D3, noir_mode::IMM16);
        bc.emit_u16(100);

        bc.emit(noir_op::TSTFR);
        bc.emit(3);  // reg_id = D3

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT((vm.sr() & noir_sr::Z) == 0, "Z=0 when flag[D3=100] is set");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_tstfr_oob)
    {
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(999);

        bc.emit(noir_op::TSTFR);
        bc.emit(0);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::err_flag_oob, "should error on OOB");
        TEST_ASSERT(vm.state() == noir_state::halted, "should be halted");
        TEST_SUCCESS;
    }

    // ── LDFR ────────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_ldfr_set_flag)
    {
        // Set flag 5, LDFR D0, #5 → D0=1, NZ: Z=0, N=0
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::SETF);
        bc.emit_u16(5);

        bc.emit(noir_op::LDFR);
        bc.emit(0);  // D0
        bc.emit_u16(5);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 1, "D0 = flag[5] = 1");
        TEST_ASSERT((vm.sr() & noir_sr::Z) == 0, "Z clear (val=1)");
        TEST_ASSERT((vm.sr() & noir_sr::N) == 0, "N always clear for 0/1");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_ldfr_clear_flag)
    {
        // Flag 5 is clear, LDFR D0, #5 → D0=0, Z=1, N=0
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::LDFR);
        bc.emit(0);  // D0
        bc.emit_u16(5);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0, "D0 = flag[5] = 0");
        TEST_ASSERT((vm.sr() & noir_sr::Z) != 0, "Z set (val=0)");
        TEST_ASSERT((vm.sr() & noir_sr::N) == 0, "N always clear for 0/1");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_ldfr_into_d2)
    {
        // LDFR D2, #80 — verify it writes to the correct register
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::SETF);
        bc.emit_u16(80);

        bc.emit(noir_op::LDFR);
        bc.emit(2);  // D2
        bc.emit_u16(80);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.reg_d(2) == 1, "D2 = flag[80] = 1");
        // D0 should be unaffected (0)
        TEST_ASSERT(vm.reg_d(0) == 0, "D0 not modified by LDFR D2");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_ldfr_oob)
    {
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::LDFR);
        bc.emit(0);
        bc.emit_u16(128);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::err_flag_oob, "should error on OOB");
        TEST_ASSERT(vm.state() == noir_state::halted, "should be halted");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_ldfr_overwrites_register)
    {
        // D0 = 0x1234, then LDFR D0, #5 → D0 should be 0 or 1, not 0x1234
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0x1234);

        bc.emit(noir_op::LDFR);
        bc.emit(0);  // D0
        bc.emit_u16(5);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0, "LDFR overwrites D0 with 0");
        TEST_SUCCESS;
    }

    // ── STFR ────────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_stfr_set_from_odd)
    {
        // D0 = 1 (bit 0 = 1), STFR D0, #70 → flag 70 = 1
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);

        bc.emit(noir_op::STFR);
        bc.emit(0);  // reg_id = D0
        bc.emit_u16(70);

        bc.emit(noir_op::LDFR);
        bc.emit(1);  // D1 = flag[70]
        bc.emit_u16(70);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.reg_d(1) == 1, "flag[70] = D0 bit 0 = 1");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_stfr_clear_from_even)
    {
        // D0 = 2 (bit 0 = 0), STFR D0, #70 → flag 70 = 0
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::SETF);
        bc.emit_u16(70);  // pre-set

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(2);  // bit 0 = 0

        bc.emit(noir_op::STFR);
        bc.emit(0);  // reg_id = D0
        bc.emit_u16(70);

        bc.emit(noir_op::LDFR);
        bc.emit(1);  // D1 = flag[70]
        bc.emit_u16(70);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.reg_d(1) == 0, "flag[70] = D0 bit 0 = 0 (D0=2)");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_stfr_uses_bit0_only)
    {
        // D0 = 0xFFFE (bit 0 = 0), STFR D0, #75 → flag 75 = 0
        // D0 = 0xFF01 (bit 0 = 1), STFR D0, #76 → flag 76 = 1
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0xFFFE);

        bc.emit(noir_op::STFR);
        bc.emit(0);
        bc.emit_u16(75);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0xFF01);

        bc.emit(noir_op::STFR);
        bc.emit(0);
        bc.emit_u16(76);

        bc.emit(noir_op::LDFR);
        bc.emit(0);  // D0 = flag[75]
        bc.emit_u16(75);

        bc.emit(noir_op::LDFR);
        bc.emit(1);  // D1 = flag[76]
        bc.emit_u16(76);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.reg_d(0) == 0, "0xFFFE bit 0 = 0 → flag 75 clear");
        TEST_ASSERT(vm.reg_d(1) == 1, "0xFF01 bit 0 = 1 → flag 76 set");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_stfr_from_d3)
    {
        // STFR D3, #90 — uses register D3
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D3, noir_mode::IMM16);
        bc.emit_u16(1);

        bc.emit(noir_op::STFR);
        bc.emit(3);  // reg_id = D3
        bc.emit_u16(90);

        bc.emit(noir_op::LDFR);
        bc.emit(0);  // D0 = flag[90]
        bc.emit_u16(90);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 1, "flag[90] = D3 bit 0 = 1");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_stfr_oob)
    {
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::STFR);
        bc.emit(0);
        bc.emit_u16(128);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::err_flag_oob, "should error on OOB");
        TEST_ASSERT(vm.state() == noir_state::halted, "should be halted");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_stfr_no_flags_affected)
    {
        // STFR should NOT modify SR flags (reference says "—")
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        // Set up known SR state: CMP 10,10 → Z=1
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);
        // Now Z=1

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);

        bc.emit(noir_op::STFR);
        bc.emit(0);
        bc.emit_u16(0);

        // SR should still have Z=1 from the CMP
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT((vm.sr() & noir_sr::Z) != 0, "Z unchanged after STFR");
        TEST_SUCCESS;
    }

    // ── Cross-byte boundary flags ───────────────────────────────────────────────

    TEST_IMPL(test_noir_flag_cross_byte_boundary)
    {
        // Flags 7 and 8 are in different bytes (byte 0 bit 7, byte 1 bit 0)
        // Setting both should work independently
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::SETF);
        bc.emit_u16(7);

        bc.emit(noir_op::SETF);
        bc.emit_u16(8);

        bc.emit(noir_op::LDFR);
        bc.emit(0);  // D0 = flag[7]
        bc.emit_u16(7);

        bc.emit(noir_op::LDFR);
        bc.emit(1);  // D1 = flag[8]
        bc.emit_u16(8);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.reg_d(0) == 1, "flag 7 set (byte 0, bit 7)");
        TEST_ASSERT(vm.reg_d(1) == 1, "flag 8 set (byte 1, bit 0)");
        TEST_SUCCESS;
    }

    // ── SETF/CLRF round-trip with odd bits ──────────────────────────────────────

    TEST_IMPL(test_noir_flag_all_bits_in_byte)
    {
        // Set all 8 flags in byte 0 (flags 0-7), clear flag 3, verify the rest
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        for (uint16_t i = 0; i < 8; ++i)
        {
            bc.emit(noir_op::SETF);
            bc.emit_u16(i);
        }

        bc.emit(noir_op::CLRF);
        bc.emit_u16(3);

        bc.emit(noir_op::LDFR);
        bc.emit(0);  // D0 = flag[3]
        bc.emit_u16(3);

        bc.emit(noir_op::LDFR);
        bc.emit(1);  // D1 = flag[4]
        bc.emit_u16(4);

        bc.emit(noir_op::LDFR);
        bc.emit(2);  // D2 = flag[2]
        bc.emit_u16(2);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.reg_d(0) == 0, "flag 3 cleared");
        TEST_ASSERT(vm.reg_d(1) == 1, "flag 4 still set");
        TEST_ASSERT(vm.reg_d(2) == 1, "flag 2 still set");
        TEST_SUCCESS;
    }

    // ── Invalid opcode ──────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_flag_ops_invalid_opcode)
    {
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        // 0x5F is reserved
        bc.emit(0x5F);
        bc.emit_u16(0);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::err_invalid_opcode, "reserved opcode should error");
        TEST_SUCCESS;
    }

    // ═════════════════════════════════════════════════════════════════════════════
    // Test suite registration
    // ═════════════════════════════════════════════════════════════════════════════

    TEST_SUITE_BEGIN(noir_vm_flag_ops)
    // SETF
    TEST(test_noir_setf_basic)
    TEST(test_noir_setf_high_index)
    TEST(test_noir_setf_multiple_flags)
    TEST(test_noir_setf_oob)
    TEST(test_noir_setf_idempotent)
    // CLRF
    TEST(test_noir_clrf_basic)
    TEST(test_noir_clrf_already_clear)
    TEST(test_noir_clrf_does_not_affect_neighbors)
    TEST(test_noir_clrf_oob)
    // TOGF
    TEST(test_noir_togf_set_to_clear)
    TEST(test_noir_togf_clear_to_set)
    TEST(test_noir_togf_double_toggle)
    TEST(test_noir_togf_oob)
    // TSTF
    TEST(test_noir_tstf_set_flag_clears_z)
    TEST(test_noir_tstf_clear_flag_sets_z)
    TEST(test_noir_tstf_does_not_modify_flag)
    TEST(test_noir_tstf_bne_pattern)
    TEST(test_noir_tstf_beq_pattern)
    TEST(test_noir_tstf_oob)
    // SETFR
    TEST(test_noir_setfr_d0)
    TEST(test_noir_setfr_d1)
    TEST(test_noir_setfr_oob)
    // CLRFR
    TEST(test_noir_clrfr_basic)
    // TOGFR
    TEST(test_noir_togfr_basic)
    TEST(test_noir_togfr_d2)
    // TSTFR
    TEST(test_noir_tstfr_set_flag)
    TEST(test_noir_tstfr_clear_flag)
    TEST(test_noir_tstfr_d3)
    TEST(test_noir_tstfr_oob)
    // LDFR
    TEST(test_noir_ldfr_set_flag)
    TEST(test_noir_ldfr_clear_flag)
    TEST(test_noir_ldfr_into_d2)
    TEST(test_noir_ldfr_oob)
    TEST(test_noir_ldfr_overwrites_register)
    // STFR
    TEST(test_noir_stfr_set_from_odd)
    TEST(test_noir_stfr_clear_from_even)
    TEST(test_noir_stfr_uses_bit0_only)
    TEST(test_noir_stfr_from_d3)
    TEST(test_noir_stfr_oob)
    TEST(test_noir_stfr_no_flags_affected)
    // Edge cases
    TEST(test_noir_flag_cross_byte_boundary)
    TEST(test_noir_flag_all_bits_in_byte)
    // Invalid
    TEST(test_noir_flag_ops_invalid_opcode)
    TEST_SUITE_END

}  // namespace NEONengine

#endif  // ACE_TEST_RUNNER
#endif  // __NOIR_VM_FLAG_OPS_TESTS_H__INCLUDED__
