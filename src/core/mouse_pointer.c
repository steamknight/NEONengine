#include "mouse_pointer.h"

#include <ace/managers/sprite.h>
#include <ace/managers/blit.h>
#include <ace/managers/system.h>
#include <ace/managers/mouse.h>

#include "core/screen.h"
#include "neonengine.h"

static tBitMap *pointers_[MOUSE_MAX_COUNT];
static tSprite *current_pointer0_;
static tSprite *current_pointer1_; // attached sprite.
 
#define POINTER_WIDTH 16


#define POINTER_HEIGHT 16
#define POINTER_BPP 2

void mouse_pointer_create(char const *filepath)
{
    BEGIN_USE_SYSTEM
    tBitMap *atlas = bitmapCreateFromFile(filepath, 0);

    for (BYTE idx = 0; idx < MOUSE_MAX_COUNT; idx++)
    {
        // Sprites need to have one extra line above and below the image.
        pointers_[idx] = bitmapCreate(
            POINTER_WIDTH, POINTER_HEIGHT + 2, 
            POINTER_BPP, BMF_CLEAR | BMF_INTERLEAVED);

        blitCopyAligned(
            atlas, idx * POINTER_WIDTH, 0,
            pointers_[idx], 0, 1,
            POINTER_WIDTH, POINTER_HEIGHT
        );
    }

    bitmapDestroy(atlas);

    spriteManagerCreate(g_main_screen->view, 0);
    systemSetDmaBit(DMAB_SPRITE, 1);

    current_pointer0_ = spriteAdd(0, pointers_[MOUSE_POINTER]);
    spriteSetEnabled(current_pointer0_, 1);

    current_pointer1_ = spriteAdd(1, pointers_[MOUSE_POINTER]);
    spriteSetEnabled(current_pointer1_, 1);
    spriteSetAttached(current_pointer1_, 1);
    END_UNUSE_SYSTEM
    systemUnuse();
}

void mouse_pointer_switch(mouse_pointer_t new_pointer)
{
}

void mouse_pointer_update(void)
{
    current_pointer0_->wX = mouseGetX(MOUSE_PORT_1);
    current_pointer0_->wY = mouseGetY(MOUSE_PORT_1);
    current_pointer1_->wX = mouseGetX(MOUSE_PORT_1);
    current_pointer1_->wY = mouseGetY(MOUSE_PORT_1);
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
        bitmapDestroy(pointers_[idx]);
    }

    spriteRemove(current_pointer0_);

    systemSetDmaBit(DMAB_SPRITE, 0);
    spriteManagerDestroy();
}
