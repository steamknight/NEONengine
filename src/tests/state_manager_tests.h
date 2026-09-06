#ifndef __STATE_MANAGER_TESTS_H__INCLUDED__
#define __STATE_MANAGER_TESTS_H__INCLUDED__

#ifdef ACE_TEST_RUNNER

#include <string.h>

#include "core/state_manager.h"
#include "test_macros.h"
#include "utils/assert.h"

namespace NEONengine
{
    namespace
    {
        using state_test_value_ptr = mtl::unique_ptr<int>;

        struct state_events
        {
            void record(char id, char event)
            {
                assert(size + 2 < sizeof(text), "State event log overflow");
                text[size++] = id;
                text[size++] = event;
                text[size] = '\0';
            }

            char text[512]{};
            size_t size = 0;
            void (*on_update)(state_control&, state_events&) = nullptr;
            void (*on_pushed)(state_events&) = nullptr;
            void (*on_suspended)(state_events&) = nullptr;
            void (*on_resumed)(state_events&) = nullptr;
            void (*on_destroy)(state_events&) = nullptr;
            state_control* saved_control = nullptr;
            state_manager* manager = nullptr;
            state_update_result nested_result = state_update_result::ok;
        };

        class observed_state : public state
        {
            public:
            observed_state(state_events& events, char id) : _events(events), _id(id)
            {
                _events.record(_id, 'c');
            }

            ~observed_state() override
            {
                _events.record(_id, 'd');
                auto action = _events.on_destroy;
                _events.on_destroy = nullptr;
                if (action) action(_events);
            }

            mtl::expected<void, char const*> pushed() override
            {
                _events.record(_id, 'p');
                if (_events.on_pushed) _events.on_pushed(_events);
                return {};
            }

            void update(state_control& control) override
            {
                _events.record(_id, 'u');
                auto action = _events.on_update;
                _events.on_update = nullptr;
                if (action) action(control, _events);
                _events.record(_id, 'e');
            }
            void suspended() override
            {
                _events.record(_id, 's');
                if (_events.on_suspended) _events.on_suspended(_events);
            }

            void resumed() override
            {
                _events.record(_id, 'r');
                if (_events.on_resumed) _events.on_resumed(_events);
            }

            private:
            state_events& _events;
            char _id;
        };

        class failed_state final : public observed_state
        {
            public:
            failed_state(state_events& events, char id, char const* message)
                : observed_state(events, id)
            {
                strncpy(_message, message, sizeof(_message) - 1);
            }

            ~failed_state() override { memset(_message, '?', sizeof(_message)); }

            mtl::expected<void, char const*> pushed() override
            {
                observed_state::pushed();
                return mtl::unexpected(static_cast<char const*>(_message));
            }

            private:
            char _message[320]{};
        };

        class unallocated_state final : public observed_state
        {
            public:
            using observed_state::observed_state;

            static void* operator new(size_t, mtl::MemF) noexcept { return nullptr; }
        };

        class owning_state final : public state
        {
            public:
            owning_state(state_test_value_ptr&& value, int& observed)
                : _value(mtl::move(value)), _observed(observed) {}

            void update(state_control& control) override
            {
                _observed = *_value;
                control.pop();
            }

            private:
            state_test_value_ptr _value;
            int& _observed;
        };

        static_assert(state_type<observed_state>);
        static_assert(!state_type<int>);
        static_assert(!state_type<void>);
    }

    TEST_IMPL(test_state_apply_and_update_are_separate)
    {
        state_events events;
        state_manager manager;
        manager.start<observed_state>(events, 'A');
        TEST_ASSERT(manager.apply_transition() == state_update_result::ok, "entry should succeed without update");
        TEST_ASSERT(strcmp(events.text, "AcAp") == 0, "applying must not run gameplay");
        TEST_ASSERT(manager.update_current() == state_update_result::ok, "current state should update separately");
        events.on_update = [](state_control& control, state_events& log) {
            control.push<observed_state>(log, 'B');
        };
        TEST_ASSERT(manager.update_current() == state_update_result::ok, "push should queue");
        TEST_ASSERT(manager.apply_transition() == state_update_result::ok, "push should enter without update");
        events.on_update = [](state_control& control, state_events&) { control.pop(); };
        TEST_ASSERT(manager.update_current() == state_update_result::ok, "child should request pop");
        TEST_ASSERT(manager.apply_transition() == state_update_result::ok, "pop should restore the parent");
        TEST_ASSERT(strcmp(events.text, "AcApAuAeAuBcAeAsBpBuBeBdAr") == 0,
            "the host must regain control after resume and before parent gameplay");
        state_ptr next(new (mtl::MemF::Fast) observed_state(events, 'C'));
        TEST_ASSERT(manager.replace(next), "the host should select a replacement after restoration");
        TEST_ASSERT(!next, "selection must transfer ownership");
        TEST_ASSERT(manager.apply_transition() == state_update_result::ok, "replacement should enter");
        TEST_ASSERT(strcmp(events.text, "AcApAuAeAuBcAeAsBpBuBeBdArCcAdCp") == 0,
            "replacement must destroy the resumed parent without another gameplay update");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_state_host_selection_preserves_callback_transition)
    {
        state_events events;
        state_manager manager;
        manager.start<observed_state>(events, 'A');
        events.on_update = [](state_control& control, state_events& log) {
            control.push<observed_state>(log, 'B');
        };
        TEST_ASSERT(manager.update() == state_update_result::ok, "callback should select B");
        state_ptr next(new (mtl::MemF::Fast) observed_state(events, 'C'));
        TEST_ASSERT(!manager.replace(next) && next, "host selection must leave both requests intact on conflict");
        TEST_ASSERT(manager.update() == state_update_result::ok, "the original callback transition should apply");
        TEST_ASSERT(strcmp(events.text, "AcApAuBcAeCcAsBpBuBe") == 0, "B must enter, not C");
        TEST_ASSERT(manager.replace(next), "the host may select C at the next idle point");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_state_update_current_rejects_unapplied_transition)
    {
        state_events events;
        state_manager manager;
        manager.start<observed_state>(events, 'A');
        TEST_ASSERT(manager.update_current() == state_update_result::error, "update cannot bypass pending entry");
        TEST_ASSERT(strcmp(events.text, "Ac") == 0, "misordered phases must run no callbacks");
        TEST_ASSERT(manager.apply_transition() == state_update_result::error, "the phase error must stay fatal");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_state_host_selection_is_rejected_during_entry)
    {
        state_events events;
        state_manager manager;
        events.manager = &manager;
        manager.start<observed_state>(events, 'A');
        events.on_pushed = [](state_events& log) {
            state_ptr next(new (mtl::MemF::Fast) observed_state(log, 'B'));
            log.manager->push(next);
        };
        TEST_ASSERT(manager.apply_transition() == state_update_result::error, "entry cannot change the stack");
        TEST_ASSERT(strcmp(events.text, "AcApBcBd") == 0, "rejected state must remain caller-owned");
        TEST_ASSERT(manager.update_current() == state_update_result::error, "fatal entry must prevent gameplay");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_state_host_replacement_requires_current_state)
    {
        state_events events;
        state_manager manager;
        state_ptr next(new (mtl::MemF::Fast) observed_state(events, 'A'));
        TEST_ASSERT(!manager.replace(next) && next, "empty replacement must fail without consuming its state");
        TEST_ASSERT(manager.apply_transition() == state_update_result::error, "invalid replacement must stay fatal");
        TEST_ASSERT(strcmp(events.text, "Ac") == 0, "empty replacement must not enter or update a state");
        manager.start<observed_state>(events, 'B');
        TEST_ASSERT(strcmp(events.text, "Ac") == 0, "start must not bypass a fatal host error");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_state_start_defers_entry)
    {
        state_events events;
        state_manager manager;
        manager.start<observed_state>(events, 'A');
        TEST_ASSERT(strcmp(events.text, "Ac") == 0, "start should only construct the state");
        TEST_ASSERT(manager.error()[0] == '\0', "a new manager should have no error");
        TEST_ASSERT(manager.update() == state_update_result::ok, "the first update should enter the state");
        TEST_ASSERT(strcmp(events.text, "AcApAuAe") == 0, "entry should precede the first state update");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_state_push_and_pop_are_deferred)
    {
        state_events events;
        state_manager manager;
        manager.start<observed_state>(events, 'A');
        events.on_update = [](state_control& control, state_events& log) {
            control.push<observed_state>(log, 'B');
        };
        TEST_ASSERT(manager.update() == state_update_result::ok, "requesting a push should succeed");
        TEST_ASSERT(strcmp(events.text, "AcApAuBcAe") == 0, "push should not suspend or enter during update");

        events.on_update = [](state_control& control, state_events&) { control.pop(); };
        TEST_ASSERT(manager.update() == state_update_result::ok, "the pushed state should update");
        TEST_ASSERT(strcmp(events.text, "AcApAuBcAeAsBpBuBe") == 0, "only the new top should update");

        TEST_ASSERT(manager.update() == state_update_result::ok, "pop should resume the previous state");
        TEST_ASSERT(strcmp(events.text, "AcApAuBcAeAsBpBuBeBdArAuAe") == 0,
            "pop should destroy the top before resuming and updating its parent");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_state_replace_does_not_resume_parent)
    {
        state_events events;
        state_manager manager;
        manager.start<observed_state>(events, 'A');
        events.on_update = [](state_control& control, state_events& log) {
            control.push<observed_state>(log, 'B');
        };
        TEST_ASSERT(manager.update() == state_update_result::ok, "push request should succeed");
        events.on_update = [](state_control& control, state_events& log) {
            control.replace<observed_state>(log, 'C');
        };
        TEST_ASSERT(manager.update() == state_update_result::ok, "replace request should succeed");
        TEST_ASSERT(strcmp(events.text, "AcApAuBcAeAsBpBuCcBe") == 0,
            "replacement should remain uninitialized until the next update");

        TEST_ASSERT(manager.update() == state_update_result::ok, "replacement should enter");
        TEST_ASSERT(strcmp(events.text, "AcApAuBcAeAsBpBuCcBeBdCpCuCe") == 0,
            "replacement should destroy B before entering C, without resuming A");

        events.on_update = [](state_control& control, state_events&) { control.pop(); };
        TEST_ASSERT(manager.update() == state_update_result::ok, "replacement should be able to pop");
        TEST_ASSERT(manager.update() == state_update_result::ok, "parent should remain on the stack");
        TEST_ASSERT(strcmp(events.text, "AcApAuBcAeAsBpBuCcBeBdCpCuCeCuCeCdArAuAe") == 0,
            "popping the replacement should resume the original parent");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_state_clear_destroys_pending_and_stack_without_resuming)
    {
        state_events events;
        state_manager manager;
        manager.start<observed_state>(events, 'A');
        events.on_update = [](state_control& control, state_events& log) {
            control.push<observed_state>(log, 'B');
        };
        TEST_ASSERT(manager.update() == state_update_result::ok, "first push should succeed");
        events.on_update = [](state_control& control, state_events& log) {
            control.push<observed_state>(log, 'C');
        };
        TEST_ASSERT(manager.update() == state_update_result::ok, "second push should succeed");
        manager.clear();
        TEST_ASSERT(strcmp(events.text, "AcApAuBcAeAsBpBuCcBeCdBdAd") == 0,
            "clear should destroy pending, top, then parent without entry or resume");
        TEST_ASSERT(manager.update() == state_update_result::finished, "clear should finish the manager");
        manager.clear();
        TEST_ASSERT(strcmp(events.text, "AcApAuBcAeAsBpBuCcBeCdBdAd") == 0,
            "clearing twice should not destroy objects twice");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_state_final_pop_finishes_on_next_update)
    {
        state_events events;
        state_manager manager;
        manager.start<observed_state>(events, 'A');
        events.on_update = [](state_control& control, state_events&) { control.pop(); };
        TEST_ASSERT(manager.update() == state_update_result::ok, "final pop should wait for the next update");
        TEST_ASSERT(strcmp(events.text, "AcApAuAe") == 0, "state should finish its callback before destruction");
        TEST_ASSERT(manager.update() == state_update_result::finished, "empty stack should finish normally");
        TEST_ASSERT(manager.update() == state_update_result::finished, "finished should persist");
        TEST_ASSERT(strcmp(events.text, "AcApAuAeAd") == 0, "final pop should destroy without another update");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_state_empty_manager_finishes)
    {
        state_manager manager;
        TEST_ASSERT(manager.update() == state_update_result::finished, "an empty manager should finish");
        TEST_ASSERT(manager.error()[0] == '\0', "normal completion should not set an error");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_state_start_forwards_owned_arguments)
    {
        int observed = 0;
        state_test_value_ptr value(new (mtl::MemF::Fast) int(42));
        TEST_ASSERT(value, "test value allocation failed");
        state_manager manager;
        manager.start<owning_state>(mtl::move(value), observed);
        TEST_ASSERT(!value, "construction should take ownership immediately");
        TEST_ASSERT(observed == 0, "start should not update the state");
        TEST_ASSERT(manager.update() == state_update_result::ok, "default pushed should succeed");
        TEST_ASSERT(observed == 42, "update should receive the owned constructor argument");
        TEST_ASSERT(manager.update() == state_update_result::finished, "owned state should be destroyed on pop");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_state_failed_start_owns_error_before_destruction)
    {
        state_events events;
        state_manager manager;
        manager.start<failed_state>(events, 'A', "Cannot load bitmap");
        TEST_ASSERT(manager.update() == state_update_result::error, "failed entry should be fatal");
        TEST_ASSERT(strcmp(events.text, "AcApAd") == 0, "failed state should be destroyed without updating");
        TEST_ASSERT(strcmp(manager.error(), "Cannot load bitmap") == 0, "error should survive state destruction");
        TEST_ASSERT(manager.update() == state_update_result::error, "empty stack must not hide a fatal error");
        manager.clear();
        TEST_ASSERT(manager.update() == state_update_result::error, "clear must preserve fatal status");
        TEST_ASSERT(strcmp(manager.error(), "Cannot load bitmap") == 0, "clear must preserve error text");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_state_failed_push_does_not_resume_parent)
    {
        state_events events;
        state_manager manager;
        manager.start<observed_state>(events, 'A');
        events.on_update = [](state_control& control, state_events& log) {
            control.push<failed_state>(log, 'B', "Cannot load sound");
        };
        TEST_ASSERT(manager.update() == state_update_result::ok, "request should not initialize the new state");
        TEST_ASSERT(manager.update() == state_update_result::error, "failed pushed should stop processing");
        TEST_ASSERT(strcmp(events.text, "AcApAuBcAeAsBpBd") == 0, "failure should neither resume nor destroy A");
        TEST_ASSERT(manager.update() == state_update_result::error, "error should persist without callbacks");
        manager.clear();
        TEST_ASSERT(strcmp(events.text, "AcApAuBcAeAsBpBdAd") == 0, "engine cleanup should destroy A without resume");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_state_failed_replace_releases_old_state_first)
    {
        state_events events;
        state_manager manager;
        manager.start<observed_state>(events, 'A');
        events.on_update = [](state_control& control, state_events& log) {
            control.replace<failed_state>(log, 'B', "Cannot load replacement");
        };
        TEST_ASSERT(manager.update() == state_update_result::ok, "replacement request should succeed");
        TEST_ASSERT(manager.update() == state_update_result::error, "failed replacement must not report finished");
        TEST_ASSERT(strcmp(events.text, "AcApAuBcAeAdBpBd") == 0,
            "old state must release resources before replacement initialization");
        TEST_ASSERT(strcmp(manager.error(), "Cannot load replacement") == 0, "replacement error should be retained");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_state_error_text_is_bounded)
    {
        char message[300];
        memset(message, 'x', sizeof(message) - 1);
        message[sizeof(message) - 1] = '\0';
        state_events events;
        state_manager manager;
        manager.start<failed_state>(events, 'A', message);
        TEST_ASSERT(manager.update() == state_update_result::error, "long errors should still be fatal");
        TEST_ASSERT(strlen(manager.error()) == 255, "error should be terminated at its documented limit");
        TEST_ASSERT(memcmp(manager.error(), message, 255) == 0, "truncation should retain the message prefix");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_state_start_allocation_failure_is_fatal)
    {
        state_events events;
        state_manager manager;
        manager.start<unallocated_state>(events, 'A');
        TEST_ASSERT(manager.update() == state_update_result::error, "allocation failure should reach the engine");
        TEST_ASSERT(events.text[0] == '\0', "a failed allocation must not call the constructor");
        TEST_ASSERT(strcmp(manager.error(), "Cannot allocate state") == 0, "allocation failure should have an error");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_state_request_allocation_failure_keeps_current_state)
    {
        state_events events;
        state_manager manager;
        manager.start<observed_state>(events, 'A');
        events.on_update = [](state_control& control, state_events& log) {
            control.replace<unallocated_state>(log, 'B');
        };
        TEST_ASSERT(manager.update() == state_update_result::error, "request allocation failure should report immediately");
        TEST_ASSERT(strcmp(events.text, "AcApAuAe") == 0, "current callback must finish without destroying its state");
        TEST_ASSERT(manager.update() == state_update_result::error, "failed request must never be applied");
        manager.clear();
        TEST_ASSERT(strcmp(events.text, "AcApAuAeAd") == 0, "engine cleanup should release the current state");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_state_second_request_is_rejected)
    {
        state_events events;
        state_manager manager;
        manager.start<observed_state>(events, 'A');
        events.on_update = [](state_control& control, state_events& log) {
            control.push<observed_state>(log, 'B');
            control.replace<observed_state>(log, 'C');
        };
        TEST_ASSERT(manager.update() == state_update_result::error, "a second request must not overwrite the first");
        TEST_ASSERT(strcmp(events.text, "AcApAuBcAe") == 0, "rejected request must not construct another object");
        TEST_ASSERT(strcmp(manager.error(), "More than one state transition requested") == 0,
            "conflicting requests should report a programming error");
        manager.clear();
        TEST_ASSERT(strcmp(events.text, "AcApAuBcAeBdAd") == 0, "cleanup should release the pending and active states");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_state_start_cannot_be_repeated)
    {
        state_events events;
        state_manager manager;
        manager.start<observed_state>(events, 'A');
        manager.start<observed_state>(events, 'B');
        TEST_ASSERT(manager.update() == state_update_result::error, "repeated start should be a programming error");
        TEST_ASSERT(strcmp(events.text, "Ac") == 0, "repeated start must not construct or enter another state");
        manager.clear();
        TEST_ASSERT(strcmp(events.text, "AcAd") == 0, "cleanup should destroy the original pending state");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_state_start_cannot_restart_finished_manager)
    {
        state_events events;
        state_manager manager;
        TEST_ASSERT(manager.update() == state_update_result::finished, "empty manager should finish");
        manager.start<observed_state>(events, 'A');
        TEST_ASSERT(manager.update() == state_update_result::error, "start after completion should be rejected");
        TEST_ASSERT(events.text[0] == '\0', "rejected start must not construct a state");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_state_control_is_rejected_outside_update)
    {
        state_events events;
        state_manager manager;
        manager.start<observed_state>(events, 'A');
        events.on_update = [](state_control& control, state_events& log) { log.saved_control = &control; };
        TEST_ASSERT(manager.update() == state_update_result::ok, "initial update should succeed");
        events.saved_control->pop();
        TEST_ASSERT(manager.update() == state_update_result::error, "retained control should be rejected outside update");
        TEST_ASSERT(strcmp(manager.error(), "State control used outside update") == 0, "misuse should be reported");
        TEST_ASSERT(strcmp(events.text, "AcApAuAe") == 0, "invalid control use must not pop the state");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_state_reentrant_update_is_rejected)
    {
        state_events events;
        state_manager manager;
        events.manager = &manager;
        manager.start<observed_state>(events, 'A');
        events.on_update = [](state_control&, state_events& log) {
            log.nested_result = log.manager->update();
        };
        TEST_ASSERT(manager.update() == state_update_result::error, "recursive update should fail");
        TEST_ASSERT(events.nested_result == state_update_result::error, "inner call should report the error");
        TEST_ASSERT(strcmp(events.text, "AcApAuAe") == 0, "recursive update must not invoke the state again");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_state_clear_during_update_is_rejected)
    {
        state_events events;
        state_manager manager;
        events.manager = &manager;
        manager.start<observed_state>(events, 'A');
        events.on_update = [](state_control&, state_events& log) { log.manager->clear(); };
        TEST_ASSERT(manager.update() == state_update_result::error, "clear during update should fail");
        TEST_ASSERT(strcmp(events.text, "AcApAuAe") == 0, "clear must not destroy the state during its callback");
        manager.clear();
        TEST_ASSERT(strcmp(events.text, "AcApAuAeAd") == 0, "clear outside the callback should still work");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_state_destructor_clears_unentered_state)
    {
        state_events events;
        {
            state_manager manager;
            manager.start<observed_state>(events, 'A');
        }
        TEST_ASSERT(strcmp(events.text, "AcAd") == 0, "manager destruction must release an unentered state");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_state_first_error_is_preserved)
    {
        state_events events;
        state_manager manager;
        manager.start<observed_state>(events, 'A');
        events.on_update = [](state_control& control, state_events& log) {
            control.push<unallocated_state>(log, 'B');
            control.pop();
        };
        TEST_ASSERT(manager.update() == state_update_result::error, "allocation failure should stop the manager");
        manager.start<observed_state>(events, 'C');
        TEST_ASSERT(strcmp(manager.error(), "Cannot allocate state") == 0, "later misuse must not replace the first error");
        TEST_ASSERT(strcmp(events.text, "AcApAuAe") == 0, "no state should be constructed after failure");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_state_control_is_rejected_during_entry)
    {
        state_events events;
        state_manager manager;
        manager.start<observed_state>(events, 'A');
        events.on_update = [](state_control& control, state_events& log) {
            log.saved_control = &control;
            control.push<observed_state>(log, 'B');
        };
        TEST_ASSERT(manager.update() == state_update_result::ok, "push request should succeed");
        events.on_pushed = [](state_events& log) { log.saved_control->pop(); };
        TEST_ASSERT(manager.update() == state_update_result::error, "pushed must not request transitions");
        TEST_ASSERT(strcmp(events.text, "AcApAuBcAeAsBp") == 0, "entry misuse should prevent the next state update");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_state_control_is_rejected_during_suspend)
    {
        state_events events;
        state_manager manager;
        manager.start<observed_state>(events, 'A');
        events.on_update = [](state_control& control, state_events& log) {
            log.saved_control = &control;
            control.push<observed_state>(log, 'B');
        };
        TEST_ASSERT(manager.update() == state_update_result::ok, "push request should succeed");
        events.on_suspended = [](state_events& log) { log.saved_control->pop(); };
        TEST_ASSERT(manager.update() == state_update_result::error, "suspended must not request transitions");
        TEST_ASSERT(strcmp(events.text, "AcApAuBcAeAs") == 0, "suspend misuse should prevent pending entry");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_state_control_is_rejected_during_resume)
    {
        state_events events;
        state_manager manager;
        manager.start<observed_state>(events, 'A');
        events.on_update = [](state_control& control, state_events& log) {
            log.saved_control = &control;
            control.push<observed_state>(log, 'B');
        };
        TEST_ASSERT(manager.update() == state_update_result::ok, "push request should succeed");
        events.on_update = [](state_control& control, state_events&) { control.pop(); };
        TEST_ASSERT(manager.update() == state_update_result::ok, "pop request should succeed");
        events.on_resumed = [](state_events& log) { log.saved_control->pop(); };
        TEST_ASSERT(manager.update() == state_update_result::error, "resumed must not request transitions");
        TEST_ASSERT(strcmp(events.text, "AcApAuBcAeAsBpBuBeBdAr") == 0, "resume misuse should prevent another update");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_state_destructor_clears_stack_and_pending_state)
    {
        state_events events;
        {
            state_manager manager;
            manager.start<observed_state>(events, 'A');
            events.on_update = [](state_control& control, state_events& log) {
                control.push<observed_state>(log, 'B');
            };
            TEST_ASSERT(manager.update() == state_update_result::ok, "first push should succeed");
            events.on_update = [](state_control& control, state_events& log) {
                control.replace<observed_state>(log, 'C');
            };
            TEST_ASSERT(manager.update() == state_update_result::ok, "replacement request should succeed");
        }
        TEST_ASSERT(strcmp(events.text, "AcApAuBcAeAsBpBuCcBeCdBdAd") == 0,
            "destructor should release pending and stacked states without resuming");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_state_destructor_cannot_request_pop)
    {
        state_events events;
        state_manager manager;
        manager.start<observed_state>(events, 'A');
        events.on_update = [](state_control& control, state_events& log) {
            control.push<observed_state>(log, 'B');
        };
        TEST_ASSERT(manager.update() == state_update_result::ok, "push request should succeed");
        events.on_update = [](state_control& control, state_events& log) {
            log.saved_control = &control;
            control.pop();
        };
        TEST_ASSERT(manager.update() == state_update_result::ok, "pop request should succeed");
        events.on_destroy = [](state_events& log) { log.saved_control->pop(); };
        TEST_ASSERT(manager.update() == state_update_result::error, "destructor must not request a transition");
        TEST_ASSERT(strcmp(manager.error(), "State control used outside update") == 0,
            "destructor misuse should report an error");
        TEST_ASSERT(strcmp(events.text, "AcApAuBcAeAsBpBuBeBd") == 0,
            "destructor misuse must not resume, update, or pop the parent");
        TEST_ASSERT(manager.update() == state_update_result::error, "invalid pop must not be applied later");
        manager.clear();
        TEST_ASSERT(strcmp(events.text, "AcApAuBcAeAsBpBuBeBdAd") == 0,
            "parent should survive until engine cleanup");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_state_destructor_cannot_push_during_clear)
    {
        state_events events;
        state_manager manager;
        manager.start<observed_state>(events, 'A');
        events.on_update = [](state_control& control, state_events& log) {
            control.push<observed_state>(log, 'B');
        };
        TEST_ASSERT(manager.update() == state_update_result::ok, "first push should succeed");
        events.on_update = [](state_control& control, state_events& log) {
            log.saved_control = &control;
            control.push<observed_state>(log, 'C');
        };
        TEST_ASSERT(manager.update() == state_update_result::ok, "second push should succeed");
        events.on_destroy = [](state_events& log) {
            log.saved_control->push<observed_state>(log, 'D');
        };
        manager.clear();
        TEST_ASSERT(manager.update() == state_update_result::error, "clear should report destructor misuse");
        TEST_ASSERT(strcmp(manager.error(), "State control used outside update") == 0,
            "clear should retain the destructor error");
        TEST_ASSERT(strcmp(events.text, "AcApAuBcAeAsBpBuCcBeCdBdAd") == 0,
            "clear must reject construction and still destroy every owned state");
        manager.clear();
        TEST_ASSERT(strcmp(events.text, "AcApAuBcAeAsBpBuCcBeCdBdAd") == 0,
            "repeated cleanup must not destroy any state twice");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_state_failed_replace_preserves_suspended_parents)
    {
        state_events events;
        state_manager manager;
        manager.start<observed_state>(events, 'A');
        events.on_update = [](state_control& control, state_events& log) {
            control.push<observed_state>(log, 'B');
        };
        TEST_ASSERT(manager.update() == state_update_result::ok, "first push should succeed");
        events.on_update = [](state_control& control, state_events& log) {
            control.push<observed_state>(log, 'C');
        };
        TEST_ASSERT(manager.update() == state_update_result::ok, "second push should succeed");
        events.on_update = [](state_control& control, state_events& log) {
            control.replace<failed_state>(log, 'D', "Cannot load replacement");
        };
        TEST_ASSERT(manager.update() == state_update_result::ok, "replacement request should succeed");
        TEST_ASSERT(manager.update() == state_update_result::error, "replacement initialization should fail");
        TEST_ASSERT(strcmp(events.text, "AcApAuBcAeAsBpBuCcBeBsCpCuDcCeCdDpDd") == 0,
            "replacement failure must leave both parents suspended and alive");
        TEST_ASSERT(manager.update() == state_update_result::error, "failed manager must not update either parent");
        manager.clear();
        TEST_ASSERT(strcmp(events.text, "AcApAuBcAeAsBpBuCcBeBsCpCuDcCeCdDpDdBdAd") == 0,
            "engine cleanup should destroy parents top-down without resuming them");
        TEST_ASSERT(strcmp(manager.error(), "Cannot load replacement") == 0,
            "cleanup must preserve the initialization error");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_state_replace_of_sole_state_can_finish_normally)
    {
        state_events events;
        state_manager manager;
        manager.start<observed_state>(events, 'A');
        events.on_update = [](state_control& control, state_events& log) {
            control.replace<observed_state>(log, 'B');
        };
        TEST_ASSERT(manager.update() == state_update_result::ok, "replacement request should succeed");
        events.on_update = [](state_control& control, state_events&) { control.pop(); };
        TEST_ASSERT(manager.update() == state_update_result::ok, "replacement should enter and update");
        TEST_ASSERT(strcmp(events.text, "AcApAuBcAeAdBpBuBe") == 0,
            "sole-state replacement should destroy the old state before entering the new one");
        TEST_ASSERT(manager.update() == state_update_result::finished, "popping the replacement should finish");
        TEST_ASSERT(strcmp(events.text, "AcApAuBcAeAdBpBuBeBd") == 0,
            "replacement should have no parent to resume");
        TEST_ASSERT(manager.error()[0] == '\0', "successful replacement should not report an error");
        TEST_SUCCESS;
    }

    TEST_SUITE_BEGIN(state_manager)
        TEST(test_state_host_replacement_requires_current_state)
        TEST(test_state_apply_and_update_are_separate)
        TEST(test_state_host_selection_preserves_callback_transition)
        TEST(test_state_update_current_rejects_unapplied_transition)
        TEST(test_state_host_selection_is_rejected_during_entry)
        TEST(test_state_start_defers_entry)
        TEST(test_state_push_and_pop_are_deferred)
        TEST(test_state_replace_does_not_resume_parent)
        TEST(test_state_clear_destroys_pending_and_stack_without_resuming)
        TEST(test_state_final_pop_finishes_on_next_update)
        TEST(test_state_empty_manager_finishes)
        TEST(test_state_start_forwards_owned_arguments)
        TEST(test_state_failed_start_owns_error_before_destruction)
        TEST(test_state_failed_push_does_not_resume_parent)
        TEST(test_state_failed_replace_releases_old_state_first)
        TEST(test_state_error_text_is_bounded)
        TEST(test_state_start_allocation_failure_is_fatal)
        TEST(test_state_request_allocation_failure_keeps_current_state)
        TEST(test_state_second_request_is_rejected)
        TEST(test_state_start_cannot_be_repeated)
        TEST(test_state_start_cannot_restart_finished_manager)
        TEST(test_state_control_is_rejected_outside_update)
        TEST(test_state_reentrant_update_is_rejected)
        TEST(test_state_clear_during_update_is_rejected)
        TEST(test_state_destructor_clears_unentered_state)
        TEST(test_state_first_error_is_preserved)
        TEST(test_state_control_is_rejected_during_entry)
        TEST(test_state_control_is_rejected_during_suspend)
        TEST(test_state_control_is_rejected_during_resume)
        TEST(test_state_destructor_clears_stack_and_pending_state)
        TEST(test_state_destructor_cannot_request_pop)
        TEST(test_state_destructor_cannot_push_during_clear)
        TEST(test_state_failed_replace_preserves_suspended_parents)
        TEST(test_state_replace_of_sole_state_can_finish_normally)
    TEST_SUITE_END
}

#endif

#endif
