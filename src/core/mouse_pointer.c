#include "mouse_pointer.h"

#include <ace/managers/sprite.h>
#include <ace/managers/blit.h>
#include <ace/managers/viewport/simplebuffer.h> 
#include <ace/managers/system.h>
#include <ace/managers/mouse.h>

#include "core/screen.h"
#include "neonengine.h"

static tBitMap *pointers_lo_[MOUSE_MAX_COUNT];
static tBitMap *pointers_hi_[MOUSE_MAX_COUNT];
static tSprite *current_pointer0_;
static tSprite *current_pointer1_; // attached sprite.

#define POINTER_WIDTH 16
#define POINTER_HEIGHT 16
#define POINTER_BPP 4
#define SPRITE_BPP 2

void mouse_pointer_create(char const *filepath)
{
    UWORD source_width;

    BEGIN_USE_SYSTEM
    tBitMap *atlas = bitmapCreateFromFile(filepath, 0);

    for (BYTE idx = 0; idx < MOUSE_MAX_COUNT; idx++)
    {
        // Sprites need to have one extra line above and below the image.
        tBitMap *temp = bitmapCreate(
            POINTER_WIDTH, POINTER_HEIGHT + 2,
            POINTER_BPP, BMF_CLEAR | BMF_INTERLEAVED);

        pointers_hi_[idx] = bitmapCreate(
            POINTER_WIDTH, POINTER_HEIGHT + 2,
            SPRITE_BPP, BMF_CLEAR | BMF_INTERLEAVED);

        pointers_lo_[idx] = bitmapCreate(
            POINTER_WIDTH, POINTER_HEIGHT + 2,
            SPRITE_BPP, BMF_CLEAR | BMF_INTERLEAVED);

        blitCopyAligned(
            atlas, idx * POINTER_WIDTH, 0,
            temp, 0, 1,
            POINTER_WIDTH, POINTER_HEIGHT);
        
        // Convert the 4bpp bitmap to 2bpp.
        source_width = bitmapGetByteWidth(temp);
        for (UWORD r = 0; r < temp->Rows; r++)
        {
            UWORD offset_src = r * temp->BytesPerRow;
            UWORD offset_dst = r * pointers_lo_[idx]->BytesPerRow;
            memcpy(pointers_lo_[idx]->Planes[0] + offset_dst, temp->Planes[0] + offset_src, source_width);
            memcpy(pointers_lo_[idx]->Planes[1] + offset_dst, temp->Planes[1] + offset_src, source_width);
            memcpy(pointers_hi_[idx]->Planes[0] + offset_dst, temp->Planes[2] + offset_src, source_width);
            memcpy(pointers_hi_[idx]->Planes[1] + offset_dst, temp->Planes[3] + offset_src, source_width);
        }

        bitmapDestroy(temp);
    }

    bitmapDestroy(atlas);

    spriteManagerCreate(g_main_screen->view, 0);
    systemSetDmaBit(DMAB_SPRITE, 1);

    current_pointer0_ = spriteAdd(0, pointers_lo_[MOUSE_POINTER]);
    spriteSetEnabled(current_pointer0_, 1);

    current_pointer1_ = spriteAdd(1, pointers_hi_[MOUSE_POINTER]);
    spriteSetEnabled(current_pointer1_, 1);
    spriteSetAttached(current_pointer1_, 1);
    END_UNUSE_SYSTEM

    systemUnuse();
}

void mouse_pointer_switch(mouse_pointer_t new_pointer)
{
    spriteSetBitmap(current_pointer0_, pointers_lo_[new_pointer]);
    spriteSetBitmap(current_pointer1_, pointers_hi_[new_pointer]);
}

void mouse_pointer_update(void)
{
    current_pointer0_->wX = mouseGetX(MOUSE_PORT_1);
    current_pointer0_->wY = mouseGetY(MOUSE_PORT_1);
    current_pointer1_->wX = current_pointer0_->wX;
    current_pointer1_->wY = current_pointer0_->wY;
    spriteRequestMetadataUpdate(current_pointer0_);
    spriteRequestMetadataUpdate(current_pointer1_);

    spriteProcessChannel(0);
    spriteProcessChannel(1);
    spriteProcess(current_pointer0_);
    spriteProcess(current_pointer1_);
}

void mouse_pointer_destroy(void)
{
    for (BYTE idx = 0; idx < MOUSE_MAX_COUNT; idx++)
    {
        bitmapDestroy(pointers_lo_[idx]);
        bitmapDestroy(pointers_hi_[idx]);
    }

    spriteRemove(current_pointer0_);
    spriteRemove(current_pointer1_);

    systemSetDmaBit(DMAB_SPRITE, 0);
    spriteManagerDestroy();
}
