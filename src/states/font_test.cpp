#include "ace++/font.h"

#include "neonengine.h"

#include <ace/managers/key.h>
#include <ace/managers/system.h>
#include <ace/managers/timer.h>
#include <ace/utils/palette.h>

#include <ace++/log.h>

#include <mtl/vector.h>

#include "core/screen.h"
#include "core/text_render.h"

ace::text_bitmap_ptr s_pTextBitmap = nullptr;
namespace NEONengine
{
    void drawFontTest(ULONG ulStartFullPage);
    void drawText(bstr_view const& bstr,
                  UWORD uwX,
                  UWORD uwY,
                  UWORD uwMaxWidth,
                  UBYTE ubColorIdx,
                  text_justify justification)
    {
        auto pTextBmp
            = g_pEngine->default_text_renderer()->create_text(bstr, uwMaxWidth, justification);
        fontDrawTextBitMap(
            screenGetBackBuffer(g_mainScreen), pTextBmp.get(), uwX, uwY, ubColorIdx, FONT_COOKIE);
    }

    void fontTestCreate(void)
    {
        ACE_LOG_BLOCK("fontTestCreate");

        screenFadeFromBlack(g_mainScreen, 25, 0, NULL);
        screenClear(g_mainScreen, 0);

        paletteLoadFromPath("data/core/base.plt", screenGetPalette(g_mainScreen), 255);

        drawText("Press the Spacebar",
                 0,
                 (200 - g_pEngine->default_font()->uwHeight) / 2,
                 320,
                 2,
                 text_justify::CENTER);
    }

    void drawFontTest()
    {
        auto const FH = g_pEngine->default_font()->uwHeight;

        ULONG ulStartFullPage = timerGetPrec();
        screenClear(g_mainScreen, 0);
        drawText((">>>"), 0, 0, 10, 1, text_justify::LEFT);
        drawText(("Left justified text"), 10, 0, 200, 1, text_justify::LEFT);
        drawText(("Center justified text"), 10, FH, 200, 9, text_justify::CENTER);
        drawText(("Right justified text"), 10, FH * 2, 200, 8, text_justify::RIGHT);
        drawText(("<<<"), 210, 0, 10, 1, text_justify::LEFT);
        drawText(("This is a longer line that should wrap around to the next line"),
                 0,
                 FH * 4,
                 100,
                 17,
                 text_justify::LEFT);
        drawText(("|||||"), 103, FH * 4, 4, 24, text_justify::LEFT);
        drawText(("This is a longer line that should wrap around to the next line"),
                 110,
                 FH * 4,
                 100,
                 26,
                 text_justify::CENTER);
        drawText(("|||||"), 213, FH * 4, 4, 24, text_justify::LEFT);
        drawText(("This is a longer line that should wrap around to the next line"),
                 220,
                 FH * 4,
                 100,
                 27,
                 text_justify::RIGHT);
        drawText(("Palette"), 0, FH * 10 + (FH >> 1), 10, 24, text_justify::LEFT);
        drawText("This has...\n\n...a few new-lines", 0, FH * 18 + 5, 160, 27, text_justify::LEFT);

        char buffer[16];
        for (UBYTE color = 1; color < 32; ++color)
        {
            UWORD x = 20 + (color / 8) * 30;
            UWORD y = FH * 10 + (color % 8) * FH;
            snprintf(buffer, sizeof(buffer), "%02d", color);
            drawText((char const*)buffer, x, y, 20, color, text_justify::LEFT);
        }

        ULONG ulStart = timerGetPrec();
        drawText(
            "This is a long string and it's going to wrap around quite a few times in order to "
            "test the worst case performance scenario. Let's see how it does. Right below this "
            "line is the time it took to render.",
            140,
            FH * 10,
            180,
            18,
            text_justify::LEFT);
        ULONG ulEnd = timerGetPrec();

        char timerBuffer[16];
        char renderBuffer[256];
        timerFormatPrec(timerBuffer, timerGetDelta(ulStart, ulEnd));
        snprintf(renderBuffer, sizeof(renderBuffer), "Rendered in %s", timerBuffer);
        drawText((char const*)renderBuffer, 140, FH * 18 + (FH >> 1), 240, 1, text_justify::LEFT);

        ULONG ulEndFullPage = timerGetPrec();
        timerFormatPrec(timerBuffer, timerGetDelta(ulStartFullPage, ulEndFullPage));
        snprintf(renderBuffer, sizeof(renderBuffer), "Whole page rendered in %s ", timerBuffer);
        drawText((char const*)renderBuffer, 0, 255 - FH, 320, 1, text_justify::CENTER);
    }

    static UBYTE ubColor    = 0;
    static ULONG ulLastTime = 0;
    static bool is_drawn    = false;

    void fontTestProcess()
    {
        if (keyUse(KEY_SPACE) and !is_drawn)
        {
            drawFontTest();
            is_drawn = true;
        }
        static auto palette00 = ace::text_bitmap_ptr(
            g_pEngine->default_text_renderer()->create_text("00", 20, text_justify::LEFT));

        ULONG delta = timerGetDelta(ulLastTime, timerGet());

        if (delta < 100) return;

        ulLastTime = timerGet();

        fontDrawTextBitMap(screenGetBackBuffer(g_mainScreen),
                           palette00.get(),
                           20,
                           g_pEngine->default_font()->uwHeight * 10,
                           ubColor++,
                           FONT_COOKIE);
        if (ubColor > 31) ubColor = 0;
    }

    void fontTestDestroy() {}

    tState g_stateFontTest = {
        .cbCreate  = fontTestCreate,
        .cbLoop    = fontTestProcess,
        .cbDestroy = fontTestDestroy,
    };
}  // namespace NEONengine