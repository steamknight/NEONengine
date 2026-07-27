#include "screen.h"

#include "neonengine.h"

#include <ace/managers/blit.h>
#include <ace/managers/mouse.h>
#include <ace/managers/viewport/simplebuffer.h>
#include <ace/utils/font.h>

#include <bartman/gcc8_c_support.h>

namespace NEONengine
{
    constexpr int BIT_DEPTH   = 8;
    // Every AGA entry, so nothing is stranded at full brightness through a fade.
    constexpr int MAX_COLORS  = 256;
    constexpr int PAL_OFFSET  = 28;
    constexpr int NTSC_OFFSET = 0;

    struct Screen
    {
        tView* pView;
        tVPort* pViewport;
        tSimpleBufferManager* pBuffer;
        tFade* pFade;
        UWORD uwOffset;
    };

    Screen* screenCreate()
    {
        Screen* screen = reinterpret_cast<Screen*>(memAllocFastClear(sizeof(Screen)));
        if (screen)
        {
            screen->uwOffset = systemIsPal() ? PAL_OFFSET : NTSC_OFFSET;

            screen->pView
                = viewCreate(0, TAG_VIEW_GLOBAL_PALETTE, TRUE, TAG_VIEW_USES_AGA, TRUE, TAG_END);

            screen->pViewport = vPortCreate(0,
                TAG_VPORT_BPP,
                BIT_DEPTH,
                TAG_VPORT_USES_AGA,
                TRUE,
                TAG_VPORT_FMODE,
                3,
                TAG_VPORT_VIEW,
                screen->pView,
                TAG_END);

            screen->pBuffer = simpleBufferCreate(0,
                TAG_SIMPLEBUFFER_BITMAP_FLAGS,
                BMF_INTERLEAVED | BMF_CLEAR,
                TAG_SIMPLEBUFFER_VPORT,
                screen->pViewport,
                TAG_SIMPLEBUFFER_IS_DBLBUF,
                TRUE,
                TAG_END);

            screen->pFade = fadeCreate(screen->pView,
                screen->pView->pFirstVPort->pPalette,
                MAX_COLORS,
                CURSOR_PALETTE_FIRST,
                CURSOR_PALETTE_COUNT);
        }

        return screen;
    }

    void screenDestroy(Screen* screen)
    {
        // you don't need to destroy the viewport, it's destroyed by the view.
        if (screen)
        {
            simpleBufferDestroy(screen->pBuffer);
            viewDestroy(screen->pView);
            fadeDestroy(screen->pFade);
            // Nothing may touch the struct past this point. Clearing the
            // caller's own pointer is the caller's job; doing it here only ever
            // cleared this function's copy of it.
            memFree(screen, sizeof(Screen));
        }
    }

    void screenLoad(Screen* screen)
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

    void screenProcess(Screen* screen)
    {
        if (screen->pFade->eState != FADE_STATE_IDLE)
        {
            fadeProcess(screen->pFade);
        }

        // viewProcessManagers() runs the simple buffer manager, which swaps the
        // double buffer once per frame. Calling simpleBufferProcess() again here
        // would swap a second time, leaving the back buffer unchanged for drawing
        // (and permanently desyncing the bob manager).
        viewProcessManagers(screen->pView);
        copProcessBlocks();

        debug_start_idle();
        vPortWaitForEnd(screen->pViewport);
        debug_stop_idle();
    }

    void screenClear(Screen* screen, UBYTE ubColorIndex)
    {
        blitRect(
            screen->pBuffer->pBack, 0, screen->uwOffset, SCREEN_WIDTH, SCREEN_HEIGHT, ubColorIndex);
    }

    void screenFadeToBlack(Screen* screen,
        UBYTE ubDuration,
        UBYTE ubFadeMusic,
        tCbFadeOnDone cbOnDone)
    {
        fadeSet(screen->pFade, FADE_STATE_OUT, ubDuration, ubFadeMusic, cbOnDone);
    }

    bool screenIsFadingOut(Screen* screen)
    {
        return screen->pFade->eState == FADE_STATE_OUT;
    }

    bool screenFadeOwnsPalette(Screen* screen)
    {
        // The two states fadeProcess() steps over. Asked before screenProcess()
        // runs, so this is what the fade is about to do this frame, not what it
        // did last one.
        return screen->pFade->eState != FADE_STATE_IDLE
            && screen->pFade->eState != FADE_STATE_EVENT_FIRED;
    }

    UBYTE screenPaletteRatio(Screen* screen)
    {
        return screen->pFade->ubRatio;
    }

    void screenFadeFromBlack(Screen* screen,
        UBYTE ubDuration,
        UBYTE ubFadeMusic,
        tCbFadeOnDone cbOnDone)
    {
        fadeSet(screen->pFade, FADE_STATE_IN, ubDuration, ubFadeMusic, cbOnDone);
    }

    void screenSetCursorBandExempt(Screen* screen, bool exempt)
    {
        fadeSetExempt(
            screen->pFade, CURSOR_PALETTE_FIRST, exempt ? CURSOR_PALETTE_COUNT : UWORD(0));
        if (!exempt) return;

        // Held out of the fade from here on, so the live entries have to be
        // given their colors now: nothing else writes them, and they would
        // otherwise keep whatever brightness the last fade happened to leave.
        auto const* pRef = reinterpret_cast<ULONG const*>(screen->pFade->pPaletteRef);
        auto* pLive      = reinterpret_cast<ULONG*>(screen->pView->pFirstVPort->pPalette);
        for (UWORD i = 0; i < CURSOR_PALETTE_COUNT; ++i)
        {
            pLive[CURSOR_PALETTE_FIRST + i] = pRef[CURSOR_PALETTE_FIRST + i];
        }
        viewUpdateGlobalPalette(screen->pView);
    }

    void screenVwait(Screen* screen)
    {
        debug_start_idle();
        vPortWaitForEnd(screen->pViewport);
        debug_stop_idle();
    }

    void screenBindMouse(Screen* screen)
    {
        // Set the bounds just slightly smaller so that the pointer is always visible
        mouseSetBounds(MOUSE_PORT_1,
            0,
            screen->uwOffset,
            SCREEN_WIDTH - 1,
            SCREEN_HEIGHT + screen->uwOffset - 1);
    }

    void screenToScreenSpace(Screen* screen, tUwRect* pRect)
    {
        pRect->uwY += screen->uwOffset;
    }

    tView* screenGetView(Screen* screen)
    {
        return screen->pView;
    }

    tBitMap* screenGetBackBuffer(Screen* screen)
    {
        return screen->pBuffer->pBack;
    }

    tBitMap* screenGetFrontBuffer(Screen* screen)
    {
        return screen->pBuffer->pFront;
    }

    UWORD* screenGetPalette(Screen* screen)
    {
        return screen->pFade->pPaletteRef;
    }

    UWORD screenGetVerticalOffset(Screen* screen)
    {
        return screen->uwOffset;
    }

    void screenDuplicateBackToFront(Screen* screen)
    {
        tBitMap* pBack  = screen->pBuffer->pBack;
        tBitMap* pFront = screen->pBuffer->pFront;
        blitCopy(pBack, 0, 0, pFront, 0, 0, SCREEN_WIDTH, pBack->Rows, MINTERM_COPY);
    }

    void screenBlitCopy(Screen* screen,
        tBitMap const* pSrc,
        WORD wSrcX,
        WORD wSrcY,
        WORD wDstX,
        WORD wDstY,
        WORD wWidth,
        WORD wHeight,
        UBYTE ubMinterm)
    {
        blitCopy(pSrc,
            wSrcX,
            wSrcY,
            screen->pBuffer->pBack,
            wDstX,
            wDstY + screen->uwOffset,
            wWidth,
            wHeight,
            ubMinterm);
    }

    void screenTextCopy(Screen* screen,
        tTextBitMap* pTextBitMap,
        UWORD uwX,
        UWORD uwY,
        UBYTE ubColor,
        UBYTE ubFlags)
    {
        fontDrawTextBitMap(
            screen->pBuffer->pBack, pTextBitMap, uwX, uwY + screen->uwOffset, ubColor, ubFlags);
    }
}  // namespace NEONengine