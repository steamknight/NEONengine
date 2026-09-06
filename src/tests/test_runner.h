#ifndef __TESTS_TEST_RUNNER_H__INCLUDED__
#define __TESTS_TEST_RUNNER_H__INCLUDED__

#ifdef ACE_TEST_RUNNER

#include "core/state.h"

namespace NEONengine
{
    class engine;

    namespace tests
    {
        class test_runner_state : public state
        {
            public:
            explicit test_runner_state(engine& engine) noexcept : _engine(engine) {}

            mtl::expected<void, char const*> pushed() override;
            void update(state_control& control) override;

            private:
            engine& _engine;
        };
    }
}

#endif
#endif
