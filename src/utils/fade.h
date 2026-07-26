/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _GERMZ_FADE_H_
#define _GERMZ_FADE_H_

#include <ace/utils/extview.h>

namespace NEONengine
{
    typedef enum _tFadeState {
        FADE_STATE_IN,
        FADE_STATE_OUT,
        FADE_STATE_PALETTE,
        FADE_STATE_IDLE,
        FADE_STATE_EVENT_FIRED
    } tFadeState;

    typedef void (*tCbFadeOnDone)(void);

    typedef struct _tFade {
        tFadeState eState;
        UWORD uwColorCount;
        UBYTE ubCnt;
        UBYTE ubCntEnd;
        UBYTE isMusic;
        UWORD uwExemptFirst;  ///< First palette entry the fade leaves alone.
        UWORD uwExemptCount;  ///< How many it leaves alone; 0 fades everything.
        UWORD* pPaletteRef;
        tCbFadeOnDone cbOnDone;
        tView *pView;
    } tFade;

    /**
     * @brief Creates a fade over @p pView's palette.
     *
     * AGA only: @p pView must have been created with TAG_VIEW_USES_AGA, so its
     * palette entries are 24-bit ULONGs behind a UWORD*.
     *
     * @param uwColorCount Entries the fade owns. A UWORD so the full 256-entry
     *                     AGA palette is expressible; a UBYTE stranded entry 255
     *                     at full brightness through every fade.
     * @param uwExemptFirst First entry to leave untouched.
     * @param uwExemptCount How many entries to leave untouched, 0 for none.
     *                      The hardware pointer draws from its own band and has
     *                      to stay readable exactly when the screen is black, so
     *                      that band is stepped over rather than dimmed.
     */
    tFade *fadeCreate(tView *pView, UWORD *pPalette, UWORD uwColorCount,
                      UWORD uwExemptFirst, UWORD uwExemptCount);

    void fadeDestroy(tFade *pFade);

    void fadeSet(
        tFade *pFade, tFadeState eState, UBYTE ubFramesToFullFade, UBYTE isMusic,
        tCbFadeOnDone cbOnDone
    );

    /**
     * @brief Processes fade-in or fade-out.
     * @param pFade Fade definition to be processed.
     *
     * @return Current fade state:
     * - FADE_STATE_EVENT_FIRED if fade is done, and OnDone event has just fired.
     * - FADE_STATE_IDLE if fade is done.
     */
    tFadeState fadeProcess(tFade *pFade);

    /**
     * @brief Chooses the window of palette entries the fade leaves alone.
     *
     * @param uwCount Entries to skip, 0 to fade the whole palette.
     */
    void fadeSetExempt(tFade *pFade, UWORD uwFirst, UWORD uwCount);
}

#endif // _GERMZ_FADE_H_
