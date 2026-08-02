#ifndef __NOIR_VM_CONTROL_FLOW_TESTS_H__INCLUDED__
#define __NOIR_VM_CONTROL_FLOW_TESTS_H__INCLUDED__

#ifdef ACE_TEST_RUNNER

#include "types.h"

#include "tests/noir_vm_test_utils.h"

namespace NEONengine
{
    // ═════════════════════════════════════════════════════════════════════════════
    // Control Flow opcodes (0x40–0x4F)
    //   JMP, BEQ, BNE, BLT, BGE, BGT, BLE, BCS, BCC
    //
    // No mode byte — all use s16 relative offset from end of instruction.
    // Branch encoding: [opcode:1][s16 offset:2] = 3 bytes total.
    //
    // Instruction sizes used in offset calculations:
    //   MOV16 dst,imm16 = 4 bytes  (opcode + mode + hi + lo)
    //   CMP   dst,imm16 = 4 bytes
    //   INC/DEC dst     = 2 bytes  (opcode + mode)
    //   CLR   dst       = 2 bytes
    //   Bxx   offset    = 3 bytes
    //   JMP   offset    = 3 bytes
    //   RET             = 1 byte
    // ═════════════════════════════════════════════════════════════════════════════

    // ── JMP ─────────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_jmp_forward)
    {
        // JMP skips a MOV16 instruction (4 bytes)
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(42);

        bc.emit(noir_op::JMP);
        bc.emit_s16(4);  // skip the next MOV16 (4 bytes)

        // Should be skipped:
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(999);

        // Landing target:
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 42, "JMP should skip MOV16, D0 stays 42");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_jmp_backward_loop)
    {
        // Simple loop: D0 counts from 0 to 3
        //   CLR D0         (2 bytes)
        //   MOV16 D1, 3    (4 bytes)
        // loop_top:         (offset 6)
        //   INC D0         (2 bytes)
        //   DEC D1         (2 bytes)
        //   CMP D1, imm(0) (4 bytes)
        //   BNE offset     (3 bytes) → back to loop_top
        //   RET
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::CLR);
        bc.emit_mode(noir_mode::REG_D0, 0);  // 2 bytes

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(3);  // 4 bytes

        u16 loop_top = bc.mark();  // offset 6

        bc.emit(noir_op::INC);
        bc.emit_mode(noir_mode::REG_D0, 0);  // 2 bytes

        bc.emit(noir_op::DEC);
        bc.emit_mode(noir_mode::REG_D1, 0);  // 2 bytes

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(0);  // 4 bytes

        u16 bne_pos = bc.mark();  // position of BNE
        bc.emit(noir_op::BNE);
        // offset = loop_top - (bne_pos + 3)
        bc.emit_s16(to<i16>(loop_top - (bne_pos + 3)));

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 3, "D0 should count up to 3");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_jmp_zero_offset)
    {
        // JMP +0 is effectively a nop — lands on next instruction
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(77);

        bc.emit(noir_op::JMP);
        bc.emit_s16(0);  // jump to immediately next instruction

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 77, "D0 should be 77");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_jmp_skip_multiple)
    {
        // JMP skips two MOV16 instructions (8 bytes)
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(100);

        bc.emit(noir_op::JMP);
        bc.emit_s16(8);  // skip two MOV16s (4+4 = 8 bytes)

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(200);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(300);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 100, "should skip both MOV16s");
        TEST_SUCCESS;
    }

    // ── BEQ ─────────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_beq_taken)
    {
        // CMP 10 == 10 → Z=1 → BEQ taken
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::BEQ);
        bc.emit_s16(4);  // skip MOV16

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(999);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 10, "BEQ taken, D0 unchanged");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_beq_not_taken)
    {
        // CMP 10 != 20 → Z=0 → BEQ not taken
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(20);

        bc.emit(noir_op::BEQ);
        bc.emit_s16(4);  // NOT taken

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(42);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 42, "BEQ not taken, D0 overwritten to 42");
        TEST_SUCCESS;
    }

    // ── BNE ─────────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_bne_taken)
    {
        // CMP 10 != 20 → Z=0 → BNE taken
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(20);

        bc.emit(noir_op::BNE);
        bc.emit_s16(4);  // skip MOV16

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(999);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 10, "BNE taken, D0 unchanged");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_bne_not_taken)
    {
        // CMP 10 == 10 → Z=1 → BNE not taken
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::BNE);
        bc.emit_s16(4);  // NOT taken

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(42);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 42, "BNE not taken, D0 overwritten to 42");
        TEST_SUCCESS;
    }

    // ── BLT (signed less than: N⊕V=1) ──────────────────────────────────────────

    TEST_IMPL(test_noir_blt_taken)
    {
        // CMP 5, 10 → 5-10 = -5 → N=1, V=0 → N⊕V=1 → BLT taken
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(5);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::BLT);
        bc.emit_s16(4);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(999);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 5, "5 < 10 → BLT taken");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_blt_not_taken_equal)
    {
        // CMP 10, 10 → 0 → N=0, V=0 → N⊕V=0 → BLT not taken
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::BLT);
        bc.emit_s16(4);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(42);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 42, "10 == 10 → BLT not taken");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_blt_not_taken_greater)
    {
        // CMP 20, 10 → 10 → N=0, V=0 → N⊕V=0 → BLT not taken
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(20);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::BLT);
        bc.emit_s16(4);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(42);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 42, "20 > 10 → BLT not taken");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_blt_negative_less_than_positive)
    {
        // CMP -5 (0xFFFB), 5 → signed: -5 < 5 → BLT taken
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(to<u16>(-5));

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(5);

        bc.emit(noir_op::BLT);
        bc.emit_s16(4);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(999);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == to<u16>(-5), "-5 < 5 → BLT taken");
        TEST_SUCCESS;
    }

    // ── BGE (signed greater or equal: N⊕V=0) ───────────────────────────────────

    TEST_IMPL(test_noir_bge_taken_greater)
    {
        // CMP 20, 10 → N=0, V=0 → N⊕V=0 → BGE taken
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(20);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::BGE);
        bc.emit_s16(4);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(999);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 20, "20 >= 10 → BGE taken");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_bge_taken_equal)
    {
        // CMP 10, 10 → N=0, V=0 → N⊕V=0 → BGE taken
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::BGE);
        bc.emit_s16(4);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(999);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 10, "10 >= 10 → BGE taken");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_bge_not_taken)
    {
        // CMP 5, 10 → 5-10 = -5 → N=1, V=0 → N⊕V=1 → BGE not taken
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(5);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::BGE);
        bc.emit_s16(4);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(42);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 42, "5 < 10 → BGE not taken");
        TEST_SUCCESS;
    }

    // ── BGT (signed greater: Z=0 AND N⊕V=0) ───────────────────────────────────

    TEST_IMPL(test_noir_bgt_taken)
    {
        // CMP 20, 10 → Z=0, N=0, V=0 → Z=0 AND N⊕V=0 → BGT taken
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(20);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::BGT);
        bc.emit_s16(4);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(999);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 20, "20 > 10 → BGT taken");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_bgt_not_taken_equal)
    {
        // CMP 10, 10 → Z=1 → Z=0 condition fails → BGT not taken
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::BGT);
        bc.emit_s16(4);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(42);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 42, "10 == 10 → BGT not taken (equal)");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_bgt_not_taken_less)
    {
        // CMP 5, 10 → N⊕V=1 → BGT not taken
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(5);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::BGT);
        bc.emit_s16(4);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(42);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 42, "5 < 10 → BGT not taken");
        TEST_SUCCESS;
    }

    // ── BLE (signed less or equal: Z=1 OR N⊕V=1) ───────────────────────────────

    TEST_IMPL(test_noir_ble_taken_less)
    {
        // CMP 5, 10 → N⊕V=1 → BLE taken
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(5);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::BLE);
        bc.emit_s16(4);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(999);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 5, "5 <= 10 → BLE taken");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_ble_taken_equal)
    {
        // CMP 10, 10 → Z=1 → BLE taken
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::BLE);
        bc.emit_s16(4);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(999);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 10, "10 <= 10 → BLE taken");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_ble_not_taken)
    {
        // CMP 20, 10 → Z=0, N=0, V=0 → N⊕V=0 → BLE not taken
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(20);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::BLE);
        bc.emit_s16(4);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(42);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 42, "20 > 10 → BLE not taken");
        TEST_SUCCESS;
    }

    // ── BCS (carry set: C=1, unsigned borrow) ───────────────────────────────────

    TEST_IMPL(test_noir_bcs_taken)
    {
        // CMP 3, 10 → 3 < 10 unsigned → C=1 → BCS taken
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(3);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::BCS);
        bc.emit_s16(4);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(999);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 3, "3 < 10 unsigned → BCS taken");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_bcs_not_taken)
    {
        // CMP 10, 3 → 10 >= 3 unsigned → C=0 → BCS not taken
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(3);

        bc.emit(noir_op::BCS);
        bc.emit_s16(4);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(42);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 42, "10 >= 3 unsigned → BCS not taken");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_bcs_unsigned_high_value)
    {
        // CMP 0x8000, 0xFFFF → 0x8000 < 0xFFFF unsigned → C=1 → BCS taken
        // Even though 0x8000 is negative signed, unsigned it's less than 0xFFFF
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0x8000);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0xFFFF);

        bc.emit(noir_op::BCS);
        bc.emit_s16(4);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(999);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0x8000, "0x8000 < 0xFFFF unsigned → BCS taken");
        TEST_SUCCESS;
    }

    // ── BCC (carry clear: C=0) ──────────────────────────────────────────────────

    TEST_IMPL(test_noir_bcc_taken)
    {
        // CMP 10, 3 → 10 >= 3 unsigned → C=0 → BCC taken
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(3);

        bc.emit(noir_op::BCC);
        bc.emit_s16(4);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(999);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 10, "10 >= 3 unsigned → BCC taken");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_bcc_not_taken)
    {
        // CMP 3, 10 → 3 < 10 unsigned → C=1 → BCC not taken
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(3);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::BCC);
        bc.emit_s16(4);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(42);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 42, "3 < 10 unsigned → BCC not taken");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_bcc_equal_no_borrow)
    {
        // CMP 10, 10 → 10 == 10 → C=0 (no borrow) → BCC taken
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::BCC);
        bc.emit_s16(4);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(999);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 10, "10 == 10 → C=0 → BCC taken");
        TEST_SUCCESS;
    }

    // ── Combined / edge case tests ──────────────────────────────────────────────

    TEST_IMPL(test_noir_branch_backward_count_to_five)
    {
        // Count D0 from 0 to 5 using BLT backward branch
        //   CLR D0             (2 bytes)
        // loop:                (mark)
        //   INC D0             (2 bytes)
        //   CMP D0, imm16(5)  (4 bytes)
        //   BLT offset         (3 bytes) → back to loop
        //   RET
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::CLR);
        bc.emit_mode(noir_mode::REG_D0, 0);

        u16 loop = bc.mark();

        bc.emit(noir_op::INC);
        bc.emit_mode(noir_mode::REG_D0, 0);  // 2 bytes

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(5);  // 4 bytes

        u16 blt_pos = bc.mark();
        bc.emit(noir_op::BLT);
        bc.emit_s16(to<i16>(loop - (blt_pos + 3)));

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 5, "should count to 5");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_branch_chain_if_else)
    {
        // if (D0 == 10) { D0 = 1 } else { D0 = 2 }
        //   MOV16 D0, 10            (4 bytes)
        //   CMP D0, imm16(10)      (4 bytes)
        //   BNE +4                  (3 bytes) → else
        //   MOV16 D0, 1 (then)     (4 bytes)
        //   JMP +4                  (3 bytes) → end
        //   MOV16 D0, 2 (else)     (4 bytes)
        //   RET                     (end)
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::BNE);
        bc.emit_s16(4 + 3);  // skip then-MOV16(4) + JMP(3) = 7

        // then:
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);

        bc.emit(noir_op::JMP);
        bc.emit_s16(4);  // skip else-MOV16(4)

        // else:
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(2);

        // end:
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 1, "D0==10 → then branch → D0=1");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_branch_chain_if_else_false)
    {
        // if (D0 == 10) { D0 = 1 } else { D0 = 2 }, with D0=99 (else path)
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(99);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::BNE);
        bc.emit_s16(4 + 3);  // skip then-MOV16(4) + JMP(3)

        // then:
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);

        bc.emit(noir_op::JMP);
        bc.emit_s16(4);  // skip else-MOV16

        // else:
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(2);

        // end:
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 2, "D0!=10 → else branch → D0=2");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_bgt_negative_values)
    {
        // CMP -1 (0xFFFF), -5 (0xFFFB) → signed: -1 > -5 → BGT taken
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(to<u16>(-1));

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(to<u16>(-5));

        bc.emit(noir_op::BGT);
        bc.emit_s16(4);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(999);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == to<u16>(-1), "-1 > -5 → BGT taken");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_ble_negative_values)
    {
        // CMP -5 (0xFFFB), -1 (0xFFFF) → signed: -5 <= -1 → BLE taken
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(to<u16>(-5));

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(to<u16>(-1));

        bc.emit(noir_op::BLE);
        bc.emit_s16(4);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(999);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == to<u16>(-5), "-5 <= -1 → BLE taken");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_branch_after_sub_flags)
    {
        // SUB sets flags too — branch should work after SUB, not just CMP
        // D0 = 10, D0 -= 10 → D0 = 0, Z=1 → BEQ taken
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::SUB);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);  // D0=0, Z=1

        bc.emit(noir_op::BEQ);
        bc.emit_s16(4);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(999);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0, "SUB sets Z → BEQ taken");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_branch_after_add_flags)
    {
        // ADD 0x7FFF + 1 = 0x8000 → N=1, V=1 → BLT checks N⊕V
        // N=1, V=1 → N⊕V=0 → BLT NOT taken
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0x7FFF);

        bc.emit(noir_op::ADD);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);  // D0=0x8000, N=1, V=1

        bc.emit(noir_op::BLT);
        bc.emit_s16(4);  // N⊕V = 1⊕1 = 0 → NOT taken

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(42);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 42, "N=1,V=1 → N^V=0 → BLT not taken");
        TEST_SUCCESS;
    }

    // ── Invalid opcode ──────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_control_flow_invalid_opcode)
    {
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        // 0x4F is not a valid control flow opcode
        bc.emit(0x4F);
        bc.emit_s16(0);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(failed_with(status, noir_error::invalid_opcode), "invalid opcode should error");
        TEST_SUCCESS;
    }

    // ═════════════════════════════════════════════════════════════════════════════
    // Test suite registration
    // ═════════════════════════════════════════════════════════════════════════════

    TEST_SUITE_BEGIN(noir_vm_control_flow)
    // JMP
    TEST(test_noir_jmp_forward)
    TEST(test_noir_jmp_backward_loop)
    TEST(test_noir_jmp_zero_offset)
    TEST(test_noir_jmp_skip_multiple)
    // BEQ
    TEST(test_noir_beq_taken)
    TEST(test_noir_beq_not_taken)
    // BNE
    TEST(test_noir_bne_taken)
    TEST(test_noir_bne_not_taken)
    // BLT
    TEST(test_noir_blt_taken)
    TEST(test_noir_blt_not_taken_equal)
    TEST(test_noir_blt_not_taken_greater)
    TEST(test_noir_blt_negative_less_than_positive)
    // BGE
    TEST(test_noir_bge_taken_greater)
    TEST(test_noir_bge_taken_equal)
    TEST(test_noir_bge_not_taken)
    // BGT
    TEST(test_noir_bgt_taken)
    TEST(test_noir_bgt_not_taken_equal)
    TEST(test_noir_bgt_not_taken_less)
    // BLE
    TEST(test_noir_ble_taken_less)
    TEST(test_noir_ble_taken_equal)
    TEST(test_noir_ble_not_taken)
    // BCS
    TEST(test_noir_bcs_taken)
    TEST(test_noir_bcs_not_taken)
    TEST(test_noir_bcs_unsigned_high_value)
    // BCC
    TEST(test_noir_bcc_taken)
    TEST(test_noir_bcc_not_taken)
    TEST(test_noir_bcc_equal_no_borrow)
    // Combined / edge cases
    TEST(test_noir_branch_backward_count_to_five)
    TEST(test_noir_branch_chain_if_else)
    TEST(test_noir_branch_chain_if_else_false)
    TEST(test_noir_bgt_negative_values)
    TEST(test_noir_ble_negative_values)
    TEST(test_noir_branch_after_sub_flags)
    TEST(test_noir_branch_after_add_flags)
    // Invalid
    TEST(test_noir_control_flow_invalid_opcode)
    TEST_SUITE_END

}  // namespace NEONengine

#endif  // ACE_TEST_RUNNER
#endif  // __NOIR_VM_CONTROL_FLOW_TESTS_H__INCLUDED__
