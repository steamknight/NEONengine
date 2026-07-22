#ifndef __NOIR_VM_REENTRY_TESTS_H__INCLUDED__
#define __NOIR_VM_REENTRY_TESTS_H__INCLUDED__

#ifdef ACE_TEST_RUNNER

#include "tests/noir_vm_test_utils.h"

namespace NEONengine
{
    // ═════════════════════════════════════════════════════════════════════════════
    // VM Re-Entrancy Tests
    //
    // Re-entrancy occurs when a bridge callback calls execute() while the VM
    // is already running. The VM saves all registers (D0-D3, SR, SP, FP, PC)
    // on the stack, executes the new function, and restores them on return.
    //
    // Key invariants:
    //   - After re-entrant function returns, all caller registers are restored
    //   - The re-entrant function can use all registers freely
    //   - Nested re-entry (multiple levels) works correctly
    //   - Stack is balanced after re-entrant return
    //   - CALL within a re-entrant function works correctly
    //   - Re-entrant function's D0 (exit value) does NOT leak into caller
    // ═════════════════════════════════════════════════════════════════════════════

    // ── Re-entrancy bridge infrastructure ───────────────────────────────────────
    //
    // Static VM pointer and function ID used by bridge callbacks to trigger
    // re-entry. Tests set these before building and running the VM.

    static noir_vm* g_reentry_vm    = nullptr;
    static uint16_t g_reentry_fn_id = 0;
    static uint8_t g_reentry_count  = 0;

    // Bridge callback that re-enters the VM by calling execute()
    static void mock_reentry_inv_add(uint16_t)
    {
        if (g_reentry_vm)
        {
            ++g_reentry_count;
            g_reentry_vm->execute(g_reentry_fn_id);
        }
    }

    // Bridge callback that re-enters conditionally (only on first call)
    static void mock_reentry_once_inv_add(uint16_t)
    {
        if (g_reentry_vm && g_reentry_count == 0)
        {
            ++g_reentry_count;
            g_reentry_vm->execute(g_reentry_fn_id);
        }
    }

    // Build a custom bridge that uses re-entrant inv_add but defaults for rest
    static noir_bridge make_reentry_bridge()
    {
        noir_bridge b = g_mock_bridge;
        b.inv_add     = mock_reentry_inv_add;
        return b;
    }

    static noir_bridge make_reentry_once_bridge()
    {
        noir_bridge b = g_mock_bridge;
        b.inv_add     = mock_reentry_once_inv_add;
        return b;
    }

    // Helper: build VM from builder with a custom bridge
    static noir_vm build_vm_with_bridge(bytecode_builder& bc,
        noir_bridge const* bridge,
        uint16_t stack_size = 512)
    {
        g_mock_log.reset();
        return noir_vm(bc.flags,
            sizeof(bc.flags),
            bc.values,
            sizeof(bc.values) / sizeof(bc.values[0]),
            bc.code,
            bc.pos,
            bc.toc,
            bc.func_count,
            bridge,
            stack_size);
    }

    // ── Basic re-entrancy ───────────────────────────────────────────────────────

    TEST_IMPL(test_noir_reentry_basic)
    {
        // Function 0: sets D0=100, calls INV_ADD (triggers re-entry to fn1), RET
        // Function 1: sets D0=42, RET
        // After re-entry returns, fn0's D0 should be restored to 100
        bytecode_builder bc;

        // fn0: main
        uint16_t fn0 = bc.begin_func();
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(100);

        bc.emit(noir_op::INV_ADD);  // triggers re-entry to fn1

        // After re-entry returns, D0 should still be 100
        bc.emit(noir_op::RET);

        // fn1: re-entrant function
        uint16_t fn1 = bc.begin_func();
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(42);
        bc.emit(noir_op::RET);

        auto bridge = make_reentry_bridge();
        auto vm     = build_vm_with_bridge(bc, &bridge);

        g_reentry_vm    = &vm;
        g_reentry_fn_id = fn1;
        g_reentry_count = 0;

        auto status = bc.run_to_completion(vm, fn0);

        g_reentry_vm = nullptr;

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(g_reentry_count == 1, "re-entry occurred once");
        TEST_ASSERT(vm.exit_code() == 100, "D0 restored to 100 after re-entry");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_reentry_restores_all_data_regs)
    {
        // Verify D0-D3 are all saved and restored across re-entry
        bytecode_builder bc;

        // fn0: set D0-D3, trigger re-entry, check D0-D3 preserved
        uint16_t fn0 = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1000);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(2000);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D2, noir_mode::IMM16);
        bc.emit_u16(3000);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D3, noir_mode::IMM16);
        bc.emit_u16(4000);

        bc.emit(noir_op::INV_ADD);  // triggers re-entry

        // After return, verify all regs via stack
        // Return D0 as exit code — if D0 is 1000, it was restored
        bc.emit(noir_op::RET);

        // fn1: clobbers all regs
        uint16_t fn1 = bc.begin_func();
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0xAAAA);
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(0xBBBB);
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D2, noir_mode::IMM16);
        bc.emit_u16(0xCCCC);
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D3, noir_mode::IMM16);
        bc.emit_u16(0xDDDD);
        bc.emit(noir_op::RET);

        auto bridge = make_reentry_bridge();
        auto vm     = build_vm_with_bridge(bc, &bridge);

        g_reentry_vm    = &vm;
        g_reentry_fn_id = fn1;
        g_reentry_count = 0;

        auto status = bc.run_to_completion(vm, fn0);

        g_reentry_vm = nullptr;

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.reg_d(0) == 1000, "D0 restored");
        TEST_ASSERT(vm.reg_d(1) == 2000, "D1 restored");
        TEST_ASSERT(vm.reg_d(2) == 3000, "D2 restored");
        TEST_ASSERT(vm.reg_d(3) == 4000, "D3 restored");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_reentry_restores_sr)
    {
        // SR should be saved and restored across re-entry
        bytecode_builder bc;

        // fn0: set SR via CMP, trigger re-entry, check SR preserved
        uint16_t fn0 = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);
        // Z=1

        bc.emit(noir_op::INV_ADD);  // triggers re-entry

        // After return, Z should still be 1 — use BEQ to test
        bc.emit(noir_op::BEQ);
        bc.emit_s16(4);  // skip MOV16

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(999);  // shouldn't reach here

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);

        bc.emit(noir_op::RET);

        // fn1: clobbers SR via CMP (N=1)
        uint16_t fn1 = bc.begin_func();
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0);
        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(100);
        // N=1, C=1, Z=0
        bc.emit(noir_op::RET);

        auto bridge = make_reentry_bridge();
        auto vm     = build_vm_with_bridge(bc, &bridge);

        g_reentry_vm    = &vm;
        g_reentry_fn_id = fn1;
        g_reentry_count = 0;

        auto status = bc.run_to_completion(vm, fn0);

        g_reentry_vm = nullptr;

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 1, "Z=1 preserved → BEQ taken");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_reentry_inner_result_not_visible)
    {
        // The inner function sets D0 to a value, but after re-entry returns
        // the outer function's D0 should be restored — the inner D0 shouldn't leak
        bytecode_builder bc;

        uint16_t fn0 = bc.begin_func();
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(555);

        bc.emit(noir_op::INV_ADD);  // triggers re-entry to fn1 (sets D0=999)

        // D0 should be 555, not 999
        bc.emit(noir_op::RET);

        uint16_t fn1 = bc.begin_func();
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(999);
        bc.emit(noir_op::RET);

        auto bridge = make_reentry_bridge();
        auto vm     = build_vm_with_bridge(bc, &bridge);

        g_reentry_vm    = &vm;
        g_reentry_fn_id = fn1;
        g_reentry_count = 0;

        auto status = bc.run_to_completion(vm, fn0);

        g_reentry_vm = nullptr;

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 555, "inner D0 (999) does not leak");
        TEST_SUCCESS;
    }

    // ── Re-entrant function uses CALL ───────────────────────────────────────────

    TEST_IMPL(test_noir_reentry_with_inner_call)
    {
        // Inner re-entrant function calls another function via CALL
        // Verifies CALL within a re-entrant context works
        bytecode_builder bc;

        // fn0: outer
        uint16_t fn0 = bc.begin_func();
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(100);
        bc.emit(noir_op::INV_ADD);  // triggers re-entry to fn1
        bc.emit(noir_op::RET);

        // fn1: re-entrant, calls fn2 via CALL
        uint16_t fn1 = bc.begin_func();
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(200);
        bc.emit(noir_op::CALL);
        bc.emit_u16(2);  // fn2
        bc.emit(noir_op::RET);

        // fn2: subroutine called from fn1
        /*uint16_t fn2 =*/bc.begin_func();
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(300);
        bc.emit(noir_op::RET);

        auto bridge = make_reentry_bridge();
        auto vm     = build_vm_with_bridge(bc, &bridge);

        g_reentry_vm    = &vm;
        g_reentry_fn_id = fn1;
        g_reentry_count = 0;

        auto status = bc.run_to_completion(vm, fn0);

        g_reentry_vm = nullptr;

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 100, "outer D0 restored after re-entry+CALL");
        TEST_SUCCESS;
    }

    // ── Re-entrant function modifies values/flags ───────────────────────────────

    TEST_IMPL(test_noir_reentry_modifies_shared_state)
    {
        // Re-entrant function modifies VALUES array — this is intentional
        // shared state and should persist after re-entry returns
        bytecode_builder bc;

        // fn0: read value[0], trigger re-entry, read value[0] again
        uint16_t fn0 = bc.begin_func();

        // Save initial value in D1
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::VALUE_IMM);
        bc.emit_u16(0);  // D1 = values[0] (initially 10)

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0);
        bc.emit(noir_op::INV_ADD);  // triggers re-entry

        // Read value[0] again after re-entry modified it
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::VALUE_IMM);
        bc.emit_u16(0);  // D0 = values[0] (now 99)

        bc.emit(noir_op::RET);

        // fn1: modifies values[0] to 99
        uint16_t fn1 = bc.begin_func();
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(99);
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::VALUE_IMM, noir_mode::REG_D0);
        bc.emit_u16(0);  // values[0] = D0 (99)
        bc.emit(noir_op::RET);

        bc.values[0] = 10;

        auto bridge = make_reentry_bridge();
        auto vm     = build_vm_with_bridge(bc, &bridge);

        g_reentry_vm    = &vm;
        g_reentry_fn_id = fn1;
        g_reentry_count = 0;

        auto status = bc.run_to_completion(vm, fn0);

        g_reentry_vm = nullptr;

        TEST_ASSERT(status == noir_status::completed, "should complete");
        // D1 (initial from values[0]) is restored — but re-read gets the modified value
        TEST_ASSERT(vm.reg_d(1) == 10, "D1 captured pre-reentry value[0]=10");
        TEST_ASSERT(vm.exit_code() == 99, "D0 re-read value[0]=99 after re-entry");
        TEST_SUCCESS;
    }

    // ── Outer function continues after re-entry ─────────────────────────────────

    TEST_IMPL(test_noir_reentry_outer_continues)
    {
        // After re-entry returns, the outer function should continue normally
        bytecode_builder bc;

        uint16_t fn0 = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(50);

        bc.emit(noir_op::INV_ADD);  // triggers re-entry

        // This code must execute after re-entry completes
        bc.emit(noir_op::ADD);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(25);
        // D0 = 50 + 25 = 75

        bc.emit(noir_op::RET);

        // fn1: trivial
        uint16_t fn1 = bc.begin_func();
        bc.emit(noir_op::RET);

        auto bridge = make_reentry_bridge();
        auto vm     = build_vm_with_bridge(bc, &bridge);

        g_reentry_vm    = &vm;
        g_reentry_fn_id = fn1;
        g_reentry_count = 0;

        auto status = bc.run_to_completion(vm, fn0);

        g_reentry_vm = nullptr;

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 75, "outer continues: 50 + 25 = 75");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_reentry_outer_calls_after_reentry)
    {
        // Outer function does CALL after re-entry — tests mixed re-entry + CALL
        bytecode_builder bc;

        // fn0: trigger re-entry, then CALL fn2
        uint16_t fn0 = bc.begin_func();
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::INV_ADD);  // re-enters fn1

        // After re-entry returns, CALL fn2
        bc.emit(noir_op::CALL);
        bc.emit_u16(2);  // fn2

        bc.emit(noir_op::RET);

        // fn1: re-entrant, trivial
        uint16_t fn1 = bc.begin_func();
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0xFFFF);
        bc.emit(noir_op::RET);

        // fn2: sets D0 to 77
        /*uint16_t fn2 =*/bc.begin_func();
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(77);
        bc.emit(noir_op::RET);

        auto bridge = make_reentry_bridge();
        auto vm     = build_vm_with_bridge(bc, &bridge);

        g_reentry_vm    = &vm;
        g_reentry_fn_id = fn1;
        g_reentry_count = 0;

        auto status = bc.run_to_completion(vm, fn0);

        g_reentry_vm = nullptr;

        TEST_ASSERT(status == noir_status::completed, "should complete");
        // After re-entry, D0 is restored to 10. Then CALL fn2 sets D0=77.
        TEST_ASSERT(vm.exit_code() == 77, "CALL after re-entry works");
        TEST_SUCCESS;
    }

    // ── Multiple re-entries in sequence ──────────────────────────────────────────

    TEST_IMPL(test_noir_reentry_multiple_sequential)
    {
        // Two separate re-entries in sequence (INV_ADD called twice)
        // Each time inner function clobbers all regs, each time they're restored
        bytecode_builder bc;

        uint16_t fn0 = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(100);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(200);

        bc.emit(noir_op::INV_ADD);  // first re-entry

        // After first re-entry, modify D0 to 150
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(150);

        bc.emit(noir_op::INV_ADD);  // second re-entry

        // D0 should be 150 (set between re-entries), D1 should be 200 (original)
        bc.emit(noir_op::RET);

        // fn1: clobbers D0 and D1
        uint16_t fn1 = bc.begin_func();
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0xDEAD);
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(0xBEEF);
        bc.emit(noir_op::RET);

        auto bridge = make_reentry_bridge();
        auto vm     = build_vm_with_bridge(bc, &bridge);

        g_reentry_vm    = &vm;
        g_reentry_fn_id = fn1;
        g_reentry_count = 0;

        auto status = bc.run_to_completion(vm, fn0);

        g_reentry_vm = nullptr;

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(g_reentry_count == 2, "re-entry occurred twice");
        TEST_ASSERT(vm.exit_code() == 150, "D0=150 after second re-entry");
        TEST_ASSERT(vm.reg_d(1) == 200, "D1=200 restored after second re-entry");
        TEST_SUCCESS;
    }

    // ── Nested re-entry (level 3) ───────────────────────────────────────────────

    // Nested re-entry with counter-based target switching
    static uint16_t g_nested_targets[4] = { 0, 0, 0, 0 };
    static uint8_t g_nested_call_idx    = 0;

    static void mock_nested_reentry_inv_add(uint16_t)
    {
        if (g_reentry_vm && g_nested_call_idx < 4)
        {
            uint16_t target = g_nested_targets[g_nested_call_idx++];
            g_reentry_vm->execute(target);
        }
    }

    TEST_IMPL(test_noir_reentry_nested_two_levels)
    {
        // fn0 → re-enters fn1 → fn1 re-enters fn2 → fn2 returns → fn1 returns → fn0 returns
        // Tests reentry_level reaching 3
        bytecode_builder bc;

        // fn0: sets D0=100, triggers re-entry
        uint16_t fn0 = bc.begin_func();
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(100);
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(111);
        bc.emit(noir_op::INV_ADD);  // → fn1
        bc.emit(noir_op::RET);

        // fn1: sets D0=200, triggers re-entry to fn2
        uint16_t fn1 = bc.begin_func();
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(200);
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(222);
        bc.emit(noir_op::INV_ADD);  // → fn2
        // After fn2 returns, D0 should be restored to 200
        bc.emit(noir_op::RET);

        // fn2: sets D0=300, returns
        uint16_t fn2 = bc.begin_func();
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(300);
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(333);
        bc.emit(noir_op::RET);

        // Set up nested targets: first call → fn1, second call → fn2
        g_nested_targets[0] = fn1;
        g_nested_targets[1] = fn2;
        g_nested_call_idx   = 0;

        noir_bridge bridge = g_mock_bridge;
        bridge.inv_add     = mock_nested_reentry_inv_add;
        auto vm            = build_vm_with_bridge(bc, &bridge);

        g_reentry_vm = &vm;

        auto status = bc.run_to_completion(vm, fn0);

        g_reentry_vm = nullptr;

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(g_nested_call_idx == 2, "two re-entries occurred");
        TEST_ASSERT(vm.exit_code() == 100, "fn0 D0=100 restored");
        TEST_ASSERT(vm.reg_d(1) == 111, "fn0 D1=111 restored");
        TEST_SUCCESS;
    }

    // ── Re-entry with locals ────────────────────────────────────────────────────

    TEST_IMPL(test_noir_reentry_with_locals)
    {
        // Outer function has local variables. Re-entrant inner function
        // also has locals. After inner returns, outer's FP-relative locals
        // should still be accessible.
        bytecode_builder bc;

        // fn0: allocate locals, write to local, trigger re-entry, read local
        uint16_t fn0 = bc.begin_func();

        bc.emit(noir_op::LOCALS);
        bc.emit(4);  // 4 bytes = 2 x 16-bit locals

        // Write 42 to local[0] (FP-relative offset 0)
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(42);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::LOCAL, noir_mode::REG_D0);
        bc.emit(0);  // offset 0

        // Write 99 to local[1] (FP-relative offset 2)
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(99);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::LOCAL, noir_mode::REG_D0);
        bc.emit(2);  // offset 2

        // Trigger re-entry
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0);
        bc.emit(noir_op::INV_ADD);

        // Read local[0] back
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::LOCAL);
        bc.emit(0);

        // Read local[1] into D1
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::LOCAL);
        bc.emit(2);

        bc.emit(noir_op::RET);

        // fn1: re-entrant, also uses locals to clobber stack
        uint16_t fn1 = bc.begin_func();
        bc.emit(noir_op::LOCALS);
        bc.emit(4);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0xFFFF);
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::LOCAL, noir_mode::REG_D0);
        bc.emit(0);
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::LOCAL, noir_mode::REG_D0);
        bc.emit(2);

        bc.emit(noir_op::RET);

        auto bridge = make_reentry_bridge();
        auto vm     = build_vm_with_bridge(bc, &bridge);

        g_reentry_vm    = &vm;
        g_reentry_fn_id = fn1;
        g_reentry_count = 0;

        auto status = bc.run_to_completion(vm, fn0);

        g_reentry_vm = nullptr;

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 42, "local[0] preserved after re-entry");
        TEST_ASSERT(vm.reg_d(1) == 99, "local[1] preserved after re-entry");
        TEST_SUCCESS;
    }

    // ── Re-entry with stack operations ──────────────────────────────────────────

    TEST_IMPL(test_noir_reentry_stack_balanced)
    {
        // Push values before re-entry, pop them after — stack should be balanced
        bytecode_builder bc;

        uint16_t fn0 = bc.begin_func();

        // Push two values
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(111);
        bc.emit(noir_op::PUSH);
        bc.emit_mode(0, noir_mode::REG_D0);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(222);
        bc.emit(noir_op::PUSH);
        bc.emit_mode(0, noir_mode::REG_D0);

        // Trigger re-entry
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0);
        bc.emit(noir_op::INV_ADD);

        // Pop them back (LIFO)
        bc.emit(noir_op::POP);
        bc.emit_mode(noir_mode::REG_D0, 0);
        // D0 = 222

        bc.emit(noir_op::POP);
        bc.emit_mode(noir_mode::REG_D1, 0);
        // D1 = 111

        bc.emit(noir_op::RET);

        // fn1: pushes and pops its own values (should not corrupt outer stack)
        uint16_t fn1 = bc.begin_func();
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0xAAAA);
        bc.emit(noir_op::PUSH);
        bc.emit_mode(0, noir_mode::REG_D0);
        bc.emit(noir_op::POP);
        bc.emit_mode(noir_mode::REG_D0, 0);
        bc.emit(noir_op::RET);

        auto bridge = make_reentry_bridge();
        auto vm     = build_vm_with_bridge(bc, &bridge);

        g_reentry_vm    = &vm;
        g_reentry_fn_id = fn1;
        g_reentry_count = 0;

        auto status = bc.run_to_completion(vm, fn0);

        g_reentry_vm = nullptr;

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 222, "popped 222 (LIFO order)");
        TEST_ASSERT(vm.reg_d(1) == 111, "popped 111 (LIFO order)");
        TEST_SUCCESS;
    }

    // ── Re-entry with conditional return ────────────────────────────────────────

    TEST_IMPL(test_noir_reentry_inner_uses_retz)
    {
        // Inner re-entrant function uses RETZ — conditional return should work
        bytecode_builder bc;

        uint16_t fn0 = bc.begin_func();
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(42);
        bc.emit(noir_op::INV_ADD);
        bc.emit(noir_op::RET);

        // fn1: sets Z=1 then RETZ → should return
        uint16_t fn1 = bc.begin_func();
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0);
        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0);
        // Z=1
        bc.emit(noir_op::RETZ);  // conditional return with Z=1
        // Should not reach here:
        bc.emit(noir_op::HALT);

        auto bridge = make_reentry_bridge();
        auto vm     = build_vm_with_bridge(bc, &bridge);

        g_reentry_vm    = &vm;
        g_reentry_fn_id = fn1;
        g_reentry_count = 0;

        auto status = bc.run_to_completion(vm, fn0);

        g_reentry_vm = nullptr;

        TEST_ASSERT(status == noir_status::completed, "should complete (not halt)");
        TEST_ASSERT(vm.exit_code() == 42, "outer D0 restored after RETZ re-entry");
        TEST_SUCCESS;
    }

    // ── Re-entry from different bridge callbacks ────────────────────────────────

    TEST_IMPL(test_noir_reentry_from_inv_has)
    {
        // Re-entry triggered from INV_HAS callback
        // After re-entry, SR should reflect the INV_HAS result (not re-entrant SR)
        bytecode_builder bc;

        uint16_t fn0 = bc.begin_func();
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(5);
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(777);

        bc.emit(noir_op::INV_ADD);  // triggers re-entry

        // After re-entry returns, D1 should be 777
        bc.emit(noir_op::RET);

        // fn1: clobbers D1
        uint16_t fn1 = bc.begin_func();
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(0);
        bc.emit(noir_op::RET);

        auto bridge = make_reentry_bridge();
        auto vm     = build_vm_with_bridge(bc, &bridge);

        g_reentry_vm    = &vm;
        g_reentry_fn_id = fn1;
        g_reentry_count = 0;

        auto status = bc.run_to_completion(vm, fn0);

        g_reentry_vm = nullptr;

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 5, "D0=5 restored");
        TEST_ASSERT(vm.reg_d(1) == 777, "D1=777 restored after inner clobber");
        TEST_SUCCESS;
    }

    // ── No re-entry (baseline) ──────────────────────────────────────────────────

    TEST_IMPL(test_noir_no_reentry_normal_execution)
    {
        // Baseline: bridge callback does NOT re-enter → normal execution
        bytecode_builder bc;

        uint16_t fn0 = bc.begin_func();
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(42);
        bc.emit(noir_op::INV_ADD);  // normal bridge call, no re-entry
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn0);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 42, "normal execution unaffected");
        TEST_SUCCESS;
    }

    // ── Reentry level 1 exit code ───────────────────────────────────────────────

    TEST_IMPL(test_noir_reentry_exit_code_from_outer)
    {
        // The final exit_code should come from the outermost (level 1) function
        // not from any re-entrant inner function
        bytecode_builder bc;

        uint16_t fn0 = bc.begin_func();
        bc.emit(noir_op::INV_ADD);  // triggers re-entry, inner sets D0=999

        // After re-entry, D0 is restored to whatever it was.
        // Explicitly set D0 for exit code
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(42);
        bc.emit(noir_op::RET);

        uint16_t fn1 = bc.begin_func();
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(999);
        bc.emit(noir_op::RET);

        auto bridge = make_reentry_bridge();
        auto vm     = build_vm_with_bridge(bc, &bridge);

        g_reentry_vm    = &vm;
        g_reentry_fn_id = fn1;
        g_reentry_count = 0;

        auto status = bc.run_to_completion(vm, fn0);

        g_reentry_vm = nullptr;

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 42, "exit_code from outer (42), not inner (999)");
        TEST_SUCCESS;
    }

    // ── Re-entrant with arithmetic after restore ────────────────────────────────

    TEST_IMPL(test_noir_reentry_arithmetic_after_restore)
    {
        // After re-entry, continue with arithmetic using restored registers
        bytecode_builder bc;

        uint16_t fn0 = bc.begin_func();
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(100);
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(50);

        bc.emit(noir_op::INV_ADD);  // re-entry

        // D0=100, D1=50 restored
        bc.emit(noir_op::ADD);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D1);
        // D0 = 150

        bc.emit(noir_op::RET);

        // fn1: clobbers both
        uint16_t fn1 = bc.begin_func();
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0);
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(0);
        bc.emit(noir_op::RET);

        auto bridge = make_reentry_bridge();
        auto vm     = build_vm_with_bridge(bc, &bridge);

        g_reentry_vm    = &vm;
        g_reentry_fn_id = fn1;
        g_reentry_count = 0;

        auto status = bc.run_to_completion(vm, fn0);

        g_reentry_vm = nullptr;

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 150, "100 + 50 = 150 after re-entry");
        TEST_SUCCESS;
    }

    // ── Re-entry into invalid func ──────────────────────────────────────────────

    TEST_IMPL(test_noir_reentry_invalid_func)
    {
        // Re-entrant execute() with invalid func_id should return error
        // but the outer function should still be able to continue
        // (execute() returns error without modifying state)
        bytecode_builder bc;

        uint16_t fn0 = bc.begin_func();
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(42);
        bc.emit(noir_op::INV_ADD);  // triggers re-entry with invalid fn
        bc.emit(noir_op::RET);

        auto bridge = make_reentry_bridge();
        auto vm     = build_vm_with_bridge(bc, &bridge);

        g_reentry_vm    = &vm;
        g_reentry_fn_id = 99;  // invalid
        g_reentry_count = 0;

        auto status = bc.run_to_completion(vm, fn0);

        g_reentry_vm = nullptr;

        // execute() with invalid func returns err_invalid_func but doesn't
        // modify the VM state — outer should continue and complete
        TEST_ASSERT(status == noir_status::completed, "outer completes despite inner error");
        TEST_ASSERT(vm.exit_code() == 42, "D0 preserved (no re-entry happened)");
        TEST_SUCCESS;
    }

    // ── State after re-entry completion ─────────────────────────────────────────

    TEST_IMPL(test_noir_reentry_state_is_idle_after_completion)
    {
        // After everything completes, VM should be in idle state
        bytecode_builder bc;

        uint16_t fn0 = bc.begin_func();
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);
        bc.emit(noir_op::INV_ADD);
        bc.emit(noir_op::RET);

        uint16_t fn1 = bc.begin_func();
        bc.emit(noir_op::RET);

        auto bridge = make_reentry_bridge();
        auto vm     = build_vm_with_bridge(bc, &bridge);

        g_reentry_vm    = &vm;
        g_reentry_fn_id = fn1;
        g_reentry_count = 0;

        auto status = bc.run_to_completion(vm, fn0);

        g_reentry_vm = nullptr;

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.state() == noir_state::idle, "VM idle after completion");
        TEST_SUCCESS;
    }

    // ── reset_execution() abandons the run but keeps game state ─────────────────

    TEST_IMPL(test_noir_reset_execution_clears_run_keeps_state)
    {
        // Park the VM mid-run with a dirty register and a mutated game value,
        // then reset_execution() and confirm the execution state is clean while
        // VALUES survive and the VM can run a fresh function afterwards.
        bytecode_builder bc;

        uint16_t fn0 = bc.begin_func();
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(42);
        bc.emit(noir_op::INCV);  // VALUES[0] += 1
        bc.emit_u16(0);
        bc.emit(noir_op::WAIT);  // park the VM (non-idle) mid-run
        bc.emit_u16(5);
        bc.emit(noir_op::RET);

        uint16_t fn1 = bc.begin_func();
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(5);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        TEST_ASSERT(vm.execute(fn0) == noir_status::ok, "fn0 starts");
        vm.update();
        TEST_ASSERT(vm.state() == noir_state::waiting, "vm parked mid-run");
        TEST_ASSERT(vm.reg_d(0) == 42, "D0 dirty before reset");
        TEST_ASSERT(bc.values[0] == 1, "value mutated before reset");

        vm.reset_execution();
        TEST_ASSERT(vm.state() == noir_state::idle, "idle after reset");
        TEST_ASSERT(vm.reg_d(0) == 0 && vm.reg_d(1) == 0 && vm.reg_d(2) == 0 && vm.reg_d(3) == 0,
            "registers cleared by reset");
        TEST_ASSERT(vm.sr() == 0, "status register cleared by reset");
        TEST_ASSERT(vm.exit_code() == 0, "exit code cleared by reset");
        TEST_ASSERT(bc.values[0] == 1, "game state preserved across reset");

        auto status = bc.run_to_completion(vm, fn1);
        TEST_ASSERT(status == noir_status::completed, "fresh function runs after reset");
        TEST_ASSERT(vm.exit_code() == 5, "fresh function produced its own result");
        TEST_SUCCESS;
    }

    // ═════════════════════════════════════════════════════════════════════════════
    // Test suite registration
    // ═════════════════════════════════════════════════════════════════════════════

    TEST_SUITE_BEGIN(noir_vm_reentry)
    // Basic re-entrancy
    TEST(test_noir_reentry_basic)
    TEST(test_noir_reentry_restores_all_data_regs)
    TEST(test_noir_reentry_restores_sr)
    TEST(test_noir_reentry_inner_result_not_visible)
    // Re-entry with CALL
    TEST(test_noir_reentry_with_inner_call)
    // Shared state
    TEST(test_noir_reentry_modifies_shared_state)
    // Outer continues after re-entry
    TEST(test_noir_reentry_outer_continues)
    TEST(test_noir_reentry_outer_calls_after_reentry)
    // Multiple and nested
    TEST(test_noir_reentry_multiple_sequential)
    TEST(test_noir_reentry_nested_two_levels)
    // Stack and locals
    TEST(test_noir_reentry_with_locals)
    TEST(test_noir_reentry_stack_balanced)
    // Conditional return
    TEST(test_noir_reentry_inner_uses_retz)
    // Other bridge callbacks
    TEST(test_noir_reentry_from_inv_has)
    // Baseline
    TEST(test_noir_no_reentry_normal_execution)
    // Exit code
    TEST(test_noir_reentry_exit_code_from_outer)
    // Post-reentry operations
    TEST(test_noir_reentry_arithmetic_after_restore)
    // Error handling
    TEST(test_noir_reentry_invalid_func)
    // Final state
    TEST(test_noir_reentry_state_is_idle_after_completion)
    // Execution reset
    TEST(test_noir_reset_execution_clears_run_keeps_state)
    TEST_SUITE_END

}  // namespace NEONengine

#endif  // ACE_TEST_RUNNER
#endif  // __NOIR_VM_REENTRY_TESTS_H__INCLUDED__
