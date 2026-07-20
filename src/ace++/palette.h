#ifndef __ACEPP__PALETTE_H___INCLUDED__
#define __ACEPP__PALETTE_H___INCLUDED__

#include <ace/utils/palette.h>

#include <ace++/file.h>

namespace ace
{
    /**
     * @brief Loads a palette from an open .plt file, consuming the handle.
     *
     * ACE closes the file on return, so ownership is taken by value: pass an
     * rvalue file_ptr, which is released into the C API and not double-closed.
     *
     * @param pFile       The open .plt file; consumed.
     * @param pPalette    Destination; for AGA this must hold a ULONG per entry.
     * @param uwMaxLength  Maximum number of colors to read.
     */
    inline void paletteLoadFromFd(file_ptr&& pFile, UWORD* pPalette, UWORD uwMaxLength)
    {
        ::paletteLoadFromFd(pFile.release(), pPalette, uwMaxLength);
    }
}  // namespace ace

#endif  // __ACEPP__PALETTE_H___INCLUDED__
