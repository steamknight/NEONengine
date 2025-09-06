#include "nine_patch.h"
#include "neonengine.h"

#include <ace/managers/blit.h>
#include <ace/managers/memory.h>
#include <ace/utils/bitmap.h>

#include "mtl/utility.h"

namespace NEONengine
{
    constexpr UBYTE NINE_PATCH_BITPLANES = 8;

    struct _NinePatch
    {
        tBitMap** ppSource;
        UWORD uwLeftBorder;
        UWORD uwTopBorder;
        UWORD uwRightBorder;
        UWORD uwBottomBorder;
        UWORD uwPatchWidth;
        UWORD uwPatchHeight;
    };

    NinePatch ninePatchCreate(tBitMap **ppSource, UWORD uwLeftBorder, UWORD uwTopBorder, UWORD uwRightBorder, UWORD uwBottomBorder)
    {
        NinePatch pPatch = (NinePatch)memAlloc(sizeof(_NinePatch), MEMF_FAST);
        if (!pPatch)
            return NULL;
        
        UWORD uwWidth = bitmapGetByteWidth(*ppSource) << 3;
        UWORD uwHeight = (*ppSource)->Rows;

        pPatch->ppSource = ppSource;

        pPatch->uwLeftBorder = uwLeftBorder;
        pPatch->uwTopBorder = uwTopBorder;
        pPatch->uwRightBorder = uwRightBorder;
        pPatch->uwBottomBorder = uwBottomBorder;

        pPatch->uwPatchWidth = uwWidth;
        pPatch->uwPatchHeight = uwHeight;

        return pPatch;
    }

    void ninePatchDestroy(NinePatch* pPatch)
    {
        if (!pPatch || !*pPatch)
            return;

        bitmapDestroy(*((*pPatch)->ppSource));
        (*pPatch)->ppSource = NULL;

        memFree(*pPatch, sizeof(_NinePatch));
        *pPatch = NULL;
    }

    tBitMap* ninePatchRender(NinePatch patch, UWORD uwWidth, UWORD uwHeight, ULONG ulFlags)
    {
        tBitMap* pResult = bitmapCreate(mtl::ROUND_16(uwWidth), mtl::ROUND_16(uwHeight), NINE_PATCH_BITPLANES, BMF_CLEAR | BMF_INTERLEAVED | ulFlags);
        if (!pResult)
            return NULL;

        // Aliases for easier reading
        UWORD l = patch->uwLeftBorder;
        UWORD t = patch->uwTopBorder;
        UWORD r = patch->uwRightBorder;
        UWORD b = patch->uwBottomBorder;

        UWORD w = patch->uwPatchWidth;
        UWORD h = patch->uwPatchHeight;

        UWORD pr = w - r;   // Patch right start
        UWORD pb = h - b;   // Patch bottom start

        UWORD uwMidWidth = w - l - r;
        UWORD uwMidHeight = h - t - b;

        UWORD uwMiddleEndX = uwWidth - r;
        UWORD uwMiddleEndY = uwHeight - b;

        tBitMap* src = *(patch->ppSource);

        blitCopy(src,  0,  0, pResult,            0,            0, l, t, MINTERM_COPY); // Top-left
        blitCopy(src, pr,  0, pResult, uwMiddleEndX,            0, r, t, MINTERM_COPY); // Top-right
        blitCopy(src,  0, pb, pResult,            0, uwMiddleEndY, l, b, MINTERM_COPY); // Bottom-left
        blitCopy(src, pr, pb, pResult, uwMiddleEndX, uwMiddleEndY, r, b, MINTERM_COPY); // Bottom-right

        // Tile top and bottom edges
        for (UWORD x = l; x < uwMiddleEndX; x += uwMidWidth) {
            UWORD tileW = MIN(uwMidWidth, uwMiddleEndX - x);
            blitCopy(src, l,  0, pResult, x,            0, tileW, t, MINTERM_COPY);     // Top edge
            blitCopy(src, l, pb, pResult, x, uwMiddleEndY, tileW, b, MINTERM_COPY);     // Bottom edge
        }

        // Tile left and right edges
        for (UWORD y = t; y < uwMiddleEndY; y += uwMidHeight) {
            UWORD tileH = MIN(uwMidHeight, uwMiddleEndY - y);
            blitCopy(src,  0, t, pResult,            0, y, l, tileH, MINTERM_COPY);     // Left edge
            blitCopy(src, pr, t, pResult, uwMiddleEndX, y, r, tileH, MINTERM_COPY);     // Right edge
        }

        // Tile center
        for (UWORD x = l; x < uwMiddleEndX; x += uwMidWidth) {
            UWORD tileW = MIN(uwMidWidth, uwMiddleEndX - x);
            for (UWORD y = t; y < uwMiddleEndY; y += uwMidHeight) {
                UWORD tileH = MIN(uwMidHeight, uwMiddleEndY - y);
                blitCopy(src, l, t, pResult, x, y, tileW, tileH, MINTERM_COPY);
            }
        }

        return pResult;
    }
}