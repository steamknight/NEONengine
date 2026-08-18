#include "screen.h"

#include "types.h"

#include "neonengine.h"

#include <ace/managers/blit.h>
#include <ace/managers/bob.h>
#include <ace/managers/mouse.h>
#include <ace/managers/viewport/simplebuffer.h>
#include <ace/utils/font.h>
#include <ace/utils/palette.h>

#include <bartman/gcc8_c_support.h>

namespace NEONengine
{
    constexpr int BIT_DEPTH   = 8;
    // Every AGA entry, so nothing is stranded at full brightness through a fade.
    constexpr int MAX_COLORS  = 256;
    constexpr int PAL_OFFSET  = 28;
    constexpr int NTSC_OFFSET = 0;

    screen::screen()
        : pView(nullptr), pViewport(nullptr), pBuffer(nullptr), pFade(nullptr), uwOffset(0), pHeldBack(nullptr)
    {
        uwOffset = systemIsPal() ? PAL_OFFSET : NTSC_OFFSET;

        pView = viewCreate(0, TAG_VIEW_GLOBAL_PALETTE, TRUE, TAG_VIEW_USES_AGA, TRUE, TAG_END);
        if (!pView) return;

        pViewport = vPortCreate(0,
            TAG_VPORT_BPP,                  BIT_DEPTH,
            TAG_VPORT_USES_AGA,             TRUE,
            TAG_VPORT_FMODE,                3,
            TAG_VPORT_VIEW,                 pView,
            TAG_END);

        pBuffer = simpleBufferCreate(0,
            TAG_SIMPLEBUFFER_BITMAP_FLAGS,  BMF_INTERLEAVED | BMF_CLEAR,
            TAG_SIMPLEBUFFER_VPORT,         pViewport,
            TAG_SIMPLEBUFFER_IS_DBLBUF,     TRUE,
            TAG_END);

        pFade = fadeCreate(pView,
            pView->pFirstVPort->pPalette,
            MAX_COLORS,
            CURSOR_PALETTE_FIRST,
            CURSOR_PALETTE_COUNT);

        bobManagerCreate(
            pBuffer->pFront, pBuffer->pBack, pBuffer->pBack->Rows);
    }

    screen::~screen()
    {
        if (!pView) return;

        bobManagerDestroy();
        viewDestroy(pView); // This also destroys the simple buffer.
        fadeDestroy(pFade);
    }

    void screen::load()
    {
        UBYTE was_using_system = systemIsUsed();
        if (was_using_system)
        {
            systemUnuse();
        }

        viewLoad(pView);

        if (was_using_system)
        {
            systemUse();
        }
    }

    void screen::fade_to_black(UBYTE ubDuration,
        UBYTE ubFadeMusic,
        tCbFadeOnDone cbOnDone)
    {
        fadeSet(pFade, FADE_STATE_OUT, ubDuration, ubFadeMusic, cbOnDone);
    }

    void screen::fade_from_black(UBYTE ubDuration,
        UBYTE ubFadeMusic,
        tCbFadeOnDone cbOnDone)
    {
        fadeSet(pFade, FADE_STATE_IN, ubDuration, ubFadeMusic, cbOnDone);
    }

    void screen::process()
    {
        if (pFade->eState != FADE_STATE_IDLE)
        {
            fadeProcess(pFade);
        }

        viewProcessManagers(pView);
        copProcessBlocks();

        debug_start_idle();
        vPortWaitForEnd(pViewport);
        debug_stop_idle();
    }

    void screen::freeze_display()
    {
        if (pHeldBack) return;

        pHeldBack      = pBuffer->pBack;
        pBuffer->pBack = pBuffer->pFront;
    }

    void screen::resume_display()
    {
        if (!pHeldBack) return;

        pBuffer->pBack = pHeldBack;
        pHeldBack      = nullptr;
    }

    void screen::restore_rect(WORD wX, WORD wY, WORD wWidth, WORD wHeight)
    {
        if (!pHeldBack) return;
        if (wWidth <= 0 || wHeight <= 0) return;

        blitCopy(pHeldBack,
            wX,
            wY + uwOffset,
            pBuffer->pBack,
            wX,
            wY + uwOffset,
            wWidth,
            wHeight,
            MINTERM_COOKIE);
    }

    void screen::duplicate_back_to_front()
    {
        tBitMap* pBack  = pBuffer->pBack;
        tBitMap* pFront = pBuffer->pFront;
        blitCopy(pBack, 0, 0, pFront, 0, 0, SCREEN_WIDTH, pBack->Rows, MINTERM_COPY);
    }

    void screen::blit_copy(tBitMap const* pSrc,
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
            pBuffer->pBack,
            wDstX,
            wDstY + uwOffset,
            wWidth,
            wHeight,
            ubMinterm);
    }

    void screen::blit_copy_mask(tBitMap const* pSrc,
        WORD wSrcX,
        WORD wSrcY,
        WORD wDstX,
        WORD wDstY,
        WORD wWidth,
        WORD wHeight,
        UBYTE const* pMsk)
    {
        blitCopyMask(pSrc,
            wSrcX,
            wSrcY,
            pBuffer->pBack,
            wDstX,
            wDstY + uwOffset,
            wWidth,
            wHeight,
            pMsk);
    }

    void screen::text_copy(tTextBitMap* pTextBitMap,
        UWORD uwX,
        UWORD uwY,
        UBYTE ubColor,
        UBYTE ubFlags)
    {
        fontDrawTextBitMap(pBuffer->pBack, pTextBitMap, uwX, uwY + uwOffset, ubColor, ubFlags);
    }

    void screen::clear(UBYTE ubColorIndex)
    {
        blitRect(pBuffer->pBack, 0, uwOffset, SCREEN_WIDTH, SCREEN_HEIGHT, ubColorIndex);
    }

    bool screen::is_fading_out() const
    {
        return pFade->eState == FADE_STATE_OUT;
    }

    bool screen::fade_owns_palette() const
    {
        return pFade->eState != FADE_STATE_IDLE && pFade->eState != FADE_STATE_EVENT_FIRED;
    }

    UBYTE screen::palette_ratio() const
    {
        return pFade->ubRatio;
    }

    void screen::publish_palette(u16 first, u16 count)
    {
        auto const* pRef = force_to<ULONG const*>(pFade->pPaletteRef);
        auto* pLive      = force_to<ULONG*>(pView->pFirstVPort->pPalette);

        UBYTE const ratio = pFade->ubRatio;
        for (u16 entry = first; entry < first + count; ++entry)
        {
            pLive[entry] = paletteColorDimAga(pRef[entry], ratio);
        }

        viewUpdateGlobalPalette(pView);
    }

    void screen::set_cursor_band_exempt(bool exempt)
    {
        fadeSetExempt(pFade, CURSOR_PALETTE_FIRST, exempt ? CURSOR_PALETTE_COUNT : UWORD(0));
        if (!exempt) return;

        auto const* pRef = force_to<ULONG const*>(pFade->pPaletteRef);
        auto* pLive      = force_to<ULONG*>(pView->pFirstVPort->pPalette);
        for (UWORD i = 0; i < CURSOR_PALETTE_COUNT; ++i)
        {
            pLive[CURSOR_PALETTE_FIRST + i] = pRef[CURSOR_PALETTE_FIRST + i];
        }
        viewUpdateGlobalPalette(pView);
    }

    void screen::vwait()
    {
        debug_start_idle();
        vPortWaitForEnd(pViewport);
        debug_stop_idle();
    }

    void screen::bind_mouse()
    {
        mouseSetBounds(MOUSE_PORT_1,
            0,
            uwOffset,
            SCREEN_WIDTH - 1,
            SCREEN_HEIGHT + uwOffset - 1);
    }

    tView* screen::view() const { return pView; }
    UWORD screen::offset() const { return uwOffset; }
    tBitMap* screen::back_buffer() const { return pBuffer ? pBuffer->pBack : nullptr; }
    tBitMap* screen::front_buffer() const { return pBuffer ? pBuffer->pFront : nullptr; }
    UWORD* screen::palette() { return pFade ? force_to<UWORD*>(pFade->pPaletteRef) : nullptr; }
    UWORD const* screen::palette() const { return pFade ? force_to<UWORD const*>(pFade->pPaletteRef) : nullptr; }


}  // namespace NEONengine
