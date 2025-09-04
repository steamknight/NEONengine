#include "neonengine.h"
#include "image_swapper.h"

#include <ace/managers/memory.h>
#include <ace/utils/file.h>

#include "utils/assert.h"

namespace NEONengine
{
    struct _ImageSwapper
    {
        Array images;
        ULONG ulImageCount;
        UBYTE* pImageData;
        ULONG ulImageDataSize;
    };

    ImageSwapper imageSwapperCreate(Array imageFilePaths)
    {
        ImageSwapper swapper = allocFastAndClear<_ImageSwapper>();
        assert(swapper, "Allocating memory for image swapper failed!");

        // swapper->images = arrayCreate(
        //     arrayLength(imageFilePaths),
        //     sizeof(UBYTE*),
        //     MEMF_FAST | MEMF_CLEAR);

        // // First, allocate room for all the images.
        // swapper->ulImageDataSize = 0L;
        // for (ULONG i = 0; i < arrayLength(imageFilePaths); ++i)
        // {
        //     char* szFilePath = arrayGet(imageFilePaths, i);
        //     LONG lFileSize = fileGetSize(szFilePath);

        //     assert(lFileSize > 0, "The file doesn't exist or is empty.");
        //     swapper->ulImageDataSize += lFileSize;
        // }

        // swapper->pImageData = memAllocFastClear(swapper->ulImageDataSize);

        // // Then, read the raw files into Fast RAM
        // UBYTE* pCurrentImage = (UBYTE*)swapper->pImageData;
        // for (ULONG i = 0; i < arrayLength(imageFilePaths); ++i)
        // {
        //     arrayPut(swapper->images, i, &pCurrentImage);

        //     char* szFilePath = arrayGet(imageFilePaths, i);
        //     LONG lFileSize = fileGetSize(szFilePath);
        //     tFile* pImage = fileOpen(szFilePath, "r");

        //     // If the image doesn't exist, we would have caught it in the previous loop
        //     if (!pImage) continue;

        //     fileRead(pImage, pCurrentImage, lFileSize);
        //     pCurrentImage += lFileSize;
        // }

        return swapper;
    }

    void imageSwapperDestroy(ImageSwapper* pSwapper)
    {
        assert(pSwapper && *pSwapper, "Trying to free a null image swapper pointer.");

        arrayDestroy(&(*pSwapper)->images);

        memFree((*pSwapper)->pImageData, (*pSwapper)->ulImageDataSize);
        *pSwapper = NULL;
    }

    void imageSwapperInit(void)
    {

    }

    void imageSwap(tBitMap* pDest, ULONG ulIndex)
    {

    }

}