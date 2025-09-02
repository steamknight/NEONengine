#ifndef __NINE_PATCH_H__INCLUDED__
#define __NINE_PATCH_H__INCLUDED__

#include <ace/types.h>

typedef struct BitMap tBitMap;
typedef struct NinePatch *NinePatch;

/**
 * @brief Create a new nine-patch structure.
 * 
 * NOTE: The nine-patch takes ownership of the patch bitmap that is used in 
 * rendering the final patch. It will destroy the bitmap once it's destroyed
 * 
 * @param ppSource A handle to the nine patch image to use
 * @param uwLeftBorder Size of the left border
 * @param uwTopBorder Size of the top border
 * @param uwRightBorder Size of the right border
 * @param uwBottomBorder Size of the bottom border
 * @return NinePatch used for calls to `ninePatchRender`
 * 
 * @see ninePatchDestroy
 * @see ninePatchRender
 */
NinePatch ninePatchCreate(tBitMap **ppSource, UWORD uwLeftBorder, UWORD uwTopBorder, UWORD uwRightBorder, UWORD uwBottomBorder);

/**
 * @brief Destorys the nine-patch and sets the owning poitner to NULL.
 * 
 * Will also destroy the patch bitmap
 * 
 * @param pPatch Pointer to the patch to destroy
 */
void ninePatchDestroy(NinePatch* pPatch);

/**
 * Renders a nine-patch bitmap with the specified dimensions and flags.
 * 
 * NOTE: Caller is reponsible for calling `destroyBitmap` on the returned bitmap.
 * 
 * @param patch Pointer to the NinePatch structure containing source and border sizes.
 * @param uwWidth Desired width of the resulting bitmap.
 * @param uwHeight Desired height of the resulting bitmap.
 * @param ulFlags Bitmap creation flags.
 * @return Pointer to the newly created tBitMap, or NULL on failure.
 */
tBitMap* ninePatchRender(NinePatch patch, UWORD uwWidth, UWORD uwHeight, ULONG ulFlags);

#endif // __NINE_PATCH_H__INCLUDED__