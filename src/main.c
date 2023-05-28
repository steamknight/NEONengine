#include <ace/generic/main.h>
#include <ace/managers/key.h>
#include <ace/managers/mouse.h>
#include <ace/managers/state.h>
#include <ace/managers/ptplayer.h>

#include "neonengine.h"
#include "core/music.h"

tStateManager *g_game_state_manager;
screen_t *g_main_screen;

void genericCreate(void)
{
    keyCreate();
    mouseCreate(MOUSE_PORT_1);
    mouseSetBounds(MOUSE_PORT_1, 0, 0, 320, 200);
    ptplayerCreate(systemIsPal());

    g_game_state_manager = stateManagerCreate();
    g_main_screen = screen_create();
    screen_load(g_main_screen);

    statePush(g_game_state_manager, &g_state_splash);
}

void genericProcess(void)
{
    keyProcess();
    mouseProcess();
    ptplayerProcess();
    stateProcess(g_game_state_manager);
    screen_process(g_main_screen);
}

void genericDestroy(void)
{
    music_free();
    stateManagerDestroy(g_game_state_manager);
    ptplayerDestroy();
    mouseDestroy();
    keyDestroy();
}