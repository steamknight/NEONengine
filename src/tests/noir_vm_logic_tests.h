#ifndef __NOIR_VM_LOGIC_TESTS_H__INCLUDED__
#define __NOIR_VM_LOGIC_TESTS_H__INCLUDED__

#ifdef ACE_TEST_RUNNER

#include "tests/noir_vm_test_utils.h"

namespace NEONengine
{
    // ═════════════════════════════════════════════════════════════════════════════
    // Logic opcodes (0x30–0x3F)
    //   AND, OR, XOR, TST, NOT, SHL, SHR, ASR
    //
    // Two-operand (AND, OR, XOR, TST): mode byte [dst:4][src:4]
    // Single-operand (NOT):            mode byte [dst:4][0000]
    // Shifts (SHL, SHR, ASR):          mode byte [dst:4][0000] + trailing u8 count
    // All set NZ flags; shifts also set C.
    // ═════════════════════════════════════════════════════════════════════════════

    // ── AND ─────────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_and_reg_imm16)
    {
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0xFF0F);

        bc.emit(noir_op::AND);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0x0F0F);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0x0F0F, "0xFF0F & 0x0F0F = 0x0F0F");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_and_reg_reg)
    {
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0xAAAA);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(0x5555);

        bc.emit(noir_op::AND);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D1);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0x0000, "0xAAAA & 0x5555 = 0x0000");
        TEST_ASSERT((vm.sr() & noir_sr::Z) != 0, "Z should be set");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_and_sets_zero_flag)
    {
        // Any value & 0 = 0 → Z=1
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0xFFFF);

        bc.emit(noir_op::AND);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0x0000);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0, "0xFFFF & 0x0000 = 0");
        TEST_ASSERT((vm.sr() & noir_sr::Z) != 0, "Z should be set");
        TEST_ASSERT((vm.sr() & noir_sr::N) == 0, "N should be clear");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_and_sets_negative_flag)
    {
        // Result with bit 15 set → N=1
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0xF000);

        bc.emit(noir_op::AND);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0x8001);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0x8000, "0xF000 & 0x8001 = 0x8000");
        TEST_ASSERT((vm.sr() & noir_sr::N) != 0, "N should be set");
        TEST_ASSERT((vm.sr() & noir_sr::Z) == 0, "Z should be clear");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_and_identity)
    {
        // x & 0xFFFF = x
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0x1234);

        bc.emit(noir_op::AND);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0xFFFF);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0x1234, "0x1234 & 0xFFFF = 0x1234");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_and_local_dst)
    {
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::LOCALS);
        bc.emit(2);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::LOCAL, noir_mode::IMM16);
        bc.emit(static_cast<uint8_t>(-2));
        bc.emit_u16(0xFF00);

        bc.emit(noir_op::AND);
        bc.emit_mode(noir_mode::LOCAL, noir_mode::IMM16);
        bc.emit(static_cast<uint8_t>(-2));
        bc.emit_u16(0x0FF0);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::LOCAL);
        bc.emit(static_cast<uint8_t>(-2));

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0x0F00, "local 0xFF00 & 0x0FF0 = 0x0F00");
        TEST_SUCCESS;
    }

    // ── OR ──────────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_or_reg_imm16)
    {
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0xF000);

        bc.emit(noir_op::OR);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0x000F);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0xF00F, "0xF000 | 0x000F = 0xF00F");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_or_reg_reg)
    {
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0xAA00);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(0x0055);

        bc.emit(noir_op::OR);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D1);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0xAA55, "0xAA00 | 0x0055 = 0xAA55");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_or_zero_is_identity)
    {
        // x | 0 = x
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0x5678);

        bc.emit(noir_op::OR);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0x0000);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0x5678, "0x5678 | 0x0000 = 0x5678");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_or_sets_negative_flag)
    {
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0x0001);

        bc.emit(noir_op::OR);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0x8000);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0x8001, "0x0001 | 0x8000 = 0x8001");
        TEST_ASSERT((vm.sr() & noir_sr::N) != 0, "N should be set");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_or_both_zero)
    {
        // 0 | 0 = 0 → Z=1
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::CLR);
        bc.emit_mode(noir_mode::REG_D0, 0);

        bc.emit(noir_op::OR);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0x0000);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0, "0 | 0 = 0");
        TEST_ASSERT((vm.sr() & noir_sr::Z) != 0, "Z should be set");
        TEST_SUCCESS;
    }

    // ── XOR ─────────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_xor_reg_imm16)
    {
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0xFF00);

        bc.emit(noir_op::XOR);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0x0FF0);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0xF0F0, "0xFF00 ^ 0x0FF0 = 0xF0F0");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_xor_reg_reg)
    {
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0x1234);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(0x1234);

        bc.emit(noir_op::XOR);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D1);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0x0000, "x ^ x = 0");
        TEST_ASSERT((vm.sr() & noir_sr::Z) != 0, "Z should be set");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_xor_self_clears)
    {
        // x ^ x = 0 for any x
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0xBEEF);

        bc.emit(noir_op::XOR);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D0);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0, "D0 ^ D0 = 0");
        TEST_ASSERT((vm.sr() & noir_sr::Z) != 0, "Z should be set");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_xor_sets_negative_flag)
    {
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0x0001);

        bc.emit(noir_op::XOR);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0x8001);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0x8000, "0x0001 ^ 0x8001 = 0x8000");
        TEST_ASSERT((vm.sr() & noir_sr::N) != 0, "N should be set");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_xor_toggle_bits)
    {
        // XOR with 0xFFFF flips all bits
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0x5A3C);

        bc.emit(noir_op::XOR);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0xFFFF);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0xA5C3, "0x5A3C ^ 0xFFFF = 0xA5C3");
        TEST_SUCCESS;
    }

    // ── TST ─────────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_tst_nonzero_result)
    {
        // TST does AND but discards result — only sets flags
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0xFF00);

        bc.emit(noir_op::TST);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0x8000);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        // D0 should NOT be modified by TST
        TEST_ASSERT(vm.exit_code() == 0xFF00, "TST must not modify D0");
        TEST_ASSERT((vm.sr() & noir_sr::N) != 0, "N set (0xFF00 & 0x8000 = 0x8000)");
        TEST_ASSERT((vm.sr() & noir_sr::Z) == 0, "Z clear");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_tst_zero_result)
    {
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0xAA00);

        bc.emit(noir_op::TST);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0x0055);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0xAA00, "TST must not modify D0");
        TEST_ASSERT((vm.sr() & noir_sr::Z) != 0, "Z set (0xAA00 & 0x0055 = 0)");
        TEST_ASSERT((vm.sr() & noir_sr::N) == 0, "N clear");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_tst_reg_reg)
    {
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0x00FF);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(0x0F0F);

        bc.emit(noir_op::TST);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D1);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0x00FF, "TST must not modify D0");
        // 0x00FF & 0x0F0F = 0x000F → nonzero
        TEST_ASSERT((vm.sr() & noir_sr::Z) == 0, "Z clear (result nonzero)");
        TEST_ASSERT((vm.sr() & noir_sr::N) == 0, "N clear");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_tst_does_not_modify_dst)
    {
        // Verify TST leaves both operands unchanged
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0x1234);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(0x00F0);

        bc.emit(noir_op::TST);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D1);

        // Move D1 into D0 so we can also check D1 was untouched via exit_code
        // Actually let's just check D0 is untouched
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0x1234, "D0 unchanged after TST");
        TEST_ASSERT(vm.reg_d(1) == 0x00F0, "D1 unchanged after TST");
        TEST_SUCCESS;
    }

    // ── NOT ─────────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_not_basic)
    {
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0x00FF);

        bc.emit(noir_op::NOT);
        bc.emit_mode(noir_mode::REG_D0, 0);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0xFF00, "~0x00FF = 0xFF00");
        TEST_ASSERT((vm.sr() & noir_sr::N) != 0, "N set (bit 15 set)");
        TEST_ASSERT((vm.sr() & noir_sr::Z) == 0, "Z clear");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_not_all_ones)
    {
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0xFFFF);

        bc.emit(noir_op::NOT);
        bc.emit_mode(noir_mode::REG_D0, 0);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0x0000, "~0xFFFF = 0x0000");
        TEST_ASSERT((vm.sr() & noir_sr::Z) != 0, "Z set");
        TEST_ASSERT((vm.sr() & noir_sr::N) == 0, "N clear");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_not_zero)
    {
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::CLR);
        bc.emit_mode(noir_mode::REG_D0, 0);

        bc.emit(noir_op::NOT);
        bc.emit_mode(noir_mode::REG_D0, 0);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0xFFFF, "~0x0000 = 0xFFFF");
        TEST_ASSERT((vm.sr() & noir_sr::N) != 0, "N set");
        TEST_ASSERT((vm.sr() & noir_sr::Z) == 0, "Z clear");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_not_double_invert)
    {
        // NOT twice should restore original value
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0xDEAD);

        bc.emit(noir_op::NOT);
        bc.emit_mode(noir_mode::REG_D0, 0);

        bc.emit(noir_op::NOT);
        bc.emit_mode(noir_mode::REG_D0, 0);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0xDEAD, "~~x = x");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_not_local_dst)
    {
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::LOCALS);
        bc.emit(2);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::LOCAL, noir_mode::IMM16);
        bc.emit(static_cast<uint8_t>(-2));
        bc.emit_u16(0x0F0F);

        bc.emit(noir_op::NOT);
        bc.emit_mode(noir_mode::LOCAL, 0);
        bc.emit(static_cast<uint8_t>(-2));

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::LOCAL);
        bc.emit(static_cast<uint8_t>(-2));

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0xF0F0, "~0x0F0F = 0xF0F0");
        TEST_SUCCESS;
    }

    // ── SHL ─────────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_shl_basic)
    {
        // 1 << 4 = 16
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);

        bc.emit(noir_op::SHL);
        bc.emit_mode(noir_mode::REG_D0, 0);
        bc.emit(4);  // shift count

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 16, "1 << 4 = 16");
        TEST_ASSERT((vm.sr() & noir_sr::Z) == 0, "Z clear");
        TEST_ASSERT((vm.sr() & noir_sr::N) == 0, "N clear");
        TEST_ASSERT((vm.sr() & noir_sr::C) == 0, "C clear (bit 12 was 0)");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_shl_sets_carry)
    {
        // 0x8000 << 1 → shifts out bit 15 → C=1, result=0
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0x8000);

        bc.emit(noir_op::SHL);
        bc.emit_mode(noir_mode::REG_D0, 0);
        bc.emit(1);  // shift count

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0, "0x8000 << 1 = 0");
        TEST_ASSERT((vm.sr() & noir_sr::C) != 0, "C set (bit shifted out)");
        TEST_ASSERT((vm.sr() & noir_sr::Z) != 0, "Z set (result is 0)");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_shl_sets_negative)
    {
        // 0x4000 << 1 = 0x8000 → N=1
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0x4000);

        bc.emit(noir_op::SHL);
        bc.emit_mode(noir_mode::REG_D0, 0);
        bc.emit(1);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0x8000, "0x4000 << 1 = 0x8000");
        TEST_ASSERT((vm.sr() & noir_sr::N) != 0, "N set");
        TEST_ASSERT((vm.sr() & noir_sr::C) == 0, "C clear (bit 15 was 0)");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_shl_by_zero)
    {
        // Shifting by 0 should leave value unchanged
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0x1234);

        bc.emit(noir_op::SHL);
        bc.emit_mode(noir_mode::REG_D0, 0);
        bc.emit(0);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0x1234, "x << 0 = x");
        TEST_ASSERT((vm.sr() & noir_sr::C) == 0, "C clear (no shift)");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_shl_by_16)
    {
        // Shifting 16-bit value by 16 should give 0
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0xFFFF);

        bc.emit(noir_op::SHL);
        bc.emit_mode(noir_mode::REG_D0, 0);
        bc.emit(16);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0, "x << 16 = 0");
        TEST_ASSERT((vm.sr() & noir_sr::Z) != 0, "Z set");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_shl_carry_last_shifted_bit)
    {
        // 0x00FF << 8: bit 8 (the last bit shifted into position 16) sets C
        // 0x00FF = 0000_0000_1111_1111
        // << 8: bit at position 16-8=8 → bit 8 of 0x00FF is 0 → C=0
        // Result: 0xFF00
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0x00FF);

        bc.emit(noir_op::SHL);
        bc.emit_mode(noir_mode::REG_D0, 0);
        bc.emit(8);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0xFF00, "0x00FF << 8 = 0xFF00");
        // C = bit at position (16-8)=8 of original: bit 8 of 0x00FF = 0
        TEST_ASSERT((vm.sr() & noir_sr::C) == 0, "C clear");
        TEST_ASSERT((vm.sr() & noir_sr::N) != 0, "N set");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_shl_local_dst)
    {
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::LOCALS);
        bc.emit(2);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::LOCAL, noir_mode::IMM16);
        bc.emit(static_cast<uint8_t>(-2));
        bc.emit_u16(0x0003);

        bc.emit(noir_op::SHL);
        bc.emit_mode(noir_mode::LOCAL, 0);
        bc.emit(static_cast<uint8_t>(-2));
        bc.emit(4);  // shift count

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::LOCAL);
        bc.emit(static_cast<uint8_t>(-2));

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0x0030, "local 0x0003 << 4 = 0x0030");
        TEST_SUCCESS;
    }

    // ── SHR ─────────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_shr_basic)
    {
        // 0x0100 >> 4 = 0x0010
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0x0100);

        bc.emit(noir_op::SHR);
        bc.emit_mode(noir_mode::REG_D0, 0);
        bc.emit(4);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0x0010, "0x0100 >> 4 = 0x0010");
        TEST_ASSERT((vm.sr() & noir_sr::Z) == 0, "Z clear");
        TEST_ASSERT((vm.sr() & noir_sr::N) == 0, "N clear (logical shift)");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_shr_sets_carry)
    {
        // 0x0001 >> 1 → last bit shifted out = 1 → C=1, result=0
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0x0001);

        bc.emit(noir_op::SHR);
        bc.emit_mode(noir_mode::REG_D0, 0);
        bc.emit(1);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0, "0x0001 >> 1 = 0");
        TEST_ASSERT((vm.sr() & noir_sr::C) != 0, "C set (bit 0 shifted out)");
        TEST_ASSERT((vm.sr() & noir_sr::Z) != 0, "Z set");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_shr_high_bit_not_preserved)
    {
        // Logical shift: 0x8000 >> 1 = 0x4000 (no sign extension)
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0x8000);

        bc.emit(noir_op::SHR);
        bc.emit_mode(noir_mode::REG_D0, 0);
        bc.emit(1);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0x4000, "0x8000 >> 1 = 0x4000 (logical)");
        TEST_ASSERT((vm.sr() & noir_sr::N) == 0, "N clear (logical shift)");
        TEST_ASSERT((vm.sr() & noir_sr::C) == 0, "C clear (bit 0 was 0)");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_shr_by_zero)
    {
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0xABCD);

        bc.emit(noir_op::SHR);
        bc.emit_mode(noir_mode::REG_D0, 0);
        bc.emit(0);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0xABCD, "x >> 0 = x");
        TEST_ASSERT((vm.sr() & noir_sr::C) == 0, "C clear (no shift)");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_shr_by_16)
    {
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0xFFFF);

        bc.emit(noir_op::SHR);
        bc.emit_mode(noir_mode::REG_D0, 0);
        bc.emit(16);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0, "x >> 16 = 0");
        TEST_ASSERT((vm.sr() & noir_sr::Z) != 0, "Z set");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_shr_carry_position)
    {
        // 0x0008 >> 3: C = bit at (count-1)=2 of original
        // 0x0008 = 0000_0000_0000_1000 → bit 2 = 0 → C=0
        // result = 0x0001
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0x0008);

        bc.emit(noir_op::SHR);
        bc.emit_mode(noir_mode::REG_D0, 0);
        bc.emit(3);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0x0001, "0x0008 >> 3 = 0x0001");
        TEST_ASSERT((vm.sr() & noir_sr::C) == 0, "C clear (bit 2 was 0)");
        TEST_SUCCESS;
    }

    // ── ASR ─────────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_asr_positive)
    {
        // 0x0040 >> 2 = 0x0010 (positive value, same as SHR)
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0x0040);

        bc.emit(noir_op::ASR);
        bc.emit_mode(noir_mode::REG_D0, 0);
        bc.emit(2);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0x0010, "0x0040 >> 2 = 0x0010");
        TEST_ASSERT((vm.sr() & noir_sr::N) == 0, "N clear");
        TEST_ASSERT((vm.sr() & noir_sr::Z) == 0, "Z clear");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_asr_negative_sign_extends)
    {
        // 0x8000 (-32768) >> 1 = 0xC000 (-16384) — sign bit preserved
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0x8000);

        bc.emit(noir_op::ASR);
        bc.emit_mode(noir_mode::REG_D0, 0);
        bc.emit(1);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0xC000, "0x8000 ASR 1 = 0xC000");
        TEST_ASSERT((vm.sr() & noir_sr::N) != 0, "N set (sign preserved)");
        TEST_ASSERT((vm.sr() & noir_sr::C) == 0, "C clear (bit 0 was 0)");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_asr_negative_full_shift)
    {
        // 0xFFFF (-1) >> 16 = 0xFFFF (-1) — all sign bits
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0xFFFF);

        bc.emit(noir_op::ASR);
        bc.emit_mode(noir_mode::REG_D0, 0);
        bc.emit(16);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0xFFFF, "-1 ASR 16 = -1 (all sign bits)");
        TEST_ASSERT((vm.sr() & noir_sr::N) != 0, "N set");
        TEST_ASSERT((vm.sr() & noir_sr::Z) == 0, "Z clear");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_asr_positive_full_shift)
    {
        // 0x7FFF >> 16 = 0x0000 (positive, shifts to zero)
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0x7FFF);

        bc.emit(noir_op::ASR);
        bc.emit_mode(noir_mode::REG_D0, 0);
        bc.emit(16);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0x0000, "0x7FFF ASR 16 = 0");
        TEST_ASSERT((vm.sr() & noir_sr::Z) != 0, "Z set");
        TEST_ASSERT((vm.sr() & noir_sr::N) == 0, "N clear");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_asr_sets_carry)
    {
        // 0x0003 >> 1: bit 0 = 1 → C=1, result = 0x0001
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0x0003);

        bc.emit(noir_op::ASR);
        bc.emit_mode(noir_mode::REG_D0, 0);
        bc.emit(1);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0x0001, "0x0003 ASR 1 = 0x0001");
        TEST_ASSERT((vm.sr() & noir_sr::C) != 0, "C set (bit 0 was 1)");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_asr_by_zero)
    {
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0x8001);

        bc.emit(noir_op::ASR);
        bc.emit_mode(noir_mode::REG_D0, 0);
        bc.emit(0);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0x8001, "x ASR 0 = x");
        TEST_ASSERT((vm.sr() & noir_sr::C) == 0, "C clear (no shift)");
        TEST_ASSERT((vm.sr() & noir_sr::N) != 0, "N set (bit 15 still set)");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_asr_negative_divide_by_two)
    {
        // -10 (0xFFF6) ASR 1 = -5 (0xFFFB)
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(static_cast<uint16_t>(-10));  // 0xFFF6

        bc.emit(noir_op::ASR);
        bc.emit_mode(noir_mode::REG_D0, 0);
        bc.emit(1);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == static_cast<uint16_t>(-5), "-10 ASR 1 = -5");
        TEST_ASSERT((vm.sr() & noir_sr::N) != 0, "N set");
        TEST_SUCCESS;
    }

    // ── Invalid opcode ──────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_logic_invalid_opcode)
    {
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        // 0x3F is not a valid logic opcode
        bc.emit(0x3F);
        bc.emit(0x00);  // mode byte

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::err_invalid_opcode, "invalid opcode should error");
        TEST_SUCCESS;
    }

    // ═════════════════════════════════════════════════════════════════════════════
    // Test suite registration
    // ═════════════════════════════════════════════════════════════════════════════

    TEST_SUITE_BEGIN(noir_vm_logic)
    // AND
    TEST(test_noir_and_reg_imm16)
    TEST(test_noir_and_reg_reg)
    TEST(test_noir_and_sets_zero_flag)
    TEST(test_noir_and_sets_negative_flag)
    TEST(test_noir_and_identity)
    TEST(test_noir_and_local_dst)
    // OR
    TEST(test_noir_or_reg_imm16)
    TEST(test_noir_or_reg_reg)
    TEST(test_noir_or_zero_is_identity)
    TEST(test_noir_or_sets_negative_flag)
    TEST(test_noir_or_both_zero)
    // XOR
    TEST(test_noir_xor_reg_imm16)
    TEST(test_noir_xor_reg_reg)
    TEST(test_noir_xor_self_clears)
    TEST(test_noir_xor_sets_negative_flag)
    TEST(test_noir_xor_toggle_bits)
    // TST
    TEST(test_noir_tst_nonzero_result)
    TEST(test_noir_tst_zero_result)
    TEST(test_noir_tst_reg_reg)
    TEST(test_noir_tst_does_not_modify_dst)
    // NOT
    TEST(test_noir_not_basic)
    TEST(test_noir_not_all_ones)
    TEST(test_noir_not_zero)
    TEST(test_noir_not_double_invert)
    TEST(test_noir_not_local_dst)
    // SHL
    TEST(test_noir_shl_basic)
    TEST(test_noir_shl_sets_carry)
    TEST(test_noir_shl_sets_negative)
    TEST(test_noir_shl_by_zero)
    TEST(test_noir_shl_by_16)
    TEST(test_noir_shl_carry_last_shifted_bit)
    TEST(test_noir_shl_local_dst)
    // SHR
    TEST(test_noir_shr_basic)
    TEST(test_noir_shr_sets_carry)
    TEST(test_noir_shr_high_bit_not_preserved)
    TEST(test_noir_shr_by_zero)
    TEST(test_noir_shr_by_16)
    TEST(test_noir_shr_carry_position)
    // ASR
    TEST(test_noir_asr_positive)
    TEST(test_noir_asr_negative_sign_extends)
    TEST(test_noir_asr_negative_full_shift)
    TEST(test_noir_asr_positive_full_shift)
    TEST(test_noir_asr_sets_carry)
    TEST(test_noir_asr_by_zero)
    TEST(test_noir_asr_negative_divide_by_two)
    // Invalid
    TEST(test_noir_logic_invalid_opcode)
    TEST_SUITE_END

}  // namespace NEONengine

#endif  // ACE_TEST_RUNNER
#endif  // __NOIR_VM_LOGIC_TESTS_H__INCLUDED__
