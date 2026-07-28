#ifndef __NINE_PATCH_H__INCLUDED__
#define __NINE_PATCH_H__INCLUDED__

#include "types.h"

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
                   u16 left,
                   u16 top,
                   u16 right,
                   u16 bottom);
        ~nine_patch() noexcept = default;

        NO_COPY(nine_patch)
        USE_DEFAULT_MOVE(nine_patch)

        ace::bitmap_ptr render(u16 width, u16 height, u32 flags);

        private:  //////////////////////////////////////////////////////////////////////////////////
        ace::bitmap_ptr _source;
        u16 _left;
        u16 _top;
        u16 _right;
        u16 _bottom;
        u16 _patch_width;
        u16 _patch_height;
    };
}  // namespace NEONengine

#endif  // __NINE_PATCH_H__INCLUDED__
