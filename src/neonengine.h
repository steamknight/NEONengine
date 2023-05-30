#ifndef __NEONENGINE_H__INCLUDED__
#define __NEONENGINE_H__INCLUDED__

#include <ace/managers/state.h>
#include <ace/managers/system.h>
#include "core/screen.h"

extern tStateManager *g_gameStateManager;
extern tState
    g_stateSplash,
    g_stateLangSelect;

extern Screen *g_mainScreen;

#if !defined(SYSTEM_GUARDS)
    #define SYSTEM_GUARDS

    #define BEGIN_USE_SYSTEM \
        UBYTE was_using_system_ = systemIsUsed(); \
        if (!was_using_system_) { \
            systemUse(); \
        }

    #define END_USE_SYSTEM \
        if (!was_using_system_) { \
            systemUnuse(); \
        }

    #define BEGIN_UNUSE_SYSTEM \
        UBYTE was_using_system_ = systemIsUsed(); \
        if (was_using_system_) { \
            systemUnuse(); \
        }

    #define END_UNUSE_SYSTEM \
        if (was_using_system_) { \
            systemUse(); \
        }

#endif //!defined(SYSTEM_GUARDS)

#endif //__NEONENGINE_H__INCLUDED__