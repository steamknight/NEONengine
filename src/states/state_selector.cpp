#include "neonengine.h"

#include <stdint.h>

#include <ace/managers/system.h>
#include <ace/managers/timer.h>

#include <ace++/bitmap.h>
#include <ace++/log.h>

#include "core/screen.h"
#include "core/text_render.h"

namespace NEONengine
{
    void stateSelectorCreate(void)
    {
        ACE_LOG_BLOCK("stateSelectorCreate");

        screenFadeFromBlack(g_mainScreen, 25, 0, nullptr);
        screenClear(g_mainScreen, 0);
    }

    void stateSelectorProcess(void) {}

    void stateSelectorDestroy(void)
    {

    }

    tState g_statestateSelector = {
        .cbCreate  = stateSelectorCreate,
        .cbLoop    = stateSelectorProcess,
        .cbDestroy = stateSelectorDestroy,
    };

}  // namespace NEONengine