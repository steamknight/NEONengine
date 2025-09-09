#include "text_render.h"

#include "neonengine.h"

#include <ace/managers/system.h>

#include <ace++/font.h>
#include <ace++/log.h>

#include <mtl/utility.h>

namespace NEONengine
{
    using namespace mtl;

    // Configuration constants
    constexpr size_t INITIAL_LINE_CAPACITY    = 16;
    constexpr size_t DEFAULT_SCRATCH_CAPACITY = 256;

    bool text_renderer::break_text_into_lines(bstr_view const& text,
                                              uint32_t* pStartIndex,
                                              uint32_t maxWidth,
                                              line_data* pOutData)
    {
        uint32_t endOfLine    = 0u;
        uint32_t lineWidth    = 0u;
        uint32_t offset       = 1u;
        uint32_t lastSpacePos = 0u;

        if (text.is_empty() || *pStartIndex >= text.length()) { return false; }

        auto textLength     = text.length();
        auto const textData = text.data();

        // Early exit for newline-only cases
        if (textData[*pStartIndex] == '\n')
        {
            pOutData->start = *pStartIndex;
            pOutData->end   = *pStartIndex;
            *pStartIndex += 1;
            return *pStartIndex <= textLength;
        }

        // NOTE: We go one character beyond the string length to catch the null terminator
        for (uint32_t idx = *pStartIndex; idx <= textLength; ++idx)
        {
            char c = (idx < textLength) ? textData[idx] : '\0';

            if (c == ' ')
            {
                lastSpacePos = idx;
                endOfLine    = idx;
            }
            else if (c == '\n' || c == '\0')
            {
                endOfLine = idx;
                break;
            }

            if (c >= ' ')
            {
                uint16_t glyphWidth = _glyphCache[to<uint8_t>(c)];

                lineWidth += glyphWidth + 1;  // +1 for spacing

                if (maxWidth > 0 && lineWidth > maxWidth)
                {
                    // Prefer breaking at last space, otherwise break at current position
                    endOfLine = (lastSpacePos > *pStartIndex) ? lastSpacePos : idx;
                    offset    = (endOfLine == lastSpacePos) ? 1 : 0;
                    break;
                }
            }
        }

        if (endOfLine == 0) { endOfLine = textLength; }

        pOutData->start = *pStartIndex;
        pOutData->end   = endOfLine;
        *pStartIndex    = endOfLine + offset;
        return true;
    }

    text_renderer::text_renderer(tFont* pFont) : _pFont(pFont)
    {
        ACE_LOG_BLOCK("NEONengine::text_renderer::text_renderer");

        if (!pFont)
        {
            NE_LOG("Text Renderer: Invalid font pointer. Skipping construction.");
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
            NE_LOG("Text Renderer: Invalid font pointer.");

            return mtl::make_error<text_renderer_ptr, error_code>(
                text_renderer::error_code::INVALID_FONT_POINTER);
        }

        return mtl::make_success<text_renderer_ptr, error_code>(
            text_renderer_ptr(new (MemF::Fast) text_renderer(pFont)));
    }

    ace::text_bitmap_ptr text_renderer::create_text(bstr_view const& text,
                                                    uint16_t maxWidth,
                                                    text_justify justification)
    {
        if (text.is_empty())
        {
            logWrite("ERROR: Bstring is null.");
            return ace::text_bitmap_ptr(nullptr);
        }

        if (!_pFont)
        {
            logWrite("ERROR: Default font is not initialized.");
            return ace::text_bitmap_ptr(nullptr);
        }

        uint32_t startIndex = 0;

        systemUse();
        auto lines       = mtl::array<line_data, INITIAL_LINE_CAPACITY>();
        auto pLineBitmap = ace::fontCreateTextBitMap(320, mtl::round_up<16>(_pFont->uwHeight));
        systemUnuse();

        // Create the individual line bitmaps
        line_data line{};
        uint32_t lineCount = 0;
        while ((break_text_into_lines(text, &startIndex, maxWidth, &line)))
        {
            lines[lineCount++] = line;
        }

        // ... and stitch them all together
        uint16_t height = _pFont->uwHeight * lineCount;
        auto pResult    = ace::fontCreateTextBitMap(mtl::round_up<16>(maxWidth), mtl::round_up<16>(height));

        pResult->uwActualWidth  = maxWidth;
        pResult->uwActualHeight = height;

        for (auto idx = 0u; idx < lineCount; ++idx)
        {
            line            = lines[idx];
            auto lineLength = line.length();
            if (lineLength == 0) continue;

            // Resize to exact needed size + null terminator
            if (_scratchArea.size() < lineLength + 1) { _scratchArea.resize(lineLength + 1); }

            // Use memcpy for bulk character copying instead of loop
            char const* srcStart = text.data() + line.start;
            memcpy(_scratchArea.data(), srcStart, lineLength);
            _scratchArea[lineLength] = '\0';

            fontFillTextBitMap(_pFont, pLineBitmap.get(), _scratchArea.data());
            logWrite(" -> %s*", _scratchArea.data());

            uint16_t x = 0;
            switch (justification)
            {
                case text_justify::RIGHT:  //
                    x = maxWidth - pLineBitmap->uwActualWidth;
                    break;

                case text_justify::CENTER:  //
                    x = (maxWidth - pLineBitmap->uwActualWidth) >> 1;
                    break;

                case text_justify::LEFT:  // fallthrough
                default: x = 0; break;
            }

            fontDrawTextBitMap(
                pResult->pBitMap, pLineBitmap.get(), x, idx * _pFont->uwHeight, 1, 0);
        }

        return pResult;
    }
}  // namespace NEONengine