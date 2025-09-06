#include "neonengine.h"

#include <ace/managers/system.h>
#include <ace/managers/timer.h>
#include <ace/utils/font.h>
#include <ace/utils/palette.h>

#include "core/screen.h"
#include "core/text_render.h"

#include "mtl/vector.h"

tTextBitMap* s_pTextBitmap = nullptr;
namespace NEONengine
{
    using textBitMap_ptr = mtl::unique_ptr<tTextBitMap, fontDestroyTextBitMap>;

    void drawText(bstr_view const& bstr, UWORD uwX, UWORD uwY, UWORD uwMaxWidth, UBYTE ubColorIdx, TextHJustify justification)
    {
        auto pTextBmp = textBitMap_ptr(textCreateFromString(bstr, uwMaxWidth, justification));
        fontDrawTextBitMap(screenGetBackBuffer(g_mainScreen), pTextBmp, uwX, uwY, ubColorIdx, FONT_COOKIE);
    }

    static UWORD s_uwFH = 11;
    void fontTestCreate(void)
    {
        logBlockBegin("fontTestCreate");

        ULONG ulStartFullPage = timerGetPrec();

        screenFadeFromBlack(g_mainScreen, 25, 0, NULL);
        screenClear(g_mainScreen, 0);

        paletteLoadFromPath("data/core/base.plt", screenGetPalette(g_mainScreen), 255);

        textRendererCreate("data/font.fnt");

        drawText((">>>"), 0, 0, 10, 1, TextHJustify::LEFT);
        drawText(("Left justified text"), 10, 0, 200, 1, TextHJustify::LEFT);
        drawText(("Center justified text"), 10, s_uwFH, 200, 9, TextHJustify::CENTER);
        drawText(("Right justified text"), 10, s_uwFH * 2, 200, 8, TextHJustify::RIGHT);
        drawText(("<<<"), 210, 0, 10, 1, TextHJustify::LEFT);
        drawText(("This is a longer line that should wrap around to the next line"),   0, s_uwFH * 4, 100, 17, TextHJustify::LEFT);
        drawText(("|||||"), 103, s_uwFH * 4, 4, 24, TextHJustify::LEFT);
        drawText(("This is a longer line that should wrap around to the next line"), 110, s_uwFH * 4, 100, 26, TextHJustify::CENTER);
        drawText(("|||||"), 213, s_uwFH * 4, 4, 24, TextHJustify::LEFT);
        drawText(("This is a longer line that should wrap around to the next line"), 220, s_uwFH * 4, 100, 27, TextHJustify::RIGHT);
        drawText(("Palette"), 0, s_uwFH * 10 + (s_uwFH >> 1), 10, 24, TextHJustify::LEFT);

        char buffer[16];
        for (UBYTE color = 1; color < 32; ++color)
        {
            UWORD x = 20 + (color / 8) * 30;
            UWORD y = s_uwFH * 10 + (color % 8) * s_uwFH;
            snprintf(buffer, sizeof(buffer), "%02d", color);
            drawText(buffer, x, y, 20, color, TextHJustify::LEFT);
        }

        ULONG ulStart = timerGetPrec();
        drawText("This is a long string and it's going to wrap around quite a few times in order to test the worst case performance scenario. Let's see how it does. Right below this line is the time it took to render.", 140, s_uwFH * 10, 180, 18, TextHJustify::LEFT);
        ULONG ulEnd = timerGetPrec();

        char timerBuffer[256];
        char renderBuffer[256]; 
        timerFormatPrec(timerBuffer, timerGetDelta(ulStart, ulEnd));
        snprintf(renderBuffer, sizeof(renderBuffer), "Rendered in %s", timerBuffer);
        drawText(renderBuffer, 140, s_uwFH * 18 + (s_uwFH >> 1), 240, 1, TextHJustify::LEFT);

        ULONG ulEndFullPage = timerGetPrec();
        timerFormatPrec(timerBuffer, timerGetDelta(ulStartFullPage, ulEndFullPage));
        snprintf(renderBuffer, sizeof(renderBuffer), "Whole page rendered in %s ", timerBuffer);
        drawText(renderBuffer, 0, 255 - s_uwFH, 320, 1, TextHJustify::CENTER);

        s_pTextBitmap = textCreateFromString("00", 20, TextHJustify::LEFT);
        fontDrawTextBitMap(screenGetBackBuffer(g_mainScreen), s_pTextBitmap, 20, s_uwFH * 10, 1, FONT_COOKIE);
    }

    static UBYTE ubColor = 0;
    static ULONG ulLastTime = 0;
    void fontTestProcess(void)
    {
        ULONG delta = timerGetDelta(ulLastTime, timerGet());

        if (delta < 100)
            return;
        
        ulLastTime = timerGet();

        fontDrawTextBitMap(screenGetBackBuffer(g_mainScreen), s_pTextBitmap, 20, s_uwFH * 10, ubColor++, FONT_COOKIE);
        if (ubColor > 31)
            ubColor = 0;
    }

    void fontTestDestroy(void)
    {
        textRendererDestroy();
        fontDestroyTextBitMap(s_pTextBitmap);
    }

    tState g_stateFontTest = {
        .cbCreate = fontTestCreate,
        .cbLoop = fontTestProcess,
        .cbDestroy = fontTestDestroy,
    };
}