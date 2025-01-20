#include "text.h"

#include "neonengine.h"
#include <ace/managers/viewport/simplebuffer.h>
#include <ace/utils/bitmap.h>
#include <ace/utils/font.h>

#include "lang.h"

static tFont *s_pDefaultFont;
static tBitMap *s_pFramePatch;
static tBitMap *s_pScratch;
//static UwSize s_uwFrameElementSize;

void textCreate(const char *szFontName, const char */*szFrameBitmap*/)
{
    s_pDefaultFont = fontCreateFromPath(szFontName);
    s_pScratch = bitmapCreate(320, 200, 8, 0);
}

void textDestroy()
{
    if (s_pDefaultFont)
    {
        fontDestroy(s_pDefaultFont);
    }

    if (s_pFramePatch)
    {
        bitmapDestroy(s_pFramePatch);
    }

    if (s_pScratch)
    {
        bitmapDestroy(s_pScratch);
    }
}

void textBegin(TextContext *pContext, UBYTE ubAddFrame, UBYTE ubCenterVertical)
{
    if (!pContext)
    {
        logWrite("ERROR: Context is null.");
        return;
    }

    memset(pContext, 0, sizeof(TextContext));
    pContext->ubAddFrame = ubAddFrame;
    pContext->ubCenterVertical = ubCenterVertical;
}

void textEnd(TextContext *pContext, UwPoint /*uwXY*/, UBYTE /*ubShouldWait*/)
{
    if (!pContext)
    {
        logWrite("ERROR: Context is null.");
        return;
    }
}

UwSize textPut(TextContext *pContext, UWORD /*uwStringId*/, UWORD /*uwMaxLength*/, TextJustify /*justification*/, UBYTE /*ubColorIdx*/)
{
    UwSize size = {0, 0};

    if (!pContext)
    {
        logWrite("ERROR: Context is null.");
        return size;
    }

    return size;
}

void textPutNewLine(TextContext *pContext, UWORD uwCount)
{
    if (!pContext)
    {
        logWrite("ERROR: Context is null.");
        return;
    }

    pContext->uwY += uwCount;
}

void textDraw(UWORD uwStringId, UwPoint uwXY, TextJustify justification, UBYTE ubColorIdx)
{
    const NeonString *pString = langGetStringById(uwStringId);
    textDrawString(pString->pData, uwXY, justification, ubColorIdx);
}

void textDrawString(const char *pString, UwPoint uwXY, TextJustify justification, UBYTE ubColorIdx)
{
    tTextBitMap *pTextBitmap = fontCreateTextBitMapFromStr(s_pDefaultFont, pString);
    UWORD uwFlags = (UWORD)justification | FONT_COOKIE;

    fontDrawStr(s_pDefaultFont, screenGetBackBuffer(g_mainScreen), uwXY.uwX, uwXY.uwY,
        pString, ubColorIdx, uwFlags, pTextBitmap);

    fontDestroyTextBitMap(pTextBitmap);
}
