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

    #if !defined(ROUND_16)
    #define ROUND_16(x) (((x) + 15) & ~15)
    #endif

    template <typename T>
    T *alloc(ULONG ulTotalSize, UBYTE ubFlags)
    {
        return reinterpret_cast<T *>(memAlloc(ulTotalSize, ubFlags));
    }

    template <typename T>
    T allocBufferFastClear(ULONG ulTotalSize)
    {
        return reinterpret_cast<T>(memAlloc(ulTotalSize, MEMF_FAST | MEMF_CLEAR));
    }

    template <typename T>
    T *allocFastAndClear()
    {
        return reinterpret_cast<T *>(memAlloc(sizeof(T), MEMF_ANY | MEMF_CLEAR));
    }
}

#endif //__NEONENGINE_H__INCLUDED__