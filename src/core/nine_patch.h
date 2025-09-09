#ifndef __NINE_PATCH_H__INCLUDED__
#define __NINE_PATCH_H__INCLUDED__

#include <stdint.h>

#include <ace/types.h>

#include "ace++/bitmap.h"
#include "mtl/utility.h"

namespace NEONengine
{
    class nine_patch
    {
        public:  ///////////////////////////////////////////////////////////////////////////////////
        /**
         * @brief Constructor
         *
         * NOTE: The nine-patch takes ownership of the patch bitmap that is used in
         * rendering the final patch. It will destroy the bitmap once it's destroyed
         *
         * @param ppSource A handle to the nine patch image to use
         * @param uwLeft Size of the left border
         * @param uwTop Size of the top border
         * @param uwRight Size of the right border
         * @param uwBottom Size of the bottom border
         * @return NinePatch used for calls to `ninePatchRender`
         *
         * @see ninePatchDestroy
         * @see ninePatchRender
         */
        nine_patch(ace::bitmap_ptr &source,
                   uint16_t left,
                   uint16_t top,
                   uint16_t right,
                   uint16_t bottom);
        ~nine_patch() noexcept = default;

        NO_COPY(nine_patch)
        USE_DEFAULT_MOVE(nine_patch)

        /**
         * Renders a nine-patch bitmap with the specified dimensions and flags.
         *
         * @param patch Pointer to the NinePatch structure containing source and border sizes.
         * @param uwWidth Desired width of the resulting bitmap.
         * @param uwHeight Desired height of the resulting bitmap.
         * @param ulFlags Bitmap creation flags.
         * @return Pointer to the newly created tBitMap, or NULL on failure.
         */

        ace::bitmap_ptr render(uint16_t width, uint16_t height, uint32_t flags);

        private:  //////////////////////////////////////////////////////////////////////////////////
        ace::bitmap_ptr _source;
        uint16_t _left;
        uint16_t _top;
        uint16_t _right;
        uint16_t _bottom;
        uint16_t _patch_width;
        uint16_t _patch_height;
    };
}  // namespace NEONengine

#endif  // __NINE_PATCH_H__INCLUDED__