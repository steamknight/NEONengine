#include "text_render.h"

#include <ace/utils/bitmap.h>
#include <ace/utils/font.h>
#include <ace/managers/system.h>

#include "mtl/cstdint.h"
#include "mtl/memory.h"
#include "mtl/vector.h"
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

    constexpr size_t INITIAL_LINE_CAPACITY = 5;

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

    text_bitmap_ptr textCreateFromString(bstr_view const& text, u16 uwMaxWidth, TextHJustify justification)
    {
        if (text.empty())
        {
            logWrite("ERROR: Bstring is null.");
            return text_bitmap_ptr(nullptr);
        }

        if (!s_pDefaultFont)
        {
            logWrite("ERROR: Default font is not initialized.");
            return text_bitmap_ptr(nullptr);
        }

        u32 ulStartIndex = 0;

        systemUse();
        auto lines = mtl::vector<u32>(INITIAL_LINE_CAPACITY);
        auto pLineBitmap = text_bitmap_ptr(fontCreateTextBitMap(320, mtl::ROUND_16(s_pDefaultFont->uwHeight)));
        systemUnuse();

        // Create the individual line bitmaps
        u32 line = 0;
        while((breakTextIntoLines(text, &ulStartIndex, uwMaxWidth, &line)))
        {
            lines.push_back(line);
        }

        // .. and stitch them all together
        u16 uwHeight = s_pDefaultFont->uwHeight * lines.size();
        auto pResult = text_bitmap_ptr(fontCreateTextBitMap(mtl::ROUND_16(uwMaxWidth), mtl::ROUND_16(uwHeight)));
        pResult->uwActualWidth = uwMaxWidth;
        pResult->uwActualHeight = uwHeight;

        u32 idx = 0;
        for (auto& line : lines)
        {
            if (!line)
                continue;
            
            u32 ulStart = LINE_START(line);
            u32 ulEnd = LINE_END(line);

            if (ulStart == ulEnd)
                continue;
            u32 needed = ulEnd - ulStart; // number of characters in this line
            if (needed == 0) continue;

            // Ensure capacity (need space for null terminator)
            if (!s_pLineScratch) {
                s_ulLineScratchCap = 128;
                s_pLineScratch = (char*)memAlloc(s_ulLineScratchCap + 1, MEMF_FAST);
                if(!s_pLineScratch) continue;
            }
            if (s_ulLineScratchCap < needed) {
                u32 newCap = s_ulLineScratchCap ? s_ulLineScratchCap : 128;
                while (newCap < needed) newCap <<= 1;
                char* pNew = (char*)memAlloc(newCap + 1, MEMF_FAST);
                if(!pNew) {
                    logWrite("ERROR: Failed to grow scratch buffer to %lu bytes", (ULONG)(newCap + 1));
                    continue;
                }
                // copy existing partial (not strictly needed for new line but keep data stable)
                for(u32 i=0;i<needed && i<s_ulLineScratchCap;++i) pNew[i]=s_pLineScratch[i];
                memFree(s_pLineScratch, s_ulLineScratchCap + 1);
                s_pLineScratch = pNew;
                s_ulLineScratchCap = newCap;
            }

            // Copy characters
            char* dst = s_pLineScratch;
            for (u32 i = 0; i < needed; ++i) {
                dst[i] = text[ulStart + i];
            }
            dst[needed] = '\0';

            fontFillTextBitMap(s_pDefaultFont, pLineBitmap, dst);

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

            fontDrawTextBitMap(pResult->pBitMap, pLineBitmap, uwX, idx++ * s_pDefaultFont->uwHeight, 1, 0);
        }

        return pResult;
    }

    text_bitmap_ptr textCreateFromId(u32 stringId, u16 uwMaxWidth, TextHJustify justification)
    {
        Bstring bstrText = langGetStringById(stringId);
        if (!bstrText)
        {
            logWrite("ERROR: Could not find string with id %ld", stringId);
            return text_bitmap_ptr(nullptr);
        }

        bstr_view view(bstrGetData(bstrText));
        return textCreateFromString(view, uwMaxWidth, justification);
    }
}