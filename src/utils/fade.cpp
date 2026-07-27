/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "fade.h"

#include "neonengine.h"

#include <ace/managers/ptplayer.h>
#include <ace/utils/palette.h>

#include <mtl/memory.h>

namespace NEONengine
{
    using namespace mtl;

    tFade *fadeCreate(tView *pView, UWORD *pPalette, UWORD uwColorCount,
                      UWORD uwExemptFirst, UWORD uwExemptCount)
    {
        logBlockBegin("fadeCreate(pView: %p, pPalette: %p, uwColorCount: %hu)",
                      pView,
                      pPalette,
                      uwColorCount);

        // AGA-only: the view is always created with TAG_VIEW_USES_AGA, so there
        // is no OCS/ECS path to fall back to.
        if (!(pView->uwFlags & VP_FLAG_AGA))
        {
            logWrite("ERR: fadeCreate() requires an AGA view");
        }

        // AGA palette entries are 24 bit, stored as one ULONG each behind a
        // UWORD* for the ACE API.
        UWORD const uwMaxColors = 1 << pView->pFirstVPort->ubBpp;
        if (uwColorCount > uwMaxColors)
        {
            logWrite("ERR: Unsupported palette size: %hu, max: %hu", uwColorCount, uwMaxColors);
            uwColorCount = uwMaxColors;
        }

        tFade *pFade         = new (MemF::Fast | MemF::Clear) tFade();
        pFade->eState        = FADE_STATE_IDLE;
        pFade->pView         = pView;
        pFade->uwColorCount  = uwColorCount;
        pFade->uwExemptFirst = uwExemptFirst;
        pFade->uwExemptCount = uwExemptCount;

        // The display is created holding the palette it was handed, so it starts
        // at full brightness. Anything that writes the display outside a fade has
        // to scale by this or it would paint at a brightness the rest of the
        // screen is not at.
        pFade->ubRatio = 255;
        pFade->pPaletteRef
            = (UWORD *)memAlloc(sizeof(ULONG) * uwMaxColors, MEMF_FAST | MEMF_CLEAR);

        auto *pRefEntries       = (ULONG *)pFade->pPaletteRef;
        auto const *pSrcEntries = (ULONG const *)pPalette;
        for (UWORD i = 0; i < uwColorCount; ++i) { pRefEntries[i] = pSrcEntries[i]; }
        logBlockEnd("fadeCreate()");
        return pFade;
    }

    void fadeDestroy(tFade *pFade)
    {
        memFree(pFade->pPaletteRef, sizeof(ULONG) * (1 << pFade->pView->pFirstVPort->ubBpp));
        memFree(pFade, sizeof(*pFade));
    }

    void fadeSet(tFade *pFade,
                 tFadeState eState,
                 UBYTE ubFramesToFullFade,
                 UBYTE isMusic,
                 tCbFadeOnDone cbOnDone)
    {
        logBlockBegin("fadeSet(pFade: %p, eState: %d, ubFramesToFullFade: %hhu, cbOnDone: %p)",
                      pFade,
                      eState,
                      ubFramesToFullFade,
                      cbOnDone);
        pFade->eState   = eState;
        pFade->ubCnt    = 0;
        // A zero-frame fade divides by zero in fadeProcess; clamp to one instant
        // frame so the fade still completes and fires cbOnDone.
        pFade->ubCntEnd = ubFramesToFullFade ? ubFramesToFullFade : 1;
        pFade->cbOnDone = cbOnDone;
        pFade->isMusic  = isMusic;
        logBlockEnd("fadeSet()");
    }

    tFadeState fadeProcess(tFade *pFade)
    {
        tFadeState eState = pFade->eState;
        if (pFade->eState != FADE_STATE_IDLE && pFade->eState != FADE_STATE_EVENT_FIRED)
        {
            ++pFade->ubCnt;

            UBYTE ubCnt = pFade->ubCnt;
            if (pFade->eState == FADE_STATE_OUT) { ubCnt = pFade->ubCntEnd - pFade->ubCnt; }

            UBYTE ubRatio = (255 * ubCnt) / pFade->ubCntEnd;
            pFade->ubRatio = ubRatio;
            auto *pRef    = (ULONG *)pFade->pPaletteRef;
            auto *pLive   = (ULONG *)pFade->pView->pFirstVPort->pPalette;

            // The hardware pointer draws from the exempt band. Dimming it
            // would black the cursor out exactly while a load blocks the
            // loop with the screen already faded down, so the fade steps
            // over the band and leaves it at its authored brightness.
            UWORD const uwExemptEnd = pFade->uwExemptFirst + pFade->uwExemptCount;
            if (pFade->uwExemptFirst) { paletteDimAga(pRef, pLive, pFade->uwExemptFirst, ubRatio); }
            if (uwExemptEnd < pFade->uwColorCount)
            {
                paletteDimAga(pRef + uwExemptEnd,
                              pLive + uwExemptEnd,
                              pFade->uwColorCount - uwExemptEnd,
                              ubRatio);
            }
            viewUpdateGlobalPalette(pFade->pView);

            if (pFade->isMusic)
            {
                UBYTE ubVolume = (64 * ubCnt) / pFade->ubCntEnd;
                ptplayerSetMasterVolume(ubVolume);
            }

            if (pFade->ubCnt >= pFade->ubCntEnd)
            {
                pFade->eState = FADE_STATE_EVENT_FIRED;
                // Save state for return incase fade object gets destroyed in fade cb
                eState = pFade->eState;
                if (pFade->cbOnDone)
                {
                    pFade->eState = FADE_STATE_IDLE;
                    pFade->cbOnDone();
                }
            }
        }
        else
        {
            pFade->eState = FADE_STATE_IDLE;
            eState        = pFade->eState;
        }
        return eState;
    }
    void fadeSetExempt(tFade *pFade, UWORD uwFirst, UWORD uwCount)
    {
        pFade->uwExemptFirst = uwFirst;
        pFade->uwExemptCount = uwCount;
    }
}  // namespace NEONengine
