#include "neonengine.h"

#include <ace/managers/system.h>
#include <ace/managers/blit.h>
#include <ace/managers/viewport/simplebuffer.h>
#include <ace/utils/font.h>
#include <ace/utils/palette.h>

#include "core/music.h"
#include "core/screen.h"
#include "core/text.h"

#include "core/game_data.h"
#include <ace/managers/timer.h>

tFont* s_pFont;
tTextBitMap* s_pTextBmp;

void fontTestCreate(void)
{
    logBlockBegin("fontTestCreate");

    paletteLoad("data/core/base.plt", g_mainScreen->pFade->pPaletteRef, 255);

    s_pFont = fontCreate("data/font.fnt");
    s_pTextBmp = fontCreateTextBitMap(16, s_pFont->uwHeight);

    fontFillTextBitMap(s_pFont, s_pTextBmp, "N");

    systemUnuse();
    screenFadeFromBlack(g_mainScreen, 10, 1, 0);

    logBlockEnd("fontTestCreate");
}

void fontTestProcess(void)
{
    static UwPoint pt = {.uwX = 0, .uwY = 28};

    screenClear(g_mainScreen, 0);

    fontDrawTextBitMap(g_mainScreen->pBuffer->pBack, s_pTextBmp, pt.uwX, pt.uwY, 2, FONT_COOKIE);
    pt.uwX++;
    pt.uwY++;
}

void fontTestDestroy(void)
{
    fontDestroy(s_pFont);
    fontDestroyTextBitMap(s_pTextBmp);
}

tState g_stateFontTest = {
    .cbCreate = fontTestCreate,
    .cbLoop = fontTestProcess,
    .cbDestroy = fontTestDestroy,
};