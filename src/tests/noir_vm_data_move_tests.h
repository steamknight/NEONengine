#ifndef __NOIR_VM_DATA_MOVE_TESTS_H__INCLUDED__
#define __NOIR_VM_DATA_MOVE_TESTS_H__INCLUDED__

#ifdef ACE_TEST_RUNNER

#include "types.h"

#include "tests/noir_vm_test_utils.h"

namespace NEONengine
{
    // ═════════════════════════════════════════════════════════════════════════════
    // Data Movement opcodes (0x10–0x1F)
    //   MOV8, MOV16, XCHG, PUSH, POP, TOP, DUP, CLR
    // ═════════════════════════════════════════════════════════════════════════════

    // ── MOV16 ────────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_mov16_imm16_to_reg)
    {
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0xBEEF);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0xBEEF, "D0 should be 0xBEEF");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_mov16_imm8_to_reg)
    {
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        // MOV16 with imm8 source — should zero-extend
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM8);
        bc.emit(0xAB);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0x00AB, "D0 should be 0x00AB (zero-extended)");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_mov16_reg_to_reg)
    {
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(0x1234);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D1);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0x1234, "D0 should be 0x1234 from D1");
        TEST_ASSERT(vm.reg_d(1) == 0x1234, "D1 should still be 0x1234");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_mov16_all_data_regs)
    {
        // Verify all 4 data registers can be independently loaded and read
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0x1111);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(0x2222);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D2, noir_mode::IMM16);
        bc.emit_u16(0x3333);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D3, noir_mode::IMM16);
        bc.emit_u16(0x4444);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.reg_d(0) == 0x1111, "D0 should be 0x1111");
        TEST_ASSERT(vm.reg_d(1) == 0x2222, "D1 should be 0x2222");
        TEST_ASSERT(vm.reg_d(2) == 0x3333, "D2 should be 0x3333");
        TEST_ASSERT(vm.reg_d(3) == 0x4444, "D3 should be 0x4444");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_mov16_local_round_trip)
    {
        // Write imm16 to a local variable, read it back into a register
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::LOCALS);
        bc.emit(4);  // 2 x 16-bit locals

        // MOV16 [FP-2], #0xCAFE   (dst=LOCAL inline first, then src=IMM16)
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::LOCAL, noir_mode::IMM16);
        bc.emit(to<u8>(-2));  // dst: LOCAL offset
        bc.emit_u16(0xCAFE);                // src: IMM16

        // MOV16 D0, [FP-2]
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::LOCAL);
        bc.emit(to<u8>(-2));  // src: LOCAL offset

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0xCAFE, "local round-trip should yield 0xCAFE");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_mov16_multiple_locals)
    {
        // Write different values to two local slots, read both back
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::LOCALS);
        bc.emit(4);  // 2 x 16-bit locals

        // [FP-2] = 0xAAAA
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::LOCAL, noir_mode::IMM16);
        bc.emit(to<u8>(-2));
        bc.emit_u16(0xAAAA);

        // [FP-4] = 0xBBBB
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::LOCAL, noir_mode::IMM16);
        bc.emit(to<u8>(-4));
        bc.emit_u16(0xBBBB);

        // D0 = [FP-2]
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::LOCAL);
        bc.emit(to<u8>(-2));

        // D1 = [FP-4]
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::LOCAL);
        bc.emit(to<u8>(-4));

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.reg_d(0) == 0xAAAA, "local[0] should be 0xAAAA");
        TEST_ASSERT(vm.reg_d(1) == 0xBBBB, "local[1] should be 0xBBBB");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_mov16_reg_to_local)
    {
        // MOV16 [FP-2], D1 — register as source, local as destination
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::LOCALS);
        bc.emit(2);

        // D1 = 0x5678
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(0x5678);

        // [FP-2] = D1
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::LOCAL, noir_mode::REG_D1);
        bc.emit(to<u8>(-2));  // dst: LOCAL offset (no src inline data)

        // D0 = [FP-2]
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::LOCAL);
        bc.emit(to<u8>(-2));

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0x5678, "local should hold D1's value");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_mov16_value_imm_round_trip)
    {
        // Write to VALUES[3] via mode byte, read it back
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        // MOV16 value[3], #0xDEAD
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::VALUE_IMM, noir_mode::IMM16);
        bc.emit_u16(3);       // dst: value index
        bc.emit_u16(0xDEAD);  // src: imm16

        // MOV16 D0, value[3]
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::VALUE_IMM);
        bc.emit_u16(3);  // src: value index

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0xDEAD, "value[3] round-trip should yield 0xDEAD");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_mov16_value_d0_round_trip)
    {
        // Write to VALUES[D0], then read back via VALUES[D0]
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        // D0 = 5 (index)
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(5);

        // value[D0] = #0x9999   — D0 used as index
        // Must use D1 to transport the value since D0 is the index
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(0x9999);

        // Store D1 into value[5] via VALUE_IMM
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::VALUE_IMM, noir_mode::REG_D1);
        bc.emit_u16(5);  // dst: value index

        // Read back value[D0] into D1
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::VALUE_D0);

        // Move D1 to D0 for exit code
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D1);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0x9999, "value[D0] round-trip should yield 0x9999");
        TEST_SUCCESS;
    }

    // ── MOV8 ─────────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_mov8_imm8_to_reg)
    {
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        // First set D0 to a value with high byte set
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0xFF00);

        // MOV8 D0, #0x42 — should zero high byte
        bc.emit(noir_op::MOV8);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM8);
        bc.emit(0x42);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0x0042, "MOV8 should zero high byte, low = 0x42");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_mov8_reg_to_reg)
    {
        // MOV8 copies only the low byte and zeroes the high byte of dst
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(0xABCD);

        // MOV8 D0, D1 → D0 should be 0x00CD (low byte of D1, high zeroed)
        bc.emit(noir_op::MOV8);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D1);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0x00CD, "MOV8 should take only low byte of src");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_mov8_local_round_trip)
    {
        // Write a byte to local, read it back with MOV8
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::LOCALS);
        bc.emit(2);

        // MOV8 [FP-2], #0x7F
        bc.emit(noir_op::MOV8);
        bc.emit_mode(noir_mode::LOCAL, noir_mode::IMM8);
        bc.emit(to<u8>(-2));  // dst: LOCAL offset
        bc.emit(0x7F);                      // src: imm8

        // MOV8 D0, [FP-2] — reads back the 16-bit word, masked to 8 bits
        bc.emit(noir_op::MOV8);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::LOCAL);
        bc.emit(to<u8>(-2));

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        // MOV8 writes 0x007F to local (zeroes high), reads back low byte = 0x7F
        TEST_ASSERT(vm.exit_code() == 0x007F, "MOV8 local round-trip should yield 0x007F");
        TEST_SUCCESS;
    }

    // ── XCHG ─────────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_xchg_registers)
    {
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0xAAAA);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(0xBBBB);

        // XCHG D0, D1
        bc.emit(noir_op::XCHG);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D1);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.reg_d(0) == 0xBBBB, "D0 should have D1's old value");
        TEST_ASSERT(vm.reg_d(1) == 0xAAAA, "D1 should have D0's old value");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_xchg_same_register)
    {
        // XCHG D2, D2 — should be a no-op
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D2, noir_mode::IMM16);
        bc.emit_u16(0x1234);

        bc.emit(noir_op::XCHG);
        bc.emit_mode(noir_mode::REG_D2, noir_mode::REG_D2);

        // Move to D0 for exit code
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D2);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0x1234, "XCHG same reg should be no-op");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_xchg_d2_d3)
    {
        // Test XCHG with non-D0/D1 registers
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D2, noir_mode::IMM16);
        bc.emit_u16(0x1111);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D3, noir_mode::IMM16);
        bc.emit_u16(0x2222);

        bc.emit(noir_op::XCHG);
        bc.emit_mode(noir_mode::REG_D2, noir_mode::REG_D3);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.reg_d(2) == 0x2222, "D2 should have D3's old value");
        TEST_ASSERT(vm.reg_d(3) == 0x1111, "D3 should have D2's old value");
        TEST_SUCCESS;
    }

    // ── PUSH / POP ───────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_push_pop_reg)
    {
        // PUSH D0, POP D1 — basic register round-trip through stack
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0xFACE);

        // PUSH D0
        bc.emit(noir_op::PUSH);
        bc.emit_mode(0, noir_mode::REG_D0);  // src-only: [0000|src]

        // Clobber D0
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0x0000);

        // POP D1
        bc.emit(noir_op::POP);
        bc.emit_mode(noir_mode::REG_D1, 0);  // dst-only: [dst|0000]

        // D0 = D1 for exit code
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D1);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0xFACE, "POP should retrieve pushed value");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_push_imm16)
    {
        // PUSH #imm16, POP D0 — push an immediate, pop it back
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        // PUSH #0x5555
        bc.emit(noir_op::PUSH);
        bc.emit_mode(0, noir_mode::IMM16);
        bc.emit_u16(0x5555);

        // POP D0
        bc.emit(noir_op::POP);
        bc.emit_mode(noir_mode::REG_D0, 0);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0x5555, "POP should yield pushed imm16");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_push_pop_lifo_order)
    {
        // PUSH A, PUSH B → POP should give B first (LIFO)
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0x1111);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(0x2222);

        // PUSH D0 (first = 0x1111)
        bc.emit(noir_op::PUSH);
        bc.emit_mode(0, noir_mode::REG_D0);

        // PUSH D1 (second = 0x2222)
        bc.emit(noir_op::PUSH);
        bc.emit_mode(0, noir_mode::REG_D1);

        // POP D2 → should get 0x2222 (last pushed)
        bc.emit(noir_op::POP);
        bc.emit_mode(noir_mode::REG_D2, 0);

        // POP D3 → should get 0x1111 (first pushed)
        bc.emit(noir_op::POP);
        bc.emit_mode(noir_mode::REG_D3, 0);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.reg_d(2) == 0x2222, "first POP should be 0x2222 (LIFO)");
        TEST_ASSERT(vm.reg_d(3) == 0x1111, "second POP should be 0x1111 (LIFO)");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_push_pop_local)
    {
        // PUSH from a local variable, POP into a register
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::LOCALS);
        bc.emit(2);

        // [FP-2] = 0x7777
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::LOCAL, noir_mode::IMM16);
        bc.emit(to<u8>(-2));
        bc.emit_u16(0x7777);

        // PUSH [FP-2]
        bc.emit(noir_op::PUSH);
        bc.emit_mode(0, noir_mode::LOCAL);
        bc.emit(to<u8>(-2));

        // POP D0
        bc.emit(noir_op::POP);
        bc.emit_mode(noir_mode::REG_D0, 0);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0x7777, "PUSH local → POP should round-trip");
        TEST_SUCCESS;
    }

    // ── TOP ──────────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_top_peeks_without_pop)
    {
        // TOP reads stack top without removing it
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        // PUSH #0x3333
        bc.emit(noir_op::PUSH);
        bc.emit_mode(0, noir_mode::IMM16);
        bc.emit_u16(0x3333);

        // TOP D0 — peek
        bc.emit(noir_op::TOP);
        bc.emit_mode(noir_mode::REG_D0, 0);

        // TOP D1 — peek again (should still be same value)
        bc.emit(noir_op::TOP);
        bc.emit_mode(noir_mode::REG_D1, 0);

        // POP D2 — now actually pop it
        bc.emit(noir_op::POP);
        bc.emit_mode(noir_mode::REG_D2, 0);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.reg_d(0) == 0x3333, "first TOP should be 0x3333");
        TEST_ASSERT(vm.reg_d(1) == 0x3333, "second TOP should still be 0x3333");
        TEST_ASSERT(vm.reg_d(2) == 0x3333, "POP should also be 0x3333");
        TEST_SUCCESS;
    }

    // ── DUP ──────────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_dup_duplicates_top)
    {
        // DUP should push a copy of the top value
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        // PUSH #0x4444
        bc.emit(noir_op::PUSH);
        bc.emit_mode(0, noir_mode::IMM16);
        bc.emit_u16(0x4444);

        // DUP
        bc.emit(noir_op::DUP);

        // POP D0 (copy)
        bc.emit(noir_op::POP);
        bc.emit_mode(noir_mode::REG_D0, 0);

        // POP D1 (original)
        bc.emit(noir_op::POP);
        bc.emit_mode(noir_mode::REG_D1, 0);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.reg_d(0) == 0x4444, "DUP copy should be 0x4444");
        TEST_ASSERT(vm.reg_d(1) == 0x4444, "DUP original should be 0x4444");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_dup_does_not_affect_deeper)
    {
        // Push two values, DUP, verify the deeper value is untouched
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        // PUSH #0x1111 (deep)
        bc.emit(noir_op::PUSH);
        bc.emit_mode(0, noir_mode::IMM16);
        bc.emit_u16(0x1111);

        // PUSH #0x2222 (top)
        bc.emit(noir_op::PUSH);
        bc.emit_mode(0, noir_mode::IMM16);
        bc.emit_u16(0x2222);

        // DUP (copies 0x2222)
        bc.emit(noir_op::DUP);

        // POP D0 = 0x2222 (dup)
        bc.emit(noir_op::POP);
        bc.emit_mode(noir_mode::REG_D0, 0);

        // POP D1 = 0x2222 (original top)
        bc.emit(noir_op::POP);
        bc.emit_mode(noir_mode::REG_D1, 0);

        // POP D2 = 0x1111 (deep)
        bc.emit(noir_op::POP);
        bc.emit_mode(noir_mode::REG_D2, 0);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.reg_d(0) == 0x2222, "dup of top");
        TEST_ASSERT(vm.reg_d(1) == 0x2222, "original top");
        TEST_ASSERT(vm.reg_d(2) == 0x1111, "deeper value untouched");
        TEST_SUCCESS;
    }

    // ── CLR ──────────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_clr_register)
    {
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0xFFFF);

        // CLR D0
        bc.emit(noir_op::CLR);
        bc.emit_mode(noir_mode::REG_D0, 0);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0x0000, "CLR should zero the register");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_clr_does_not_affect_others)
    {
        // CLR D0 should not touch D1
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0xAAAA);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(0xBBBB);

        bc.emit(noir_op::CLR);
        bc.emit_mode(noir_mode::REG_D0, 0);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0x0000, "D0 should be cleared");
        TEST_ASSERT(vm.reg_d(1) == 0xBBBB, "D1 should be untouched");
        TEST_SUCCESS;
    }

    // ── Invalid data movement opcode ─────────────────────────────────────────────

    TEST_IMPL(test_noir_data_move_invalid_opcode)
    {
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        // 0x13 and 0x19 are reserved in the data movement range
        bc.emit(0x13);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(failed_with(status, noir_error::invalid_opcode), "reserved opcode should error");
        TEST_SUCCESS;
    }

    // ── LOCAL bounds ────────────────────────────────────────────────────────────
    //
    // A LOCAL names an address as fp plus a signed byte, and fp can sit
    // anywhere in the stack. The pair can therefore name an address outside it,
    // and the session stacks lie end to end, so the read or write would land in
    // the neighbour's. A stack has two ends and each already has a name: off the
    // bottom is an overflow, off the top is an underflow.

    namespace
    {
        /// LOCALS 8 on a 16 byte stack, leaving the lowest slot at fp-8.
        u16 begin_local_fn(bytecode_builder& bc)
        {
            u16 fn = bc.begin_func();
            bc.emit(noir_op::LOCALS);
            bc.emit(8);
            return fn;
        }
    }  // namespace

    TEST_IMPL(test_noir_local_lowest_slot_works)
    {
        // The slot one byte further down is the first that is out, so this is
        // the boundary the guard must not creep past.
        bytecode_builder bc;
        u16 fn = begin_local_fn(bc);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::LOCAL, noir_mode::IMM16);
        bc.emit(to<u8>(-8));
        bc.emit_u16(0x5A5A);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::LOCAL);
        bc.emit(to<u8>(-8));

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm(16);
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "the lowest slot is legal");
        TEST_ASSERT(vm.exit_code() == 0x5A5A, "and holds what was put in it");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_local_read_below_its_stack)
    {
        bytecode_builder bc;
        u16 fn = begin_local_fn(bc);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::LOCAL);
        bc.emit(to<u8>(-9));
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm(16);
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(failed_with(status, noir_error::stack_overflow), "one byte under is out");
        TEST_ASSERT(message_has(vm, "falls below"), "and says which end it left by");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_local_read_above_its_stack)
    {
        // fp is 8 on a 16 byte stack, so +7 leaves one byte of the two inside.
        bytecode_builder bc;
        u16 fn = begin_local_fn(bc);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::LOCAL);
        bc.emit(7);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm(16);
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(failed_with(status, noir_error::stack_underflow), "half a word over is out");
        TEST_ASSERT(message_has(vm, "reaches past"), "and says which end it left by");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_local_write_below_its_stack)
    {
        // The destination goes through the cached pair, not write_operand.
        bytecode_builder bc;
        u16 fn = begin_local_fn(bc);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::LOCAL, noir_mode::IMM16);
        bc.emit(to<u8>(-9));
        bc.emit_u16(0x1234);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm(16);
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(failed_with(status, noir_error::stack_overflow), "writing under is out too");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_local_pop_outside_its_stack)
    {
        // POP writes through write_operand, the one path the others miss.
        bytecode_builder bc;
        u16 fn = begin_local_fn(bc);

        bc.emit(noir_op::POP);
        bc.emit_mode(noir_mode::LOCAL, 0);
        bc.emit(to<u8>(-9));
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm(16);
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(failed_with(status, noir_error::stack_overflow), "popping into it is out too");
        TEST_SUCCESS;
    }

    // ── Test suite registration ─────────────────────────────────────────────────

    TEST_SUITE_BEGIN(noir_vm_data_move)
    // MOV16
    TEST(test_noir_mov16_imm16_to_reg)
    TEST(test_noir_mov16_imm8_to_reg)
    TEST(test_noir_mov16_reg_to_reg)
    TEST(test_noir_mov16_all_data_regs)
    TEST(test_noir_mov16_local_round_trip)
    TEST(test_noir_mov16_multiple_locals)
    TEST(test_noir_mov16_reg_to_local)
    TEST(test_noir_mov16_value_imm_round_trip)
    TEST(test_noir_mov16_value_d0_round_trip)
    // MOV8
    TEST(test_noir_mov8_imm8_to_reg)
    TEST(test_noir_mov8_reg_to_reg)
    TEST(test_noir_mov8_local_round_trip)
    // XCHG
    TEST(test_noir_xchg_registers)
    TEST(test_noir_xchg_same_register)
    TEST(test_noir_xchg_d2_d3)
    // PUSH / POP
    TEST(test_noir_push_pop_reg)
    TEST(test_noir_push_imm16)
    TEST(test_noir_push_pop_lifo_order)
    TEST(test_noir_push_pop_local)
    // TOP
    TEST(test_noir_top_peeks_without_pop)
    // DUP
    TEST(test_noir_dup_duplicates_top)
    TEST(test_noir_dup_does_not_affect_deeper)
    // CLR
    TEST(test_noir_clr_register)
    TEST(test_noir_clr_does_not_affect_others)
    // Invalid
    TEST(test_noir_data_move_invalid_opcode)
    // LOCAL bounds
    TEST(test_noir_local_lowest_slot_works)
    TEST(test_noir_local_read_below_its_stack)
    TEST(test_noir_local_read_above_its_stack)
    TEST(test_noir_local_write_below_its_stack)
    TEST(test_noir_local_pop_outside_its_stack)
    TEST_SUITE_END

}  // namespace NEONengine

#endif  // ACE_TEST_RUNNER
#endif  // __NOIR_VM_DATA_MOVE_TESTS_H__INCLUDED__
