#include <ace/generic/main.h>
#include <ace/managers/key.h>
#include <ace/managers/mouse.h>
#include <ace/managers/state.h>
#include <ace/managers/ptplayer.h>

#include "neonengine.h"
#include "build_number.h"
#include "core/music.h"
#include "core/game_data.h"

tStateManager *g_gameStateManager;
Screen g_mainScreen;

UBYTE ubDebugToggle = 1;

void genericCreate(void)
{
    keyCreate();
    mouseCreate(MOUSE_PORT_1);
    ptplayerCreate(systemIsPal());

    g_gameStateManager = stateManagerCreate();
    g_mainScreen = screenCreate();
    screenLoad(g_mainScreen);
    screenBindMouse(g_mainScreen);

#ifdef ACE_TEST_RUNNER
    statePush(g_gameStateManager, &g_stateTestRunner);
#else
    statePush(g_gameStateManager, &g_stateFontTest);
#endif
}

void genericProcess(void)
{
    keyProcess();
    mouseProcess();
    ptplayerProcess();
    stateProcess(g_gameStateManager);
    screenProcess(g_mainScreen);

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
}

void genericDestroy(void)
{
    screenDestroy(g_mainScreen);
    musicFree();
    stateManagerDestroy(g_gameStateManager);
    ptplayerDestroy();
    mouseDestroy();
    keyDestroy();
    systemDestroy();
}