// #define ACE_TEST_RUNNER
#include "neonengine.h"

#include <ace/generic/main.h>
#include <ace/managers/key.h>
#include <ace/managers/mouse.h>
#include <ace/managers/ptplayer.h>
#include <ace/managers/state.h>

#include "build_number.h"
#include "core/game_data.h"
#include "core/music.h"
#include "test.h"

using namespace NEONengine;

extern "C" void __cxa_finalize(void*);

UBYTE ubDebugToggle = 1;

void genericCreate(void)
{
    keyCreate();
    mouseCreate(MOUSE_PORT_1);
    ptplayerCreate(systemIsPal());

    g_gameStateManager = stateManagerCreate();
    g_mainScreen       = screenCreate();

    auto engineResult = engine::initialize();
    if (!engineResult)
    {
        NE_LOG("Could not create NEONengine. Error %d", mtl::to<int>(engineResult.error()));
        return;
    }
    g_pEngine = mtl::move(engineResult.value());

    screenLoad(NEONengine::g_mainScreen);
    screenBindMouse(NEONengine::g_mainScreen);

#ifdef ACE_TEST_RUNNER
    statePush(g_gameStateManager, &tests::g_stateTestRunner);
#else
    statePush(g_gameStateManager, &g_stateScene);
#endif
}

void genericProcess(void)
{
    keyProcess();
    mouseProcess();
    ptplayerProcess();
    stateProcess(g_gameStateManager);
    screenProcess(NEONengine::g_mainScreen);

    if (keyUse(KEY_F1))
    {
        if (ubDebugToggle)
        {
            statePush(g_gameStateManager, &g_stateDebugView);
        }
        else
        {
            statePop(g_gameStateManager);
        }

        ubDebugToggle = 1 - ubDebugToggle;
    }

    if (keyUse(KEY_F2))
    {
        auto chipSize = memGetFreeChipSize();
        auto fastSize = AvailMem(MEMF_FAST);
        auto anySize  = AvailMem(MEMF_ANY);

        NE_LOG("Chip: %ld.%3ld KB, Fast: %ld.%3ld KB, Any:  %ld.%3ld KB ",
            chipSize >> 10,
            chipSize & 0x3FF,
            fastSize >> 10,
            fastSize & 0x3FF,
            anySize >> 10,
            anySize & 0x3FF);
    }
}

void genericDestroy(void)
{
    screenDestroy(NEONengine::g_mainScreen);
    musicFree();
    stateManagerDestroy(g_gameStateManager);
    ptplayerDestroy();
    mouseDestroy();
    keyDestroy();
    systemDestroy();

    // Run global/static destructors that registered with __cxa_atexit
    __cxa_finalize(nullptr);
}
