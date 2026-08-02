#ifndef __NOIR_VM_ARITHMETIC_TESTS_H__INCLUDED__
#define __NOIR_VM_ARITHMETIC_TESTS_H__INCLUDED__

#ifdef ACE_TEST_RUNNER

#include "types.h"

#include "tests/noir_vm_test_utils.h"

namespace NEONengine
{
    // ═════════════════════════════════════════════════════════════════════════════
    // Arithmetic opcodes (0x20–0x2F)
    //   ADD, SUB, MUL, DIV, MOD, CMP, NEG, INC, DEC
    //
    // All two-operand: mode byte [dst:4][src:4], result in dst.
    // Single-operand (NEG, INC, DEC): mode byte [dst:4][0000].
    // Flag helpers: N=0x80, Z=0x40, C=0x20, V=0x10
    // ═════════════════════════════════════════════════════════════════════════════

    // ── ADD ──────────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_add_reg_imm16)
    {
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        // D0 = 100
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(100);

        // D0 += 200
        bc.emit(noir_op::ADD);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(200);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 300, "100 + 200 = 300");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_add_reg_reg)
    {
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0x1000);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(0x0234);

        // D0 += D1
        bc.emit(noir_op::ADD);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D1);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0x1234, "0x1000 + 0x0234 = 0x1234");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_add_sets_zero_flag)
    {
        // 0 + 0 = 0 → Z=1, N=0
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::CLR);
        bc.emit_mode(noir_mode::REG_D0, 0);

        bc.emit(noir_op::ADD);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0, "0 + 0 = 0");
        TEST_ASSERT((vm.sr() & noir_sr::Z) != 0, "Z should be set");
        TEST_ASSERT((vm.sr() & noir_sr::N) == 0, "N should be clear");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_add_sets_negative_flag)
    {
        // Add two values that produce a negative (bit 15 set) result
        // 0x7FFF + 1 = 0x8000 → N=1, V=1 (signed overflow)
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0x7FFF);

        bc.emit(noir_op::ADD);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0x8000, "0x7FFF + 1 = 0x8000");
        TEST_ASSERT((vm.sr() & noir_sr::N) != 0, "N should be set");
        TEST_ASSERT((vm.sr() & noir_sr::V) != 0, "V should be set (signed overflow)");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_add_sets_carry_flag)
    {
        // 0xFFFF + 1 = 0x10000 → wraps to 0x0000, C=1
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0xFFFF);

        bc.emit(noir_op::ADD);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0x0000, "0xFFFF + 1 wraps to 0");
        TEST_ASSERT((vm.sr() & noir_sr::C) != 0, "C should be set (unsigned overflow)");
        TEST_ASSERT((vm.sr() & noir_sr::Z) != 0, "Z should be set");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_add_no_overflow_flags_clear)
    {
        // Simple add with no overflow: 10 + 20 = 30
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::ADD);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(20);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 30, "10 + 20 = 30");
        TEST_ASSERT((vm.sr() & noir_sr::N) == 0, "N clear");
        TEST_ASSERT((vm.sr() & noir_sr::Z) == 0, "Z clear");
        TEST_ASSERT((vm.sr() & noir_sr::C) == 0, "C clear");
        TEST_ASSERT((vm.sr() & noir_sr::V) == 0, "V clear");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_add_local_dst)
    {
        // ADD with LOCAL as destination — verifies cached read/write
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::LOCALS);
        bc.emit(2);

        // [FP-2] = 50
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::LOCAL, noir_mode::IMM16);
        bc.emit(to<u8>(-2));
        bc.emit_u16(50);

        // [FP-2] += 25
        bc.emit(noir_op::ADD);
        bc.emit_mode(noir_mode::LOCAL, noir_mode::IMM16);
        bc.emit(to<u8>(-2));
        bc.emit_u16(25);

        // D0 = [FP-2]
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::LOCAL);
        bc.emit(to<u8>(-2));

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 75, "local 50 + 25 = 75");
        TEST_SUCCESS;
    }

    // ── SUB ──────────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_sub_reg_imm16)
    {
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(500);

        bc.emit(noir_op::SUB);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(200);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 300, "500 - 200 = 300");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_sub_to_zero)
    {
        // 42 - 42 = 0 → Z=1
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(42);

        bc.emit(noir_op::SUB);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(42);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0, "42 - 42 = 0");
        TEST_ASSERT((vm.sr() & noir_sr::Z) != 0, "Z should be set");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_sub_underflow_sets_carry)
    {
        // 3 - 5 = -2 (0xFFFE unsigned) → C=1 (borrow), N=1
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(3);

        bc.emit(noir_op::SUB);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(5);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0xFFFE, "3 - 5 = 0xFFFE (unsigned)");
        TEST_ASSERT((vm.sr() & noir_sr::C) != 0, "C should be set (borrow)");
        TEST_ASSERT((vm.sr() & noir_sr::N) != 0, "N should be set (negative)");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_sub_signed_overflow)
    {
        // 0x8000 - 1 = 0x7FFF → signed overflow (neg - pos = pos)
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0x8000);

        bc.emit(noir_op::SUB);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0x7FFF, "0x8000 - 1 = 0x7FFF");
        TEST_ASSERT((vm.sr() & noir_sr::V) != 0, "V should be set (signed overflow)");
        TEST_ASSERT((vm.sr() & noir_sr::N) == 0, "N should be clear");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_sub_reg_reg)
    {
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1000);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(250);

        bc.emit(noir_op::SUB);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D1);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 750, "1000 - 250 = 750");
        TEST_SUCCESS;
    }

    // ── MUL ──────────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_mul_basic)
    {
        // 7 * 6 = 42
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(7);

        bc.emit(noir_op::MUL);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(6);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 42, "7 * 6 = 42");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_mul_by_zero)
    {
        // Anything * 0 = 0 → Z=1
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(12345);

        bc.emit(noir_op::MUL);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0, "x * 0 = 0");
        TEST_ASSERT((vm.sr() & noir_sr::Z) != 0, "Z should be set");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_mul_negative)
    {
        // 10 * (-3) = -30 → 0xFFE2 → N=1
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::MUL);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(to<u16>(-3));  // 0xFFFD

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == to<u16>(-30), "10 * -3 = -30");
        TEST_ASSERT((vm.sr() & noir_sr::N) != 0, "N should be set");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_mul_overflow_sets_v)
    {
        // 256 * 256 = 65536 → truncated to 0x0000, V=1
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(256);

        bc.emit(noir_op::MUL);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(256);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0, "256*256 truncated to 0");
        TEST_ASSERT((vm.sr() & noir_sr::V) != 0, "V should be set (truncated)");
        TEST_ASSERT((vm.sr() & noir_sr::Z) != 0, "Z should be set");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_mul_reg_reg)
    {
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(15);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(20);

        bc.emit(noir_op::MUL);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D1);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 300, "15 * 20 = 300");
        TEST_SUCCESS;
    }

    // ── DIV ──────────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_div_basic)
    {
        // 42 / 6 = 7
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(42);

        bc.emit(noir_op::DIV);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(6);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 7, "42 / 6 = 7");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_div_negative)
    {
        // -30 / 10 = -3 → 0xFFFD
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(to<u16>(-30));

        bc.emit(noir_op::DIV);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == to<u16>(-3), "-30 / 10 = -3");
        TEST_ASSERT((vm.sr() & noir_sr::N) != 0, "N should be set");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_div_truncation)
    {
        // 7 / 2 = 3 (integer truncation)
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(7);

        bc.emit(noir_op::DIV);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(2);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 3, "7 / 2 = 3 (truncated)");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_div_by_zero_halts)
    {
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(42);

        bc.emit(noir_op::DIV);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0);

        bc.emit(noir_op::RET);  // should not reach

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(failed_with(status, noir_error::division_zero), "div by zero should error");
        TEST_ASSERT(vm.state() == noir_session_state::died, "should be dead");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_div_result_zero)
    {
        // 0 / 5 = 0 → Z=1
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::CLR);
        bc.emit_mode(noir_mode::REG_D0, 0);

        bc.emit(noir_op::DIV);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(5);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0, "0 / 5 = 0");
        TEST_ASSERT((vm.sr() & noir_sr::Z) != 0, "Z should be set");
        TEST_SUCCESS;
    }

    // ── MOD ──────────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_mod_basic)
    {
        // 17 % 5 = 2
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(17);

        bc.emit(noir_op::MOD);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(5);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 2, "17 % 5 = 2");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_mod_even_division)
    {
        // 20 % 5 = 0 → Z=1
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(20);

        bc.emit(noir_op::MOD);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(5);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0, "20 % 5 = 0");
        TEST_ASSERT((vm.sr() & noir_sr::Z) != 0, "Z should be set");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_mod_negative)
    {
        // -7 % 3 = -1 (C truncation toward zero)
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(to<u16>(-7));

        bc.emit(noir_op::MOD);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(3);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == to<u16>(-1), "-7 % 3 = -1");
        TEST_ASSERT((vm.sr() & noir_sr::N) != 0, "N should be set");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_mod_by_zero_halts)
    {
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(42);

        bc.emit(noir_op::MOD);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(failed_with(status, noir_error::division_zero), "mod by zero should error");
        TEST_ASSERT(vm.state() == noir_session_state::died, "should be dead");
        TEST_SUCCESS;
    }

    // ── CMP ──────────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_cmp_equal)
    {
        // CMP 42, 42 → Z=1, C=0
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(42);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(42);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 42, "CMP should not modify D0");
        TEST_ASSERT((vm.sr() & noir_sr::Z) != 0, "Z should be set (equal)");
        TEST_ASSERT((vm.sr() & noir_sr::C) == 0, "C should be clear");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_cmp_less_than)
    {
        // CMP 3, 10 → 3 < 10 → C=1 (borrow), N=1
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(3);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 3, "CMP should not modify D0");
        TEST_ASSERT((vm.sr() & noir_sr::Z) == 0, "Z should be clear");
        TEST_ASSERT((vm.sr() & noir_sr::C) != 0, "C should be set (borrow)");
        TEST_ASSERT((vm.sr() & noir_sr::N) != 0, "N should be set (negative result)");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_cmp_greater_than)
    {
        // CMP 10, 3 → 10 > 3 → no borrow, result positive
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(3);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 10, "CMP should not modify D0");
        TEST_ASSERT((vm.sr() & noir_sr::Z) == 0, "Z clear");
        TEST_ASSERT((vm.sr() & noir_sr::C) == 0, "C clear (no borrow)");
        TEST_ASSERT((vm.sr() & noir_sr::N) == 0, "N clear");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_cmp_does_not_modify_dst)
    {
        // CMP must discard the subtraction result
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(100);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(50);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D1);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 100, "D0 must be unchanged after CMP");
        TEST_ASSERT(vm.reg_d(1) == 50, "D1 must be unchanged after CMP");
        TEST_SUCCESS;
    }

    // ── NEG ──────────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_neg_positive)
    {
        // NEG 42 → -42 (0xFFD6)
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(42);

        bc.emit(noir_op::NEG);
        bc.emit_mode(noir_mode::REG_D0, 0);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == to<u16>(-42), "NEG 42 = -42");
        TEST_ASSERT((vm.sr() & noir_sr::N) != 0, "N should be set");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_neg_negative)
    {
        // NEG(-42) → 42
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(to<u16>(-42));

        bc.emit(noir_op::NEG);
        bc.emit_mode(noir_mode::REG_D0, 0);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 42, "NEG(-42) = 42");
        TEST_ASSERT((vm.sr() & noir_sr::N) == 0, "N should be clear");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_neg_zero)
    {
        // NEG 0 → 0, Z=1
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::CLR);
        bc.emit_mode(noir_mode::REG_D0, 0);

        bc.emit(noir_op::NEG);
        bc.emit_mode(noir_mode::REG_D0, 0);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0, "NEG 0 = 0");
        TEST_ASSERT((vm.sr() & noir_sr::Z) != 0, "Z should be set");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_neg_min_int)
    {
        // NEG 0x8000 → 0x8000 (two's complement wrap), V=1
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0x8000);

        bc.emit(noir_op::NEG);
        bc.emit_mode(noir_mode::REG_D0, 0);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0x8000, "NEG 0x8000 wraps to 0x8000");
        TEST_ASSERT((vm.sr() & noir_sr::N) != 0, "N should be set");
        TEST_ASSERT((vm.sr() & noir_sr::V) != 0, "V should be set (overflow)");
        TEST_SUCCESS;
    }

    // ── INC ──────────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_inc_basic)
    {
        // INC 41 → 42
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(41);

        bc.emit(noir_op::INC);
        bc.emit_mode(noir_mode::REG_D0, 0);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 42, "INC 41 = 42");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_inc_overflow)
    {
        // INC 0xFFFF → 0x0000, C=1, Z=1
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0xFFFF);

        bc.emit(noir_op::INC);
        bc.emit_mode(noir_mode::REG_D0, 0);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0, "INC 0xFFFF wraps to 0");
        TEST_ASSERT((vm.sr() & noir_sr::C) != 0, "C should be set (overflow)");
        TEST_ASSERT((vm.sr() & noir_sr::Z) != 0, "Z should be set");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_inc_signed_overflow)
    {
        // INC 0x7FFF → 0x8000, V=1 (positive → negative)
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0x7FFF);

        bc.emit(noir_op::INC);
        bc.emit_mode(noir_mode::REG_D0, 0);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0x8000, "INC 0x7FFF = 0x8000");
        TEST_ASSERT((vm.sr() & noir_sr::N) != 0, "N should be set");
        TEST_ASSERT((vm.sr() & noir_sr::V) != 0, "V should be set (signed overflow)");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_inc_local)
    {
        // INC on a local variable — exercises cached dst path
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::LOCALS);
        bc.emit(2);

        // [FP-2] = 99
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::LOCAL, noir_mode::IMM16);
        bc.emit(to<u8>(-2));
        bc.emit_u16(99);

        // INC [FP-2]
        bc.emit(noir_op::INC);
        bc.emit_mode(noir_mode::LOCAL, 0);
        bc.emit(to<u8>(-2));

        // D0 = [FP-2]
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::LOCAL);
        bc.emit(to<u8>(-2));

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 100, "INC local 99 = 100");
        TEST_SUCCESS;
    }

    // ── DEC ──────────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_dec_basic)
    {
        // DEC 43 → 42
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(43);

        bc.emit(noir_op::DEC);
        bc.emit_mode(noir_mode::REG_D0, 0);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 42, "DEC 43 = 42");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_dec_to_zero)
    {
        // DEC 1 → 0, Z=1
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);

        bc.emit(noir_op::DEC);
        bc.emit_mode(noir_mode::REG_D0, 0);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0, "DEC 1 = 0");
        TEST_ASSERT((vm.sr() & noir_sr::Z) != 0, "Z should be set");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_dec_underflow)
    {
        // DEC 0 → 0xFFFF, C=1 (borrow), N=1
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::CLR);
        bc.emit_mode(noir_mode::REG_D0, 0);

        bc.emit(noir_op::DEC);
        bc.emit_mode(noir_mode::REG_D0, 0);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0xFFFF, "DEC 0 wraps to 0xFFFF");
        TEST_ASSERT((vm.sr() & noir_sr::C) != 0, "C should be set (borrow)");
        TEST_ASSERT((vm.sr() & noir_sr::N) != 0, "N should be set");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_dec_signed_overflow)
    {
        // DEC 0x8000 → 0x7FFF, V=1 (negative → positive)
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0x8000);

        bc.emit(noir_op::DEC);
        bc.emit_mode(noir_mode::REG_D0, 0);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0x7FFF, "DEC 0x8000 = 0x7FFF");
        TEST_ASSERT((vm.sr() & noir_sr::V) != 0, "V should be set (signed overflow)");
        TEST_ASSERT((vm.sr() & noir_sr::N) == 0, "N should be clear");
        TEST_SUCCESS;
    }

    // ── Invalid arithmetic opcode ────────────────────────────────────────────────

    TEST_IMPL(test_noir_arithmetic_invalid_opcode)
    {
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        // 0x29 is reserved in the arithmetic range
        bc.emit(0x29);
        // Need a mode byte so the dispatch reads it
        bc.emit_mode(noir_mode::REG_D0, 0);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(failed_with(status, noir_error::invalid_opcode), "reserved opcode should error");
        TEST_SUCCESS;
    }

    // ── Arithmetic with value[] modes ────────────────────────────────────────────

    TEST_IMPL(test_noir_add_value_imm_dst)
    {
        // ADD value[2], #10 — add immediate to a VALUES entry
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        // Pre-set value[2] = 50 via MOV16
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::VALUE_IMM, noir_mode::IMM16);
        bc.emit_u16(2);   // dst: value index
        bc.emit_u16(50);  // src: immediate

        // ADD value[2], #10
        bc.emit(noir_op::ADD);
        bc.emit_mode(noir_mode::VALUE_IMM, noir_mode::IMM16);
        bc.emit_u16(2);   // dst: value index
        bc.emit_u16(10);  // src: immediate

        // D0 = value[2]
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::VALUE_IMM);
        bc.emit_u16(2);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 60, "value[2]: 50 + 10 = 60");
        TEST_SUCCESS;
    }

    // ── Test suite registration ─────────────────────────────────────────────────

    TEST_SUITE_BEGIN(noir_vm_arithmetic)
    // ADD
    TEST(test_noir_add_reg_imm16)
    TEST(test_noir_add_reg_reg)
    TEST(test_noir_add_sets_zero_flag)
    TEST(test_noir_add_sets_negative_flag)
    TEST(test_noir_add_sets_carry_flag)
    TEST(test_noir_add_no_overflow_flags_clear)
    TEST(test_noir_add_local_dst)
    // SUB
    TEST(test_noir_sub_reg_imm16)
    TEST(test_noir_sub_to_zero)
    TEST(test_noir_sub_underflow_sets_carry)
    TEST(test_noir_sub_signed_overflow)
    TEST(test_noir_sub_reg_reg)
    // MUL
    TEST(test_noir_mul_basic)
    TEST(test_noir_mul_by_zero)
    TEST(test_noir_mul_negative)
    TEST(test_noir_mul_overflow_sets_v)
    TEST(test_noir_mul_reg_reg)
    // DIV
    TEST(test_noir_div_basic)
    TEST(test_noir_div_negative)
    TEST(test_noir_div_truncation)
    TEST(test_noir_div_by_zero_halts)
    TEST(test_noir_div_result_zero)
    // MOD
    TEST(test_noir_mod_basic)
    TEST(test_noir_mod_even_division)
    TEST(test_noir_mod_negative)
    TEST(test_noir_mod_by_zero_halts)
    // CMP
    TEST(test_noir_cmp_equal)
    TEST(test_noir_cmp_less_than)
    TEST(test_noir_cmp_greater_than)
    TEST(test_noir_cmp_does_not_modify_dst)
    // NEG
    TEST(test_noir_neg_positive)
    TEST(test_noir_neg_negative)
    TEST(test_noir_neg_zero)
    TEST(test_noir_neg_min_int)
    // INC
    TEST(test_noir_inc_basic)
    TEST(test_noir_inc_overflow)
    TEST(test_noir_inc_signed_overflow)
    TEST(test_noir_inc_local)
    // DEC
    TEST(test_noir_dec_basic)
    TEST(test_noir_dec_to_zero)
    TEST(test_noir_dec_underflow)
    TEST(test_noir_dec_signed_overflow)
    // Invalid
    TEST(test_noir_arithmetic_invalid_opcode)
    // Value mode dst
    TEST(test_noir_add_value_imm_dst)
    TEST_SUITE_END

}  // namespace NEONengine

#endif  // ACE_TEST_RUNNER
#endif  // __NOIR_VM_ARITHMETIC_TESTS_H__INCLUDED__
