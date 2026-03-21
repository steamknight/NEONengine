#ifndef __NOIR_VM_SCENE_TESTS_H__INCLUDED__
#define __NOIR_VM_SCENE_TESTS_H__INCLUDED__

#ifdef ACE_TEST_RUNNER

#include "tests/noir_vm_test_utils.h"

namespace NEONengine
{
    // ═════════════════════════════════════════════════════════════════════════════
    // Game Calls — Scene opcodes (0xA0–0xAF)
    //   GOTO_SCENE, GET_SCENE, HOTSPOT_ON, HOTSPOT_OFF, SET_POINTER
    //
    // All scene opcodes are 1-byte (opcode only). Arguments passed in D0.
    // Bridge function pointers are called.
    //
    // GOTO_SCENE  (0xA0): D0=scene_id → bridge->goto_scene(D0)  — blocking
    // GET_SCENE   (0xA1): bridge->get_scene() → D0              — non-blocking
    // HOTSPOT_ON  (0xA2): D0=id → bridge->hotspot_on(D0)        — non-blocking
    // HOTSPOT_OFF (0xA3): D0=id → bridge->hotspot_off(D0)       — non-blocking
    // SET_POINTER (0xA4): D0=type → bridge->set_pointer(D0)     — non-blocking
    // 0xA5–0xAF: Reserved → err_invalid_opcode
    //
    // SR: All scene opcodes leave SR unchanged.
    // Registers: D1–D3 preserved. D0 preserved except for GET_SCENE which writes.
    // ═════════════════════════════════════════════════════════════════════════════

    // ── GOTO_SCENE ──────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_goto_scene_basic)
    {
        // GOTO_SCENE with D0=5 → bridge->goto_scene(5)
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(5);

        bc.emit(noir_op::GOTO_SCENE);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);
        vm.update();

        TEST_ASSERT(g_mock_log.goto_scene_called, "goto_scene bridge called");
        TEST_ASSERT(g_mock_log.goto_scene_id == 5, "goto_scene received scene_id 5");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_goto_scene_blocks_vm)
    {
        // GOTO_SCENE is a blocking opcode
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(3);

        bc.emit(noir_op::GOTO_SCENE);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);
        auto status = vm.update();

        TEST_ASSERT(vm.state() == noir_state::blocked, "VM should be blocked");
        TEST_ASSERT(status == noir_status::yielded, "update returns yielded while blocked");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_goto_scene_stays_blocked)
    {
        // VM stays blocked across multiple update() calls until unblocked
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);

        bc.emit(noir_op::GOTO_SCENE);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);
        vm.update();

        auto s1 = vm.update();
        TEST_ASSERT(s1 == noir_status::yielded, "still yielded after 2nd update");
        TEST_ASSERT(vm.state() == noir_state::blocked, "still blocked after 2nd update");

        auto s2 = vm.update();
        TEST_ASSERT(s2 == noir_status::yielded, "still yielded after 3rd update");
        TEST_ASSERT(vm.state() == noir_state::blocked, "still blocked after 3rd update");

        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_goto_scene_resumes_after_unblock)
    {
        // After decrement_blocking(), VM resumes and completes
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::GOTO_SCENE);

        // Code after unblock
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(888);

        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);
        vm.update();

        TEST_ASSERT(vm.state() == noir_state::blocked, "blocked after GOTO_SCENE");

        vm.decrement_blocking();
        auto status = vm.update();

        TEST_ASSERT(status == noir_status::completed, "completes after unblock");
        TEST_ASSERT(vm.exit_code() == 888, "code after GOTO_SCENE executed");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_goto_scene_preserves_d0)
    {
        // D0 should be preserved across the blocking period
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(42);

        bc.emit(noir_op::GOTO_SCENE);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);
        vm.update();

        vm.decrement_blocking();
        vm.update();

        TEST_ASSERT(vm.exit_code() == 42, "D0 preserved across GOTO_SCENE block");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_goto_scene_preserves_all_regs)
    {
        // All data registers preserved across GOTO_SCENE
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(11);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(22);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D2, noir_mode::IMM16);
        bc.emit_u16(33);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D3, noir_mode::IMM16);
        bc.emit_u16(44);

        bc.emit(noir_op::GOTO_SCENE);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);
        vm.update();

        vm.decrement_blocking();
        vm.update();

        TEST_ASSERT(vm.reg_d(0) == 11, "D0 preserved");
        TEST_ASSERT(vm.reg_d(1) == 22, "D1 preserved");
        TEST_ASSERT(vm.reg_d(2) == 33, "D2 preserved");
        TEST_ASSERT(vm.reg_d(3) == 44, "D3 preserved");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_goto_scene_preserves_sr)
    {
        // SR preserved across GOTO_SCENE
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::CLR);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D0);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);  // 0 - 1 = -1, sets N

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(5);

        bc.emit(noir_op::GOTO_SCENE);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);
        vm.update();

        vm.decrement_blocking();
        vm.update();

        TEST_ASSERT((vm.sr() & noir_sr::N) != 0, "N flag preserved across block");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_goto_scene_zero_id)
    {
        // GOTO_SCENE with D0=0
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::CLR);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D0);

        bc.emit(noir_op::GOTO_SCENE);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);
        vm.update();

        TEST_ASSERT(g_mock_log.goto_scene_called, "goto_scene called");
        TEST_ASSERT(g_mock_log.goto_scene_id == 0, "goto_scene received id 0");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_goto_scene_max_id)
    {
        // GOTO_SCENE with D0=0xFFFF
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0xFFFF);

        bc.emit(noir_op::GOTO_SCENE);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);
        vm.update();

        TEST_ASSERT(g_mock_log.goto_scene_called, "goto_scene called");
        TEST_ASSERT(g_mock_log.goto_scene_id == 0xFFFF, "goto_scene received max id");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_goto_scene_inside_call)
    {
        // GOTO_SCENE inside a called function blocks, then returns correctly
        bytecode_builder bc;

        // fn0: main — calls fn1
        uint16_t fn0 = bc.begin_func();
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(50);

        bc.emit(noir_op::CALL);
        bc.emit_u16(1);  // call fn1

        bc.emit(noir_op::RET);

        // fn1: does GOTO_SCENE
        uint16_t fn1 = bc.begin_func();
        (void)fn1;
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(8);

        bc.emit(noir_op::GOTO_SCENE);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(77);

        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn0);
        vm.update();

        TEST_ASSERT(vm.state() == noir_state::blocked, "blocked inside fn1");
        TEST_ASSERT(g_mock_log.goto_scene_id == 8, "goto_scene with id 8");

        vm.decrement_blocking();
        auto status = vm.update();

        TEST_ASSERT(status == noir_status::completed, "completes after unblock");
        TEST_ASSERT(vm.exit_code() == 77, "D0=77 returned from fn1");
        TEST_SUCCESS;
    }

    // ── GET_SCENE ───────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_get_scene_basic)
    {
        // GET_SCENE → D0 = bridge->get_scene()
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::GET_SCENE);
        bc.emit(noir_op::RET);

        auto vm                     = bc.build_vm();
        g_mock_log.get_scene_result = 42;
        auto status                 = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 42, "D0 = bridge result 42");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_get_scene_overwrites_d0)
    {
        // GET_SCENE should overwrite whatever was in D0
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(999);

        bc.emit(noir_op::GET_SCENE);
        bc.emit(noir_op::RET);

        auto vm                     = bc.build_vm();
        g_mock_log.get_scene_result = 7;
        auto status                 = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 7, "D0 overwritten to 7");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_get_scene_preserves_other_regs)
    {
        // GET_SCENE should not modify D1, D2, D3
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(100);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D2, noir_mode::IMM16);
        bc.emit_u16(200);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D3, noir_mode::IMM16);
        bc.emit_u16(300);

        bc.emit(noir_op::GET_SCENE);
        bc.emit(noir_op::RET);

        auto vm                     = bc.build_vm();
        g_mock_log.get_scene_result = 5;
        auto status                 = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.reg_d(1) == 100, "D1 preserved");
        TEST_ASSERT(vm.reg_d(2) == 200, "D2 preserved");
        TEST_ASSERT(vm.reg_d(3) == 300, "D3 preserved");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_get_scene_preserves_sr)
    {
        // GET_SCENE should not modify SR
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::CLR);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D0);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);  // sets N

        bc.emit(noir_op::GET_SCENE);
        bc.emit(noir_op::RET);

        auto vm                     = bc.build_vm();
        g_mock_log.get_scene_result = 3;
        auto status                 = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT((vm.sr() & noir_sr::N) != 0, "N flag preserved");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_get_scene_does_not_block)
    {
        // GET_SCENE is non-blocking
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::GET_SCENE);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(777);

        bc.emit(noir_op::RET);

        auto vm                     = bc.build_vm();
        g_mock_log.get_scene_result = 1;
        auto status                 = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete without blocking");
        TEST_ASSERT(vm.reg_d(1) == 777, "execution continued past GET_SCENE");
        TEST_ASSERT(vm.state() != noir_state::blocked, "not blocked");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_get_scene_zero)
    {
        // GET_SCENE returns 0
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::GET_SCENE);
        bc.emit(noir_op::RET);

        auto vm                     = bc.build_vm();
        g_mock_log.get_scene_result = 0;
        auto status                 = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0, "D0 = 0");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_get_scene_max)
    {
        // GET_SCENE returns 0xFFFF
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::GET_SCENE);
        bc.emit(noir_op::RET);

        auto vm                     = bc.build_vm();
        g_mock_log.get_scene_result = 0xFFFF;
        auto status                 = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 0xFFFF, "D0 = 0xFFFF");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_get_scene_use_in_branch)
    {
        // Common pattern: get current scene, compare, branch
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::GET_SCENE);

        // CMP D0, 5
        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(5);

        // BEQ → skip over MOV16 0
        bc.emit(noir_op::BEQ);
        bc.emit_s16(4);  // skip MOV16+mode+u16 (4 bytes)

        // Not taken: D0=0
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(0);

        // Taken or fall-through: D0=555
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(555);

        bc.emit(noir_op::RET);

        auto vm                     = bc.build_vm();
        g_mock_log.get_scene_result = 5;
        auto status                 = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 555, "branch taken when scene == 5");
        TEST_SUCCESS;
    }

    // ── HOTSPOT_ON ──────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_hotspot_on_basic)
    {
        // HOTSPOT_ON with D0=7 → bridge->hotspot_on(7)
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(7);

        bc.emit(noir_op::HOTSPOT_ON);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(g_mock_log.hotspot_on_id == 7, "hotspot_on received id 7");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_hotspot_on_preserves_d0)
    {
        // D0 unchanged after HOTSPOT_ON
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(99);

        bc.emit(noir_op::HOTSPOT_ON);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 99, "D0 unchanged");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_hotspot_on_preserves_other_regs)
    {
        // D1–D3 unchanged
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(10);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D2, noir_mode::IMM16);
        bc.emit_u16(20);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D3, noir_mode::IMM16);
        bc.emit_u16(30);

        bc.emit(noir_op::HOTSPOT_ON);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.reg_d(1) == 10, "D1 preserved");
        TEST_ASSERT(vm.reg_d(2) == 20, "D2 preserved");
        TEST_ASSERT(vm.reg_d(3) == 30, "D3 preserved");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_hotspot_on_preserves_sr)
    {
        // SR unchanged
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::CLR);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D0);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(2);

        bc.emit(noir_op::HOTSPOT_ON);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT((vm.sr() & noir_sr::N) != 0, "N flag preserved");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_hotspot_on_does_not_block)
    {
        // HOTSPOT_ON is non-blocking
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);

        bc.emit(noir_op::HOTSPOT_ON);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(444);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 444, "code continued past HOTSPOT_ON");
        TEST_SUCCESS;
    }

    // ── HOTSPOT_OFF ─────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_hotspot_off_basic)
    {
        // HOTSPOT_OFF with D0=4 → bridge->hotspot_off(4)
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(4);

        bc.emit(noir_op::HOTSPOT_OFF);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(g_mock_log.hotspot_off_id == 4, "hotspot_off received id 4");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_hotspot_off_preserves_d0)
    {
        // D0 unchanged after HOTSPOT_OFF
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(66);

        bc.emit(noir_op::HOTSPOT_OFF);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 66, "D0 unchanged");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_hotspot_off_preserves_other_regs)
    {
        // D1–D3 unchanged
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(111);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D2, noir_mode::IMM16);
        bc.emit_u16(222);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D3, noir_mode::IMM16);
        bc.emit_u16(333);

        bc.emit(noir_op::HOTSPOT_OFF);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.reg_d(1) == 111, "D1 preserved");
        TEST_ASSERT(vm.reg_d(2) == 222, "D2 preserved");
        TEST_ASSERT(vm.reg_d(3) == 333, "D3 preserved");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_hotspot_off_preserves_sr)
    {
        // SR unchanged
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::CLR);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D0);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(3);

        bc.emit(noir_op::HOTSPOT_OFF);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT((vm.sr() & noir_sr::N) != 0, "N flag preserved");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_hotspot_off_does_not_block)
    {
        // HOTSPOT_OFF is non-blocking
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);

        bc.emit(noir_op::HOTSPOT_OFF);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(333);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 333, "code continued past HOTSPOT_OFF");
        TEST_SUCCESS;
    }

    // ── SET_POINTER ─────────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_set_pointer_basic)
    {
        // SET_POINTER with D0=2 → bridge->set_pointer(2)
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(2);

        bc.emit(noir_op::SET_POINTER);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(g_mock_log.set_pointer_type == 2, "set_pointer received type 2");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_set_pointer_preserves_d0)
    {
        // D0 unchanged after SET_POINTER
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(88);

        bc.emit(noir_op::SET_POINTER);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 88, "D0 unchanged");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_set_pointer_preserves_other_regs)
    {
        // D1–D3 unchanged
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D1, noir_mode::IMM16);
        bc.emit_u16(50);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D2, noir_mode::IMM16);
        bc.emit_u16(60);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D3, noir_mode::IMM16);
        bc.emit_u16(70);

        bc.emit(noir_op::SET_POINTER);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.reg_d(1) == 50, "D1 preserved");
        TEST_ASSERT(vm.reg_d(2) == 60, "D2 preserved");
        TEST_ASSERT(vm.reg_d(3) == 70, "D3 preserved");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_set_pointer_preserves_sr)
    {
        // SR unchanged
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::CLR);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::REG_D0);

        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(4);

        bc.emit(noir_op::SET_POINTER);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT((vm.sr() & noir_sr::N) != 0, "N flag preserved");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_set_pointer_does_not_block)
    {
        // SET_POINTER is non-blocking
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);

        bc.emit(noir_op::SET_POINTER);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(222);

        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(vm.exit_code() == 222, "code continued past SET_POINTER");
        TEST_SUCCESS;
    }

    // ── Combined patterns ───────────────────────────────────────────────────────

    TEST_IMPL(test_noir_hotspot_on_then_off)
    {
        // Enable then disable a hotspot
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(9);

        bc.emit(noir_op::HOTSPOT_ON);
        bc.emit(noir_op::HOTSPOT_OFF);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::completed, "should complete");
        TEST_ASSERT(g_mock_log.hotspot_on_id == 9, "hotspot_on received id 9");
        TEST_ASSERT(g_mock_log.hotspot_off_id == 9, "hotspot_off received id 9");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_goto_scene_then_get_scene)
    {
        // After GOTO_SCENE completes, GET_SCENE should work
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(15);

        bc.emit(noir_op::GOTO_SCENE);

        // After unblock, get the current scene
        bc.emit(noir_op::GET_SCENE);
        bc.emit(noir_op::RET);

        auto vm                     = bc.build_vm();
        g_mock_log.get_scene_result = 15;
        vm.execute(fn);
        vm.update();

        TEST_ASSERT(vm.state() == noir_state::blocked, "blocked on GOTO_SCENE");

        vm.decrement_blocking();
        auto status = vm.update();

        TEST_ASSERT(status == noir_status::completed, "completes");
        TEST_ASSERT(vm.exit_code() == 15, "GET_SCENE returned new scene id");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_goto_scene_set_pointer_hotspot)
    {
        // Full scene setup pattern: GOTO_SCENE → SET_POINTER → HOTSPOT_ON
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        // GOTO_SCENE 3
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(3);
        bc.emit(noir_op::GOTO_SCENE);

        // After scene loads: SET_POINTER 1
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(1);
        bc.emit(noir_op::SET_POINTER);

        // HOTSPOT_ON 5
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(5);
        bc.emit(noir_op::HOTSPOT_ON);

        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(fn);
        vm.update();

        TEST_ASSERT(vm.state() == noir_state::blocked, "blocked on GOTO_SCENE");
        TEST_ASSERT(g_mock_log.goto_scene_id == 3, "goto_scene(3)");

        vm.decrement_blocking();
        auto status = vm.update();

        TEST_ASSERT(status == noir_status::completed, "completes");
        TEST_ASSERT(g_mock_log.set_pointer_type == 1, "set_pointer(1)");
        TEST_ASSERT(g_mock_log.hotspot_on_id == 5, "hotspot_on(5)");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_get_scene_conditional_goto)
    {
        // Check current scene, conditionally go to a different one
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(noir_op::GET_SCENE);

        // CMP D0, 5 — if not scene 5, jump to GOTO_SCENE
        bc.emit(noir_op::CMP);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(5);

        bc.emit(noir_op::BNE);
        bc.emit_s16(1);  // skip RET

        // If equal, just return
        bc.emit(noir_op::RET);

        // Not equal: goto scene 5
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(5);
        bc.emit(noir_op::GOTO_SCENE);

        bc.emit(noir_op::RET);

        auto vm                     = bc.build_vm();
        g_mock_log.get_scene_result = 3;
        vm.execute(fn);
        vm.update();

        // Current scene was 3, not 5, so should branch to GOTO_SCENE
        TEST_ASSERT(g_mock_log.goto_scene_called, "goto_scene called");
        TEST_ASSERT(g_mock_log.goto_scene_id == 5, "goto_scene(5)");
        TEST_ASSERT(vm.state() == noir_state::blocked, "blocked on GOTO_SCENE");

        vm.decrement_blocking();
        auto status = vm.update();
        TEST_ASSERT(status == noir_status::completed, "completes after unblock");
        TEST_SUCCESS;
    }

    // ── Invalid opcodes ─────────────────────────────────────────────────────────

    TEST_IMPL(test_noir_scene_invalid_0xA5)
    {
        // 0xA5 is not a valid scene opcode
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(0xA5);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::err_invalid_opcode, "0xA5 is invalid");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_scene_invalid_0xAF)
    {
        // 0xAF is not a valid scene opcode
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(0xAF);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::err_invalid_opcode, "0xAF is invalid");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_noir_scene_invalid_0xA8)
    {
        // mid-range invalid opcode
        bytecode_builder bc;
        uint16_t fn = bc.begin_func();

        bc.emit(0xA8);
        bc.emit(noir_op::RET);

        auto vm     = bc.build_vm();
        auto status = bc.run_to_completion(vm, fn);

        TEST_ASSERT(status == noir_status::err_invalid_opcode, "0xA8 is invalid");
        TEST_SUCCESS;
    }

    // ══════════════════════════════════════════════════════════════════════════════
    // Test suite registration
    // ══════════════════════════════════════════════════════════════════════════════

    constexpr char const* noir_vm_scene_name = "NoirVM Scene (0xA_)";

    Test noir_vm_scene_tests[] = {
        // GOTO_SCENE
        { "goto_scene basic", test_noir_goto_scene_basic },
        { "goto_scene blocks VM", test_noir_goto_scene_blocks_vm },
        { "goto_scene stays blocked", test_noir_goto_scene_stays_blocked },
        { "goto_scene resumes", test_noir_goto_scene_resumes_after_unblock },
        { "goto_scene preserves D0", test_noir_goto_scene_preserves_d0 },
        { "goto_scene preserves regs", test_noir_goto_scene_preserves_all_regs },
        { "goto_scene preserves SR", test_noir_goto_scene_preserves_sr },
        { "goto_scene zero id", test_noir_goto_scene_zero_id },
        { "goto_scene max id", test_noir_goto_scene_max_id },
        { "goto_scene inside CALL", test_noir_goto_scene_inside_call },

        // GET_SCENE
        { "get_scene basic", test_noir_get_scene_basic },
        { "get_scene overwrites D0", test_noir_get_scene_overwrites_d0 },
        { "get_scene preserves regs", test_noir_get_scene_preserves_other_regs },
        { "get_scene preserves SR", test_noir_get_scene_preserves_sr },
        { "get_scene does not block", test_noir_get_scene_does_not_block },
        { "get_scene zero", test_noir_get_scene_zero },
        { "get_scene max", test_noir_get_scene_max },
        { "get_scene use in branch", test_noir_get_scene_use_in_branch },

        // HOTSPOT_ON
        { "hotspot_on basic", test_noir_hotspot_on_basic },
        { "hotspot_on preserves D0", test_noir_hotspot_on_preserves_d0 },
        { "hotspot_on preserves regs", test_noir_hotspot_on_preserves_other_regs },
        { "hotspot_on preserves SR", test_noir_hotspot_on_preserves_sr },
        { "hotspot_on does not block", test_noir_hotspot_on_does_not_block },

        // HOTSPOT_OFF
        { "hotspot_off basic", test_noir_hotspot_off_basic },
        { "hotspot_off preserves D0", test_noir_hotspot_off_preserves_d0 },
        { "hotspot_off preserves regs", test_noir_hotspot_off_preserves_other_regs },
        { "hotspot_off preserves SR", test_noir_hotspot_off_preserves_sr },
        { "hotspot_off does not block", test_noir_hotspot_off_does_not_block },

        // SET_POINTER
        { "set_pointer basic", test_noir_set_pointer_basic },
        { "set_pointer preserves D0", test_noir_set_pointer_preserves_d0 },
        { "set_pointer preserves regs", test_noir_set_pointer_preserves_other_regs },
        { "set_pointer preserves SR", test_noir_set_pointer_preserves_sr },
        { "set_pointer does not block", test_noir_set_pointer_does_not_block },

        // Combined patterns
        { "hotspot on then off", test_noir_hotspot_on_then_off },
        { "goto then get scene", test_noir_goto_scene_then_get_scene },
        { "scene setup pattern", test_noir_goto_scene_set_pointer_hotspot },
        { "get scene conditional goto", test_noir_get_scene_conditional_goto },

        // Invalid opcodes
        { "invalid 0xA5", test_noir_scene_invalid_0xA5 },
        { "invalid 0xAF", test_noir_scene_invalid_0xAF },
        { "invalid 0xA8", test_noir_scene_invalid_0xA8 },
    };

}  // namespace NEONengine

#endif  // ACE_TEST_RUNNER
#endif  // __NOIR_VM_SCENE_TESTS_H__INCLUDED__
