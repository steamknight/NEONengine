// #define ACE_TEST_RUNNER

#include "types.h"

#include "neonengine.h"

#include <ace/generic/main.h>
#include <ace/managers/key.h>
#include <ace/managers/mouse.h>
#include <ace/managers/ptplayer.h>
#include <ace/managers/state.h>

#include "build_number.h"
#include "core/asset_cache.h"
#include "core/debug_keys.h"
#include "core/game_data.h"
#include "core/logger.h"
#include "core/music.h"
#include "test.h"

NE_LOG_DOMAIN(Core);

using namespace NEONengine;

extern "C" void __cxa_finalize(void*);

void genericCreate(void)
{
    log_init();

    keyCreate();
    mouseCreate(MOUSE_PORT_1);
    ptplayerCreate(systemIsPal());

    g_gameStateManager = stateManagerCreate();
    g_mainScreen       = screenCreate();

    auto engineResult = engine::initialize();
    if (!engineResult)
    {
        NE_LOG("Could not create NEONengine. Error %d", to<int>(engineResult.error()));
        return;
    }
    g_pEngine = mtl::move(engineResult.value());
    g_pEngine->overlays().bind(&g_stateMessage, &g_stateDialogue);

    screenLoad(NEONengine::g_mainScreen);
    screenBindMouse(NEONengine::g_mainScreen);

    // The boot scene loads the moment its state is pushed, and F4 cannot be
    // pressed before that, so this is the only chance to see what engine
    // startup alone left in the cache. Everything after it belongs to a scene.
    asset_cache_log();

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

    g_pEngine->overlays().present(g_gameStateManager);

    g_pEngine->update();

    stateProcess(g_gameStateManager);
    screenProcess(NEONengine::g_mainScreen);

    debug_keys_process();
}

void genericDestroy(void)
{
    stateManagerDestroy(g_gameStateManager);
    asset_cache_clear();
    screenDestroy(NEONengine::g_mainScreen);
    musicFree();
    ptplayerDestroy();
    mouseDestroy();
    keyDestroy();
    systemDestroy();

    // Run global/static destructors that registered with __cxa_atexit
    __cxa_finalize(nullptr);
}
