#ifndef __NEONENGINE_H__INCLUDED__
#define __NEONENGINE_H__INCLUDED__

#include <ace/managers/state.h>
#include <ace/managers/system.h>
#include "core/screen.h"
#include "core/game_data.h"

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

extern Screen g_mainScreen;

extern GameData *g_gameData;

#if !defined(SAFE_CB_CALL)
#define SAFE_CB_CALL(fn, ...) \
    if (fn)                   \
        fn(__VA_ARGS__);
#endif // !defined (SAFE_CB_CALL)

#if !defined(ROUND_16)
#define ROUND_16(x) (((x) + 15) & ~15)
#endif

#endif //__NEONENGINE_H__INCLUDED__