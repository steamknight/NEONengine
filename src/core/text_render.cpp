#include "text_render.h"

#include "types.h"

#include "neonengine.h"

#include <ace/managers/system.h>

#include <ace++/font.h>
#include <ace++/log.h>

#include <mtl/utility.h>
#include <mtl/expected.h>

#include "core/logger.h"

NE_LOG_DOMAIN(Text);

namespace NEONengine
{
    using namespace mtl;

    constexpr size_t DEFAULT_SCRATCH_CAPACITY = 256;

    bool text_renderer::break_text_into_lines(bstr_view const& text,
                                              u32* pStartIndex,
                                              u32 maxWidth,
                                              line_data* pOutData)
    {
        if (text.is_empty() || *pStartIndex >= text.length()) { return false; }

        auto const textLength = text.length();
        auto const textData   = text.data();
        auto const start      = *pStartIndex;

        // Handle standalone newline
        if (textData[start] == '\n')
        {
            pOutData->start = start;
            pOutData->end   = start;
            pOutData->width = 0;
            *pStartIndex    = start + 1;
            return start + 1 <= textLength;
        }

        u32 lineWidth    = 0;
        u32 breakWidth   = 0;
        u32 lastBreakPos = 0;
        bool     hasBreak     = false;

        for (u32 idx = start; idx <= textLength; ++idx)
        {
            char c = (idx < textLength) ? textData[idx] : '\0';

            // Hard breaks: newline or end of string
            if (c == '\n' || c == '\0')
            {
                pOutData->start = start;
                pOutData->end   = idx;
                pOutData->width = to<u16>(lineWidth);
                *pStartIndex    = idx + 1;
                return true;
            }

            // Track word boundaries
            if (c == ' ')
            {
                lastBreakPos = idx;
                breakWidth   = lineWidth;
                hasBreak     = true;
            }

            // Measure width (1px inter-character spacing per ACE font spec)
            u32 const glyphWidth = _glyph_cache[to<u8>(c)] + 1;
            lineWidth += glyphWidth;

            // Soft wrap when exceeding maxWidth (0 = no wrapping)
            if (maxWidth > 0 && lineWidth > maxWidth)
            {
                if (hasBreak)
                {
                    pOutData->start = start;
                    pOutData->end   = lastBreakPos;
                    pOutData->width = to<u16>(breakWidth);
                    *pStartIndex    = lastBreakPos + 1;
                }
                else
                {
                    pOutData->start = start;
                    pOutData->end   = idx;
                    // The overflowing character starts the next line, so its own
                    // width is not part of this one.
                    pOutData->width = to<u16>(lineWidth - glyphWidth);
                    // Guarantee forward progress: consume at least one character
                    *pStartIndex = (idx > start) ? idx : idx + 1;
                }
                return true;
            }
        }

        // Safety fallback
        pOutData->start = start;
        pOutData->end   = textLength;
        pOutData->width = to<u16>(lineWidth);
        *pStartIndex    = textLength + 1;
        return true;
    }

    text_renderer::text_renderer(tFont* pFont)
        : _font(pFont)
        , _line_bitmap(nullptr)
        , _line_bitmap_width(0)
    {
        ACE_LOG_BLOCK("NEONengine::text_renderer::text_renderer");

        if (!pFont)
        {
            NE_LOG("Invalid font pointer. Skipping construction.");
            return;
        }

        for (auto glyph = 0; glyph < 256; ++glyph)
        {
            _glyph_cache[glyph] = fontGlyphWidth(_font, (char)glyph);
        }

        _scratch_area.resize(DEFAULT_SCRATCH_CAPACITY);
    }

    text_renderer::result text_renderer::create(tFont* pFont)
    {
        if (!pFont)
        {
            NE_LOG("Invalid font pointer.");
            return mtl::unexpected(text_renderer::error_code::INVALID_FONT_POINTER);
        }

        return text_renderer_ptr(new (MemF::Fast) text_renderer(pFont));
    }

    void text_renderer::ensure_line_bitmap(u16 width)
    {
        auto const alignedWidth = mtl::round_up<16>(width);
        if (_line_bitmap && _line_bitmap_width >= alignedWidth) { return; }

        systemUse();
        _line_bitmap = ace::fontCreateTextBitMap(
            alignedWidth, mtl::round_up<16>(_font->uwHeight));
        systemUnuse();

        _line_bitmap_width = alignedWidth;
    }

    u32 text_renderer::lay_out_lines(bstr_view const& text, u16 maxWidth, line_data* pLines)
    {
        u32 startIndex = 0;
        u32 lineCount  = 0;
        line_data line{};

        while (lineCount < MAX_LINES && break_text_into_lines(text, &startIndex, maxWidth, &line))
        {
            pLines[lineCount++] = line;
        }

        return lineCount;
    }

    u16 text_renderer::content_width(line_data const* pLines, u32 lineCount, u16 maxWidth)
    {
        // The widest line is what the text actually occupies. A single character
        // too wide to wrap can exceed the allowance, so it is still capped.
        u16 width = 0;
        for (auto idx = 0u; idx < lineCount; ++idx)
        {
            if (pLines[idx].width > width) width = pLines[idx].width;
        }
        if (width > maxWidth) width = maxWidth;

        return width;
    }

    text_extent text_renderer::measure_text(bstr_view const& text, u16 maxWidth)
    {
        if (text.is_empty() || !_font) return text_extent{};

        auto lines          = mtl::array<line_data, MAX_LINES>();
        u32 const lineCount = lay_out_lines(text, maxWidth, lines.begin());
        if (lineCount == 0) return text_extent{};

        return text_extent{ content_width(lines.begin(), lineCount, maxWidth),
            to<u16>(_font->uwHeight * lineCount) };
    }

    ace::text_bitmap_ptr text_renderer::create_text(bstr_view const& text,
                                                    u16 maxWidth,
                                                    text_justify justification)
    {
        if (text.is_empty())
        {
            NE_LOG("text is empty.");
            return ace::text_bitmap_ptr(nullptr);
        }

        if (!_font)
        {
            NE_LOG("Default font is not initialized.");
            return ace::text_bitmap_ptr(nullptr);
        }

        ensure_line_bitmap(maxWidth);
        if (!_line_bitmap)
        {
            NE_LOG("Could not allocate the line bitmap.");
            return ace::text_bitmap_ptr(nullptr);
        }

        // Break text into lines
        auto lines = mtl::array<line_data, MAX_LINES>();
        line_data line{};
        u32 const lineCount = lay_out_lines(text, maxWidth, lines.begin());

        if (lineCount == 0) { return ace::text_bitmap_ptr(nullptr); }

        u16 const contentWidth = content_width(lines.begin(), lineCount, maxWidth);
        if (contentWidth == 0) return ace::text_bitmap_ptr(nullptr);

        // Allocate the result bitmap
        u16 height = _font->uwHeight * lineCount;
        auto pResult
            = ace::fontCreateTextBitMap(mtl::round_up<16>(contentWidth), mtl::round_up<16>(height));

        if (!pResult)
        {
            NE_LOG("Could not allocate a %hux%hu text bitmap.", contentWidth, height);
            return ace::text_bitmap_ptr(nullptr);
        }

        pResult->uwActualWidth  = contentWidth;
        pResult->uwActualHeight = height;

        // Render each line and composite into the result
        for (auto idx = 0u; idx < lineCount; ++idx)
        {
            line            = lines[idx];
            auto lineLength = line.length();
            if (lineLength == 0) continue;

            if (_scratch_area.size() < lineLength + 1) { _scratch_area.resize(lineLength + 1); }

            char const* srcStart = text.data() + line.start;
            memcpy(_scratch_area.data(), srcStart, lineLength);
            _scratch_area[lineLength] = '\0';

            fontFillTextBitMap(_font, _line_bitmap.get(), _scratch_area.data());
            NE_LOG("-> %s", _scratch_area.data());

            // Justified within the text's own width, not the width it was
            // allowed, so the bitmap holds no empty margin for a caller to
            // draw a box around.
            u16 const slack = contentWidth > _line_bitmap->uwActualWidth
                ? to<u16>(contentWidth - _line_bitmap->uwActualWidth)
                : u16(0);

            u16 x = 0;
            switch (justification)
            {
                case text_justify::RIGHT: x = slack; break;

                case text_justify::CENTER: x = slack >> 1; break;

                case text_justify::LEFT:  // fallthrough
                default: x = 0; break;
            }

            fontDrawTextBitMap(
                pResult->pBitMap, _line_bitmap.get(), x, idx * _font->uwHeight, 1, 0);
        }

        return pResult;
    }
}  // namespace NEONengine
