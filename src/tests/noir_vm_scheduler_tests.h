#ifndef __NOIR_VM_SCHEDULER_TESTS_H__INCLUDED__
#define __NOIR_VM_SCHEDULER_TESTS_H__INCLUDED__

#ifdef ACE_TEST_RUNNER

#include "types.h"

#include "tests/noir_vm_test_utils.h"

namespace NEONengine
{
    // ═════════════════════════════════════════════════════════════════════════════
    // Scheduler tests
    //
    // Several sessions run at once, each with its own registers and its own
    // stack, sharing a per-frame instruction pool. These tests aim at the ways
    // that can go wrong rather than at the ways it is meant to work:
    //
    //   - one session's stack seen through another's base pointer
    //   - a base pointer left dangling by the growth past four stacks
    //   - the register window written back over the wrong session
    //   - a pass loop that never ends because nobody can make progress
    //   - a session removed from the run list taking its neighbours with it
    //   - a freed stack slot handed to a new session while its owner still runs
    // ═════════════════════════════════════════════════════════════════════════════

    // ── A script that parks itself in its own local ──────────────────────────────
    //
    //   LOCALS 2
    //   MOV16  local(-2), #value      ; write before the interleave
    //   WAIT   1                      ; let every other session run
    //   MOV16  VALUES[slot], local(-2); read after it
    //   RET
    //
    // If two sessions share a stack, or a base pointer goes stale, the value
    // that comes back is not the one that went in.

    inline u16 emit_local_roundtrip(bytecode_builder& bc, u16 value, u16 value_slot, u16 waits)
    {
        u16 fn = bc.begin_func();

        bc.emit(noir_op::LOCALS);
        bc.emit(2);

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::LOCAL, noir_mode::IMM16);
        bc.emit(to<u8>(0xFE));  // fp - 2
        bc.emit_u16(value);

        for (u16 i = 0; i < waits; ++i)
        {
            bc.emit(noir_op::WAIT);
            bc.emit_u16(1);
        }

        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::VALUE_IMM, noir_mode::LOCAL);
        bc.emit_u16(value_slot);
        bc.emit(to<u8>(0xFE));

        bc.emit(noir_op::RET);
        return fn;
    }

    // Runs the VM until nothing is left or the update cap is hit. Returns the
    // number of updates spent; the cap doubles as a hang detector.
    inline u16 run_until_empty(noir_vm& vm, u16 max_updates = 200, noir_error* caught = nullptr)
    {
        u16 updates = 0;
        while (updates < max_updates && vm.session_count() > 0)
        {
            auto status = vm.update();
            if (caught && !status && *caught == noir_error::none) *caught = status.error();
            ++updates;
        }
        return updates;
    }

    // ── Two sessions must not share a stack ─────────────────────────────────────

    TEST_IMPL(test_sched_two_sessions_keep_own_locals)
    {
        bytecode_builder bc;
        u16 fn0 = emit_local_roundtrip(bc, 0x1111, 0, 1);
        u16 fn1 = emit_local_roundtrip(bc, 0x2222, 1, 1);

        auto vm = bc.build_vm();
        TEST_ASSERT(vm.execute(fn0), "first session starts");
        TEST_ASSERT(vm.execute(fn1), "second session starts");
        TEST_ASSERT(vm.session_count() == 2, "both are in the run list");

        u16 updates = run_until_empty(vm);
        TEST_ASSERT(updates < 200, "both sessions ended");
        TEST_ASSERT(bc.values[0] == 0x1111, "first session read back its own local");
        TEST_ASSERT(bc.values[1] == 0x2222, "second session read back its own local");
        TEST_SUCCESS;
    }

    // ── A fifth session grows the stack allocation under the others ─────────────

    TEST_IMPL(test_sched_growth_past_four_keeps_stacks)
    {
        // Four sessions fill the slots the VM starts with. They are parked in
        // WAIT with live data in their locals when the fifth arrives and the
        // allocation moves.
        bytecode_builder bc;
        u16 fn[6];
        for (u16 i = 0; i < 6; ++i)
            fn[i] = emit_local_roundtrip(bc, to<u16>(0x100 + i), i, 3);

        auto vm = bc.build_vm();

        for (u16 i = 0; i < 4; ++i)
            TEST_ASSERT(vm.execute(fn[i]), "first four start");

        // Let them write their locals and park.
        vm.update();

        // These two force the allocation to grow twice over.
        TEST_ASSERT(vm.execute(fn[4]), "fifth session starts");
        TEST_ASSERT(vm.execute(fn[5]), "sixth session starts");
        TEST_ASSERT(vm.session_count() == 6, "six sessions live");

        u16 updates = run_until_empty(vm);
        TEST_ASSERT(updates < 200, "every session ended");

        for (u16 i = 0; i < 6; ++i)
            TEST_ASSERT(bc.values[i] == to<u16>(0x100 + i), "each local survived the growth");

        TEST_SUCCESS;
    }

    // ── The pool must not be handed out twice ───────────────────────────────────

    TEST_IMPL(test_sched_pool_caps_the_update)
    {
        // Two sessions that never end, and a pool one session can drink dry.
        // The one that goes first should take the lot and leave the other
        // nothing at all until the next update comes round to it.
        bytecode_builder bc;
        u16 fn[2];
        for (u16 i = 0; i < 2; ++i)
        {
            fn[i]         = bc.begin_func();
            u16 const top = bc.mark();
            bc.emit(noir_op::INCV);
            bc.emit_u16(i);
            u16 const jmp = bc.mark();
            bc.emit(noir_op::JMP);
            bc.emit_s16(to<i16>(to<i32>(top) - to<i32>(jmp) - 3));
        }

        auto vm = bc.build_vm();
        vm.set_frame_budget(5);
        vm.execute(fn[0]);
        vm.execute(fn[1]);

        vm.update();
        TEST_ASSERT(bc.values[0] > 0, "the first session ran");
        TEST_ASSERT(bc.values[1] == 0, "the pool was gone before the second one");

        vm.update();
        TEST_ASSERT(bc.values[1] > 0, "the next update refilled the pool for the other");
        TEST_ASSERT(vm.session_count() == 2, "both are still looping");
        TEST_SUCCESS;
    }

    // ── A pool too small for anyone must not spin ───────────────────────────────

    TEST_IMPL(test_sched_zero_pool_does_not_hang)
    {
        bytecode_builder bc;
        u16 fn = bc.begin_func();
        bc.emit(noir_op::NOP);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.set_frame_budget(0);
        vm.execute(fn);

        auto status = vm.update();
        TEST_ASSERT(status == noir_result::yielded, "nothing ran, session still live");
        TEST_ASSERT(vm.session_count() == 1, "session untouched");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_sched_zero_budget_does_not_hang)
    {
        bytecode_builder bc;
        u16 fn = bc.begin_func();
        bc.emit(noir_op::NOP);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.set_session_budget(0);
        vm.execute(fn);

        auto status = vm.update();
        TEST_ASSERT(status == noir_result::yielded, "nothing ran, session still live");
        TEST_ASSERT(vm.session_count() == 1, "session untouched");
        TEST_SUCCESS;
    }

    // ── Nobody starves ──────────────────────────────────────────────────────────

    TEST_IMPL(test_sched_rotation_feeds_every_session)
    {
        // Five sessions that never end, and a pool that covers barely one of
        // them per update. A scheduler that always began at the front would
        // run the first session for ever and never reach the fifth.
        bytecode_builder bc;
        u16 fn[5];
        for (u16 i = 0; i < 5; ++i)
        {
            fn[i]         = bc.begin_func();
            u16 const top = bc.mark();
            bc.emit(noir_op::INCV);
            bc.emit_u16(i);
            u16 const jmp = bc.mark();
            bc.emit(noir_op::JMP);
            bc.emit_s16(to<i16>(to<i32>(top) - to<i32>(jmp) - 3));
        }

        auto vm = bc.build_vm();
        vm.set_frame_budget(4);
        for (u16 i = 0; i < 5; ++i) vm.execute(fn[i]);

        for (u16 update = 0; update < 20; ++update) vm.update();

        TEST_ASSERT(vm.session_count() == 5, "all five are still looping");
        for (u16 i = 0; i < 5; ++i) TEST_ASSERT(bc.values[i] > 0, "every session got a turn");
        TEST_SUCCESS;
    }

    // ── One script, one session ─────────────────────────────────────────────────

    TEST_IMPL(test_sched_duplicate_script_refused)
    {
        bytecode_builder bc;
        u16 fn = bc.begin_func();
        bc.emit(noir_op::WAIT);
        bc.emit_u16(3);
        bc.emit(noir_op::INCV);
        bc.emit_u16(0);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        TEST_ASSERT(vm.execute(fn), "first request starts a session");
        TEST_ASSERT(failed_with(vm.execute(fn), noir_error::duplicate_script), "second is dropped");
        TEST_ASSERT(vm.session_count() == 1, "only one session exists");

        // Still refused once it is parked in a WAIT rather than running.
        vm.update();
        TEST_ASSERT(failed_with(vm.execute(fn), noir_error::duplicate_script), "a waiting one counts");

        u16 updates = run_until_empty(vm);
        TEST_ASSERT(updates < 200, "session ended");
        TEST_ASSERT(bc.values[0] == 1, "the script ran once, not twice");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_sched_finished_script_may_run_again)
    {
        // The duplicate check must look at live sessions, not at every script
        // the VM has ever seen.
        bytecode_builder bc;
        u16 fn = bc.begin_func();
        bc.emit(noir_op::INCV);
        bc.emit_u16(0);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        TEST_ASSERT(vm.execute(fn), "first run starts");
        run_until_empty(vm);
        TEST_ASSERT(vm.session_count() == 0, "run list emptied");

        TEST_ASSERT(vm.execute(fn), "the same script may start again");
        run_until_empty(vm);
        TEST_ASSERT(bc.values[0] == 2, "it ran both times");
        TEST_SUCCESS;
    }

    // ── Removing the dead must not disturb the living ───────────────────────────

    TEST_IMPL(test_sched_removal_keeps_the_survivors)
    {
        // Four sessions in a row; the middle two end on the first update while
        // the outer two are parked. A compaction that skips an entry after a
        // removal loses one of the survivors.
        bytecode_builder bc;

        u16 keep0 = emit_local_roundtrip(bc, 0xAAAA, 0, 4);

        u16 die0 = bc.begin_func();
        bc.emit(noir_op::RET);

        u16 die1 = bc.begin_func();
        bc.emit(noir_op::RET);

        u16 keep1 = emit_local_roundtrip(bc, 0xBBBB, 1, 4);

        auto vm = bc.build_vm();
        vm.execute(keep0);
        vm.execute(die0);
        vm.execute(die1);
        vm.execute(keep1);
        TEST_ASSERT(vm.session_count() == 4, "four sessions queued");

        vm.update();
        TEST_ASSERT(vm.session_count() == 2, "the two that returned were removed");

        u16 updates = run_until_empty(vm);
        TEST_ASSERT(updates < 200, "the survivors ended");
        TEST_ASSERT(bc.values[0] == 0xAAAA, "first survivor kept its stack");
        TEST_ASSERT(bc.values[1] == 0xBBBB, "last survivor kept its stack");
        TEST_SUCCESS;
    }

    // ── A freed stack slot must not be handed to a live session ─────────────────

    TEST_IMPL(test_sched_reused_slot_does_not_clobber)
    {
        // A runs and parks in slot 0. B takes slot 1 and ends. C then starts and
        // should get slot 1 back - not slot 0, which A is still sitting in.
        bytecode_builder bc;
        u16 fn_a = emit_local_roundtrip(bc, 0x5A5A, 0, 6);

        u16 fn_b = bc.begin_func();
        bc.emit(noir_op::RET);

        u16 fn_c = emit_local_roundtrip(bc, 0x7C7C, 1, 2);

        auto vm = bc.build_vm();
        vm.execute(fn_a);
        vm.execute(fn_b);
        vm.update();
        TEST_ASSERT(vm.session_count() == 1, "B is gone, A remains");

        vm.execute(fn_c);
        u16 updates = run_until_empty(vm);
        TEST_ASSERT(updates < 200, "both ended");
        TEST_ASSERT(bc.values[0] == 0x5A5A, "A's local survived C starting");
        TEST_ASSERT(bc.values[1] == 0x7C7C, "C got a stack of its own");
        TEST_SUCCESS;
    }

    // ── One session's error is not another's ────────────────────────────────────

    TEST_IMPL(test_sched_error_kills_only_its_own_session)
    {
        bytecode_builder bc;
        u16 healthy = emit_local_roundtrip(bc, 0x3C3C, 0, 2);

        u16 broken = bc.begin_func();
        bc.emit(noir_op::WAIT);
        bc.emit_u16(1);
        bc.emit(0x4F);  // reserved in the control flow range
        bc.emit_s16(0);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(healthy);
        vm.execute(broken);

        u16 updates = run_until_empty(vm);
        TEST_ASSERT(updates < 200, "the run list drained");
        TEST_ASSERT(bc.values[0] == 0x3C3C, "the healthy session finished its work");
        TEST_SUCCESS;
    }

    // ── WAIT is a property of a session, not of the VM ──────────────────────────

    TEST_IMPL(test_sched_wait_parks_only_the_waiter)
    {
        bytecode_builder bc;

        u16 waiter = bc.begin_func();
        bc.emit(noir_op::WAIT);
        bc.emit_u16(5);
        bc.emit(noir_op::INCV);
        bc.emit_u16(0);
        bc.emit(noir_op::RET);

        u16 runner = bc.begin_func();
        bc.emit(noir_op::INCV);
        bc.emit_u16(1);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(waiter);
        vm.execute(runner);

        vm.update();
        TEST_ASSERT(bc.values[1] == 1, "the runner ran while the other waited");
        TEST_ASSERT(bc.values[0] == 0, "the waiter has not passed its WAIT");
        TEST_ASSERT(vm.session_count() == 1, "the runner was removed, the waiter stayed");

        run_until_empty(vm);
        TEST_ASSERT(bc.values[0] == 1, "the waiter resumed later");
        TEST_SUCCESS;
    }

    // ── A whole budget, no more and no less ─────────────────────────────────────

    TEST_IMPL(test_sched_budget_is_per_session_per_frame)
    {
        // Both sessions want more than their budget. With a pool of 40 and a
        // budget of 10 each should get exactly 10 in one update: eleven NOPs
        // means neither reaches its INCV on the first.
        bytecode_builder bc;
        u16 fn[2];
        for (u16 i = 0; i < 2; ++i)
        {
            fn[i] = bc.begin_func();
            for (u16 n = 0; n < 11; ++n) bc.emit(noir_op::NOP);
            bc.emit(noir_op::INCV);
            bc.emit_u16(i);
            bc.emit(noir_op::RET);
        }

        auto vm = bc.build_vm();
        vm.execute(fn[0]);
        vm.execute(fn[1]);

        vm.update();
        TEST_ASSERT(bc.values[0] == 0, "first session stopped on its budget");
        TEST_ASSERT(bc.values[1] == 0, "second session stopped on its budget");

        vm.update();
        TEST_ASSERT(bc.values[0] == 1 && bc.values[1] == 1, "both finished on the second");
        TEST_SUCCESS;
    }

    // ── What a session does not use goes back to the pool ───────────────────────

    TEST_IMPL(test_sched_unused_slice_returns_to_the_pool)
    {
        // A parks itself two instructions in, so eight of its ten belong to
        // whoever comes next. Charging the whole slice instead of what was
        // spent would leave B nothing at all.
        bytecode_builder bc;

        u16 stopper = bc.begin_func();
        bc.emit(noir_op::NOP);
        bc.emit(noir_op::NOP);
        bc.emit(noir_op::WAIT);
        bc.emit_u16(5);
        bc.emit(noir_op::RET);

        u16 looper    = bc.begin_func();
        u16 const top = bc.mark();
        bc.emit(noir_op::INCV);
        bc.emit_u16(1);
        u16 const jmp = bc.mark();
        bc.emit(noir_op::JMP);
        bc.emit_s16(to<i16>(to<i32>(top) - to<i32>(jmp) - 3));

        auto vm = bc.build_vm();
        vm.set_frame_budget(10);
        vm.execute(stopper);
        vm.execute(looper);

        vm.update();

        // A spent 2 of the pool's 10, so B got a slice of 8: four INCV/JMP pairs.
        TEST_ASSERT(bc.values[1] > 0, "the unused instructions reached the second session");
        TEST_ASSERT(bc.values[1] == 4, "and it got exactly what the first one left");
        TEST_SUCCESS;
    }

    // ── Bridge callbacks that reach back into the VM ────────────────────────────
    //
    // INV_ADD is immediate: the session keeps running after it. These bridges
    // use that to touch the VM from inside an opcode, which is where the
    // register window and the run list are at their most fragile.

    namespace
    {
        noir_vm* s_sched_vm     = nullptr;
        u16 s_sched_spawn_id    = 0;
        u16 s_sched_spawn_count = 0;

        /// Immediate, and only ever appends: starting a script is always legal.
        void sched_inv_add(mock_bridge_log& log, u16 id)
        {
            log.inv_added[log.inv_add_count++] = id;
            if (!s_sched_vm) return;

            if (s_sched_spawn_count > 0)
            {
                --s_sched_spawn_count;
                s_sched_vm->execute(s_sched_spawn_id);
            }
        }

        /// Terminal, and tears the VM down where it stands - the one bridge
        /// call allowed to, and the shape a real NEW_GAME would take.
        void sched_new_game(mock_bridge_log& log)
        {
            log.new_game_called = true;
            if (s_sched_vm) s_sched_vm->reset_execution();
        }

        noir_bridge make_sched_bridge()
        {
            noir_bridge b = g_mock_bridge;
            b.inv_add     = { on_log<sched_inv_add> };
            b.new_game    = { on_log<sched_new_game> };
            return b;
        }

        noir_bridge const g_sched_bridge = make_sched_bridge();

        void sched_bridge_reset(noir_vm* vm)
        {
            s_sched_vm          = vm;
            s_sched_spawn_id    = 0;
            s_sched_spawn_count = 0;
        }
    }  // namespace

    TEST_IMPL(test_sched_spawn_from_opcode_keeps_caller_intact)
    {
        // The spawning session writes a local, calls INV_ADD - which starts
        // another script - and then reads its local back. execute() runs
        // through the register window, so a caller left unrestored shows up
        // here as a wrong value or a wrecked PC.
        bytecode_builder bc;

        u16 spawner = bc.begin_func();
        bc.emit(noir_op::LOCALS);
        bc.emit(2);
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::LOCAL, noir_mode::IMM16);
        bc.emit(to<u8>(0xFE));
        bc.emit_u16(0x9E9E);
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
        bc.emit_u16(77);
        bc.emit(noir_op::INV_ADD);
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::VALUE_IMM, noir_mode::REG_D0);
        bc.emit_u16(2);
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::VALUE_IMM, noir_mode::LOCAL);
        bc.emit_u16(0);
        bc.emit(to<u8>(0xFE));
        bc.emit(noir_op::RET);

        u16 spawned = bc.begin_func();
        bc.emit(noir_op::INCV);
        bc.emit_u16(1);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm(256, &g_sched_bridge);
        sched_bridge_reset(&vm);
        s_sched_spawn_id    = spawned;
        s_sched_spawn_count = 1;

        vm.execute(spawner);
        vm.update();

        TEST_ASSERT(g_mock_log.inv_add_count == 1, "the bridge was reached");
        TEST_ASSERT(g_mock_log.inv_added[0] == 77, "the bridge saw D0");
        TEST_ASSERT(bc.values[0] == 0x9E9E, "the spawner read back its own local");
        TEST_ASSERT(bc.values[2] == 77, "D0 survived the spawn");
        TEST_ASSERT(bc.values[1] == 0, "the spawned script waits for the next update");

        u16 updates = run_until_empty(vm);
        TEST_ASSERT(updates < 200, "both ended");
        TEST_ASSERT(bc.values[1] == 1, "the spawned script ran afterwards");

        sched_bridge_reset(nullptr);
        TEST_SUCCESS;
    }

    TEST_IMPL(test_sched_spawn_from_opcode_may_grow_the_stacks)
    {
        // Same again, but the run list is already full, so the spawn
        // reallocates the stacks while the caller is loaded and its base
        // pointer is live. Growing copies the old stacks into the new
        // allocation, so a caller that keeps writing through the old pointer
        // is only caught by writing *after* the growth and reading back on a
        // later update, once the base has been worked out afresh.
        bytecode_builder bc;

        u16 parked[3];
        for (u16 i = 0; i < 3; ++i) parked[i] = emit_local_roundtrip(bc, to<u16>(0x200 + i), i, 4);

        u16 spawner = bc.begin_func();
        bc.emit(noir_op::LOCALS);
        bc.emit(2);
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::LOCAL, noir_mode::IMM16);
        bc.emit(to<u8>(0xFE));
        bc.emit_u16(0x1111);  // copied into the new allocation by the growth
        bc.emit(noir_op::INV_ADD);
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::LOCAL, noir_mode::IMM16);
        bc.emit(to<u8>(0xFE));
        bc.emit_u16(0x4D4D);  // has to land in the new allocation, not the old
        bc.emit(noir_op::WAIT);
        bc.emit_u16(1);
        bc.emit(noir_op::MOV16);
        bc.emit_mode(noir_mode::VALUE_IMM, noir_mode::LOCAL);
        bc.emit_u16(3);
        bc.emit(to<u8>(0xFE));
        bc.emit(noir_op::RET);

        u16 spawned = bc.begin_func();
        bc.emit(noir_op::INCV);
        bc.emit_u16(4);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm(256, &g_sched_bridge);
        sched_bridge_reset(&vm);
        s_sched_spawn_id    = spawned;
        s_sched_spawn_count = 1;

        for (u16 i = 0; i < 3; ++i) vm.execute(parked[i]);
        vm.execute(spawner);
        TEST_ASSERT(vm.session_count() == 4, "all four slots taken");

        u16 updates = run_until_empty(vm);
        TEST_ASSERT(updates < 200, "everything ended");

        for (u16 i = 0; i < 3; ++i)
            TEST_ASSERT(bc.values[i] == to<u16>(0x200 + i), "parked locals survived the growth");
        TEST_ASSERT(bc.values[3] == 0x4D4D, "the spawner wrote into the new allocation");
        TEST_ASSERT(bc.values[4] == 1, "the fifth session ran");

        sched_bridge_reset(nullptr);
        TEST_SUCCESS;
    }

    TEST_IMPL(test_sched_reset_from_opcode_drops_everything)
    {
        // A terminal call is allowed to tear the VM down where it stands, and a
        // real NEW_GAME eventually will. The update is then walking a list that
        // no longer exists, and has to leave the VM usable.
        bytecode_builder bc;

        u16 resetter = bc.begin_func();
        bc.emit(noir_op::NEW_GAME);
        bc.emit(noir_op::INCV);  // must never run: the session is gone
        bc.emit_u16(0);
        bc.emit(noir_op::RET);

        u16 other = emit_local_roundtrip(bc, 0x6161, 1, 2);

        auto vm = bc.build_vm(256, &g_sched_bridge);
        sched_bridge_reset(&vm);

        vm.execute(resetter);
        vm.execute(other);

        auto status = vm.update();
        TEST_ASSERT(status == noir_result::completed, "the update ended cleanly");
        TEST_ASSERT(!vm.terminated(), "the teardown it asked for already happened");
        TEST_ASSERT(vm.session_count() == 0, "every session was dropped");
        TEST_ASSERT(vm.state() == noir_session_state::ended, "including the one that was running");
        TEST_ASSERT(bc.values[0] == 0, "the instruction after the reset never ran");

        // The VM has to be usable afterwards.
        vm.execute(other);
        u16 updates = run_until_empty(vm);
        TEST_ASSERT(updates < 200, "a fresh session runs after the reset");
        TEST_ASSERT(bc.values[1] == 0x6161, "and it runs correctly");

        sched_bridge_reset(nullptr);
        TEST_SUCCESS;
    }

    // ── The stack has two ends and both of them kill ────────────────────────────

    TEST_IMPL(test_sched_stack_overflow_kills_the_session)
    {
        // A 16-byte stack holds the 8-byte sentinel frame and four pushes. The
        // fifth runs out of room.
        bytecode_builder bc;

        u16 greedy = bc.begin_func();
        for (u16 i = 0; i < 5; ++i)
        {
            bc.emit(noir_op::PUSH);
            bc.emit_mode(0, noir_mode::IMM16);
            bc.emit_u16(to<u16>(0xC000 + i));
        }
        bc.emit(noir_op::INCV);  // must never run
        bc.emit_u16(0);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm(16);
        TEST_ASSERT(vm.execute(greedy), "the frame itself fits");

        TEST_ASSERT(failed_with(vm.update(), noir_error::stack_overflow), "overflow was reported");
        TEST_ASSERT(bc.values[0] == 0, "the script stopped at the failed push");
        TEST_ASSERT(vm.session_count() == 0, "and the session was removed");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_sched_stack_underflow_kills_the_session)
    {
        // Four pops empty the frame execute() pushed. The fifth has nothing
        // left to take.
        bytecode_builder bc;

        u16 hungry = bc.begin_func();
        for (u16 i = 0; i < 5; ++i)
        {
            bc.emit(noir_op::POP);
            bc.emit_mode(noir_mode::REG_D0, 0);
        }
        bc.emit(noir_op::INCV);  // must never run
        bc.emit_u16(0);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm(256);
        TEST_ASSERT(vm.execute(hungry), "it starts");

        TEST_ASSERT(failed_with(vm.update(), noir_error::stack_underflow), "underflow was reported");
        TEST_ASSERT(bc.values[0] == 0, "the script stopped at the failed pop");
        TEST_ASSERT(vm.session_count() == 0, "and the session was removed");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_sched_stack_death_spares_the_neighbours)
    {
        // The stacks are separate, so one session running out must not disturb
        // a session sitting next to it.
        bytecode_builder bc;

        u16 healthy = emit_local_roundtrip(bc, 0x2B2B, 0, 3);

        u16 doomed = bc.begin_func();
        bc.emit(noir_op::WAIT);
        bc.emit_u16(1);
        for (u16 i = 0; i < 5; ++i)
        {
            bc.emit(noir_op::POP);
            bc.emit_mode(noir_mode::REG_D0, 0);
        }
        bc.emit(noir_op::INCV);  // must never run
        bc.emit_u16(1);
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(healthy);
        vm.execute(doomed);

        auto caught = noir_error::none;
        u16 updates = run_until_empty(vm, 200, &caught);
        TEST_ASSERT(updates < 200, "the run list drained");
        TEST_ASSERT(caught == noir_error::stack_underflow, "the doomed one died");
        TEST_ASSERT(bc.values[1] == 0, "and it died where it ran out");
        TEST_ASSERT(bc.values[0] == 0x2B2B, "the neighbour finished its work untouched");
        TEST_SUCCESS;
    }

    // ── A death outranks the ordinary ───────────────────────────────────────────

    TEST_IMPL(test_sched_failure_outranks_yielded)
    {
        // Sessions remaining is the ordinary case and worth saying only when
        // nothing worse happened. A session dying is not ordinary, so the
        // update reports that instead - even though the survivor is still live.
        bytecode_builder bc;

        u16 survivor = bc.begin_func();
        for (u16 i = 0; i < 40; ++i) bc.emit(noir_op::NOP);
        bc.emit(noir_op::RET);

        // The entry frame is eight bytes, so it takes five pops to run out.
        u16 doomed = bc.begin_func();
        for (u16 i = 0; i < 5; ++i)
        {
            bc.emit(noir_op::POP);
            bc.emit_mode(noir_mode::REG_D0, 0);
        }
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(survivor);
        vm.execute(doomed);

        auto status = vm.update();

        TEST_ASSERT(failed_with(status, noir_error::stack_underflow), "the death is reported");
        TEST_ASSERT(vm.session_count() == 1, "though a session is still live");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_sched_failure_does_not_outlive_its_update)
    {
        // The verdict belongs to one update. A death in this one must not be
        // handed back by the next, which is the trap a sticky error code sets.
        bytecode_builder bc;

        u16 survivor = bc.begin_func();
        for (u16 i = 0; i < 40; ++i) bc.emit(noir_op::NOP);
        bc.emit(noir_op::RET);

        // The entry frame is eight bytes, so it takes five pops to run out.
        u16 doomed = bc.begin_func();
        for (u16 i = 0; i < 5; ++i)
        {
            bc.emit(noir_op::POP);
            bc.emit_mode(noir_mode::REG_D0, 0);
        }
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        vm.execute(survivor);
        vm.execute(doomed);

        TEST_ASSERT(failed_with(vm.update(), noir_error::stack_underflow),
            "the update the death happened in");

        auto next = vm.update();

        TEST_ASSERT(next == noir_result::yielded, "the next one is ordinary again");
        TEST_ASSERT(message_has("popped more than it pushed"),
            "the account of it is still on record");
        TEST_SUCCESS;
    }

    // ── Nothing to run ──────────────────────────────────────────────────────────

    TEST_IMPL(test_sched_update_with_empty_run_list)
    {
        bytecode_builder bc;
        bc.begin_func();
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm();
        TEST_ASSERT(vm.update() == noir_result::ok, "an empty run list is not an error");
        TEST_ASSERT(vm.session_count() == 0, "and it stays empty");
        TEST_SUCCESS;
    }

    // ── A session that dies on its first push ───────────────────────────────────

    TEST_IMPL(test_sched_session_that_cannot_be_framed_is_not_listed)
    {
        // Eight bytes of stack hold the sentinel frame and nothing else, so
        // the second session cannot even be started. It must not be left in
        // the run list half-built.
        bytecode_builder bc;
        u16 fn0 = bc.begin_func();
        bc.emit(noir_op::WAIT);
        bc.emit_u16(2);
        bc.emit(noir_op::RET);

        u16 fn1 = bc.begin_func();
        bc.emit(noir_op::RET);

        auto vm = bc.build_vm(4);
        TEST_ASSERT(!vm.execute(fn0), "a four-byte stack cannot hold a frame");
        TEST_ASSERT(vm.session_count() == 0, "nothing was added to the run list");

        // A script that dies before its first instruction has still died, and
        // the player is owed the same account as for one that got further.
        TEST_ASSERT(message_is("script 0: ran out of its 4 byte stack"), "the player was told");

        TEST_ASSERT(!vm.execute(fn1), "and the next one fails the same way");
        TEST_ASSERT(message_is("script 1: ran out of its 4 byte stack"), "and told again");
        TEST_ASSERT(vm.update() == noir_result::ok, "update has nothing to do");
        TEST_SUCCESS;
    }

    // ── Test suite registration ─────────────────────────────────────────────────

    TEST_SUITE_BEGIN(noir_vm_scheduler)
    TEST(test_sched_two_sessions_keep_own_locals)
    TEST(test_sched_growth_past_four_keeps_stacks)
    TEST(test_sched_pool_caps_the_update)
    TEST(test_sched_zero_pool_does_not_hang)
    TEST(test_sched_zero_budget_does_not_hang)
    TEST(test_sched_rotation_feeds_every_session)
    TEST(test_sched_duplicate_script_refused)
    TEST(test_sched_finished_script_may_run_again)
    TEST(test_sched_removal_keeps_the_survivors)
    TEST(test_sched_reused_slot_does_not_clobber)
    TEST(test_sched_error_kills_only_its_own_session)
    TEST(test_sched_wait_parks_only_the_waiter)
    TEST(test_sched_budget_is_per_session_per_frame)
    TEST(test_sched_unused_slice_returns_to_the_pool)
    TEST(test_sched_spawn_from_opcode_keeps_caller_intact)
    TEST(test_sched_spawn_from_opcode_may_grow_the_stacks)
    TEST(test_sched_reset_from_opcode_drops_everything)
    TEST(test_sched_stack_overflow_kills_the_session)
    TEST(test_sched_stack_underflow_kills_the_session)
    TEST(test_sched_stack_death_spares_the_neighbours)
    TEST(test_sched_failure_outranks_yielded)
    TEST(test_sched_failure_does_not_outlive_its_update)
    TEST(test_sched_update_with_empty_run_list)
    TEST(test_sched_session_that_cannot_be_framed_is_not_listed)
    TEST_SUITE_END

}  // namespace NEONengine

#endif  // ACE_TEST_RUNNER
#endif  // __NOIR_VM_SCHEDULER_TESTS_H__INCLUDED__
