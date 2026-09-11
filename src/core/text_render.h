
/**
 * @file text_render.h
 * @brief Text rendering and layout for NEONengine.
 *
 */
#ifndef __TEXT_RENDERER__INCLUDED_H__
#define __TEXT_RENDERER__INCLUDED_H__

#include "types.h"

#include <stdint.h>

#include <ace++/font.h>

#include <mtl/array.h>
#include <mtl/expected.h>
#include <mtl/memory.h>
#include <mtl/vector.h>

#include "utils/bstr_view.h"

namespace NEONengine
{
    /**
     * @enum text_justify
     * @brief Defines how the text should be justified horizontally.
     */
    enum class text_justify
    {
        LEFT,
        RIGHT,
        CENTER,
    };

    /**
     * @class text_renderer
     * @brief Renders text using ace++ font and provides line breaking and justification.
     *
     * Usage:
     * @code
     * auto result = text_renderer::create(pFont);
     * if (result) { auto bmp = result.value()->create_text("Hello", 200, text_justify::CENTER); }
     * @endcode
     */
    class text_renderer;
    using text_renderer_ptr = mtl::unique_ptr<text_renderer>;

    /**
     * @brief The space a wrapped string would take, in pixels.
     */
    struct text_extent
    {
        u16 width{ 0 };
        u16 height{ 0 };
    };

    class text_renderer
    {
        public:  ///////////////////////////////////////////////////////////////////////////////////
        enum class error_code
        {
            INVALID_FONT_POINTER,
        };

        using result = mtl::expected<text_renderer_ptr, error_code>;

        public:  ///////////////////////////////////////////////////////////////////////////////////
        /**
         * @brief Renders @p text into a bitmap, wrapped to @p maxWidth.
         *
         * The bitmap is as wide as the widest line the text broke into, which
         * is at most @p maxWidth, so a caller drawing a box around it can size
         * that box to the text rather than to the width it allowed.
         */
        ace::text_bitmap_ptr create_text(bstr_view const& text,
                                         u16 maxWidth,
                                         text_justify justification);

        /**
         * @brief What create_text() would report for @p text, without drawing it.
         *
         * Breaks the text exactly as create_text() does but allocates no
         * bitmap, so a caller sizing a layout around several strings pays for
         * the ones it goes on to draw and no more.
         *
         * @return The extent, zero in both directions when there is nothing to
         *         draw or no font to draw it with.
         */
        text_extent measure_text(bstr_view const& text, u16 maxWidth);

        /**
         * @brief Returns the height of a line of text, in pixels.
         */
        u16 line_height() const { return _font ? _font->uwHeight : 0; }

        static result create(tFont* pFont);

        private:  //////////////////////////////////////////////////////////////////////////////////
        static constexpr size_t MAX_LINES = 16;

        struct line_data
        {
            u16 start;
            u16 end;
            u16 width;  ///< Rendered width in pixels, measured as ACE measures it.

            size_t length() const { return end - start; }
        };

        text_renderer(tFont* pFont);

        bool break_text_into_lines(bstr_view const& text,
                                   u32* pStartIndex,
                                   u32 maxWidth,
                                   line_data* pOutData);

        /**
         * @brief Breaks @p text into at most MAX_LINES lines in @p pLines.
         *
         * @return How many lines it broke into, zero when there are none.
         */
        u32 lay_out_lines(bstr_view const& text, u16 maxWidth, line_data* pLines);

        /** @brief The width @p lineCount lines occupy, capped at @p maxWidth. */
        static u16 content_width(line_data const* pLines, u32 lineCount, u16 maxWidth);

        void ensure_line_bitmap(u16 width);

        private:  //////////////////////////////////////////////////////////////////////////////////
        tFont* _font;
        mtl::vector<char> _scratch_area;
        mtl::array<u16, 256> _glyph_cache;
        ace::text_bitmap_ptr _line_bitmap;
        u16 _line_bitmap_width;
    };

}  // namespace NEONengine

#endif  // __TEXT_RENDERER__INCLUDED_H__
