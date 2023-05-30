#include "neonengine.h"

#include <ace/managers/system.h>
#include <ace/managers/blit.h>
#include <ace/managers/viewport/simplebuffer.h>
#include <ace/utils/palette.h>
#include <ace/managers/mouse.h>
#include "core/screen.h"
#include "core/mouse_pointer.h"

#define STATE_NAME "State: Language Selection"
#define FADE_DURATION 25

//static Layer *flags_layer_;

void langSelectCreate(void)
{
    logBlockBegin(STATE_NAME);
    screenFadeFromBlack(g_mainScreen, FADE_DURATION, 0, NULL);
    screenClear(g_mainScreen, 0);

    paletteLoad("data/core/base.plt", g_mainScreen->pFade->pPaletteRef, 255);
    tBitMap *pFlags = bitmapCreateFromFile("data/core/flags.bm", 0);
    blitCopyAligned(
        pFlags, 0, 0,
        g_mainScreen->pBuffer->pBack, 0, g_mainScreen->uwOffset,
        80, 128
    );
    bitmapDestroy(pFlags);

    mousePointerCreate("data/core/pointers.bm");
//    flags_layer_ = layerCreate();
}

void langSelectProcess(void)
{
    static eMousePointer currentPointerGfx = MOUSE_POINTER;
    mousePointerUpdate();

    // Added to Test the mouse pointer code.
    // And sprite bitmap switching for AGA - VAIRN.
    if(mouseUse(MOUSE_PORT_1, MOUSE_LMB))
    {
        currentPointerGfx +=1;
        if(currentPointerGfx == MOUSE_MAX_COUNT)
        {
            currentPointerGfx = MOUSE_POINTER;
        }

        mousePointerSwitch(currentPointerGfx);
    }
}

void langSelectDestroy(void)
{
    logBlockEnd(STATE_NAME);
    mousePointerDestroy();
//    layerDestroy(flags_layer_);
}

tState g_stateLangSelect = {
    .cbCreate = langSelectCreate,
    .cbLoop = langSelectProcess,
    .cbDestroy = langSelectDestroy,
};