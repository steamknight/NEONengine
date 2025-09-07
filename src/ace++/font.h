#ifndef __ACEPP__FONT_H___INCLUDED__
#define __ACEPP__FONT_H___INCLUDED__

#include <ace/utils/font.h>
#include "mtl/memory.h"

namespace ace
{
    using font_ptr = mtl::unique_ptr<tFont, fontDestroy>;
    using text_bitmap_ptr = mtl::unique_ptr<tTextBitMap, fontDestroyTextBitMap>;

    inline font_ptr fontCreateFromPath(char const* szPath)
    {
        return font_ptr(::fontCreateFromPath(szPath));
    }

    inline font_ptr fontCreateFromFd(tFile *pFontFile)
    {
        return font_ptr(::fontCreateFromFd(pFontFile));
    }

    inline text_bitmap_ptr fontCreateTextBitMap(UWORD uwWidth, UWORD uwHeight)
    {
        return text_bitmap_ptr(::fontCreateTextBitMap(uwWidth, uwHeight));
    }

    inline text_bitmap_ptr fontCreateTextBitMapFromStr(tFont const* pFont, char const* szText)
    {
        return text_bitmap_ptr(::fontCreateTextBitMapFromStr(pFont, szText));
    }
}

#endif // __ACEPP__FONT_H___INCLUDED__