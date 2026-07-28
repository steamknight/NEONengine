#ifndef __NOIR_VM_VALUE_OPS_TESTS_H__INCLUDED__
#define __NOIR_VM_VALUE_OPS_TESTS_H__INCLUDED__

#ifdef ACE_TEST_RUNNER

#include "types.h"

#include "tests/noir_vm_test_utils.h"

namespace NEONengine
{
    // ═════════════════════════════════════════════════════════════════════════════
    // Value Operations opcodes (0x60–0x6F)
    //   CMPV, TSTV, CLRV, INCV, DECV
    //
    // VALUES is a borrowed u16 array (32 entries in test builder).
    // Encoding: [op] [u16 index] (3 bytes), CMPV adds [u16 imm] (5 bytes).
    // OOB → err_value_oob, HALT.
    //
    // SR flags:
    //   CMPV: NZCV (subtraction flags: value[idx] - imm)
    //   TSTV: NZ   (tests value itself)
    //   CLRV: —    (no flags changed)
    //   INCV: NZCV (addition flags: value[idx] + 1)
    //   DECV: NZCV (subtraction flags: value[idx] - 1)
    // ═════════════════════════════════════════════════════════════════════════════

    // ── CMPV ────────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_cmpv_equal)
    {
        // value[0] = 100, CMPV #0, 100 → Z=1
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.values[0] = 100;

        bc.emit(noir_op::CMPV);
        bc.emit_u16(0);    // index
        bc.emit_u16(100);  // immediate

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT((vm.sr() & noir_sr::Z) != 0, "Z=1 when value == imm");
        TEST_ASSERT((vm.sr() & noir_sr::N) == 0, "N=0");
        TEST_ASSERT((vm.sr() & noir_sr::C) == 0, "C=0 (no borrow)");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_cmpv_less_than)
    {
        // value[5] = 30, CMPV #5, 50 → 30-50 = -20 → N=1, C=1 (borrow)
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.values[5] = 30;

        bc.emit(noir_op::CMPV);
        bc.emit_u16(5);
        bc.emit_u16(50);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT((vm.sr() & noir_sr::Z) == 0, "Z=0 (not equal)");
        TEST_ASSERT((vm.sr() & noir_sr::N) != 0, "N=1 (result negative)");
        TEST_ASSERT((vm.sr() & noir_sr::C) != 0, "C=1 (borrow: 30 < 50)");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_cmpv_greater_than)
    {
        // value[3] = 200, CMPV #3, 50 → 200-50 = 150 → positive, no borrow
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.values[3] = 200;

        bc.emit(noir_op::CMPV);
        bc.emit_u16(3);
        bc.emit_u16(50);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT((vm.sr() & noir_sr::Z) == 0, "Z=0");
        TEST_ASSERT((vm.sr() & noir_sr::N) == 0, "N=0 (positive result)");
        TEST_ASSERT((vm.sr() & noir_sr::C) == 0, "C=0 (no borrow)");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_cmpv_does_not_modify_value)
    {
        // CMPV should not change the value
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.values[10] = 42;

        bc.emit(noir_op::CMPV);
        bc.emit_u16(10);
        bc.emit_u16(0);

        // Load value into D0 to verify
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::VALUE_IMM);
        bc.emit_u16(10);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 42, "CMPV should not modify value");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_cmpv_signed_overflow)
    {
        // value = 0x8000 (-32768), CMPV with 1 → 0x8000-1 = 0x7FFF → V=1 (signed overflow)
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.values[0] = 0x8000;

        bc.emit(noir_op::CMPV);
        bc.emit_u16(0);
        bc.emit_u16(1);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT((vm.sr() & noir_sr::V) != 0, "V=1 (signed overflow: neg - pos = pos)");
        TEST_ASSERT((vm.sr() & noir_sr::N) == 0, "N=0 (result 0x7FFF is positive)");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_cmpv_with_branch)
    {
        // Common pattern: CMPV + BEQ
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.values[0] = 10;

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0);

        bc.emit(noir_op::CMPV);
        bc.emit_u16(0);
        bc.emit_u16(10);  // values[0] == 10 → Z=1

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
        TEST_ASSERT(vm.exit_code() == 1, "CMPV+BEQ: value==10 → branch taken");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_cmpv_oob)
    {
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::CMPV);
        bc.emit_u16(32);  // OOB (32 values, indices 0-31)
        bc.emit_u16(0);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::err_value_oob, "should error on OOB");
        TEST_ASSERT(vm.state() == noir_state::halted, "should be halted");
        TEST_SUCCESS;
    }

    // ── TSTV ────────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_tstv_zero)
    {
        // value[0] = 0, TSTV → Z=1, N=0
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.values[0] = 0;

        bc.emit(noir_op::TSTV);
        bc.emit_u16(0);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT((vm.sr() & noir_sr::Z) != 0, "Z=1 when value is 0");
        TEST_ASSERT((vm.sr() & noir_sr::N) == 0, "N=0");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_tstv_positive)
    {
        // value[1] = 42, TSTV → Z=0, N=0
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.values[1] = 42;

        bc.emit(noir_op::TSTV);
        bc.emit_u16(1);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT((vm.sr() & noir_sr::Z) == 0, "Z=0 when value nonzero");
        TEST_ASSERT((vm.sr() & noir_sr::N) == 0, "N=0 for positive value");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_tstv_negative)
    {
        // value[2] = 0x8000 (negative), TSTV → Z=0, N=1
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.values[2] = 0x8000;

        bc.emit(noir_op::TSTV);
        bc.emit_u16(2);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT((vm.sr() & noir_sr::Z) == 0, "Z=0");
        TEST_ASSERT((vm.sr() & noir_sr::N) != 0, "N=1 when bit 15 set");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_tstv_does_not_modify_value)
    {
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.values[4] = 1234;

        bc.emit(noir_op::TSTV);
        bc.emit_u16(4);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::VALUE_IMM);
        bc.emit_u16(4);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 1234, "TSTV should not modify value");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_tstv_with_bne)
    {
        // Common pattern: TSTV + BNE = "if value != 0"
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.values[0] = 5;

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0);

        bc.emit(noir_op::TSTV);
        bc.emit_u16(0);  // value[0]=5 → Z=0

        bc.emit(noir_op::BNE);
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
        TEST_ASSERT(vm.exit_code() == 1, "TSTV+BNE: value!=0 → branch taken");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_tstv_oob)
    {
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::TSTV);
        bc.emit_u16(32);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::err_value_oob, "should error on OOB");
        TEST_ASSERT(vm.state() == noir_state::halted, "should be halted");
        TEST_SUCCESS;
    }

    // ── CLRV ────────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_clrv_basic)
    {
        // value[0] = 500, CLRV → value[0] = 0
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.values[0] = 500;

        bc.emit(noir_op::CLRV);
        bc.emit_u16(0);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::VALUE_IMM);
        bc.emit_u16(0);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0, "CLRV sets value to 0");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_clrv_already_zero)
    {
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.values[5] = 0;

        bc.emit(noir_op::CLRV);
        bc.emit_u16(5);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::VALUE_IMM);
        bc.emit_u16(5);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0, "CLRV on zero stays zero");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_clrv_does_not_affect_sr)
    {
        // CLRV should NOT modify SR flags (spec says "—")
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.values[0] = 100;

        // Set up known SR: CMP 10,10 → Z=1
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);
        // Now Z=1

        bc.emit(noir_op::CLRV);
        bc.emit_u16(0);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT((vm.sr() & noir_sr::Z) != 0, "Z unchanged after CLRV");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_clrv_does_not_affect_neighbors)
    {
        // Clear value[5] should not touch value[4] or value[6]
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.values[4] = 111;
        bc.values[5] = 222;
        bc.values[6] = 333;

        bc.emit(noir_op::CLRV);
        bc.emit_u16(5);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::VALUE_IMM);
        bc.emit_u16(4);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::VALUE_IMM);
        bc.emit_u16(5);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D2, noir_mode::VALUE_IMM);
        bc.emit_u16(6);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.reg_d(0) == 111, "value[4] unchanged");
        TEST_ASSERT(vm.reg_d(1) == 0, "value[5] cleared");
        TEST_ASSERT(vm.reg_d(2) == 333, "value[6] unchanged");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_clrv_oob)
    {
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::CLRV);
        bc.emit_u16(32);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::err_value_oob, "should error on OOB");
        TEST_ASSERT(vm.state() == noir_state::halted, "should be halted");
        TEST_SUCCESS;
    }

    // ── INCV ────────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_incv_basic)
    {
        // value[0] = 10, INCV → value[0] = 11
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.values[0] = 10;

        bc.emit(noir_op::INCV);
        bc.emit_u16(0);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::VALUE_IMM);
        bc.emit_u16(0);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 11, "10 + 1 = 11");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_incv_from_zero)
    {
        // value[3] = 0, INCV → value[3] = 1
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.values[3] = 0;

        bc.emit(noir_op::INCV);
        bc.emit_u16(3);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::VALUE_IMM);
        bc.emit_u16(3);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 1, "0 + 1 = 1");
        TEST_ASSERT((vm.sr() & noir_sr::Z) == 0, "Z=0 (result nonzero)");
        TEST_ASSERT((vm.sr() & noir_sr::N) == 0, "N=0");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_incv_overflow)
    {
        // value = 0xFFFF, INCV → wraps to 0x0000, C=1, Z=1
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.values[0] = 0xFFFF;

        bc.emit(noir_op::INCV);
        bc.emit_u16(0);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::VALUE_IMM);
        bc.emit_u16(0);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0, "0xFFFF + 1 wraps to 0");
        TEST_ASSERT((vm.sr() & noir_sr::C) != 0, "C=1 (unsigned overflow)");
        TEST_ASSERT((vm.sr() & noir_sr::Z) != 0, "Z=1 (result is 0)");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_incv_signed_overflow)
    {
        // value = 0x7FFF (32767), INCV → 0x8000 (-32768) → V=1, N=1
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.values[0] = 0x7FFF;

        bc.emit(noir_op::INCV);
        bc.emit_u16(0);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::VALUE_IMM);
        bc.emit_u16(0);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0x8000, "0x7FFF + 1 = 0x8000");
        TEST_ASSERT((vm.sr() & noir_sr::V) != 0, "V=1 (signed overflow)");
        TEST_ASSERT((vm.sr() & noir_sr::N) != 0, "N=1 (negative result)");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_incv_multiple)
    {
        // INCV three times: 0 → 1 → 2 → 3
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.values[0] = 0;

        bc.emit(noir_op::INCV);
        bc.emit_u16(0);

        bc.emit(noir_op::INCV);
        bc.emit_u16(0);

        bc.emit(noir_op::INCV);
        bc.emit_u16(0);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::VALUE_IMM);
        bc.emit_u16(0);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 3, "0 incremented 3 times = 3");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_incv_oob)
    {
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::INCV);
        bc.emit_u16(32);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::err_value_oob, "should error on OOB");
        TEST_ASSERT(vm.state() == noir_state::halted, "should be halted");
        TEST_SUCCESS;
    }

    // ── DECV ────────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_decv_basic)
    {
        // value[0] = 10, DECV → value[0] = 9
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.values[0] = 10;

        bc.emit(noir_op::DECV);
        bc.emit_u16(0);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::VALUE_IMM);
        bc.emit_u16(0);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 9, "10 - 1 = 9");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_decv_to_zero)
    {
        // value[0] = 1, DECV → 0, Z=1
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.values[0] = 1;

        bc.emit(noir_op::DECV);
        bc.emit_u16(0);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::VALUE_IMM);
        bc.emit_u16(0);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0, "1 - 1 = 0");
        TEST_ASSERT((vm.sr() & noir_sr::Z) != 0, "Z=1");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_decv_underflow)
    {
        // value = 0, DECV → 0xFFFF, C=1 (borrow), N=1
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.values[0] = 0;

        bc.emit(noir_op::DECV);
        bc.emit_u16(0);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::VALUE_IMM);
        bc.emit_u16(0);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0xFFFF, "0 - 1 wraps to 0xFFFF");
        TEST_ASSERT((vm.sr() & noir_sr::C) != 0, "C=1 (borrow)");
        TEST_ASSERT((vm.sr() & noir_sr::N) != 0, "N=1 (negative)");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_decv_signed_overflow)
    {
        // value = 0x8000 (-32768), DECV → 0x7FFF → V=1 (signed overflow: neg - pos = pos)
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.values[0] = 0x8000;

        bc.emit(noir_op::DECV);
        bc.emit_u16(0);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::VALUE_IMM);
        bc.emit_u16(0);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0x7FFF, "0x8000 - 1 = 0x7FFF");
        TEST_ASSERT((vm.sr() & noir_sr::V) != 0, "V=1 (signed overflow)");
        TEST_ASSERT((vm.sr() & noir_sr::N) == 0, "N=0 (positive result)");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_decv_multiple)
    {
        // DECV three times: 5 → 4 → 3 → 2
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.values[0] = 5;

        bc.emit(noir_op::DECV);
        bc.emit_u16(0);

        bc.emit(noir_op::DECV);
        bc.emit_u16(0);

        bc.emit(noir_op::DECV);
        bc.emit_u16(0);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::VALUE_IMM);
        bc.emit_u16(0);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 2, "5 decremented 3 times = 2");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_decv_oob)
    {
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::DECV);
        bc.emit_u16(32);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::err_value_oob, "should error on OOB");
        TEST_ASSERT(vm.state() == noir_state::halted, "should be halted");
        TEST_SUCCESS;
    }

    // ── Combined patterns ───────────────────────────────────────────────────────

    TEST_IMPL(test_noir_incv_then_cmpv_countdown)
    {
        // Count value[0] from 0 to 3 using INCV + CMPV + BLT loop
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.values[0] = 0;

        u16 loop = bc.mark();

        bc.emit(noir_op::INCV);
        bc.emit_u16(0);  // 3 bytes

        bc.emit(noir_op::CMPV);
        bc.emit_u16(0);
        bc.emit_u16(3);  // 5 bytes

        u16 blt_pos = bc.mark();
        bc.emit(noir_op::BLT);
        bc.emit_s16(to<i16>(loop - (blt_pos + 3)));  // 3 bytes

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::VALUE_IMM);
        bc.emit_u16(0);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 3, "value counted up to 3");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_decv_then_tstv_countdown)
    {
        // Decrement value[0] from 5 until zero using DECV + TSTV + BNE loop
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.values[0] = 5;

        // Count how many decrements in D0
        bc.emit(noir_op::CLR);
        bc.emit_mode(noir_mode::REG_D0, 0);  // 2 bytes

        u16 loop = bc.mark();

        bc.emit(noir_op::DECV);
        bc.emit_u16(0);  // 3 bytes

        bc.emit(noir_op::INC);
        bc.emit_mode(noir_mode::REG_D0, 0);  // 2 bytes

        bc.emit(noir_op::TSTV);
        bc.emit_u16(0);  // 3 bytes

        u16 bne_pos = bc.mark();
        bc.emit(noir_op::BNE);
        bc.emit_s16(to<i16>(loop - (bne_pos + 3)));  // 3 bytes

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 5, "decremented 5 times to reach 0");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_clrv_then_incv)
    {
        // Set value to large, CLRV, then INCV → should be 1
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.values[10] = 50000;

        bc.emit(noir_op::CLRV);
        bc.emit_u16(10);

        bc.emit(noir_op::INCV);
        bc.emit_u16(10);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::VALUE_IMM);
        bc.emit_u16(10);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 1, "CLRV then INCV = 1");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_value_independence)
    {
        // Modify different value indices and verify no cross-talk
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.values[0] = 10;
        bc.values[1] = 20;
        bc.values[2] = 30;

        bc.emit(noir_op::INCV);
        bc.emit_u16(0);  // values[0] = 11

        bc.emit(noir_op::DECV);
        bc.emit_u16(2);  // values[2] = 29

        // Check all three
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::VALUE_IMM);
        bc.emit_u16(0);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::VALUE_IMM);
        bc.emit_u16(1);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D2, noir_mode::VALUE_IMM);
        bc.emit_u16(2);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.reg_d(0) == 11, "value[0] incremented to 11");
        TEST_ASSERT(vm.reg_d(1) == 20, "value[1] unchanged at 20");
        TEST_ASSERT(vm.reg_d(2) == 29, "value[2] decremented to 29");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_value_high_index)
    {
        // Use index 31 (last valid)
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.values[31] = 100;

        bc.emit(noir_op::INCV);
        bc.emit_u16(31);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::VALUE_IMM);
        bc.emit_u16(31);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 101, "value[31] = 100 + 1 = 101");
        TEST_SUCCESS;
    }

    // ── Invalid opcode ──────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_value_ops_invalid_opcode)
    {
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        // 0x6F is reserved
        bc.emit(0x6F);
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

    TEST_SUITE_BEGIN(noir_vm_value_ops)
    // CMPV
    TEST(test_noir_cmpv_equal)
    TEST(test_noir_cmpv_less_than)
    TEST(test_noir_cmpv_greater_than)
    TEST(test_noir_cmpv_does_not_modify_value)
    TEST(test_noir_cmpv_signed_overflow)
    TEST(test_noir_cmpv_with_branch)
    TEST(test_noir_cmpv_oob)
    // TSTV
    TEST(test_noir_tstv_zero)
    TEST(test_noir_tstv_positive)
    TEST(test_noir_tstv_negative)
    TEST(test_noir_tstv_does_not_modify_value)
    TEST(test_noir_tstv_with_bne)
    TEST(test_noir_tstv_oob)
    // CLRV
    TEST(test_noir_clrv_basic)
    TEST(test_noir_clrv_already_zero)
    TEST(test_noir_clrv_does_not_affect_sr)
    TEST(test_noir_clrv_does_not_affect_neighbors)
    TEST(test_noir_clrv_oob)
    // INCV
    TEST(test_noir_incv_basic)
    TEST(test_noir_incv_from_zero)
    TEST(test_noir_incv_overflow)
    TEST(test_noir_incv_signed_overflow)
    TEST(test_noir_incv_multiple)
    TEST(test_noir_incv_oob)
    // DECV
    TEST(test_noir_decv_basic)
    TEST(test_noir_decv_to_zero)
    TEST(test_noir_decv_underflow)
    TEST(test_noir_decv_signed_overflow)
    TEST(test_noir_decv_multiple)
    TEST(test_noir_decv_oob)
    // Combined patterns
    TEST(test_noir_incv_then_cmpv_countdown)
    TEST(test_noir_decv_then_tstv_countdown)
    TEST(test_noir_clrv_then_incv)
    TEST(test_noir_value_independence)
    TEST(test_noir_value_high_index)
    // Invalid
    TEST(test_noir_value_ops_invalid_opcode)
    TEST_SUITE_END

}  // namespace NEONengine

#endif  // ACE_TEST_RUNNER
#endif  // __NOIR_VM_VALUE_OPS_TESTS_H__INCLUDED__
