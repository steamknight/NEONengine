#ifndef __STATE_MANAGER_H__INCLUDED__
#define __STATE_MANAGER_H__INCLUDED__

#include "state.h"

namespace NEONengine
{
    enum class state_update_result : unsigned char
    {
        ok,
        finished,
        error,
    };

    /**
     * @brief Borrowed transition access, valid only during a state's update.
     *
     * Push and replace construct immediately; entry waits for the next manager
     * update. Constructors must not perform fallible resource loading.
     * Callers must keep borrowed inputs alive until the state's last use.
     * Only one transition may be requested per update.
     */
    class state_control
    {
        friend class state_manager;

        public:
        NO_COPY(state_control)
        NO_MOVE(state_control)

        template<state_type T, typename... Args>
        void push(Args&&... args);

        template<state_type T, typename... Args>
        void replace(Args&&... args);

        void pop();

        private:
        explicit state_control(state_manager& manager) : _manager(manager) {}

        private:
        state_manager& _manager;
    };

    class state_manager
    {
        friend class state_control;

        public:
        state_manager() : _control(*this) {}
        ~state_manager();

        NO_COPY(state_manager)
        NO_MOVE(state_manager)

        /**
         * @brief Constructs the first state; entry waits for update().
         *
         * Call once, before the first update. Constructor argument lifetimes
         * follow the same rules as state_control::push().
         */
        template<state_type T, typename... Args>
        void start(Args&&... args)
        {
            if (_started || _phase != phase::idle || _result != state_update_result::ok)
            {
                fail("State manager already started");
                return;
            }
            _started = true;
            _transition = transition::push;
            construct_pending<T>(mtl::forward<Args>(args)...);
        }

        /**
         * @brief Applies the pending transition, then updates the top state.
         * @return error after any fatal failure, including on later calls.
         * After failure, later calls invoke no state callbacks.
         * The engine handles reporting and cleanup.
         */
        [[nodiscard]] state_update_result update();
        [[nodiscard]] state_update_result apply_transition();
        [[nodiscard]] state_update_result update_current();

        // Idle selection never replaces a transition already chosen by a state.
        bool push(state_ptr& pending);
        bool replace(state_ptr& pending);
        bool has_transition() const noexcept { return _transition != transition::none; }
        state* current() const noexcept { return _top.get(); }
        void fail(char const* message);

        /// @brief Destroys states without resuming them; preserves any fatal error.
        void clear();

        /// @return First error, truncated to 255 bytes; valid until manager destruction.
        /// Empty before an error, and preserved by clear().
        char const* error() const noexcept { return _error; }

        private:
        enum class transition : unsigned char { none, push, pop, replace };
        enum class phase : unsigned char { idle, constructing, transitioning, updating, clearing };

        private:
        template<state_type T, typename... Args>
        void construct_pending(Args&&... args)
        {
            static_assert(alignof(T) <= alignof(max_align_t), "State exceeds allocator alignment");
            auto const previous_phase = _phase;
            _phase = phase::constructing;
            _pending.reset(new (mtl::MemF::Fast) T(mtl::forward<Args>(args)...));
            _phase = previous_phase;
            if (!_pending) fail("Cannot allocate state");
        }

        bool request(transition operation);
        void apply_pending();
        void remove_top();

        private:
        state_ptr _top{nullptr};
        state_ptr _pending{nullptr};
        state_control _control;
        state_update_result _result = state_update_result::ok;
        transition _transition = transition::none;
        phase _phase = phase::idle;
        bool _started = false;
        char _error[256]{};
    };

    template<state_type T, typename... Args>
    void state_control::push(Args&&... args)
    {
        if (_manager.request(state_manager::transition::push))
            _manager.construct_pending<T>(mtl::forward<Args>(args)...);
    }

    template<state_type T, typename... Args>
    void state_control::replace(Args&&... args)
    {
        if (_manager.request(state_manager::transition::replace))
            _manager.construct_pending<T>(mtl::forward<Args>(args)...);
    }
}

#endif
