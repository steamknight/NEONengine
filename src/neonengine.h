#ifndef __NEONENGINE_H__INCLUDED__
#define __NEONENGINE_H__INCLUDED__

#include <ace/managers/state.h>
#include <ace/managers/system.h>
#include "core/screen.h"
#include "core/game_data.h"

namespace NEONengine
{
    extern tStateManager *g_gameStateManager;
    extern tState
        g_stateFontTest,
        g_stateDebugView,
        g_stateSplash,
        g_stateLangSelect,
        g_stateDialogueTest;

    #ifdef ACE_TEST_RUNNER
    extern tState
        g_stateTestRunner;
    #endif

    extern Screen* g_mainScreen;

    extern GameData* g_gameData;

    #if !defined(SAFE_CB_CALL)
    #define SAFE_CB_CALL(fn, ...) \
        if (fn)                   \
            fn(__VA_ARGS__);
    #endif // !defined (SAFE_CB_CALL)

}

#endif //__NEONENGINE_H__INCLUDED__