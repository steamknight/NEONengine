#include "mouse_pointer.h"

#include <ace/managers/sprite.h>
#include <ace/managers/blit.h>
#include <ace/managers/viewport/simplebuffer.h>
#include <ace/managers/system.h>
#include <ace/managers/mouse.h>

#include "core/screen.h"
#include "neonengine.h"

static tBitMap *s_pPointersLo[MOUSE_MAX_COUNT];
static tBitMap *s_pPointersHi[MOUSE_MAX_COUNT];
static tSprite *s_pCurrentPointer0;
static tSprite *s_pCurrentPointer1; // attached sprite.

#define POINTER_WIDTH 16
#define POINTER_HEIGHT 16
#define POINTER_BPP 4
#define SPRITE_BPP 2

void mousePointerCreate(char const *szFilePath)
{
    UWORD uwSourceWidth;

    systemUse();
    tBitMap *pAtlas = bitmapCreateFromPath(szFilePath, 0);

    for (BYTE idx = 0; idx < MOUSE_MAX_COUNT; idx++)
    {
        // Sprites need to have one extra line above and below the image.
        tBitMap *pPointer = bitmapCreate(
            POINTER_WIDTH, POINTER_HEIGHT + 2,
            POINTER_BPP, BMF_CLEAR | BMF_INTERLEAVED);

        s_pPointersHi[idx] = bitmapCreate(
            POINTER_WIDTH, POINTER_HEIGHT + 2,
            SPRITE_BPP, BMF_CLEAR | BMF_INTERLEAVED);

        s_pPointersLo[idx] = bitmapCreate(
            POINTER_WIDTH, POINTER_HEIGHT + 2,
            SPRITE_BPP, BMF_CLEAR | BMF_INTERLEAVED);

        blitCopyAligned(
            pAtlas, idx * POINTER_WIDTH, 0,
            pPointer, 0, 1,
            POINTER_WIDTH, POINTER_HEIGHT);

        // Convert the 4bpp bitmap to 2bpp.
        uwSourceWidth = bitmapGetByteWidth(pPointer);
        for (UWORD r = 0; r < pPointer->Rows; r++)
        {
            UWORD uwOffsetSrc = r * pPointer->BytesPerRow;
            UWORD uwOffsetDst = r * s_pPointersLo[idx]->BytesPerRow;
            memcpy(s_pPointersLo[idx]->Planes[0] + uwOffsetDst, pPointer->Planes[0] + uwOffsetSrc, uwSourceWidth);
            memcpy(s_pPointersLo[idx]->Planes[1] + uwOffsetDst, pPointer->Planes[1] + uwOffsetSrc, uwSourceWidth);
            memcpy(s_pPointersHi[idx]->Planes[0] + uwOffsetDst, pPointer->Planes[2] + uwOffsetSrc, uwSourceWidth);
            memcpy(s_pPointersHi[idx]->Planes[1] + uwOffsetDst, pPointer->Planes[3] + uwOffsetSrc, uwSourceWidth);
        }

        bitmapDestroy(pPointer);
    }

    bitmapDestroy(pAtlas);

    spriteManagerCreate(screenGetView(g_mainScreen), 0, NULL);
    systemSetDmaBit(DMAB_SPRITE, 1);

    s_pCurrentPointer0 = spriteAdd(0, s_pPointersLo[MOUSE_POINTER]);
    spriteSetEnabled(s_pCurrentPointer0, 1);

    s_pCurrentPointer1 = spriteAdd(1, s_pPointersHi[MOUSE_POINTER]);
    spriteSetEnabled(s_pCurrentPointer1, 1);
    spriteSetAttached(s_pCurrentPointer1, 1);

    systemUnuse();
}

void mousePointerSwitch(eMousePointer newPointer)
{
    spriteSetBitmap(s_pCurrentPointer0, s_pPointersLo[newPointer]);
    spriteSetBitmap(s_pCurrentPointer1, s_pPointersHi[newPointer]);
}

void mousePointerUpdate(void)
{
    s_pCurrentPointer0->wX = mouseGetX(MOUSE_PORT_1);
    s_pCurrentPointer0->wY = mouseGetY(MOUSE_PORT_1);
    s_pCurrentPointer1->wX = s_pCurrentPointer0->wX;
    s_pCurrentPointer1->wY = s_pCurrentPointer0->wY;
    spriteRequestMetadataUpdate(s_pCurrentPointer0);
    spriteRequestMetadataUpdate(s_pCurrentPointer1);

    spriteProcessChannel(0);
    spriteProcessChannel(1);
    spriteProcess(s_pCurrentPointer0);
    spriteProcess(s_pCurrentPointer1);
}

void mousePointerDestroy(void)
{
    for (BYTE idx = 0; idx < MOUSE_MAX_COUNT; idx++)
    {
        bitmapDestroy(s_pPointersLo[idx]);
        bitmapDestroy(s_pPointersHi[idx]);
    }

    spriteRemove(s_pCurrentPointer0);
    spriteRemove(s_pCurrentPointer1);

    systemSetDmaBit(DMAB_SPRITE, 0);
    spriteManagerDestroy();
}
