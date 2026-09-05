#ifndef __CORE_STATE_H__INCLUDED__
#define __CORE_STATE_H__INCLUDED__

#include <mtl/expected.h>
#include <mtl/memory.h>

namespace NEONengine
{
    class state_control;
    class state_manager;
    class state;

    using state_ptr = mtl::unique_ptr<state>;

    class state
    {
        friend class state_manager;

        public:
        /// @brief Releases resources even if pushed() failed or never ran.
        virtual ~state() = default;

        NO_COPY(state)
        NO_MOVE(state)

        /**
         * @brief Performs fallible setup after construction.
         * @return Success, or error text that remains valid until destruction.
         * Failure stops the manager; it does not resume the previous state.
         */
        virtual mtl::expected<void, char const*> pushed() { return {}; }
        virtual void update(state_control& control) = 0;
        virtual void suspended() {}
        virtual void resumed() {}

        protected:
        state() = default;

        private:
        state_ptr _previous{nullptr};
    };

    template<typename T>
    concept state_type = requires(T* value) { state_ptr(value); };
}

#endif
