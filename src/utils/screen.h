#ifndef __SCREEN_H__INCLUDED__
#define __SCREEN_H__INCLUDED__

#include <ace/managers/system.h>
#include <ace/managers/blit.h>
#include <ace/managers/viewport/simplebuffer.h>
#include <ace/managers/blit.h>
#include <ace/utils/palette.h>
#include <exec/types.h>

#include "utils/fade.h"

typedef struct screen
{
    tView *view;
    tVPort *viewport;
    tSimpleBufferManager *buffer;
    tFade *fade;
    UWORD offset;
} screen_t;

screen_t *screen_create(void);
void screen_destroy(screen_t *screen);
void screen_load(screen_t *screen);
void screen_process(screen_t *screen);
void screen_fade_to_black(screen_t *screen, UBYTE duration, UBYTE fade_music, tCbFadeOnDone on_done_fn);
void screen_fade_from_black(screen_t *screen, UBYTE duration, UBYTE fade_music, tCbFadeOnDone on_done_fn);
void screen_vwait(screen_t *screen);

#endif //__SCREEN_H__INCLUDED__