#include "nine_patch.h"
#include "neonengine.h"

#include <ace/managers/blit.h>
#include <ace/managers/memory.h>
#include <ace/utils/bitmap.h>
#include <stdint.h>

#include "mtl/utility.h"

namespace NEONengine
{
    constexpr UBYTE NINE_PATCH_BITPLANES = 8;

    nine_patch::nine_patch(ace::bitmap_ptr &source, uint16_t left, uint16_t top, uint16_t right, uint16_t bottom)
        : _source(mtl::move(source))
        , _left(left)
        , _top(top)
        , _right(right)
        , _bottom(bottom)
        , _patch_width(bitmapGetByteWidth(_source.get()) << 3)
        , _patch_height(_source->Rows)
    {}

    /**
     * Renders a nine-patch bitmap with the specified dimensions and flags.
     * 
     * @param patch Pointer to the NinePatch structure containing source and border sizes.
     * @param uwWidth Desired width of the resulting bitmap.
     * @param uwHeight Desired height of the resulting bitmap.
     * @param ulFlags Bitmap creation flags.
     * @return Pointer to the newly created tBitMap, or NULL on failure.
     */

    ace::bitmap_ptr nine_patch::render(uint16_t width, uint16_t height, uint32_t flags)
    {
        auto result = ace::bitmapCreate(width, height, NINE_PATCH_BITPLANES, BMF_CLEAR | BMF_INTERLEAVED | flags);
        if (!result)
            return nullptr;

        // Aliases for easier reading
        uint16_t l = _left;
        uint16_t t = _top;
        uint16_t r = _right;
        uint16_t b = _bottom;

        uint16_t w = _patch_width;
        uint16_t h = _patch_height;

        uint16_t pr = w - r;   // Patch right start
        uint16_t pb = h - b;   // Patch bottom start

        uint16_t uwMidWidth = w - l - r;
        uint16_t uwMidHeight = h - t - b;

        uint16_t uwMiddleEndX = width - r;
        uint16_t uwMiddleEndY = height - b;

        tBitMap* src = _source.get();
        tBitMap* dst = result.get();

        blitCopy(src,  0,  0, dst,            0,            0, l, t, MINTERM_COPY); // Top-left
        blitCopy(src, pr,  0, dst, uwMiddleEndX,            0, r, t, MINTERM_COPY); // Top-right
        blitCopy(src,  0, pb, dst,            0, uwMiddleEndY, l, b, MINTERM_COPY); // Bottom-left
        blitCopy(src, pr, pb, dst, uwMiddleEndX, uwMiddleEndY, r, b, MINTERM_COPY); // Bottom-right

        // Tile top and bottom edges
        for (uint16_t x = l; x < uwMiddleEndX; x += uwMidWidth) {
            uint16_t tileW = MIN(uwMidWidth, uwMiddleEndX - x);
            blitCopy(src, l,  0, dst, x,            0, tileW, t, MINTERM_COPY);     // Top edge
            blitCopy(src, l, pb, dst, x, uwMiddleEndY, tileW, b, MINTERM_COPY);     // Bottom edge
        }

        // Tile left and right edges
        for (uint16_t y = t; y < uwMiddleEndY; y += uwMidHeight) {
            uint16_t tileH = MIN(uwMidHeight, uwMiddleEndY - y);
            blitCopy(src,  0, t, dst,            0, y, l, tileH, MINTERM_COPY);     // Left edge
            blitCopy(src, pr, t, dst, uwMiddleEndX, y, r, tileH, MINTERM_COPY);     // Right edge
        }

        // Tile center
        for (uint16_t x = l; x < uwMiddleEndX; x += uwMidWidth) {
            uint16_t tileW = MIN(uwMidWidth, uwMiddleEndX - x);
            for (uint16_t y = t; y < uwMiddleEndY; y += uwMidHeight) {
                uint16_t tileH = MIN(uwMidHeight, uwMiddleEndY - y);
                blitCopy(src, l, t, dst, x, y, tileW, tileH, MINTERM_COPY);
            }
        }

        return result;
    }
}