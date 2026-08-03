#ifndef __NOIR_VM_DEBUG_TESTS_H__INCLUDED__
#define __NOIR_VM_DEBUG_TESTS_H__INCLUDED__

#ifdef ACE_TEST_RUNNER

#include "types.h"

#include "tests/noir_vm_test_utils.h"

namespace NEONengine
{
    // ═════════════════════════════════════════════════════════════════════════════
    // Debug opcodes (0xE0–0xEF)
    //   DBG_LOG, DBG_LOGS, DBG_REG, DBG_STK, DBG_STAT, DBG_FLAG, DBG_VAL
    //
    // GAME_DEBUG builds: debug opcodes call bridge or logWrite for diagnostics.
    // Release builds:    debug opcodes are NOP (DBG_LOGS skips inline data).
    //
    // DBG_LOG   (0xE0): D0=string_id → bridge->debug_log(D0)
    // DBG_LOGS  (0xE1): u8 len + inline bytes → bridge->debug_print(str, len)
    // DBG_REG   (0xE2): Dump D0–D3, SR, SP, FP, PC to log
    // DBG_STK   (0xE3): Dump top 16 bytes of stack to log
    // DBG_STAT  (0xE4): Dump debug stats (max_stack and the script that set it)
    // DBG_FLAG  (0xE5): D0=index → log flag[D0], or D0=0xFFFF → log all flags
    // DBG_VAL   (0xE6): D0=index → log value[D0], or D0=0xFFFF → log all values
    // 0xE7–0xEF: Unknown — treated as NOP
    //
    // SR:        All debug opcodes leave SR unchanged.
    // Registers: All debug opcodes preserve D0–D3.
    // Status:    All return noir_result::ok.
    // ═════════════════════════════════════════════════════════════════════════════

    // ── DBG_LOG ─────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_dbg_log_returns_ok)
    {
        // DBG_LOG should complete without error
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(42);

        bc.emit(noir_op::DBG_LOG);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "DBG_LOG completes");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_dbg_log_preserves_registers)
    {
        // DBG_LOG must not modify any registers
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

        bc.emit(noir_op::DBG_LOG);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "completed");
        TEST_ASSERT(vm.reg_d(0) == 100, "D0 preserved");
        TEST_ASSERT(vm.reg_d(1) == 200, "D1 preserved");
        TEST_ASSERT(vm.reg_d(2) == 300, "D2 preserved");
        TEST_ASSERT(vm.reg_d(3) == 400, "D3 preserved");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_dbg_log_preserves_sr)
    {
        // DBG_LOG must not modify SR
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        // Set Z flag via CMP D0,D0 (equal → Z=1)
        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D0);

        bc.emit(noir_op::DBG_LOG);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "completed");
        TEST_ASSERT((vm.sr() & noir_sr::Z) != 0, "Z flag preserved");
        TEST_SUCCESS;
    }

#ifdef GAME_DEBUG
    TEST_IMPL(test_noir_dbg_log_calls_bridge)
    {
        // DBG_LOG must call bridge->debug_log with D0 as string_id
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(77);

        bc.emit(noir_op::DBG_LOG);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "completed");
        TEST_ASSERT(g_mock_log.debug_log_called, "debug_log bridge called");
        TEST_ASSERT(g_mock_log.debug_log_string_id == 77, "string_id == 77");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_dbg_log_zero_string_id)
    {
        // DBG_LOG with D0=0
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::CLR);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D0);

        bc.emit(noir_op::DBG_LOG);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "completed");
        TEST_ASSERT(g_mock_log.debug_log_called, "debug_log bridge called");
        TEST_ASSERT(g_mock_log.debug_log_string_id == 0, "string_id == 0");
        TEST_SUCCESS;
    }
#endif  // GAME_DEBUG

    // ── DBG_LOGS ────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_dbg_logs_skips_inline_data)
    {
        // DBG_LOGS must advance PC past u8 len + len bytes of inline data
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::DBG_LOGS);
        bc.emit(5);  // len = 5
        bc.emit('H');
        bc.emit('e');
        bc.emit('l');
        bc.emit('l');
        bc.emit('o');

        // If PC isn't advanced correctly, this MOV will be missed or misread
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(999);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "completed");
        TEST_ASSERT(vm.reg_d(0) == 999, "D0 == 999 (PC advanced past inline data)");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_dbg_logs_zero_length)
    {
        // DBG_LOGS with len=0 — no inline data to skip
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::DBG_LOGS);
        bc.emit(0);  // len = 0

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(555);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "completed");
        TEST_ASSERT(vm.reg_d(0) == 555, "D0 == 555 (PC correct with zero-len)");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_dbg_logs_preserves_registers)
    {
        // DBG_LOGS must not modify D0–D3
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(11);
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(22);

        bc.emit(noir_op::DBG_LOGS);
        bc.emit(3);  // len = 3
        bc.emit('A');
        bc.emit('B');
        bc.emit('C');

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "completed");
        TEST_ASSERT(vm.reg_d(0) == 11, "D0 preserved");
        TEST_ASSERT(vm.reg_d(1) == 22, "D1 preserved");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_dbg_logs_preserves_sr)
    {
        // DBG_LOGS must not modify SR
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        // Set N flag by loading a negative value
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_s16(-1);
        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0);

        bc.emit(noir_op::DBG_LOGS);
        bc.emit(2);
        bc.emit('O');
        bc.emit('K');

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "completed");
        TEST_ASSERT((vm.sr() & noir_sr::N) != 0, "N flag preserved");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_dbg_logs_multiple_in_sequence)
    {
        // Multiple DBG_LOGS in a row — PC must advance correctly over each
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::DBG_LOGS);
        bc.emit(3);
        bc.emit('F');
        bc.emit('O');
        bc.emit('O');

        bc.emit(noir_op::DBG_LOGS);
        bc.emit(3);
        bc.emit('B');
        bc.emit('A');
        bc.emit('R');

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1234);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "completed");
        TEST_ASSERT(vm.reg_d(0) == 1234, "D0 == 1234 (PC advanced past both)");
        TEST_SUCCESS;
    }

#ifdef GAME_DEBUG
    TEST_IMPL(test_noir_dbg_logs_calls_bridge)
    {
        // DBG_LOGS must call bridge->debug_print with the inline string
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::DBG_LOGS);
        bc.emit(5);
        bc.emit('H');
        bc.emit('e');
        bc.emit('l');
        bc.emit('l');
        bc.emit('o');

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "completed");
        TEST_ASSERT(g_mock_log.debug_print_called, "debug_print bridge called");
        TEST_ASSERT(g_mock_log.debug_print_len == 5, "len == 5");
        TEST_ASSERT(g_mock_log.debug_print_buf[0] == 'H', "buf[0] == 'H'");
        TEST_ASSERT(g_mock_log.debug_print_buf[4] == 'o', "buf[4] == 'o'");
        TEST_SUCCESS;
    }
#endif  // GAME_DEBUG

    // ── DBG_REG ─────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_dbg_reg_returns_ok)
    {
        // DBG_REG should complete without error
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::DBG_REG);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "DBG_REG completes");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_dbg_reg_preserves_registers)
    {
        // DBG_REG must not modify any registers
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0x1234);
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(0x5678);
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D2, noir_mode::IMM16);
        bc.emit_u16(0x9ABC);
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D3, noir_mode::IMM16);
        bc.emit_u16(0xDEF0);

        bc.emit(noir_op::DBG_REG);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "completed");
        TEST_ASSERT(vm.reg_d(0) == 0x1234, "D0 == 0x1234");
        TEST_ASSERT(vm.reg_d(1) == 0x5678, "D1 == 0x5678");
        TEST_ASSERT(vm.reg_d(2) == 0x9ABC, "D2 == 0x9ABC");
        TEST_ASSERT(vm.reg_d(3) == 0xDEF0, "D3 == 0xDEF0");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_dbg_reg_preserves_sr)
    {
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        // Set flags via CMP
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(5);
        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(5);

        bc.emit(noir_op::DBG_REG);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "completed");
        TEST_ASSERT((vm.sr() & noir_sr::Z) != 0, "Z preserved after DBG_REG");
        TEST_ASSERT((vm.sr() & noir_sr::N) == 0, "N clear preserved after DBG_REG");
        TEST_SUCCESS;
    }

    // ── DBG_STK ─────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_dbg_stk_returns_ok)
    {
        // DBG_STK should complete without error
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::DBG_STK);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "DBG_STK completes");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_dbg_stk_preserves_registers)
    {
        // DBG_STK must not modify D0–D3
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(111);

        bc.emit(noir_op::PUSH);
        bc.emit_mode(0, noir_mode::IMM16);
        bc.emit_u16(0xABCD);

        bc.emit(noir_op::DBG_STK);

        bc.emit(noir_op::POP);
        bc.emit_mode(noir_mode::REG_D1, 0);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "completed");
        TEST_ASSERT(vm.reg_d(0) == 111, "D0 preserved");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_dbg_stk_with_data_on_stack)
    {
        // DBG_STK with data on the stack should not corrupt it
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::PUSH);
        bc.emit_mode(0, noir_mode::IMM16);
        bc.emit_u16(0x1234);

        bc.emit(noir_op::PUSH);
        bc.emit_mode(0, noir_mode::IMM16);
        bc.emit_u16(0x5678);

        bc.emit(noir_op::DBG_STK);

        // Stack should still have both values in order
        bc.emit(noir_op::POP);
        bc.emit_mode(noir_mode::REG_D0, 0);
        bc.emit(noir_op::POP);
        bc.emit_mode(noir_mode::REG_D1, 0);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "completed");
        TEST_ASSERT(vm.reg_d(0) == 0x5678, "top of stack == 0x5678");
        TEST_ASSERT(vm.reg_d(1) == 0x1234, "second on stack == 0x1234");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_dbg_stk_empty_stack)
    {
        // DBG_STK on an empty stack (no user data) should not crash
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::DBG_STK);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "completed on empty stack");
        TEST_SUCCESS;
    }

    // ── DBG_STAT ────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_dbg_stat_returns_ok)
    {
        // DBG_STAT should complete without error
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::DBG_STAT);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "DBG_STAT completes");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_dbg_stat_preserves_registers)
    {
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0x7FFF);
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(42);

        bc.emit(noir_op::DBG_STAT);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "completed");
        TEST_ASSERT(vm.reg_d(0) == 0x7FFF, "D0 preserved");
        TEST_ASSERT(vm.reg_d(1) == 42, "D1 preserved");
        TEST_SUCCESS;
    }

    // ── DBG_FLAG ────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_dbg_flag_single_returns_ok)
    {
        // DBG_FLAG with D0 = valid flag index
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(5);

        bc.emit(noir_op::DBG_FLAG);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "DBG_FLAG single completes");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_dbg_flag_all_returns_ok)
    {
        // DBG_FLAG with D0 = 0xFFFF → dump all flags
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0xFFFF);

        bc.emit(noir_op::DBG_FLAG);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "DBG_FLAG all completes");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_dbg_flag_preserves_d0)
    {
        // DBG_FLAG must preserve D0 (the flag index)
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::DBG_FLAG);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "completed");
        TEST_ASSERT(vm.reg_d(0) == 10, "D0 preserved (flag index)");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_dbg_flag_preserves_sr)
    {
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        // Set Z=1 via CMP D0,D0 (equal → Z=1)
        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D0);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(3);

        bc.emit(noir_op::DBG_FLAG);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "completed");
        TEST_ASSERT((vm.sr() & noir_sr::Z) != 0, "Z from CMP preserved");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_dbg_flag_invalid_index)
    {
        // DBG_FLAG with out-of-bounds index should NOT halt — just silently skip
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        // flags array is 16 bytes = 128 flags, so index 200 is OOB
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(200);

        bc.emit(noir_op::DBG_FLAG);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "DBG_FLAG OOB completes (no error)");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_dbg_flag_with_set_flag)
    {
        // Set a flag, then DBG_FLAG should report it — verify no crash
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::SETF);
        bc.emit_u16(7);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(7);

        bc.emit(noir_op::DBG_FLAG);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "completed");
        TEST_SUCCESS;
    }

    // ── DBG_VAL ─────────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_dbg_val_single_returns_ok)
    {
        // DBG_VAL with D0 = valid value index
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0);

        bc.emit(noir_op::DBG_VAL);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "DBG_VAL single completes");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_dbg_val_all_returns_ok)
    {
        // DBG_VAL with D0 = 0xFFFF → dump all values
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0xFFFF);

        bc.emit(noir_op::DBG_VAL);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "DBG_VAL all completes");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_dbg_val_preserves_d0)
    {
        // DBG_VAL must preserve D0
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(5);

        bc.emit(noir_op::DBG_VAL);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "completed");
        TEST_ASSERT(vm.reg_d(0) == 5, "D0 preserved (value index)");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_dbg_val_invalid_index)
    {
        // DBG_VAL with out-of-bounds index should NOT halt — just silently skip
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        // values array has 32 entries, so index 100 is OOB
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(100);

        bc.emit(noir_op::DBG_VAL);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "DBG_VAL OOB completes (no error)");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_dbg_val_with_nonzero_value)
    {
        // Set a value, then DBG_VAL — verify no crash
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::INCV);
        bc.emit_u16(3);
        bc.emit(noir_op::INCV);
        bc.emit_u16(3);
        bc.emit(noir_op::INCV);
        bc.emit_u16(3);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(3);

        bc.emit(noir_op::DBG_VAL);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "completed");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_dbg_val_preserves_sr)
    {
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        // Set carry flag
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0x7FFF);
        bc.emit(noir_op::ADD);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0x7FFF);

        bc.emit(noir_op::DBG_VAL);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "completed");
        // After 0x7FFF + 0x7FFF = 0xFFFE → N=1, V=1 (signed overflow)
        TEST_ASSERT((vm.sr() & noir_sr::N) != 0, "N flag preserved");
        TEST_ASSERT((vm.sr() & noir_sr::V) != 0, "V flag preserved");
        TEST_SUCCESS;
    }

    // ── Unknown debug opcodes ───────────────────────────────────────────────────

    TEST_IMPL(test_noir_dbg_unknown_opcode_is_nop)
    {
        // Opcodes 0xE7–0xEF should be treated as NOP, not errors
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(42);

        bc.emit(0xE7);  // Unknown debug opcode
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "unknown 0xE7 treated as NOP");
        TEST_ASSERT(vm.reg_d(0) == 42, "D0 preserved");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_dbg_unknown_0xEF_is_nop)
    {
        // Last debug opcode slot
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(0xEF);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "unknown 0xEF treated as NOP");
        TEST_SUCCESS;
    }

    // ── Sequencing / integration ────────────────────────────────────────────────

    TEST_IMPL(test_noir_dbg_all_opcodes_in_sequence)
    {
        // Run all debug opcodes in sequence — verify no crash and correct completion
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);

        bc.emit(noir_op::DBG_LOG);

        bc.emit(noir_op::DBG_LOGS);
        bc.emit(4);
        bc.emit('T');
        bc.emit('E');
        bc.emit('S');
        bc.emit('T');

        bc.emit(noir_op::DBG_REG);
        bc.emit(noir_op::DBG_STK);
        bc.emit(noir_op::DBG_STAT);
        bc.emit(noir_op::DBG_FLAG);
        bc.emit(noir_op::DBG_VAL);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(7777);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "all debug opcodes complete");
        TEST_ASSERT(vm.reg_d(0) == 7777, "D0 == 7777 after all debug opcodes");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_dbg_interleaved_with_computation)
    {
        // Debug opcodes interleaved with arithmetic — they must not interfere
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::DBG_REG);

        bc.emit(noir_op::ADD);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(20);

        bc.emit(noir_op::DBG_LOGS);
        bc.emit(5);
        bc.emit('a');
        bc.emit('f');
        bc.emit('t');
        bc.emit('e');
        bc.emit('r');

        bc.emit(noir_op::MUL);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(2);

        bc.emit(noir_op::DBG_STK);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "completed");
        // (10 + 20) * 2 = 60
        TEST_ASSERT(vm.reg_d(0) == 60, "D0 == 60 ((10+20)*2)");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_dbg_does_not_affect_branching)
    {
        // Debug opcode between CMP and branch must not disturb flags
        bytecode_builder bc;
        u16 fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(5);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(5);

        // Z=1 now. DBG_REG must NOT disturb it.
        bc.emit(noir_op::DBG_REG);

        // BEQ should still be taken since Z=1
        u16 branch_pos = bc.mark();
        bc.emit(noir_op::BEQ);
        bc.emit_s16(0);  // placeholder

        // Not taken path — set D0 = 999
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(999);
        bc.emit(noir_op::RET);

        // Taken path — set D0 = 42
        u16 target = bc.mark();
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(42);
        bc.emit(noir_op::RET);

        // Patch branch offset: target - (branch_pos + 3) since BEQ is 1 byte opcode + 2 byte offset
        i16 offset          = to<i16>(target - (branch_pos + 3));
        bc.code[branch_pos + 1] = to<u8>(to<u16>(offset) >> 8);
        bc.code[branch_pos + 2] = to<u8>(to<u16>(offset) & 0xFF);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_result::completed, "completed");
        TEST_ASSERT(vm.reg_d(0) == 42, "BEQ taken (Z=1 preserved past DBG_REG)");
        TEST_SUCCESS;
    }

    // ─── Test suite registration ────────────────────────────────────────────────

    TEST_SUITE_BEGIN(noir_vm_debug)
    TEST(test_noir_dbg_log_returns_ok)
    TEST(test_noir_dbg_log_preserves_registers)
    TEST(test_noir_dbg_log_preserves_sr)
#ifdef GAME_DEBUG
    TEST(test_noir_dbg_log_calls_bridge)
    TEST(test_noir_dbg_log_zero_string_id)
#endif
    TEST(test_noir_dbg_logs_skips_inline_data)
    TEST(test_noir_dbg_logs_zero_length)
    TEST(test_noir_dbg_logs_preserves_registers)
    TEST(test_noir_dbg_logs_preserves_sr)
    TEST(test_noir_dbg_logs_multiple_in_sequence)
#ifdef GAME_DEBUG
    TEST(test_noir_dbg_logs_calls_bridge)
#endif
    TEST(test_noir_dbg_reg_returns_ok)
    TEST(test_noir_dbg_reg_preserves_registers)
    TEST(test_noir_dbg_reg_preserves_sr)
    TEST(test_noir_dbg_stk_returns_ok)
    TEST(test_noir_dbg_stk_preserves_registers)
    TEST(test_noir_dbg_stk_with_data_on_stack)
    TEST(test_noir_dbg_stk_empty_stack)
    TEST(test_noir_dbg_stat_returns_ok)
    TEST(test_noir_dbg_stat_preserves_registers)
    TEST(test_noir_dbg_flag_single_returns_ok)
    TEST(test_noir_dbg_flag_all_returns_ok)
    TEST(test_noir_dbg_flag_preserves_d0)
    TEST(test_noir_dbg_flag_preserves_sr)
    TEST(test_noir_dbg_flag_invalid_index)
    TEST(test_noir_dbg_flag_with_set_flag)
    TEST(test_noir_dbg_val_single_returns_ok)
    TEST(test_noir_dbg_val_all_returns_ok)
    TEST(test_noir_dbg_val_preserves_d0)
    TEST(test_noir_dbg_val_invalid_index)
    TEST(test_noir_dbg_val_with_nonzero_value)
    TEST(test_noir_dbg_val_preserves_sr)
    TEST(test_noir_dbg_unknown_opcode_is_nop)
    TEST(test_noir_dbg_unknown_0xEF_is_nop)
    TEST(test_noir_dbg_all_opcodes_in_sequence)
    TEST(test_noir_dbg_interleaved_with_computation)
    TEST(test_noir_dbg_does_not_affect_branching)
    TEST_SUITE_END

}  // namespace NEONengine

#endif  // ACE_TEST_RUNNER
#endif  // __NOIR_VM_DEBUG_TESTS_H__INCLUDED__
