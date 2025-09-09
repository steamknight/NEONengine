
/**
 * @file text_render.h
 * @brief Text rendering and layout for NEONengine.
 *
 */
#ifndef __TEXT_RENDERER__INCLUDED_H__
#define __TEXT_RENDERER__INCLUDED_H__

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
    /**
     * @brief Unique pointer to text_renderer.
     */
    using text_renderer_ptr = mtl::unique_ptr<text_renderer>;

    class text_renderer
    {
        public:  ///////////////////////////////////////////////////////////////////////////////////
        /**
         * @enum error_code
         * @brief Error codes for text_renderer operations.
         */
        enum class error_code
        {
            INVALID_FONT_POINTER,
        };

        /**
         * @brief Result type for text_renderer creation.
         */
        using result = mtl::expected<text_renderer_ptr, error_code>;

        public:  ///////////////////////////////////////////////////////////////////////////////////
        /**
         * @brief Render text to a bitmap with justification and max width.
         * @param text The text to render.
         * @param maxWidth Maximum width of the rendered text.
         * @param justification Horizontal justification.
         * @return Pointer to rendered text bitmap.
         */
        ace::text_bitmap_ptr create_text(bstr_view const& text,
                                         uint16_t maxWidth,
                                         text_justify justification);

        /**
         * @brief Create a text_renderer from a font pointer.
         * @param pFont Pointer to .
         * @return result (success: text_renderer_ptr, error: error_code)
         */
        static result create(tFont* pFont);

        private:  //////////////////////////////////////////////////////////////////////////////////
        struct line_data
        {
            uint16_t start;
            uint16_t end;

            /**
             * @brief Get the length of the line.
             * @return Number of characters in the line.
             */
            size_t length() { return end - start; }
        };

        /**
         * @brief Construct a text_renderer from a font pointer.
         * @param pFont Pointer to ace font.
         */
        text_renderer(tFont* pFont);

        bool break_text_into_lines(bstr_view const& text,
                                   uint32_t* pStartIndex,
                                   uint32_t maxWidth,
                                   line_data* pOutData);

        private:  //////////////////////////////////////////////////////////////////////////////////
        tFont* _pFont;
        mtl::vector<char> _scratchArea;
        mtl::array<uint16_t, 256> _glyphCache;
    };

}  // namespace NEONengine

#endif  // __TEXT_RENDERER__INCLUDED_H__