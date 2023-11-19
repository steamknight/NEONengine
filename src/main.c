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
Screen *g_mainScreen;

void genericCreate(void)
{
    keyCreate();
    mouseCreate(MOUSE_PORT_1);
    ptplayerCreate(systemIsPal());

    g_gameStateManager = stateManagerCreate();
    g_mainScreen = screenCreate();
    screenLoad(g_mainScreen);
    screenBindMouse(g_mainScreen);

    statePush(g_gameStateManager, &g_stateFontTest);
}

void genericProcess(void)
{
    keyProcess();
    mouseProcess();
    ptplayerProcess();
    stateProcess(g_gameStateManager);
    screenProcess(g_mainScreen);
}

void genericDestroy(void)
{
    screenDestroy(g_mainScreen);
    musicFree();
    stateManagerDestroy(g_gameStateManager);
    ptplayerDestroy();
    mouseDestroy();
    keyDestroy();
}