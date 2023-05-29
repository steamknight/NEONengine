#ifndef __SCREEN_H__INCLUDED__
#define __SCREEN_H__INCLUDED__

#include "utils/fade.h"

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 200

typedef struct _tSimpleBufferManager tSimpleBufferManager;

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

void screen_clear(screen_t* screen, UBYTE color_index);
void screen_fade_to_black(screen_t *screen, UBYTE duration, UBYTE fade_music, tCbFadeOnDone on_done_fn);
void screen_fade_from_black(screen_t *screen, UBYTE duration, UBYTE fade_music, tCbFadeOnDone on_done_fn);

void screen_vwait(screen_t *screen);
void screen_bind_mouse(screen_t *screen);

#endif //__SCREEN_H__INCLUDED__