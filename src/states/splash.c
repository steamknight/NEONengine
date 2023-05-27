#include "splash.h"

#include <ace/managers/game.h> // Used for gameExit()
#include <ace/managers/system.h>
#include <ace/managers/blit.h>
#include <ace/managers/viewport/simplebuffer.h>
#include <ace/managers/ptplayer.h>
#include <ace/utils/palette.h>

#include "neonengine.h"
#include "utils/music.h"
#include "utils/screen.h"

enum SPLASH_STATE
{
    SPLASH_STATE_FADE_IN,
    SPLASH_STATE_WAIT,
    SPLASH_STATE_FADE_OUT,
};

static enum SPLASH_STATE current_state_;
static UBYTE delay_;

#define STATE_NAME "State: Splash Screen"
#define FADE_DURATION 25
#define DELAY_DURATION 100

void change_state(enum SPLASH_STATE new_state);
void state_process(void);
void on_fade_in_complete(void);
void on_fade_out_complete(void);

void splash_create(void)
{
    logBlockBegin(STATE_NAME);

    paletteLoad("data/mpg.plt", g_main_screen->fade->pPaletteRef, 255);
    tBitMap *logo = bitmapCreateFromFile("data/mpg.bm", 0);
    blitCopy(
        logo, 0, 0,
        g_main_screen->buffer->pBack, 0, g_main_screen->offset,
        320, 200, MINTERM_COOKIE
    );
    bitmapDestroy(logo);

    music_load("data/music/theme.mod");

    systemUnuse();
    music_play_current(1);

    delay_ = 0;
    change_state(SPLASH_STATE_FADE_IN);
}

void splash_process(void)
{
    state_process();
}

/*
 * Changes to the new state, and runs state initialization code.
 */
void change_state(enum SPLASH_STATE new_state)
{
    switch (new_state)
    {
        case SPLASH_STATE_FADE_IN:
            screen_fade_from_black(g_main_screen, FADE_DURATION, 1, on_fade_in_complete);
            break;

        case SPLASH_STATE_WAIT:
            break;

        case SPLASH_STATE_FADE_OUT:
            screen_fade_to_black(g_main_screen, FADE_DURATION, 0, on_fade_out_complete);
            break;
    }

    current_state_ = new_state;
}

/*
 * Processes the current state
 */
void state_process(void)
{
    switch (current_state_)
    {
        case SPLASH_STATE_FADE_IN:
            break;

        case SPLASH_STATE_WAIT:
            if (delay_ >= DELAY_DURATION)
            {
                change_state(SPLASH_STATE_FADE_OUT);
            }

            delay_++;
            break;

        case SPLASH_STATE_FADE_OUT:
            break;
    }
}

void on_fade_in_complete(void)
{
    change_state(SPLASH_STATE_WAIT);
}

void on_fade_out_complete(void)
{
    gameExit();
}

void splash_destroy(void)
{
    systemUse();
    logBlockEnd(STATE_NAME);
}

tState g_state_splash = {
    .cbCreate = splash_create,
    .cbLoop = splash_process,
    .cbDestroy = splash_destroy
};