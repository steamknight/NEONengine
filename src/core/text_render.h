#ifndef __TEXT_RENDERER__INCLUDED_H__
#define __TEXT_RENDERER__INCLUDED_H__

#include <ace/types.h>
#include "ace++/font.h"

#include "mtl/memory.h"
#include "utils/bstr_view.h"

namespace NEONengine
{
    /**
     * @brief Defines how the text should be justified horizontally
     */
    enum class TextHJustify
    {
        LEFT,
        RIGHT,
        CENTER,
    };

    void textRendererCreate(char const *szFontName);
    void textRendererDestroy();

    ace::text_bitmap_ptr textCreateFromString(bstr_view const& text, UWORD uwMaxWidth, TextHJustify justification);
    ace::text_bitmap_ptr textCreateFromId(ULONG stringId, UWORD uwMaxWidth, TextHJustify justification);
}


#endif // __TEXT_RENDERER__INCLUDED_H__