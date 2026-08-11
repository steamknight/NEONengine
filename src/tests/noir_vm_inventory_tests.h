#ifndef __NOIR_VM_INVENTORY_TESTS_H__INCLUDED__
#define __NOIR_VM_INVENTORY_TESTS_H__INCLUDED__

#ifdef ACE_TEST_RUNNER

#include "types.h"

#include "core/game_session.h"
#include "tests/noir_vm_test_utils.h"

#include <stdio.h>

namespace NEONengine
{
    namespace
    {
        game_session s_session_bridge_inventory;

        void session_bridge_inv_add(u16 item_id) { s_session_bridge_inventory.add_item(item_id); }
        void session_bridge_inv_remove(u16 item_id) { s_session_bridge_inventory.remove_item(item_id); }
        bool session_bridge_inv_has(u16 item_id) { return s_session_bridge_inventory.has(item_id); }
        u16 session_bridge_inv_count(u16 item_id) { return s_session_bridge_inventory.count(item_id); }
        void session_bridge_inv_clear_item(u16 item_id)
        {
            s_session_bridge_inventory.clear_item(item_id);
        }

#ifdef GAME_DEBUG
        void session_bridge_report_invalid_item(u16 item_id)
        {
            char message[NOIR_ERROR_MESSAGE_SIZE];
            snprintf(message, sizeof(message), "inventory item %hu is out of range", item_id);
            mock_report_error(message);
        }
#endif

        noir_bridge session_inventory_bridge(u32 item_count)
        {
            s_session_bridge_inventory.create_for_test(0, 0, item_count);
#ifdef GAME_DEBUG
            s_session_bridge_inventory.set_invalid_item_id_reporter(session_bridge_report_invalid_item);
#endif

            noir_bridge bridge = g_mock_bridge;
            bridge.inv_add     = {session_bridge_inv_add};
            bridge.inv_remove  = {session_bridge_inv_remove};
            bridge.inv_has     = {session_bridge_inv_has};
            bridge.inv_count   = {session_bridge_inv_count};
            bridge.inv_clear_item = {session_bridge_inv_clear_item};
            return bridge;
        }
    }  // namespace

    // ═════════════════════════════════════════════════════════════════════════════
    // Game Calls — Inventory opcodes (0x80–0x8F)
    //   INV_ADD, INV_REMOVE, INV_HAS, INV_COUNT, INV_CLEAR
    //
    // All inventory opcodes are 1-byte (opcode only). Arguments passed in
    // D0–D3, return values in D0. Bridge function pointers are called.
    //
    // INV_ADD    (0x80): D0=item_id → bridge->inv_add(D0)
    // INV_REMOVE (0x81): D0=item_id → bridge->inv_remove(D0)
    // INV_HAS    (0x82): D0=item_id → bridge->inv_has(D0) → Z (Z=0=yes, Z=1=no)
    // INV_COUNT  (0x83): D0=item_id → bridge->inv_count(D0) → D0=count
    // INV_CLEAR  (0x84): D0=item_id → bridge->inv_clear_item(D0)
    // 0x85–0x8F: Reserved → err_invalid_opcode
    //
    // SR:
    //   INV_HAS sets Z (and clears N, C, V). All others leave SR unchanged.
    // ═════════════════════════════════════════════════════════════════════════════

    // ── INV_ADD ─────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_inv_add_basic)
    {
        // INV_ADD with D0=42 → bridge->inv_add(42)
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(42);

        bc.emit(noir_op::INV_ADD);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(g_mock_log.inv_add_count == 1, "inv_add called once");
        TEST_ASSERT(g_mock_log.inv_added[0] == 42, "inv_add received item_id 42");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_inv_add_preserves_d0)
    {
        // D0 should not be modified by INV_ADD
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(123);

        bc.emit(noir_op::INV_ADD);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 123, "D0 unchanged after INV_ADD");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_inv_add_multiple)
    {
        // Add several items in sequence
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);
        bc.emit(noir_op::INV_ADD);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(20);
        bc.emit(noir_op::INV_ADD);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(30);
        bc.emit(noir_op::INV_ADD);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(g_mock_log.inv_add_count == 3, "inv_add called 3 times");
        TEST_ASSERT(g_mock_log.inv_added[0] == 10, "first item 10");
        TEST_ASSERT(g_mock_log.inv_added[1] == 20, "second item 20");
        TEST_ASSERT(g_mock_log.inv_added[2] == 30, "third item 30");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_inv_add_preserves_sr)
    {
        // INV_ADD should not modify SR
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        // Set up known SR: CMP 10,10 → Z=1
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);
        // Now Z=1

        bc.emit(noir_op::INV_ADD);  // Should not touch SR

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT((vm.sr() & noir_sr::Z) != 0, "Z preserved after INV_ADD");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_inv_add_preserves_other_regs)
    {
        // D1-D3 should be untouched
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(99);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(111);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D2, noir_mode::IMM16);
        bc.emit_u16(222);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D3, noir_mode::IMM16);
        bc.emit_u16(333);

        bc.emit(noir_op::INV_ADD);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.reg_d(1) == 111, "D1 unchanged");
        TEST_ASSERT(vm.reg_d(2) == 222, "D2 unchanged");
        TEST_ASSERT(vm.reg_d(3) == 333, "D3 unchanged");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_inv_add_zero_item_id)
    {
        // item_id = 0 should still call bridge
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::CLR);
        bc.emit_mode(noir_mode::REG_D0, 0);

        bc.emit(noir_op::INV_ADD);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(g_mock_log.inv_add_count == 1, "called once");
        TEST_ASSERT(g_mock_log.inv_added[0] == 0, "item_id 0 passed correctly");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_inv_add_max_item_id)
    {
        // item_id = 0xFFFF (max u16)
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0xFFFF);

        bc.emit(noir_op::INV_ADD);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(g_mock_log.inv_added[0] == 0xFFFF, "max item_id passed correctly");
        TEST_SUCCESS;
    }

    // ── INV_REMOVE ──────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_inv_remove_basic)
    {
        // INV_REMOVE with D0=7 → bridge->inv_remove(7)
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(7);

        bc.emit(noir_op::INV_REMOVE);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(g_mock_log.inv_remove_count == 1, "inv_remove called once");
        TEST_ASSERT(g_mock_log.inv_removed[0] == 7, "inv_remove received item_id 7");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_inv_remove_preserves_d0)
    {
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(55);

        bc.emit(noir_op::INV_REMOVE);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 55, "D0 unchanged after INV_REMOVE");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_inv_remove_preserves_sr)
    {
        // INV_REMOVE should not modify SR
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(5);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(5);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D1);
        // Z=1

        bc.emit(noir_op::INV_REMOVE);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT((vm.sr() & noir_sr::Z) != 0, "Z preserved after INV_REMOVE");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_inv_remove_multiple)
    {
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(100);
        bc.emit(noir_op::INV_REMOVE);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(200);
        bc.emit(noir_op::INV_REMOVE);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(g_mock_log.inv_remove_count == 2, "inv_remove called twice");
        TEST_ASSERT(g_mock_log.inv_removed[0] == 100, "first remove 100");
        TEST_ASSERT(g_mock_log.inv_removed[1] == 200, "second remove 200");
        TEST_SUCCESS;
    }

    // ── INV_HAS ─────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_inv_has_found)
    {
        // bridge returns true → Z=0 (has item)
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(5);

        bc.emit(noir_op::INV_HAS);

        bc.emit(noir_op::RET);

        auto vm                   = bc.build_vm();
        g_mock_log.inv_has_result = true;
        auto status               = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(g_mock_log.inv_has_query == 5, "queried item_id 5");
        TEST_ASSERT((vm.sr() & noir_sr::Z) == 0, "Z=0 when has item");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_inv_has_not_found)
    {
        // bridge returns false → Z=1 (doesn't have item)
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(99);

        bc.emit(noir_op::INV_HAS);

        bc.emit(noir_op::RET);

        auto vm                   = bc.build_vm();
        g_mock_log.inv_has_result = false;
        auto status               = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(g_mock_log.inv_has_query == 99, "queried item_id 99");
        TEST_ASSERT((vm.sr() & noir_sr::Z) != 0, "Z=1 when item not found");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_inv_has_preserves_d0)
    {
        // D0 is input only — should not be modified
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(77);

        bc.emit(noir_op::INV_HAS);

        bc.emit(noir_op::RET);

        auto vm                   = bc.build_vm();
        g_mock_log.inv_has_result = true;
        auto status               = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 77, "D0 unchanged after INV_HAS");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_inv_has_clears_other_flags)
    {
        // INV_HAS should clear N, C, V flags
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        // Set up SR with N=1: CMP 0, 100 → 0-100 = negative
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(100);
        // N=1, C=1

        bc.emit(noir_op::INV_HAS);  // Should replace SR entirely

        bc.emit(noir_op::RET);

        auto vm                   = bc.build_vm();
        g_mock_log.inv_has_result = true;
        auto status               = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT((vm.sr() & noir_sr::N) == 0, "N cleared by INV_HAS");
        TEST_ASSERT((vm.sr() & noir_sr::C) == 0, "C cleared by INV_HAS");
        TEST_ASSERT((vm.sr() & noir_sr::V) == 0, "V cleared by INV_HAS");
        TEST_ASSERT((vm.sr() & noir_sr::Z) == 0, "Z=0 (has item)");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_inv_has_with_beq)
    {
        // Common pattern: INV_HAS + BEQ = branch if NOT has
        // If item not found (Z=1), BEQ taken
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(5);

        bc.emit(noir_op::INV_HAS);

        bc.emit(noir_op::BEQ);
        bc.emit_s16(4);  // skip MOV16 (4 bytes)

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(999);  // should be skipped

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);  // branch target

        bc.emit(noir_op::RET);

        auto vm                   = bc.build_vm();
        g_mock_log.inv_has_result = false;  // not found → Z=1
        auto status               = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 1, "BEQ taken: item not found");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_inv_has_with_bne)
    {
        // Common pattern: INV_HAS + BNE = branch if HAS
        // If item found (Z=0), BNE taken
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(5);

        bc.emit(noir_op::INV_HAS);

        bc.emit(noir_op::BNE);
        bc.emit_s16(4);  // skip MOV16

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(999);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);  // branch target

        bc.emit(noir_op::RET);

        auto vm                   = bc.build_vm();
        g_mock_log.inv_has_result = true;  // found → Z=0
        auto status               = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 1, "BNE taken: item found");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_inv_has_preserves_other_regs)
    {
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(200);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D2, noir_mode::IMM16);
        bc.emit_u16(300);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D3, noir_mode::IMM16);
        bc.emit_u16(400);

        bc.emit(noir_op::INV_HAS);

        bc.emit(noir_op::RET);

        auto vm                   = bc.build_vm();
        g_mock_log.inv_has_result = true;
        auto status               = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.reg_d(1) == 200, "D1 unchanged");
        TEST_ASSERT(vm.reg_d(2) == 300, "D2 unchanged");
        TEST_ASSERT(vm.reg_d(3) == 400, "D3 unchanged");
        TEST_SUCCESS;
    }

    // ── INV_COUNT ───────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_inv_count_basic)
    {
        // bridge->inv_count(12) returns 5 → D0 = 5
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(12);

        bc.emit(noir_op::INV_COUNT);

        bc.emit(noir_op::RET);

        auto vm                     = bc.build_vm();
        g_mock_log.inv_count_result = 5;
        auto status                 = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(g_mock_log.inv_count_query == 12, "inv_count queried item_id 12");
        TEST_ASSERT(vm.exit_code() == 5, "D0 = inv_count result (5)");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_inv_count_zero)
    {
        // Empty inventory for item 2: bridge returns 0
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(2);

        bc.emit(noir_op::INV_COUNT);

        bc.emit(noir_op::RET);

        auto vm                     = bc.build_vm();
        g_mock_log.inv_count_result = 0;
        auto status                 = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(g_mock_log.inv_count_query == 2, "count queried item_id 2");
        TEST_ASSERT(vm.exit_code() == 0, "D0 = 0 (empty inventory)");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_inv_count_large)
    {
        // Large count value for one item
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(77);

        bc.emit(noir_op::INV_COUNT);

        bc.emit(noir_op::RET);

        auto vm                     = bc.build_vm();
        g_mock_log.inv_count_result = 1000;
        auto status                 = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(g_mock_log.inv_count_query == 77, "count queried item_id 77");
        TEST_ASSERT(vm.exit_code() == 1000, "D0 = 1000");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_inv_count_overwrites_d0)
    {
        // D0 carries the item id in, then receives the count out.
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(9999);  // item id

        bc.emit(noir_op::INV_COUNT);

        bc.emit(noir_op::RET);

        auto vm                     = bc.build_vm();
        g_mock_log.inv_count_result = 3;
        auto status                 = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(g_mock_log.inv_count_query == 9999, "count queried the incoming D0 item id");
        TEST_ASSERT(vm.exit_code() == 3, "D0 overwritten with count (3)");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_inv_count_preserves_other_regs)
    {
        // D1-D3 should be untouched
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(111);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D2, noir_mode::IMM16);
        bc.emit_u16(222);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D3, noir_mode::IMM16);
        bc.emit_u16(333);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(88);

        bc.emit(noir_op::INV_COUNT);

        bc.emit(noir_op::RET);

        auto vm                     = bc.build_vm();
        g_mock_log.inv_count_result = 7;
        auto status                 = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(g_mock_log.inv_count_query == 88, "count queried item_id from D0");
        TEST_ASSERT(vm.exit_code() == 7, "D0 = count");
        TEST_ASSERT(vm.reg_d(1) == 111, "D1 unchanged");
        TEST_ASSERT(vm.reg_d(2) == 222, "D2 unchanged");
        TEST_ASSERT(vm.reg_d(3) == 333, "D3 unchanged");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_inv_count_preserves_sr)
    {
        // INV_COUNT should not modify SR
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);  // item id

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);
        // Z=1

        bc.emit(noir_op::INV_COUNT);

        bc.emit(noir_op::RET);

        auto vm                     = bc.build_vm();
        g_mock_log.inv_count_result = 3;
        auto status                 = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(g_mock_log.inv_count_query == 10, "count queried item_id 10");
        TEST_ASSERT((vm.sr() & noir_sr::Z) != 0, "Z preserved after INV_COUNT");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_inv_count_with_cmp)
    {
        // Common pattern: INV_COUNT + CMP to check if one item has enough copies
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(3);  // queried item id

        bc.emit(noir_op::INV_COUNT);  // D0 = 3

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(5);
        // 3 - 5 = -2 → N=1 (count < 5)

        bc.emit(noir_op::RET);

        auto vm                     = bc.build_vm();
        g_mock_log.inv_count_result = 3;
        auto status                 = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(g_mock_log.inv_count_query == 3, "count queried item_id 3");
        TEST_ASSERT((vm.sr() & noir_sr::N) != 0, "N=1 (3 < 5)");
        TEST_ASSERT((vm.sr() & noir_sr::Z) == 0, "Z=0 (3 != 5)");
        TEST_SUCCESS;
    }

    // ── INV_CLEAR ───────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_inv_clear_basic)
    {
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(9);

        bc.emit(noir_op::INV_CLEAR);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(g_mock_log.inv_clear_called, "inv_clear_item called");
        TEST_ASSERT(g_mock_log.inv_cleared_item_id == 9, "clear_item queried item_id 9");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_inv_clear_preserves_regs)
    {
        // All registers should be preserved
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);  // item id

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(20);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D2, noir_mode::IMM16);
        bc.emit_u16(30);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D3, noir_mode::IMM16);
        bc.emit_u16(40);

        bc.emit(noir_op::INV_CLEAR);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(g_mock_log.inv_cleared_item_id == 10, "clear_item queried item_id from D0");
        TEST_ASSERT(vm.exit_code() == 10, "D0 unchanged");
        TEST_ASSERT(vm.reg_d(1) == 20, "D1 unchanged");
        TEST_ASSERT(vm.reg_d(2) == 30, "D2 unchanged");
        TEST_ASSERT(vm.reg_d(3) == 40, "D3 unchanged");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_inv_clear_preserves_sr)
    {
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        // Set D0 to the queried item id.
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(4);

        // Set N=1 using D1 so D0 keeps that item id.
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(0);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(100);
        // N=1, C=1

        bc.emit(noir_op::INV_CLEAR);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(g_mock_log.inv_cleared_item_id == 4, "clear_item queried item_id 4");
        TEST_ASSERT((vm.sr() & noir_sr::N) != 0, "N preserved after INV_CLEAR");
        TEST_ASSERT((vm.sr() & noir_sr::C) != 0, "C preserved after INV_CLEAR");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_inventory_ops_use_session_store_bridge)
    {
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);
        bc.emit(noir_op::INV_ADD);
        bc.emit(noir_op::INV_ADD);
        bc.emit(noir_op::INV_HAS);
        bc.emit(noir_op::INV_COUNT);
        // INV_COUNT answers in D0, so the item id is gone. Park the count in D1,
        // put the id back for the two opcodes that still need it, then return
        // the count so the exit code still reports what INV_COUNT found.
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::REG_D0);
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);
        bc.emit(noir_op::INV_REMOVE);
        bc.emit(noir_op::INV_CLEAR);
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D1);
        bc.emit(noir_op::RET);

        auto bridge = session_inventory_bridge(3);
        auto vm     = bc.build_vm(256, &bridge);
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 2, "INV_COUNT should return the per-item count");
        TEST_ASSERT(s_session_bridge_inventory.count(1) == 0, "remove+clear should leave item 1 at zero");
        TEST_SUCCESS;
    }

#ifdef GAME_DEBUG
    TEST_IMPL(test_noir_inventory_out_of_range_reports_and_preserves_state)
    {
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(9);
        bc.emit(noir_op::INV_ADD);
        bc.emit(noir_op::INV_REMOVE);
        bc.emit(noir_op::INV_HAS);
        bc.emit(noir_op::INV_COUNT);
        // INV_COUNT answers in D0, so the id has to go back before INV_CLEAR
        // or the clear lands on whatever item that count names.
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(9);
        bc.emit(noir_op::INV_CLEAR);
        bc.emit(noir_op::RET);

        auto bridge = session_inventory_bridge(2);
        s_session_bridge_inventory.add_item(0);

        auto vm     = bc.build_vm(256, &bridge);
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "out-of-range ids should not kill the session");
        TEST_ASSERT(message_has("inventory item 9 is out of range"),
            "invalid item id should report through the VM error channel");
        TEST_ASSERT(s_session_bridge_inventory.count(0) == 1,
            "out-of-range inventory opcodes should not change owned items");
        TEST_ASSERT(s_session_bridge_inventory.count(1) == 0,
            "out-of-range inventory opcodes should not touch other items");
        TEST_SUCCESS;
    }
#endif

    // ── Combined patterns ───────────────────────────────────────────────────────

    TEST_IMPL(test_noir_inv_add_then_has)
    {
        // Typical game pattern: add item, then check if has it
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(42);

        bc.emit(noir_op::INV_ADD);

        // Now check: bridge says we have it
        bc.emit(noir_op::INV_HAS);

        bc.emit(noir_op::RET);

        auto vm                   = bc.build_vm();
        g_mock_log.inv_has_result = true;
        auto status               = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(g_mock_log.inv_add_count == 1, "add was called");
        TEST_ASSERT(g_mock_log.inv_added[0] == 42, "added item 42");
        TEST_ASSERT(g_mock_log.inv_has_query == 42, "has queried item 42");
        TEST_ASSERT((vm.sr() & noir_sr::Z) == 0, "Z=0 (has item)");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_inv_add_remove_sequence)
    {
        // Add item, then remove it
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::INV_ADD);

        bc.emit(noir_op::INV_REMOVE);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(g_mock_log.inv_add_count == 1, "add called once");
        TEST_ASSERT(g_mock_log.inv_remove_count == 1, "remove called once");
        TEST_ASSERT(g_mock_log.inv_added[0] == 10, "added item 10");
        TEST_ASSERT(g_mock_log.inv_removed[0] == 10, "removed item 10");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_inv_add_count_sequence)
    {
        // Add several items, then count one specific item id
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);
        bc.emit(noir_op::INV_ADD);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(2);
        bc.emit(noir_op::INV_ADD);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(2);  // query count for item 2

        bc.emit(noir_op::INV_COUNT);

        bc.emit(noir_op::RET);

        auto vm                     = bc.build_vm();
        g_mock_log.inv_count_result = 2;
        auto status                 = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(g_mock_log.inv_add_count == 2, "add called twice");
        TEST_ASSERT(g_mock_log.inv_count_query == 2, "count queried item 2");
        TEST_ASSERT(vm.exit_code() == 2, "D0 = count (2)");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_inv_clear_then_count)
    {
        // Clear one item, then count that same item → 0
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(5);

        bc.emit(noir_op::INV_CLEAR);

        bc.emit(noir_op::INV_COUNT);

        bc.emit(noir_op::RET);

        auto vm                     = bc.build_vm();
        g_mock_log.inv_count_result = 0;
        auto status                 = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(g_mock_log.inv_clear_called, "clear was called");
        TEST_ASSERT(g_mock_log.inv_cleared_item_id == 5, "clear targeted item 5");
        TEST_ASSERT(g_mock_log.inv_count_query == 5, "count queried item 5");
        TEST_ASSERT(vm.exit_code() == 0, "D0 = 0 after clear + count");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_inv_has_conditional_add)
    {
        // Game pattern: if not has item → add it
        // INV_HAS + BNE (skip add if already have) + INV_ADD
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(50);

        bc.emit(noir_op::INV_HAS);

        // BNE: skip add if has (Z=0). Since Z=1, branch NOT taken → add executes
        bc.emit(noir_op::BNE);
        bc.emit_s16(1);  // skip INV_ADD (1 byte)

        bc.emit(noir_op::INV_ADD);

        bc.emit(noir_op::RET);

        auto vm                   = bc.build_vm();
        g_mock_log.inv_has_result = false;  // don't have it → Z=1
        auto status               = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(g_mock_log.inv_add_count == 1, "item added (didn't have it)");
        TEST_ASSERT(g_mock_log.inv_added[0] == 50, "added correct item");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_inv_has_conditional_skip)
    {
        // Same pattern but already has item → skip add
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(50);

        bc.emit(noir_op::INV_HAS);

        // BNE: skip add if has (Z=0). Since Z=0, branch taken → skip add
        bc.emit(noir_op::BNE);
        bc.emit_s16(1);  // skip INV_ADD

        bc.emit(noir_op::INV_ADD);

        bc.emit(noir_op::RET);

        auto vm                   = bc.build_vm();
        g_mock_log.inv_has_result = true;  // already have it → Z=0
        auto status               = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(g_mock_log.inv_add_count == 0, "item NOT added (already had it)");
        TEST_SUCCESS;
    }

    // ── Invalid opcodes ─────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_inv_invalid_opcode_0x86)
    {
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        // 0x80-0x85 are all defined inventory opcodes, so the reserved part of
        // the range starts here. 0x85 is INV_SELECTED and belongs in a test of
        // its own, below.
        bc.emit(0x86);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(failed_with(status, noir_error::invalid_opcode), "0x86 reserved");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_inv_selected_reads_the_bridge)
    {
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::INV_SELECTED);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == MOCK_SELECTED_ITEM, "INV_SELECTED returns the bridge answer");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_inv_unwired_bridge_slot_fails_the_script)
    {
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::INV_SELECTED);
        bc.emit(noir_op::RET);

        // A host that leaves a slot empty used to send the VM through a null
        // pointer to address zero. It has to kill the script instead.
        noir_bridge bridge     = g_mock_bridge;
        bridge.inv_selected_item = {};

        auto vm     = bc.build_vm(256, &bridge);
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(failed_with(status, noir_error::unbound_bridge), "unwired slot should fail");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_inv_invalid_opcode_0x8f)
    {
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(0x8F);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(failed_with(status, noir_error::invalid_opcode), "0x8F reserved");
        TEST_SUCCESS;
    }

    // ═════════════════════════════════════════════════════════════════════════════
    // Test suite registration
    // ═════════════════════════════════════════════════════════════════════════════

    TEST_SUITE_BEGIN(noir_vm_inventory)
    // INV_ADD
    TEST(test_noir_inv_add_basic)
    TEST(test_noir_inv_add_preserves_d0)
    TEST(test_noir_inv_add_multiple)
    TEST(test_noir_inv_add_preserves_sr)
    TEST(test_noir_inv_add_preserves_other_regs)
    TEST(test_noir_inv_add_zero_item_id)
    TEST(test_noir_inv_add_max_item_id)
    // INV_REMOVE
    TEST(test_noir_inv_remove_basic)
    TEST(test_noir_inv_remove_preserves_d0)
    TEST(test_noir_inv_remove_preserves_sr)
    TEST(test_noir_inv_remove_multiple)
    // INV_HAS
    TEST(test_noir_inv_has_found)
    TEST(test_noir_inv_has_not_found)
    TEST(test_noir_inv_has_preserves_d0)
    TEST(test_noir_inv_has_clears_other_flags)
    TEST(test_noir_inv_has_with_beq)
    TEST(test_noir_inv_has_with_bne)
    TEST(test_noir_inv_has_preserves_other_regs)
    // INV_COUNT
    TEST(test_noir_inv_count_basic)
    TEST(test_noir_inv_count_zero)
    TEST(test_noir_inv_count_large)
    TEST(test_noir_inv_count_overwrites_d0)
    TEST(test_noir_inv_count_preserves_other_regs)
    TEST(test_noir_inv_count_preserves_sr)
    TEST(test_noir_inv_count_with_cmp)
    // INV_CLEAR
    TEST(test_noir_inv_clear_basic)
    TEST(test_noir_inv_clear_preserves_regs)
    TEST(test_noir_inv_clear_preserves_sr)
    TEST(test_noir_inventory_ops_use_session_store_bridge)
#ifdef GAME_DEBUG
    TEST(test_noir_inventory_out_of_range_reports_and_preserves_state)
#endif
    // Combined patterns
    TEST(test_noir_inv_add_then_has)
    TEST(test_noir_inv_add_remove_sequence)
    TEST(test_noir_inv_add_count_sequence)
    TEST(test_noir_inv_clear_then_count)
    TEST(test_noir_inv_has_conditional_add)
    TEST(test_noir_inv_has_conditional_skip)
    // Invalid opcodes
    TEST(test_noir_inv_invalid_opcode_0x86)
    TEST(test_noir_inv_selected_reads_the_bridge)
    TEST(test_noir_inv_unwired_bridge_slot_fails_the_script)
    TEST(test_noir_inv_invalid_opcode_0x8f)
    TEST_SUITE_END

}  // namespace NEONengine

#endif  // ACE_TEST_RUNNER
#endif  // __NOIR_VM_INVENTORY_TESTS_H__INCLUDED__
