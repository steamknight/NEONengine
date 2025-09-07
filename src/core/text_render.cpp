#include "text_render.h"

#include <ace/utils/bitmap.h>
#include <ace/utils/font.h>
#include <ace/managers/system.h>

#include "ace++/font.h"

#include <mtl/cstdint.h>
#include <mtl/memory.h>
#include <mtl/array.h>
#include <mtl/vector.h>
#include <mtl/utility.h>
#include "core/lang.h"
#include "utils/bstr_view.h"

namespace NEONengine
{
    using namespace mtl;
    
    // Configuration constants
    constexpr size_t INITIAL_LINE_CAPACITY = 16;
    constexpr size_t DEFAULT_SCRATCH_CAPACITY = 256;
    
    static tFont* s_font;
    static mtl::vector<char> s_scratch_area;
    static mtl::array<uint16_t, 256> s_glyph_cache(0);

    struct line_data
    {
        u16 start;
        u16 end;

        size_t length() { return end - start; }
    };

    bool break_text_into_lines(bstr_view const& text, u32* p_start_index, u32 max_width, line_data* p_out_data)
    {
        u32 end_of_line = 0u;
        u32 line_width = 0u;
        u32 offset = 1u;
        u32 last_space_pos = 0u;

        if (!text || *p_start_index >= text.length())
        {
            return false;
        }

        auto text_length = text.length();
        auto const text_data = text.data();
        
        // Early exit for newline-only cases
        if (text_data[*p_start_index] == '\n')
        {
            p_out_data->start = *p_start_index;
            p_out_data->end = *p_start_index;
            *p_start_index += 1;
            return *p_start_index <= text_length;
        }

        // NOTE: We go one character beyond the string length to catch the null terminator
        for (u32 idx = *p_start_index; idx <= text_length; ++idx)
        {
            char c = (idx < text_length) ? text_data[idx] : '\0';

            if (c == ' ')
            {
                last_space_pos = idx;
                end_of_line = idx;
            }
            else if (c == '\n' || c == '\0')
            {
                end_of_line = idx;
                break;
            }

            if (c >= ' ')
            {
                u16 glyphWidth = s_glyph_cache[to<u8>(c)];

                line_width += glyphWidth + 1; // +1 for spacing
                
                if (max_width > 0 && line_width > max_width)
                {
                    // Prefer breaking at last space, otherwise break at current position
                    end_of_line = (last_space_pos > *p_start_index) ? last_space_pos : idx;
                    offset = (end_of_line == last_space_pos) ? 1 : 0;
                    break;
                }
            }
        }

        if (end_of_line == 0)
        {
            end_of_line = text_length;
        }

        p_out_data->start = *p_start_index;
        p_out_data->end = end_of_line;
        *p_start_index = end_of_line + offset;
        return true;
    }


    void textRendererCreate(char const *szFontName)
    {
        s_font = fontCreateFromPath(szFontName);
        for (auto glyph = 0; glyph < 256; ++glyph)
        {
            s_glyph_cache[glyph] = fontGlyphWidth(s_font, (char)glyph);
        }

        s_scratch_area.resize(DEFAULT_SCRATCH_CAPACITY);
    }

    void textRendererDestroy()
    {
        if (s_font)
        {
            fontDestroy(s_font);
            s_font = nullptr;
        }

        s_scratch_area.clear();
    }

    ace::text_bitmap_ptr textCreateFromString(bstr_view const& text, u16 uwMaxWidth, TextHJustify justification)
    {
        if (text.empty())
        {
            logWrite("ERROR: Bstring is null.");
            return ace::text_bitmap_ptr(nullptr);
        }

        if (!s_font)
        {
            logWrite("ERROR: Default font is not initialized.");
            return ace::text_bitmap_ptr(nullptr);
        }

        u32 ulStartIndex = 0;

        systemUse();
        auto lines = mtl::array<line_data, INITIAL_LINE_CAPACITY>();
        auto pLineBitmap = ace::text_bitmap_ptr(fontCreateTextBitMap(320, mtl::round_up<16>(s_font->uwHeight)));
        systemUnuse();

        // Create the individual line bitmaps
        line_data line{};
        u32 ulLineCount = 0;
        while((break_text_into_lines(text, &ulStartIndex, uwMaxWidth, &line)))
        {
            lines[ulLineCount++] = line;
        }

        // .. and stitch them all together
        u16 uwHeight = s_font->uwHeight * ulLineCount;
        auto pResult = ace::text_bitmap_ptr(fontCreateTextBitMap(mtl::round_up<16>(uwMaxWidth), mtl::round_up<16>(uwHeight)));
        pResult->uwActualWidth = uwMaxWidth;
        pResult->uwActualHeight = uwHeight;

        for (auto idx = 0u; idx < ulLineCount; ++idx)
        {
            line = lines[idx];
            auto line_length = line.length();
            if (line_length == 0)
                continue;

            // Resize to exact needed size + null terminator
            if (s_scratch_area.size() < line_length + 1)
            {
                logWrite("*******************************************scratch resize");
                s_scratch_area.resize(line_length + 1);
            }

            // Use memcpy for bulk character copying instead of loop
            const char* srcStart = text.data() + line.start;
            memcpy(s_scratch_area.data(), srcStart, line_length);
            s_scratch_area[line_length] = '\0';

            fontFillTextBitMap(s_font, pLineBitmap.get(), s_scratch_area.data());
            logWrite(" -> %s*", s_scratch_area.data());

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

            fontDrawTextBitMap(pResult->pBitMap, pLineBitmap.get(), uwX, idx * s_font->uwHeight, 1, 0);
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