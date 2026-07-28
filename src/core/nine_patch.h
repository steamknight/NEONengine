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
         * @brief Takes ownership of @p source and destroys it with the patch.
         */
        nine_patch(ace::bitmap_ptr &source,
                   uint16_t left,
                   uint16_t top,
                   uint16_t right,
                   uint16_t bottom);
        ~nine_patch() noexcept = default;

        NO_COPY(nine_patch)
        USE_DEFAULT_MOVE(nine_patch)

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