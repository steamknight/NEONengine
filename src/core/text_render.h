
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
         */
        ace::text_bitmap_ptr create_text(bstr_view const& text,
                                         u16 maxWidth,
                                         text_justify justification);

        static result create(tFont* pFont);

        private:  //////////////////////////////////////////////////////////////////////////////////
        static constexpr size_t MAX_LINES = 16;

        struct line_data
        {
            u16 start;
            u16 end;

            size_t length() const { return end - start; }
        };

        text_renderer(tFont* pFont);

        bool break_text_into_lines(bstr_view const& text,
                                   u32* pStartIndex,
                                   u32 maxWidth,
                                   line_data* pOutData);

        void ensure_line_bitmap(u16 width);

        private:  //////////////////////////////////////////////////////////////////////////////////
        tFont* _pFont;
        mtl::vector<char> _scratchArea;
        mtl::array<u16, 256> _glyphCache;
        ace::text_bitmap_ptr _pLineBitmap;
        u16 _lineBitmapWidth;
    };

}  // namespace NEONengine

#endif  // __TEXT_RENDERER__INCLUDED_H__
