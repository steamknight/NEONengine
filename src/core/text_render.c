#include "text_render.h"

#include <ace/utils/bitmap.h>
#include <ace/utils/font.h>
#include <ace/managers/system.h>

#include "utils/array.h"
#include "core/lang.h"

#define ROUND_16(x) (((x) + 15) & ~15)

static tFont* s_pDefaultFont;

static const WORD INITIAL_LINE_CAPACITY = 5;

#define LINE_START(line) (ULONG)(((line) & 0xFFFF0000) >> 16)
#define LINE_END(line)   (ULONG)((line) & 0x0000FFFF)
#define MAKE_LINE(start, end) (((ULONG)(start) << 16) | ((ULONG)(end) & 0x0000FFFF))

ULONG breakTextIntoLines(Bstring bstrText, ULONG* pStartIndex, ULONG uwMaxWidth, ULONG* pOutInfo)
{
    ULONG ulTextLength = bstrLength(bstrText);
    ULONG ulEndOfLine = 0;
    ULONG ulLineWidth = 0;
    ULONG ulOffset = 1;

    if (!bstrText || *pStartIndex >= ulTextLength)
    {
        return FALSE;
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

    *pOutInfo = MAKE_LINE(*pStartIndex, ulEndOfLine);
    *pStartIndex = ulEndOfLine + ulOffset;
    return TRUE;
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

    systemUse();
    Array lines = arrayCreate(INITIAL_LINE_CAPACITY, sizeof(ULONG), MEMF_FAST);
    tTextBitMap* pLineBitmap = fontCreateTextBitMap(320, ROUND_16(s_pDefaultFont->uwHeight));
    systemUnuse();

    // Create the individual line bitmaps
    ULONG line = 0;
    while((breakTextIntoLines(bstrText, &ulStartIndex, uwMaxWidth, &line)))
    {
        if (ulLineCount >= arrayLength(lines))
        {
            arrayAutoResize(&lines);
        }

        arrayPut(lines, ulLineCount++, &line);
    }

    // .. and stitch them all together
    UWORD uwHeight = s_pDefaultFont->uwHeight * ulLineCount;
    tTextBitMap* pResult = fontCreateTextBitMap(ROUND_16(uwMaxWidth), ROUND_16(uwHeight));
    pResult->uwActualWidth = uwMaxWidth;
    pResult->uwActualHeight = uwHeight;

    for (ULONG idx = 0; idx < ulLineCount; ++idx)
    {
        ULONG line = *(ULONG*)arrayGet(lines, idx);

        if (!line)
            continue;
        
        ULONG ulStart = LINE_START(line);
        ULONG ulEnd = LINE_END(line);

        // Split the string into a line and the remainder by temporarily terminating it
        char* pBuffer = bstrGetData(bstrText);
        char temp = pBuffer[ulEnd];

        pBuffer[ulEnd] = '\0';
        fontFillTextBitMap(s_pDefaultFont, pLineBitmap, pBuffer + ulStart);
        pBuffer[ulEnd] = temp;

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
    }
    
    systemUse();
    fontDestroyTextBitMap(pLineBitmap);
    arrayDestroy(&lines);
    systemUnuse();

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