#include "neonengine.h"

#include <ace/managers/blit.h>
#include <ace/managers/system.h>
#include <ace/utils/bitmap.h>
#include <ace/managers/viewport/simplebuffer.h>
#include <ace/managers/timer.h>
#include <ace/utils/font.h>
#include <ace/utils/palette.h>

#include "core/screen.h"
#include "core/text_render.h"
#include "core/nine_patch.h"
#include "core/music.h"

#include "utils/bstr_view.h"

namespace NEONengine 
{
    void dialogueTestCreate(void)
    {
        logBlockBegin("dialogueTestCreate");

        screenFadeFromBlack(g_mainScreen, 25, 0, NULL);
        screenClear(g_mainScreen, 0);

        paletteLoadFromPath("data/core/base.plt", screenGetPalette(g_mainScreen), 255);
        textRendererCreate("data/font.fnt");

        tBitMap* pPatchBitmap = bitmapCreateFromPath("data/core/frame_9.bm", 0);

        bstr_view text = "I'm the love child of Icarus and Sisyphus; no matter how hard I try to rise above, my hubris crashes me face first back into the Gutter.\n\nAnd the cycle continues.";

        UWORD uwMargins = 8;
        UWORD uwWidth = 240;

        ULONG ulStartText = timerGetPrec();
        tTextBitMap* pTextBitmap = textCreateFromString(text, uwWidth - uwMargins * 2, TextHJustify::LEFT);
        ULONG ulEndText = timerGetPrec();

        NinePatch pPatch = ninePatchCreate(&pPatchBitmap, 16, 16, 16, 16);
        UWORD uwHeight = pTextBitmap->uwActualHeight + uwMargins * 2;

        ULONG ulStartPatch = timerGetPrec();
        tBitMap* pRenderedPatch = ninePatchRender(pPatch, uwWidth, uwHeight, 0);
        ULONG ulEndPatch = timerGetPrec();

        ULONG ulStartRender = timerGetPrec();
        screenBlitCopy(g_mainScreen, pRenderedPatch, 0, 0, 0, 0, uwWidth, uwHeight, MINTERM_COOKIE);
        screenTextCopy(g_mainScreen, pTextBitmap, uwMargins, uwMargins, 1, FONT_COOKIE | FONT_SHADOW);
        ULONG ulEndRender = timerGetPrec();

        char timerBuffer[256];

        timerFormatPrec(timerBuffer, timerGetDelta(ulStartText, ulEndText));
        snprintf(timerBuffer, sizeof(timerBuffer), "Text created in %s", timerBuffer);
        tTextBitMap* pTextCreate = textCreateFromString(timerBuffer, 320, TextHJustify::CENTER);

        timerFormatPrec(timerBuffer, timerGetDelta(ulStartPatch, ulEndPatch));
        snprintf(timerBuffer, sizeof(timerBuffer), "Patch created in %s", timerBuffer);
        tTextBitMap* pPatchCreate = textCreateFromString(timerBuffer, 320, TextHJustify::CENTER);

        timerFormatPrec(timerBuffer, timerGetDelta(ulStartRender, ulEndRender));
        snprintf(timerBuffer, sizeof(timerBuffer), "Rendered in %s", timerBuffer);
        tTextBitMap* pRender = textCreateFromString(timerBuffer, 320, TextHJustify::CENTER);

        screenTextCopy(g_mainScreen, pTextCreate, 0, 180, 1, FONT_COOKIE);
        screenTextCopy(g_mainScreen, pPatchCreate, 0, 191, 1, FONT_COOKIE);
        screenTextCopy(g_mainScreen, pRender, 0, 202, 1, FONT_COOKIE);

        fontDestroyTextBitMap(pTextCreate);
        fontDestroyTextBitMap(pPatchCreate);
        fontDestroyTextBitMap(pTextBitmap);
        fontDestroyTextBitMap(pRender);

        ninePatchDestroy(&pPatch);
        bitmapDestroy(pRenderedPatch);
    }


    void dialogueTestProcess(void)
    {

    }

    void dialogueTestDestroy(void)
    {

    }

    tState g_stateDialogueTest = {
        .cbCreate = dialogueTestCreate,
        .cbLoop = dialogueTestProcess,
        .cbDestroy = dialogueTestDestroy,
    };

}