#include "screen.h"

#include <ace/managers/blit.h>
#include <ace/managers/mouse.h>
#include <ace/managers/viewport/simplebuffer.h>

#include "neonengine.h"

#define BIT_DEPTH 8
#define MAX_COLORS 255
#define PAL_OFFSET 28
#define NTSC_OFFSET 0

struct Screen
{
    tView *pView;
    tVPort *pViewport;
    tSimpleBufferManager *pBuffer;
    tFade *pFade;
    UWORD uwOffset;
};

Screen screenCreate(void)
{
    Screen screen = memAllocFastClear(sizeof(struct Screen));
    if (screen)
    {
        screen->uwOffset = systemIsPal() ? PAL_OFFSET : NTSC_OFFSET;

        screen->pView = viewCreate(0,
            TAG_VIEW_GLOBAL_PALETTE, TRUE,
            TAG_VIEW_USES_AGA, TRUE,
        TAG_END);

        screen->pViewport = vPortCreate(0,
            TAG_VPORT_BPP, BIT_DEPTH,
            TAG_VPORT_VIEW, screen->pView,
        TAG_END);

        screen->pBuffer = simpleBufferCreate(0,
            TAG_SIMPLEBUFFER_BITMAP_FLAGS, BMF_INTERLEAVED | BMF_CLEAR,
            TAG_SIMPLEBUFFER_VPORT, screen->pViewport,
            TAG_SIMPLEBUFFER_IS_DBLBUF, TRUE,
        TAG_END);

        screen->pFade = fadeCreate(screen->pView, screen->pView->pFirstVPort->pPalette, MAX_COLORS);
    }

    return screen;
}

void screenDestroy(Screen screen)
{
    // you don't need to destroy the viewport, it's destroyed by the view.
    if (screen)
    {
        simpleBufferDestroy(screen->pBuffer);
        viewDestroy(screen->pView);
        fadeDestroy(screen->pFade);
        memFree(screen, sizeof(Screen));
    }
}

void screenLoad(Screen screen)
{
    UBYTE was_using_system = systemIsUsed();
    if (was_using_system)
    {
        systemUnuse();
    }

    viewLoad(screen->pView);

    if (was_using_system)
    {
        systemUse();
    }
}

void screenProcess(Screen screen)
{
    if (screen->pFade->eState != FADE_STATE_IDLE)
    {
        fadeProcess(screen->pFade);
    }

    viewProcessManagers(screen->pView);
    copProcessBlocks();

    simpleBufferProcess(screen->pBuffer);

    vPortWaitForEnd(screen->pViewport);
}

void screenClear(Screen screen, UBYTE ubColorIndex)
{
    blitRect(screen->pBuffer->pBack, 0, screen->uwOffset, SCREEN_WIDTH, SCREEN_HEIGHT, ubColorIndex);
}

void screenFadeToBlack(Screen screen, UBYTE ubDuration, UBYTE ubFadeMusic, tCbFadeOnDone cbOnDone)
{
    fadeSet(screen->pFade, FADE_STATE_OUT, ubDuration, ubFadeMusic, cbOnDone);
}

void screenFadeFromBlack(Screen screen, UBYTE ubDuration, UBYTE ubFadeMusic, tCbFadeOnDone cbOnDone)
{
    fadeSet(screen->pFade, FADE_STATE_IN, ubDuration, ubFadeMusic, cbOnDone);
}

void screenVwait(Screen screen)
{
    vPortWaitForEnd(screen->pViewport);
}

void screenBindMouse(Screen screen)
{
    // Set the bounds just slightly smaller so that the pointer is always visible
    mouseSetBounds(MOUSE_PORT_1,
        0, screen->uwOffset,
        SCREEN_WIDTH - 1, SCREEN_HEIGHT + screen->uwOffset - 1
    );
}

void screenToScreenSpace(Screen screen, tUwRect *pRect)
{
    pRect->uwY += screen->uwOffset;
}

inline tView* screenGetView(Screen screen)
{
    return screen->pView;
}

inline tBitMap* screenGetBackBuffer(Screen screen)
{
    return screen->pBuffer->pBack;
}

inline tBitMap* screenGetFrontBuffer(Screen screen)
{
    return screen->pBuffer->pFront;
}

inline UWORD* screenGetPalette(Screen screen)
{
    return screen->pFade->pPaletteRef;
}

void screenBlitCopy(Screen screen, const tBitMap *pSrc, WORD wSrcX, WORD wSrcY,
    WORD wDstX, WORD wDstY, WORD wWidth, WORD wHeight,
    UBYTE ubMinterm)
{
    blitCopy(
        pSrc, wSrcX, wSrcY,
        screen->pBuffer->pBack, wDstX, wDstY + screen->uwOffset,
        wWidth, wHeight, ubMinterm
    );
}