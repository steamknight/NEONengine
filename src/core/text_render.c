#include "text_render.h"

#include <ace/utils/bitmap.h>
#include <ace/utils/font.h>

#include "utils/array.h"
#include "core/lang.h"

#define ROUND_16(x) (((x) + 15) & ~15)

typedef struct _TextRenderContext
{
    tBitMap *pBitmap;
} TextRenderContext;

static tFont* s_pDefaultFont;

static const WORD INITIAL_LINE_CAPACITY = 5;

tTextBitMap* breakTextIntoLines(Bstring bstrText, ULONG* pStartIndex, ULONG uwMaxWidth)
{
    ULONG ulTextLength = bstrLength(bstrText);
    ULONG ulEndOfLine = 0;
    ULONG ulLineWidth = 0;
    ULONG ulOffset = 1;

    if (!bstrText || *pStartIndex >= ulTextLength)
    {
        return NULL;
    }

    char* pBuffer = bstrGetData(bstrText);

    // NOTE: We go one character beyond the string length to catch the null terminator
    for (ULONG idx = *pStartIndex; idx < ulTextLength + 1; ++idx)
    {
        char c = pBuffer[idx];
        
        if (c == ' ')
        {
            ulEndOfLine = idx;
        }
        else if (c == '\n' || c == '\0')
        {
            ulEndOfLine = idx;
            break;
        }

        if (c >= ' ')
        {
            ulLineWidth += fontGlyphWidth(s_pDefaultFont, c) + 1; // +1 for spacing
            
            if (uwMaxWidth > 0 && ulLineWidth > uwMaxWidth)
            {
                // If we haven't found a space to break on, break at the current character
                if (ulEndOfLine == 0)
                {
                    ulEndOfLine = idx;
                    ulOffset = 0;
                }
                break;
            }
        }
    }

    if (ulEndOfLine == 0)
    {
        ulEndOfLine = ulTextLength;
    }


    // Split the string into a line and the remainder by temporarily terminating it
    char temp = pBuffer[ulEndOfLine];
    pBuffer[ulEndOfLine] = '\0';
    tTextBitMap* pLineBitmap = fontCreateTextBitMapFromStr(s_pDefaultFont, pBuffer + *pStartIndex);
    pBuffer[ulEndOfLine] = temp;

    
    *pStartIndex = ulEndOfLine + ulOffset;
    return pLineBitmap;
}


void textRendererCreate(char const *szFontName)
{
    s_pDefaultFont = fontCreateFromPath(szFontName);
}

void textRendererDestroy()
{
    if (s_pDefaultFont)
    {
        fontDestroy(s_pDefaultFont);
        s_pDefaultFont = NULL;
    }
}

tTextBitMap* textCreateFromString1(Bstring bstrText, UWORD uwMaxWidth, TextHJustify justification)
{
    if (!bstrText)
    {
        logWrite("ERROR: Bstring is NULL.");
        return NULL;
    }

    if (!s_pDefaultFont)
    {
        logWrite("ERROR: Default font is not initialized.");
        return NULL;
    }

    ULONG ulEndOfLine = 0;
    ULONG ulLineWidth = 0;
    ULONG ulCurrentWidth = 0;
    ULONG ulTextLength = bstrLength(bstrText);
    
    Bstring bstrWork = bstrClone(bstrText, MEMF_FAST);
    char* pBuffer = bstrGetData(bstrWork);

    for (ULONG idx = 0; idx < ulTextLength; ++idx)
    {
        char c = pBuffer[idx];
        
        if (c == ' ')
        {
            ulEndOfLine = idx;
            ulLineWidth = ulCurrentWidth + fontGlyphWidth(s_pDefaultFont, c) + 1;
        }

        if (c >= ' ')
        {
            ulCurrentWidth += fontGlyphWidth(s_pDefaultFont, c) + 1; // +1 for spacing
            
            if (uwMaxWidth > 0 && ulCurrentWidth > uwMaxWidth)
            {
                pBuffer[ulEndOfLine] = '\n';
                ulCurrentWidth -= ulLineWidth;
            }
        }
    }

    tTextBitMap* pResult = fontCreateTextBitMapFromStr(s_pDefaultFont, pBuffer);
    bstrDestroy(&bstrWork);

    return pResult;

}

tTextBitMap* textCreateFromString(Bstring bstrText, UWORD uwMaxWidth, TextHJustify justification)
{
    if (!bstrText)
    {
        logWrite("ERROR: Bstring is NULL.");
        return NULL;
    }

    if (!s_pDefaultFont)
    {
        logWrite("ERROR: Default font is not initialized.");
        return NULL;
    }

    ULONG ulLineCount = 0;
    ULONG ulStartIndex = 0;
    Array lineBitmaps = arrayCreate(INITIAL_LINE_CAPACITY, sizeof(tTextBitMap*), MEMF_FAST);
    tTextBitMap* pLineBitmap = NULL;

    // Create the individual line bitmaps
    while((pLineBitmap = breakTextIntoLines(bstrText, &ulStartIndex, uwMaxWidth)))
    {
        if (ulLineCount >= arrayLength(lineBitmaps))
        {
            arrayAutoResize(&lineBitmaps);
        }

        arrayPut(lineBitmaps, ulLineCount++, &pLineBitmap);
    }

    // .. and stitch them all together
    UWORD uwHeight = s_pDefaultFont->uwHeight * ulLineCount;
    tTextBitMap* pResult = fontCreateTextBitMap(ROUND_16(uwMaxWidth), ROUND_16(uwHeight));
    pResult->uwActualWidth = uwMaxWidth;
    pResult->uwActualHeight = uwHeight;

    for (ULONG idx = 0; idx < ulLineCount; ++idx)
    {
        pLineBitmap = *(tTextBitMap**)arrayGet(lineBitmaps, idx);
        if (!pLineBitmap)
            continue;
        
        UWORD uwX = 0;
        switch (justification)
        {
            case TX_RIGHT_JUSTIFY:
                uwX = uwMaxWidth - pLineBitmap->uwActualWidth;
            break;

            case TX_CENTER_JUSTIFY:
                uwX = (uwMaxWidth - pLineBitmap->uwActualWidth) >> 1;
            break;

            case TX_LEFT_JUSTIFY:  // fallthrough
            default:
                uwX = 0;
                break;
        }

        fontDrawTextBitMap(pResult->pBitMap, pLineBitmap, uwX, idx * s_pDefaultFont->uwHeight, 1, 0);
        fontDestroyTextBitMap(pLineBitmap);
    }

    return pResult;
}

tTextBitMap* textCreateFromId(ULONG stringId, UWORD uwMaxWidth, TextHJustify justification)
{
    Bstring bstrText = langGetStringById(stringId);
    if (!bstrText)
    {
        logWrite("ERROR: Could not find string with id %ld", stringId);
        return NULL;
    }

    return textCreateFromString(bstrText, uwMaxWidth, justification);
}