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
    drawText(B("00"),  20, 100, 20, 0, TX_LEFT_JUSTIFY);
    drawText(B("01"),  20, 110, 20, 1, TX_LEFT_JUSTIFY);
    drawText(B("02"),  20, 120, 20, 2, TX_LEFT_JUSTIFY);
    drawText(B("03"),  20, 130, 20, 3, TX_LEFT_JUSTIFY);
    drawText(B("04"),  20, 140, 20, 4, TX_LEFT_JUSTIFY);
    drawText(B("05"),  20, 150, 20, 5, TX_LEFT_JUSTIFY);
    drawText(B("06"),  20, 160, 20, 6, TX_LEFT_JUSTIFY);
    drawText(B("07"),  20, 170, 20, 7, TX_LEFT_JUSTIFY);
    drawText(B("08"),  50, 100, 20, 8, TX_LEFT_JUSTIFY);
    drawText(B("09"),  50, 110, 20, 9, TX_LEFT_JUSTIFY);
    drawText(B("10"),  50, 120, 20, 10, TX_LEFT_JUSTIFY);
    drawText(B("11"),  50, 130, 20, 11, TX_LEFT_JUSTIFY);
    drawText(B("12"),  50, 140, 20, 12, TX_LEFT_JUSTIFY);
    drawText(B("13"),  50, 150, 20, 13, TX_LEFT_JUSTIFY);
    drawText(B("14"),  50, 160, 20, 14, TX_LEFT_JUSTIFY);
    drawText(B("15"),  50, 170, 20, 15, TX_LEFT_JUSTIFY);
    drawText(B("16"),  80, 100, 20, 16, TX_LEFT_JUSTIFY);
    drawText(B("17"),  80, 110, 20, 17, TX_LEFT_JUSTIFY);
    drawText(B("18"),  80, 120, 20, 18, TX_LEFT_JUSTIFY);
    drawText(B("19"),  80, 130, 20, 19, TX_LEFT_JUSTIFY);
    drawText(B("20"),  80, 140, 20, 20, TX_LEFT_JUSTIFY);
    drawText(B("21"),  80, 150, 20, 21, TX_LEFT_JUSTIFY);
    drawText(B("22"),  80, 160, 20, 22, TX_LEFT_JUSTIFY);
    drawText(B("23"),  80, 170, 20, 23, TX_LEFT_JUSTIFY);
    drawText(B("24"), 110, 100, 20, 24, TX_LEFT_JUSTIFY);
    drawText(B("25"), 110, 110, 20, 25, TX_LEFT_JUSTIFY);
    drawText(B("26"), 110, 120, 20, 26, TX_LEFT_JUSTIFY);
    drawText(B("27"), 110, 130, 20, 27, TX_LEFT_JUSTIFY);
    drawText(B("28"), 110, 140, 20, 28, TX_LEFT_JUSTIFY);
    drawText(B("29"), 110, 150, 20, 29, TX_LEFT_JUSTIFY);
    drawText(B("30"), 110, 160, 20, 30, TX_LEFT_JUSTIFY);
    drawText(B("31"), 110, 170, 20, 31, TX_LEFT_JUSTIFY);

    drawText(bstrCreateF(MEMF_FAST, "Font height: %d px", 8), 0, 180, 240, 1, TX_LEFT_JUSTIFY);

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