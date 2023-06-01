#include "screen.h"

#include <ace/managers/blit.h>
#include <ace/managers/mouse.h>
#include <ace/managers/viewport/simplebuffer.h>

#include "neonengine.h"

Screen *screenCreate(void)
{
    Screen *pScreen = memAllocFastClear(sizeof(Screen));
    if (pScreen)
    {
        pScreen->uwOffset = systemIsPal() ? 28 : 0;

        pScreen->pView = viewCreate(0,
            TAG_VIEW_GLOBAL_PALETTE, 1,
            TAG_VIEW_USES_AGA, 1,
        TAG_END);

        pScreen->pViewport = vPortCreate(0,
            TAG_VPORT_BPP, 8,
            TAG_VPORT_VIEW, pScreen->pView,
        TAG_END);

        pScreen->pBuffer = simpleBufferCreate(0,
            TAG_SIMPLEBUFFER_BITMAP_FLAGS, BMF_INTERLEAVED |  BMF_CLEAR,
            TAG_SIMPLEBUFFER_VPORT, pScreen->pViewport,
            TAG_SIMPLEBUFFER_IS_DBLBUF, 1,
        TAG_END);

        pScreen->pFade = fadeCreate(pScreen->pView, pScreen->pView->pFirstVPort->pPalette, 255);
    }

    return pScreen;
}

void screenDestroy(Screen *pScreen)
{
    // you don't need to destroy the viewport, it's destroyed by the view.
    if (pScreen)
    {
        simpleBufferDestroy(pScreen->pBuffer);
        viewDestroy(pScreen->pView);
        fadeDestroy(pScreen->pFade);
        memFree(pScreen, sizeof(Screen));
    }
}

void screenLoad(Screen *pScreen)
{
    BEGIN_UNUSE_SYSTEM
    viewLoad(pScreen->pView);
    END_UNUSE_SYSTEM
}

void screenProcess(Screen *pScreen)
{
    if (pScreen->pFade->eState != FADE_STATE_IDLE)
    {
        fadeProcess(pScreen->pFade);
    }

    vPortWaitForEnd(pScreen->pViewport);
}

void screenClear(Screen *pScreen, UBYTE ubColorIndex)
{
    blitRect(pScreen->pBuffer->pBack, 0, pScreen->uwOffset, SCREEN_WIDTH, SCREEN_HEIGHT, ubColorIndex);
}

void screenFadeToBlack(Screen *pScreen, UBYTE ubDuration, UBYTE ubFadeMusic, tCbFadeOnDone cbOnDone)
{
    fadeSet(pScreen->pFade, FADE_STATE_OUT, ubDuration, ubFadeMusic, cbOnDone);
}

void screenFadeFromBlack(Screen *pScreen, UBYTE ubDuration, UBYTE ubFadeMusic, tCbFadeOnDone cbOnDone)
{
    fadeSet(pScreen->pFade, FADE_STATE_IN, ubDuration, ubFadeMusic, cbOnDone);
}

void screenVwait(Screen *pScreen)
{
    vPortWaitForEnd(pScreen->pViewport);
}

void screenBindMouse(Screen *pScreen)
{
    // Set the bounds just slightly smaller so that the pointer is always visible
    mouseSetBounds(MOUSE_PORT_1,
        0, pScreen->uwOffset,
        SCREEN_WIDTH - 1, SCREEN_HEIGHT + pScreen->uwOffset - 1
    );
}
