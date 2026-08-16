/**
 * @file state_host.h
 * @brief Presents a class as one of ACE's state tables.
 *
 * One object per state type, built as the state is pushed and destroyed as it
 * is popped.
 *
 * A hosted state never pops itself. ACE runs cbDestroy inside statePop, so a
 * state that popped itself would be destroyed with its own member functions
 * still on the stack. create() and loop() report whether the state stays up,
 * and the host pops once they have returned.
 */

#ifndef __STATE_HOST_H__INCLUDED__
#define __STATE_HOST_H__INCLUDED__

#include <ace/managers/state.h>

#include "utils/assert.h"

namespace NEONengine
{
    /// The stack every hosted state is pushed on.
    extern tStateManager* g_gameStateManager;

    /// What a class must offer for @ref state_host to drive it.
    template<typename T>
    concept hosted_state = requires(T& state) {
        T();
        bool(state.create());
        bool(state.loop());
    };

    /// A state with something to put down as another goes up over it.
    template<typename T>
    concept suspending_state = requires(T& state) { state.suspend(); };

    /// A state with something to pick up as the one over it comes down.
    template<typename T>
    concept resuming_state = requires(T& state) { state.resume(); };

    /** @brief Holds the one @p T a build ever has, and the table ACE drives it by. */
    template<hosted_state T>
    class state_host
    {
        public:  //////////////////////////////////////////////////////////////////////////////////
        /**
         * @brief The table for @p T, built at compile time.
         *
         * A @p T without suspend() or resume() leaves ACE's slots null, which
         * is what ACE tests before calling them.
         */
        static constexpr tState table() noexcept
        {
            return tState{
                .cbCreate  = &on_create,
                .cbLoop    = &on_loop,
                .cbDestroy = &on_destroy,
                .cbSuspend = suspend_slot(),
                .cbResume  = resume_slot(),
                .pPrev     = nullptr,
            };
        }

        private:  /////////////////////////////////////////////////////////////////////////////////
        /** @brief The state now up. Good only between create and destroy. */
        static T& instance() noexcept
        {
            assert(_live, "reached a state that is not up");
            return *reinterpret_cast<T*>(_storage);
        }

        static void on_create()
        {
            assert(!_live, "pushed a state that is already up");

            new (_storage) T();
            _live = true;

            // Nothing may touch the object after this: the pop destroys it.
            if (!instance().create()) statePop(g_gameStateManager);
        }

        static void on_loop()
        {
            if (!instance().loop()) statePop(g_gameStateManager);
        }

        static void on_destroy()
        {
            instance().~T();
            _live = false;
        }

        static void on_suspend() requires suspending_state<T> { instance().suspend(); }
        static void on_resume() requires resuming_state<T> { instance().resume(); }

        static constexpr tStateCb suspend_slot() noexcept
        {
            if constexpr (suspending_state<T>) return &on_suspend;
            else return nullptr;
        }

        static constexpr tStateCb resume_slot() noexcept
        {
            if constexpr (resuming_state<T>) return &on_resume;
            else return nullptr;
        }

        alignas(T) static inline unsigned char _storage[sizeof(T)];
        static inline bool _live = false;
    };
}  // namespace NEONengine

#endif  //__STATE_HOST_H__INCLUDED__
