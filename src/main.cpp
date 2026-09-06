// #define ACE_TEST_RUNNER

#include "types.h"

#include "neonengine.h"

#include <ace/generic/main.h>
#include <ace/managers/game.h>
#include <ace/managers/key.h>
#include <ace/managers/mouse.h>
#include <ace/managers/ptplayer.h>

#include <mtl/memory.h>

#include "build_number.h"
#include "core/logger.h"

#ifdef ACE_TEST_RUNNER
#include "test.h"
#include "tests/test_runner.h"
#endif

NE_LOG_DOMAIN(Core);

using namespace NEONengine;

extern "C" void __cxa_finalize(void*);

void genericCreate(void)
{
    auto engineResult = engine::initialize();
    if (!engineResult)
    {
        NE_LOG("Could not create NEONengine. Error %d", to<int>(engineResult.error()));
        // ACE runs its loop whatever this function does, and every frame of it
        // would reach through a null engine.
        gameExit();
        return;
    }

    g_pEngine = mtl::move(engineResult.value());

#ifdef ACE_TEST_RUNNER
    g_pEngine->start_state<tests::test_runner_state>(*g_pEngine);
#else
    g_pEngine->start();
#endif
}

void genericProcess(void)
{
    g_pEngine->process();
}

void genericDestroy(void)
{
    NEONengine::g_pEngine = nullptr;

    // Run global/static destructors that registered with __cxa_atexit
    __cxa_finalize(nullptr);
}
