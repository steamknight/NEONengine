#include "screen.h"

#include <ace/managers/blit.h>
#include <ace/managers/viewport/simplebuffer.h>

#include "neonengine.h"

screen_t *screen_create(void)
{
    screen_t *screen = memAllocFastClear(sizeof(screen_t));
    if (screen)
    {
        screen->offset = systemIsPal() ? 28 : 0;

        screen->view = viewCreate(0,
            TAG_VIEW_GLOBAL_PALETTE, 1,
            TAG_VIEW_USES_AGA, 1,
        TAG_END);

        screen->viewport = vPortCreate(0,
            TAG_VPORT_BPP, 8,
            TAG_VPORT_VIEW, screen->view,
        TAG_END);

        screen->buffer = simpleBufferCreate(0,
            TAG_SIMPLEBUFFER_BITMAP_FLAGS, BMF_CLEAR,
            TAG_SIMPLEBUFFER_VPORT, screen->viewport,
            TAG_SIMPLEBUFFER_IS_DBLBUF, 1,
        TAG_END);

        screen->fade = fadeCreate(screen->view, screen->view->pFirstVPort->pPalette, 255);
    }

    return screen;
}

void screen_destroy(screen_t *screen)
{
    // you don't need to destroy the viewport, it's destroyed by the view.
    if (screen)
    {
        simpleBufferDestroy(screen->buffer);
        viewDestroy(screen->view);
        fadeDestroy(screen->fade);
        memFree(screen, sizeof(screen_t));
    }
}

void screen_load(screen_t *screen)
{
    BEGIN_UNUSE_SYSTEM
    viewLoad(screen->view);
    END_UNUSE_SYSTEM
}

void screen_process(screen_t *screen)
{
    if (screen->fade->eState != FADE_STATE_IDLE)
    {
        fadeProcess(screen->fade);
    }

    vPortWaitForEnd(screen->viewport);
}

void screen_clear(screen_t *screen, UBYTE color_index)
{
    blitRect(screen->buffer->pBack, 0, screen->offset, SCREEN_WIDTH, SCREEN_HEIGHT, color_index);
}

void screen_fade_to_black(screen_t *screen, UBYTE duration, UBYTE fade_music, tCbFadeOnDone on_done_fn)
{
    fadeSet(screen->fade, FADE_STATE_OUT, duration, fade_music, on_done_fn);
}

void screen_fade_from_black(screen_t *screen, UBYTE duration, UBYTE fade_music, tCbFadeOnDone on_done_fn)
{
    fadeSet(screen->fade, FADE_STATE_IN, duration, fade_music, on_done_fn);
}

void screen_vwait(screen_t *screen)
{
    vPortWaitForEnd(screen->viewport);
}