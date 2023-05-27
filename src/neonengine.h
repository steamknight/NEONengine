#ifndef __NEONENGINE_H__INCLUDED__
#define __NEONENGINE_H__INCLUDED__

#include <ace/managers/state.h>
#include "utils/screen.h"

extern tStateManager *g_game_state_manager;
extern screen_t *g_main_screen;

#if !defined(SYSTEM_GUARDS)
    #define SYSTEM_GUARDS

    #define BEGIN_USE_SYSTEM \
        UBYTE using_system_ = systemIsUsed(); \
        if (!using_system_) { \
            systemUse(); \
        }

    #define END_USE_SYSTEM \
        if (using_system_) { \
            systemUnuse(); \
        }

    #define BEGIN_UNUSE_SYSTEM \
        UBYTE using_system_ = systemIsUsed(); \
        if (using_system_) { \
            systemUnuse(); \
        }

    #define END_UNUSE_SYSTEM \
        if (!using_system_) { \
            systemUse(); \
        }

#endif //!defined(SYSTEM_GUARDS)

#endif //__NEONENGINE_H__INCLUDED__