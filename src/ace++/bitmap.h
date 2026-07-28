#ifndef __ACEPP__BITMAP_H___INCLUDED__
#define __ACEPP__BITMAP_H___INCLUDED__

#include <ace/utils/bitmap.h>

#include <ace++/file.h>

#include <mtl/memory.h>
#include <mtl/utility.h>

namespace ace
{
    using bitmap_ptr = mtl::unique_ptr<tBitMap, ::bitmapDestroy>;

    inline bitmap_ptr bitmapCreate(UWORD uwWidth, UWORD uwHeight, UBYTE ubDepth, UBYTE ubFlags)
    {
        // Bitmap creation will fail if the width is not a multiple of 16.
        return bitmap_ptr(::bitmapCreate(mtl::round_up<16>(uwWidth), uwHeight, ubDepth, ubFlags));
    }

    inline bitmap_ptr bitmapCreateFromPath(char const* szPath, UBYTE isFast)
    {
        return bitmap_ptr(::bitmapCreateFromPath(szPath, isFast));
    }

    inline bitmap_ptr bitmapCreateFromFd(file_ptr&& pFile, UBYTE isFast)
    {
        // ACE's bitmapCreateFromFd closes the file itself; release ownership to
        // it so the file_ptr does not double-close the same handle.
        return bitmap_ptr(::bitmapCreateFromFd(pFile.release(), isFast));
    }
}  // namespace ace

#endif  // __ACEPP__BITMAP_H___INCLUDED__