#ifndef __IMAGE_SWAPPER_H__INCLUDED__
#define __IMAGE_SWAPPER_H__INCLUDED__

#include <ace/types.h>
#include <ace/utils/bitmap.h>

#include "utils/array.h"

typedef struct ImageSwapper *ImageSwapper;

ImageSwapper imageSwapperCreate(Array imageFilePaths);

void imageSwapperDestroy(ImageSwapper* pSwapper);

void imageSwap(tBitMap* pDest, ULONG ulIndex);

#endif // __IMAGE_SWAPPER_H__INCLUDED__/
