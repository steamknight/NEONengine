#include "text_render.h"

#include <ace/utils/bitmap.h>
#include <ace/utils/font.h>
#include <ace/managers/system.h>

#include "ace++/font.h"

#include "mtl/cstdint.h"
#include "mtl/memory.h"
#include "mtl/array.h"
#include "mtl/utility.h"
#include "core/lang.h"
#include "utils/bstr_view.h"

namespace NEONengine
{
    using namespace mtl;
    static tFont* s_pDefaultFont;
    // Reusable per-line temporary buffer (raw, manually managed)
    static char* s_pLineScratch = nullptr;
    static u32   s_ulLineScratchCap = 0; // characters capacity (excludes null terminator)

    constexpr size_t INITIAL_LINE_CAPACITY = 16;

    #define LINE_START(line) to<u32>(((line) & 0xFFFF0000) >> 16)
    #define LINE_END(line)   to<u32>((line) & 0x0000FFFF)
    #define MAKE_LINE(start, end) ((to<u32>((start)) << 16) | (to<u32>((end)) & 0x0000FFFF))

    bool breakTextIntoLines(bstr_view const& text, u32* pStartIndex, u32 uwMaxWidth, u32* pOutInfo)
    {;
        u32 ulEndOfLine = 0;
        u32 ulLineWidth = 0;
        u32 ulOffset = 1;

        if (!text || *pStartIndex >= text.length())
        {
            return false;
        }

        // NOTE: We go one character beyond the string length to catch the null terminator
        for (u32 idx = *pStartIndex; idx < text.length() + 1; ++idx)
        {
            char c = text[idx];

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
            ulEndOfLine = text.length();
        }

        *pOutInfo = MAKE_LINE(*pStartIndex, ulEndOfLine);
        *pStartIndex = ulEndOfLine + ulOffset;
        return true;
    }


    void textRendererCreate(char const *szFontName)
    {
        s_pDefaultFont = fontCreateFromPath(szFontName);

        if(!s_pLineScratch) {
            s_ulLineScratchCap = 128;
            s_pLineScratch = (char*)memAlloc(s_ulLineScratchCap + 1, MEMF_FAST);
            if(!s_pLineScratch) {
                s_ulLineScratchCap = 0;
            } else {
                s_pLineScratch[0] = '\0';
            }
        }
    }

    void textRendererDestroy()
    {
        if (s_pDefaultFont)
        {
            fontDestroy(s_pDefaultFont);
            s_pDefaultFont = nullptr;
        }

        if(s_pLineScratch) {
            memFree(s_pLineScratch, s_ulLineScratchCap + 1);
            s_pLineScratch = nullptr;
            s_ulLineScratchCap = 0;
        }
    }

    ace::text_bitmap_ptr textCreateFromString(bstr_view const& text, u16 uwMaxWidth, TextHJustify justification)
    {
        if (text.empty())
        {
            logWrite("ERROR: Bstring is null.");
            return ace::text_bitmap_ptr(nullptr);
        }

        if (!s_pDefaultFont)
        {
            logWrite("ERROR: Default font is not initialized.");
            return ace::text_bitmap_ptr(nullptr);
        }

        u32 ulStartIndex = 0;

        systemUse();
        auto lines = mtl::array<u32, INITIAL_LINE_CAPACITY>();
        auto pLineBitmap = ace::text_bitmap_ptr(fontCreateTextBitMap(320, mtl::round_up<16>(s_pDefaultFont->uwHeight)));
        systemUnuse();

        // Create the individual line bitmaps
        u32 line = 0;
        u32 ulLineCount = 0;
        while((breakTextIntoLines(text, &ulStartIndex, uwMaxWidth, &line)))
        {
            lines[ulLineCount++] = line;
        }

        // .. and stitch them all together
        u16 uwHeight = s_pDefaultFont->uwHeight * ulLineCount;
        auto pResult = ace::text_bitmap_ptr(fontCreateTextBitMap(mtl::round_up<16>(uwMaxWidth), mtl::round_up<16>(uwHeight)));
        pResult->uwActualWidth = uwMaxWidth;
        pResult->uwActualHeight = uwHeight;

        for (auto idx = 0u; idx < ulLineCount; ++idx)
        {
            line = lines[idx];

            if (!line)
                continue;
            
            u32 ulStart = LINE_START(line);
            u32 ulEnd = LINE_END(line);

            if (ulStart == ulEnd)
                continue;

            u32 needed = ulEnd - ulStart; // number of characters in this line
            if (needed == 0)
                continue;

            // Copy characters
            char* dst = s_pLineScratch;
            for (u32 i = 0; i < needed; ++i) {
                dst[i] = text[ulStart + i];
            }
            dst[needed] = '\0';

            fontFillTextBitMap(s_pDefaultFont, pLineBitmap.get(), dst);

            u16 uwX = 0;
            switch (justification)
            {
                case TextHJustify::RIGHT:
                    uwX = uwMaxWidth - pLineBitmap->uwActualWidth;
                break;

                case TextHJustify::CENTER:
                    uwX = (uwMaxWidth - pLineBitmap->uwActualWidth) >> 1;
                break;

                case TextHJustify::LEFT:  // fallthrough
                default:
                    uwX = 0;
                    break;
            }

            fontDrawTextBitMap(pResult->pBitMap, pLineBitmap.get(), uwX, idx * s_pDefaultFont->uwHeight, 1, 0);
        }

        return pResult;
    }

    ace::text_bitmap_ptr textCreateFromId(u32 stringId, u16 uwMaxWidth, TextHJustify justification)
    {
        Bstring bstrText = langGetStringById(stringId);
        if (!bstrText)
        {
            logWrite("ERROR: Could not find string with id %ld", stringId);
            return ace::text_bitmap_ptr(nullptr);
        }

        bstr_view view(bstrGetData(bstrText));
        return textCreateFromString(view, uwMaxWidth, justification);
    }
}