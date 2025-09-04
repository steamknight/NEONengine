#include "neonengine.h"

namespace NEONengine
{
    tStateManager *g_gameStateManager = nullptr;

    #ifdef ACE_TEST_RUNNER
    tState
        g_stateTestRunner = nullptr;
    #endif

    Screen* g_mainScreen = nullptr;

    GameData *g_gameData = nullptr;
}