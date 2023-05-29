#include "neonengine.h"

#include <ace/managers/system.h>
#include <ace/managers/blit.h>
#include <ace/managers/viewport/simplebuffer.h>
#include <ace/utils/palette.h>
#include <ace/managers/mouse.h>
#include "core/screen.h"
#include "core/mouse_pointer.h"

#define STATE_NAME "State: Language Selection"
#define FADE_DURATION 25

void lang_select_create(void)
{
    logBlockBegin(STATE_NAME);
    screen_fade_from_black(g_main_screen, FADE_DURATION, 0, NULL);
    screen_clear(g_main_screen, 0);
    
    paletteLoad("data/core/base.plt", g_main_screen->fade->pPaletteRef, 255);
    tBitMap *logo = bitmapCreateFromFile("data/core/flags.bm", 0);
    blitCopyAligned(
        logo, 0, 0,
        g_main_screen->buffer->pBack, 0, g_main_screen->offset,
        80, 128
    );
    bitmapDestroy(logo);

    mouse_pointer_create("data/core/pointers.bm");
}

void lang_select_process(void)
{
    static mouse_pointer_t current_pointer_gfx = MOUSE_POINTER;
    mouse_pointer_update();

    // Added to Test the mouse pointer code.
    // And sprite bitmap switching for AGA - VAIRN.
    if(mouseUse(MOUSE_PORT_1, MOUSE_LMB))
    {
        current_pointer_gfx +=1;
        if(current_pointer_gfx == MOUSE_MAX_COUNT)
        {
            current_pointer_gfx = MOUSE_POINTER;
        }

        mouse_pointer_switch(current_pointer_gfx);
    }
}

void lang_select_destroy(void)
{
    logBlockEnd(STATE_NAME);
    mouse_pointer_destroy();
}

tState g_state_lang_select = {
    .cbCreate = lang_select_create,
    .cbLoop = lang_select_process,
    .cbDestroy = lang_select_destroy,
};