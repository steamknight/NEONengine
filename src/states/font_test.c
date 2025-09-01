#include "neonengine.h"

#include <ace/managers/system.h>
#include <ace/managers/timer.h>
#include <ace/utils/font.h>
#include <ace/utils/palette.h>

#include "core/screen.h"
#include "core/text_render.h"

static tTextBitMap* s_pTextBitmap;

void drawText(Bstring bstr, UWORD uwX, UWORD uwY, UWORD uwMaxWidth, UBYTE ubColorIdx, TextHJustify justification)
{
    tTextBitMap* pTextBmp = textCreateFromString(bstr, uwMaxWidth, justification);
    fontDrawTextBitMap(screenGetBackBuffer(g_mainScreen), pTextBmp, uwX, uwY, ubColorIdx, FONT_COOKIE);
    fontDestroyTextBitMap(pTextBmp);
    bstrDestroy(&bstr);
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

    drawText(B(">>>"), 0, 0, 10, 1, TX_LEFT_JUSTIFY);
    drawText(B("Left justified text"), 10, 0, 200, 1, TX_LEFT_JUSTIFY);
    drawText(B("Center justified text"), 10, s_uwFH, 200, 9, TX_CENTER_JUSTIFY);
    drawText(B("Right justified text"), 10, s_uwFH * 2, 200, 8, TX_RIGHT_JUSTIFY);
    drawText(B("<<<"), 210, 0, 10, 1, TX_LEFT_JUSTIFY);
    drawText(B("This is a longer line that should wrap around to the next line"),   0, s_uwFH * 4, 100, 17, TX_LEFT_JUSTIFY);
    drawText(B("|||||"), 103, s_uwFH * 4, 4, 24, TX_LEFT_JUSTIFY);
    drawText(B("This is a longer line that should wrap around to the next line"), 110, s_uwFH * 4, 100, 26, TX_CENTER_JUSTIFY);
    drawText(B("|||||"), 213, s_uwFH * 4, 4, 24, TX_LEFT_JUSTIFY);
    drawText(B("This is a longer line that should wrap around to the next line"), 220, s_uwFH * 4, 100, 27, TX_RIGHT_JUSTIFY);
    drawText(B("Palette"), 0, s_uwFH * 10 + (s_uwFH >> 1), 10, 24, TX_LEFT_JUSTIFY);

    for (UBYTE color = 1; color < 32; ++color)
    {
        UWORD x = 20 + (color / 8) * 30;
        UWORD y = s_uwFH * 10 + (color % 8) * s_uwFH;
        drawText(bstrCreateF(MEMF_FAST, "%02d", color), x, y, 20, color, TX_LEFT_JUSTIFY);
    }

    ULONG ulStart = timerGetPrec();
    drawText(B("This is a long string and it's going to wrap around quite a few times in order to test the worst case performance scenario. Let's see how it does. Right below this line is the time it took to render."), 140, s_uwFH * 10, 180, 18, TX_LEFT_JUSTIFY);
    ULONG ulEnd = timerGetPrec();

    char timerBuffer[256];
    timerFormatPrec(timerBuffer, timerGetDelta(ulStart, ulEnd));
    drawText(bstrCreateF(MEMF_FAST, "Rendered in %s ", timerBuffer), 140, s_uwFH * 18 + (s_uwFH >> 1), 240, 1, TX_LEFT_JUSTIFY);

    Bstring palette0 = B("00");
    s_pTextBitmap = textCreateFromString(palette0, 20, TX_LEFT_JUSTIFY);
    bstrDestroy(&palette0);

    ULONG ulEndFullPage = timerGetPrec();
    timerFormatPrec(timerBuffer, timerGetDelta(ulStartFullPage, ulEndFullPage));
    drawText(bstrCreateF(MEMF_FAST, "Whole page rendered in %s ", timerBuffer), 0, 255 - s_uwFH, 320, 1, TX_CENTER_JUSTIFY);
}

static UBYTE ubColor = 0;
static ULONG ulLastTime = 0;
void fontTestProcess(void)
{
    ULONG delta = timerGetDelta(ulLastTime, timerGet());

    if (delta < 10)
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