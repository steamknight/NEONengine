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

    // Pushed here rather than by whoever asked for it. A bridge call runs deep
    // inside the running state's own callback, and pushing from there would
    // draw the box and then let the rest of that callback paint over it. 
    if (g_pEngine->has_pending_message())
    {
        statePush(g_gameStateManager, &g_stateMessage);
    }
    // Same reason, and never in the same frame as a message: two overlays would
    // freeze the display twice and the second would paint over the first.
    else if (g_pEngine->has_pending_dialogue())
    {
        statePush(g_gameStateManager, &g_stateDialogue);
    }

    stateProcess(g_gameStateManager);
    screenProcess(NEONengine::g_mainScreen);

    // The state stack already records whether the stats are up, so asking it is
    // both shorter and safe when something else - a message box - is on top.
    if (keyUse(KEY_F1))
    {
        tState const* current = g_gameStateManager->pCurrent;
        if (current == &g_stateScene)
        {
            statePush(g_gameStateManager, &g_stateDebugView);
        }
        else if (current == &g_stateDebugView)
        {
            statePop(g_gameStateManager);
        }
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

    // Reporting the cache walks the game data once per entry to work out what
    // each one is, so it stays on a key rather than running every load. A hitch
    // when it is pressed is the point of the trade.
    if (keyUse(KEY_F3))
    {
        asset_cache_log();
    }

    if (keyUse(KEY_F4))
    {
        asset_cache_clear();
        NE_LOG("asset cache cleared on request");
    }

    // ACE's own output buries ours on a busy frame, so it can be switched off
    // and back on while the game runs. ACE_DEBUG stays on either way, so the
    // checks that depend on it keep working.
    if (keyUse(KEY_F5))
    {
        log_mute_ace(!log_ace_muted());
        NE_LOG_ALWAYS("ACE logging %s", log_ace_muted() ? "muted" : "on");
    }
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
