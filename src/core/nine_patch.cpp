#include "nine_patch.h"

#include "types.h"

#include "neonengine.h"

#include <stdint.h>

#include <ace/managers/blit.h>
#include <ace/managers/memory.h>
#include <ace/utils/bitmap.h>

#include "mtl/utility.h"

namespace NEONengine
{
    constexpr UBYTE NINE_PATCH_BITPLANES = 8;

    nine_patch::nine_patch(ace::bitmap_ptr& source,
                           u16 left,
                           u16 top,
                           u16 right,
                           u16 bottom)
        : _source(mtl::move(source))
        , _left(left)
        , _top(top)
        , _right(right)
        , _bottom(bottom)
        , _patch_width(bitmapGetByteWidth(_source.get()) << 3)
        , _patch_height(_source->Rows)
    {}

    u16 nine_patch::width_for(u16 minimum) const
    {
        return to<u16>(minimum + ((_patch_width - minimum) & 0x0F));
    }

    ace::bitmap_ptr nine_patch::render(u16 width, u16 height, u32 flags)
    {
        width = width_for(width);

        auto result = ace::bitmapCreate(
            width, height, NINE_PATCH_BITPLANES, BMF_CLEAR | BMF_INTERLEAVED | flags);
        if (!result) return nullptr;

        u16 l = _left;
        u16 t = _top;
        u16 r = _right;
        u16 b = _bottom;

        u16 w = _patch_width;
        u16 h = _patch_height;

        u16 pr = w - r;  // Patch right start
        u16 pb = h - b;  // Patch bottom start

        u16 uwMidWidth  = w - l - r;
        u16 uwMidHeight = h - t - b;

        // Every tile is read from the same column, so the step must be a multiple
        // of 16 to keep each one at the source's offset mod 16, which blitCopy
        // requires. Rows have no such rule.
        u16 uwStepX = uwMidWidth >= 16 ? to<u16>(uwMidWidth & 0xFFF0) : uwMidWidth;
        u16 uwStepY = uwMidHeight;

        u16 uwMiddleEndX = width - r;
        u16 uwMiddleEndY = height - b;

        tBitMap* src = _source.get();
        tBitMap* dst = result.get();

        blitCopy(src, 0, 0, dst, 0, 0, l, t, MINTERM_COOKIE);                          // Top-left
        blitCopy(src, pr, 0, dst, uwMiddleEndX, 0, r, t, MINTERM_COOKIE);              // Top-right
        blitCopy(src, 0, pb, dst, 0, uwMiddleEndY, l, b, MINTERM_COOKIE);              // Bottom-left
        blitCopy(src, pr, pb, dst, uwMiddleEndX, uwMiddleEndY, r, b, MINTERM_COOKIE);  // Bottom-right

        // Tile top and bottom edges
        for (u16 x = l; x < uwMiddleEndX; x += uwStepX)
        {
            u16 tileW = MIN(uwStepX, uwMiddleEndX - x);
            blitCopy(src, l, 0, dst, x, 0, tileW, t, MINTERM_COOKIE);              // Top edge
            blitCopy(src, l, pb, dst, x, uwMiddleEndY, tileW, b, MINTERM_COOKIE);  // Bottom edge
        }

        // Tile left and right edges
        for (u16 y = t; y < uwMiddleEndY; y += uwStepY)
        {
            u16 tileH = MIN(uwStepY, uwMiddleEndY - y);
            blitCopy(src, 0, t, dst, 0, y, l, tileH, MINTERM_COOKIE);              // Left edge
            blitCopy(src, pr, t, dst, uwMiddleEndX, y, r, tileH, MINTERM_COOKIE);  // Right edge
        }

        // Tile center
        for (u16 x = l; x < uwMiddleEndX; x += uwStepX)
        {
            u16 tileW = MIN(uwStepX, uwMiddleEndX - x);
            for (u16 y = t; y < uwMiddleEndY; y += uwStepY)
            {
                u16 tileH = MIN(uwStepY, uwMiddleEndY - y);
                blitCopy(src, l, t, dst, x, y, tileW, tileH, MINTERM_COOKIE);
            }
        }

        return result;
    }
}  // namespace NEONengine
