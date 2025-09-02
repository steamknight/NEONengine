#ifndef __TEXT_RENDERER__INCLUDED_H__
#define __TEXT_RENDERER__INCLUDED_H__

#include <ace/types.h>

#include "utils/bstr.h"

typedef UWORD tTextToken;
typedef struct _tTextBitMap tTextBitMap;

/**
 * @brief Defines how the text should be justified horizontally
 */
typedef enum _eTextHJustify
{
    TX_LEFT_JUSTIFY,
    TX_RIGHT_JUSTIFY,
    TX_CENTER_JUSTIFY,
} TextHJustify;


void textRendererCreate(char const *szFontName);
void textRendererDestroy();

tTextBitMap* textCreateFromString(Bstring bstrText, UWORD uwMaxWidth, TextHJustify justification);
tTextBitMap* textCreateFromId(ULONG stringId, UWORD uwMaxWidth, TextHJustify justification);



#endif // __TEXT_RENDERER__INCLUDED_H__