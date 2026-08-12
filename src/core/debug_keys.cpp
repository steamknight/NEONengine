#include "core/debug_keys.h"

#include "neonengine.h"

#include <ace/managers/key.h>
#include <ace/managers/memory.h>
#include <ace/managers/state.h>

#include "core/asset_cache.h"
#include "core/logger.h"
#include "types.h"

NE_LOG_DOMAIN(Core);

namespace NEONengine
{
    namespace
    {
        /** @brief Puts the stats overlay up, or takes it down. */
        void toggle_debug_view()
        {
            // The state stack already records whether the stats are up, so
            // asking it is both shorter and safe when something else - a
            // message box - is on top.
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

        /** @brief Reports what is left of each kind of memory. */
        void log_free_memory()
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
    }  // namespace

    void debug_keys_process()
    {
        if (keyUse(KEY_F1)) toggle_debug_view();

        if (keyUse(KEY_F2)) log_free_memory();

        // Reporting the cache walks the game data once per entry to work out
        // what each one is, so it stays on a key rather than running every
        // load. A hitch when it is pressed is the point of the trade.
        if (keyUse(KEY_F3)) asset_cache_log();

        if (keyUse(KEY_F4))
        {
            asset_cache_clear();
            NE_LOG("asset cache cleared on request");
        }

        // ACE's own output buries ours on a busy frame, so it can be switched
        // off and back on while the game runs. ACE_DEBUG stays on either way,
        // so the checks that depend on it keep working.
        if (keyUse(KEY_F5))
        {
            log_mute_ace(!log_ace_muted());
            NE_LOG_ALWAYS("ACE logging %s", log_ace_muted() ? "muted" : "on");
        }
    }
}  // namespace NEONengine
