#include "neonengine.h"

#include <ace/managers/system.h>
#include <ace/managers/timer.h>
#include <ace/utils/font.h>
#include <ace/utils/palette.h>

#include "core/screen.h"
#include "core/text_render.h"

// static tFont* s_pFont;
static tTextBitMap* s_pTextBitmap;

void drawText(Bstring bstr, UWORD uwX, UWORD uwY, UWORD uwMaxWidth, UBYTE ubColorIdx, TextHJustify justification)
{
    tTextBitMap* pTextBmp = textCreateFromString(bstr, uwMaxWidth, justification);
    fontDrawTextBitMap(screenGetBackBuffer(g_mainScreen), pTextBmp, uwX, uwY, ubColorIdx, FONT_COOKIE);
    fontDestroyTextBitMap(pTextBmp);
    bstrDestroy(&bstr);
}

void fontTestCreate(void)
{
    logBlockBegin("fontTestCreate");
    screenFadeFromBlack(g_mainScreen, 25, 0, NULL);
    screenClear(g_mainScreen, 0);

    //s_pFont = fontCreateFromPath("data/font.fnt");

    paletteLoadFromPath("data/core/base.plt", screenGetPalette(g_mainScreen), 255);

    for (UBYTE i = 0; i < 32; ++i)
    {
        logWrite("Color %02d: $%04x", i, screenGetPalette(g_mainScreen)[i]);
    }

    textRendererCreate("data/font.fnt");

    drawText(B(">>>"), 0, 0, 10, 1, TX_LEFT_JUSTIFY);
    drawText(B("Left justified text"), 10, 0, 200, 1, TX_LEFT_JUSTIFY);
    drawText(B("Center justified text"), 10, 10, 200, 9, TX_CENTER_JUSTIFY);
    drawText(B("Right justified text"), 10, 20, 200, 8, TX_RIGHT_JUSTIFY);
    drawText(B("<<<"), 210, 0, 10, 1, TX_LEFT_JUSTIFY);
    drawText(B("This is a longer line that should wrap around to the next line"),   0, 40, 100, 17, TX_LEFT_JUSTIFY);
    drawText(B("|||||"), 100, 40, 10, 24, TX_LEFT_JUSTIFY);
    drawText(B("This is a longer line that should wrap around to the next line"), 110, 40, 100, 26, TX_CENTER_JUSTIFY);
    drawText(B("|||||"), 210, 40, 10, 24, TX_LEFT_JUSTIFY);
    drawText(B("This is a longer line that should wrap around to the next line"), 220, 40, 100, 27, TX_RIGHT_JUSTIFY);
    drawText(B("Palette"), 0, 105, 10, 24, TX_LEFT_JUSTIFY);

    for (UBYTE color = 0; color < 32; ++color)
    {
        UWORD x = 20 + (color / 8) * 30;
        UWORD y = 100 + (color % 8) * 10;
        drawText(bstrCreateF(MEMF_FAST, "%02d", color), x, y, 20, color, TX_LEFT_JUSTIFY);
    }

    ULONG ulStart = timerGetPrec();
    drawText(B("This is a long string and it's going to wrap around quite a few times in order to test the worst case performance scenario. Let's see how it does. Right below this line is the time it took to render."), 140, 100, 180, 18, TX_LEFT_JUSTIFY);
    ULONG ulEnd = timerGetPrec();

    char timerBuffer[256];
    timerFormatPrec(timerBuffer, timerGetDelta(ulStart, ulEnd));
    drawText(bstrCreateF(MEMF_FAST, "Rendered in %s ", timerBuffer), 140, 185, 240, 1, TX_LEFT_JUSTIFY);

}

static UBYTE ubColor = 0;
void fontTestProcess(void)
{
    // fontDrawTextBitMap(screenGetBackBuffer(g_mainScreen), s_pTextBitmap, 0, 0, ubColor++, FONT_COOKIE);

    // if (ubColor == 255)
    // {
    //     logWrite("Cycled through all colors");
    // }
}

void fontTestDestroy(void)
{
    //fontDestroy(s_pFont);

    textRendererDestroy();
    fontDestroyTextBitMap(s_pTextBitmap);
}

tState g_stateFontTest = {
    .cbCreate = fontTestCreate,
    .cbLoop = fontTestProcess,
    .cbDestroy = fontTestDestroy,
};