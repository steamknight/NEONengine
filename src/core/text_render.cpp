#include "text_render.h"

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
                                              uint32_t* pStartIndex,
                                              uint32_t maxWidth,
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
            *pStartIndex    = start + 1;
            return start + 1 <= textLength;
        }

        uint32_t lineWidth    = 0;
        uint32_t lastBreakPos = 0;
        bool     hasBreak     = false;

        for (uint32_t idx = start; idx <= textLength; ++idx)
        {
            char c = (idx < textLength) ? textData[idx] : '\0';

            // Hard breaks: newline or end of string
            if (c == '\n' || c == '\0')
            {
                pOutData->start = start;
                pOutData->end   = idx;
                *pStartIndex    = idx + 1;
                return true;
            }

            // Track word boundaries
            if (c == ' ')
            {
                lastBreakPos = idx;
                hasBreak     = true;
            }

            // Measure width (1px inter-character spacing per ACE font spec)
            lineWidth += _glyphCache[to<uint8_t>(c)] + 1;

            // Soft wrap when exceeding maxWidth (0 = no wrapping)
            if (maxWidth > 0 && lineWidth > maxWidth)
            {
                if (hasBreak)
                {
                    pOutData->start = start;
                    pOutData->end   = lastBreakPos;
                    *pStartIndex    = lastBreakPos + 1;
                }
                else
                {
                    pOutData->start = start;
                    pOutData->end   = idx;
                    // Guarantee forward progress: consume at least one character
                    *pStartIndex = (idx > start) ? idx : idx + 1;
                }
                return true;
            }
        }

        // Safety fallback
        pOutData->start = start;
        pOutData->end   = textLength;
        *pStartIndex    = textLength + 1;
        return true;
    }

    text_renderer::text_renderer(tFont* pFont)
        : _pFont(pFont)
        , _pLineBitmap(nullptr)
        , _lineBitmapWidth(0)
    {
        ACE_LOG_BLOCK("NEONengine::text_renderer::text_renderer");

        if (!pFont)
        {
            NE_LOG("Invalid font pointer. Skipping construction.");
            return;
        }

        for (auto glyph = 0; glyph < 256; ++glyph)
        {
            _glyphCache[glyph] = fontGlyphWidth(_pFont, (char)glyph);
        }

        _scratchArea.resize(DEFAULT_SCRATCH_CAPACITY);
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

    void text_renderer::ensure_line_bitmap(uint16_t width)
    {
        auto const alignedWidth = mtl::round_up<16>(width);
        if (_pLineBitmap && _lineBitmapWidth >= alignedWidth) { return; }

        systemUse();
        _pLineBitmap = ace::fontCreateTextBitMap(
            alignedWidth, mtl::round_up<16>(_pFont->uwHeight));
        systemUnuse();

        _lineBitmapWidth = alignedWidth;
    }

    ace::text_bitmap_ptr text_renderer::create_text(bstr_view const& text,
                                                    uint16_t maxWidth,
                                                    text_justify justification)
    {
        if (text.is_empty())
        {
            NE_LOG("text is empty.");
            return ace::text_bitmap_ptr(nullptr);
        }

        if (!_pFont)
        {
            NE_LOG("Default font is not initialized.");
            return ace::text_bitmap_ptr(nullptr);
        }

        ensure_line_bitmap(maxWidth);

        // Break text into lines
        uint32_t startIndex = 0;
        auto lines = mtl::array<line_data, MAX_LINES>();
        line_data line{};
        uint32_t lineCount = 0;

        while (lineCount < MAX_LINES
               && break_text_into_lines(text, &startIndex, maxWidth, &line))
        {
            lines[lineCount++] = line;
        }

        if (lineCount == 0) { return ace::text_bitmap_ptr(nullptr); }

        // Allocate the result bitmap
        uint16_t height = _pFont->uwHeight * lineCount;
        auto pResult
            = ace::fontCreateTextBitMap(mtl::round_up<16>(maxWidth), mtl::round_up<16>(height));

        pResult->uwActualWidth  = maxWidth;
        pResult->uwActualHeight = height;

        // Render each line and composite into the result
        for (auto idx = 0u; idx < lineCount; ++idx)
        {
            line            = lines[idx];
            auto lineLength = line.length();
            if (lineLength == 0) continue;

            if (_scratchArea.size() < lineLength + 1) { _scratchArea.resize(lineLength + 1); }

            char const* srcStart = text.data() + line.start;
            memcpy(_scratchArea.data(), srcStart, lineLength);
            _scratchArea[lineLength] = '\0';

            fontFillTextBitMap(_pFont, _pLineBitmap.get(), _scratchArea.data());
            NE_LOG("-> %s", _scratchArea.data());

            uint16_t x = 0;
            switch (justification)
            {
                case text_justify::RIGHT:
                    x = maxWidth - _pLineBitmap->uwActualWidth;
                    break;

                case text_justify::CENTER:
                    x = (maxWidth - _pLineBitmap->uwActualWidth) >> 1;
                    break;

                case text_justify::LEFT:  // fallthrough
                default: x = 0; break;
            }

            fontDrawTextBitMap(
                pResult->pBitMap, _pLineBitmap.get(), x, idx * _pFont->uwHeight, 1, 0);
        }

        return pResult;
    }
}  // namespace NEONengine
