#include "neonengine.h"

#include <printf.h>

#include <proto/exec.h> // Bartman's compiler needs this

#include <ace/managers/system.h>
#include <ace/managers/timer.h>
#include <ace/utils/font.h>
#include <ace/utils/palette.h>

#include "core/screen.h"

namespace NEONengine
{
    static tFont* s_pFont;
    static tTextBitMap* s_pTextBmp;
    static tTextBitMap* s_pElapsedTimeBmp;
    static char s_memSize[64];
    static char s_elapsedTime[32];
    static ULONG s_ulDelta = 0;
    static ULONG s_ulFps;

    #define DELAY 50

    void debugViewCreate(void)
    {
        logBlockBegin("debugViewCreate");

        s_pFont = fontCreateFromPath("data/font.fnt");

        s_pTextBmp = fontCreateTextBitMap(160, s_pFont->uwHeight * 3);
        s_pElapsedTimeBmp = fontCreateTextBitMap(160, s_pFont->uwHeight);
        s_ulDelta = timerGet();

        s_ulFps = systemIsPal() ? 50 : 60;

        logBlockEnd("debugViewCreate");
    }

    void debugViewProcess(void)
    {
        ULONG ulNow = timerGet();
        ULONG ulDelta = timerGetDelta(s_ulDelta, ulNow);

        if (ulDelta < DELAY) return;

        s_ulDelta = ulNow;

        sprintf(s_memSize, "Chip: %ld KB \nFast: %ld KB \nAny:  %ld KB ", memGetFreeChipSize() >> 10, AvailMem(MEMF_FAST) >> 10, AvailMem(MEMF_ANY) >> 10);
        //sprintf(s_memSize, "Chip: %ld KB ", memGetChipSize() >> 10);
        fontFillTextBitMap(s_pFont, s_pTextBmp, s_memSize);

        sprintf(s_elapsedTime, "Elapsed Time: %lds.", ulNow / s_ulFps);
        fontFillTextBitMap(s_pFont, s_pElapsedTimeBmp, s_elapsedTime);

        blitRect(screenGetBackBuffer(g_mainScreen), 0, 0, s_pTextBmp->uwActualWidth, s_pTextBmp->uwActualHeight, 0);
        fontDrawTextBitMap(screenGetBackBuffer(g_mainScreen), s_pTextBmp, 0, 0, 24, FONT_COOKIE);

        blitRect(screenGetBackBuffer(g_mainScreen), 0, SCREEN_HEIGHT - s_pElapsedTimeBmp->uwActualHeight, s_pElapsedTimeBmp->uwActualWidth, s_pElapsedTimeBmp->uwActualHeight, 0);
        fontDrawTextBitMap(screenGetBackBuffer(g_mainScreen), s_pElapsedTimeBmp, 0, SCREEN_HEIGHT - s_pElapsedTimeBmp->uwActualHeight, 24, FONT_COOKIE);
    }

    void debugViewDestroy(void)
    {
        fontDestroy(s_pFont);
        fontDestroyTextBitMap(s_pTextBmp);
        fontDestroyTextBitMap(s_pElapsedTimeBmp);
    }

    tState g_stateDebugView = {
        .cbCreate = debugViewCreate,
        .cbLoop = debugViewProcess,
        .cbDestroy = debugViewDestroy,
    };
}