#include "neonengine.h"

#include <ace/managers/system.h>
#include <ace/managers/blit.h>
#include <ace/managers/viewport/simplebuffer.h>
#include <ace/utils/palette.h>

#include "core/music.h"
#include "core/screen.h"

enum SplashState
{
    SPLASH_STATE_FADE_IN,
    SPLASH_STATE_WAIT,
    SPLASH_STATE_FADE_OUT,
};

static enum SplashState s_currentState;
static UWORD s_uwDelay;

#define STATE_NAME "State: Splash Screen"
#define FADE_DURATION 25
#define DELAY_DURATION 100

void changeState(enum SplashState newState);
void processState(void);
void onFadeInComplete(void);
void onFadeOutComplete(void);

void splashCreate(void)
{
    logBlockBegin(STATE_NAME);

    paletteLoadFromPath("data/mpg.plt", screenGetPalette(g_mainScreen), 255);
    tBitMap *pLogo = bitmapCreateFromPath("data/mpg.bm", 0);

    screenBlitCopy(g_mainScreen, pLogo, 0, 0, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, MINTERM_COPY);

    bitmapDestroy(pLogo);

    musicLoad("data/music/theme.mod");

    systemUnuse();
    musicPlayCurrent(1);

    s_uwDelay = 0;
    changeState(SPLASH_STATE_FADE_IN);
}

void splashProcess(void)
{
    /*
     * The processState function could easily just be the splashProcess function,
     * however, I'm leaving it like this as a minimal template for future states.
     */
    processState();
}

/*
 * Changes to the new state, and runs state initialization code.
 */
void changeState(enum SplashState newState)
{
    switch (newState)
    {
        case SPLASH_STATE_FADE_IN:
            screenFadeFromBlack(g_mainScreen, FADE_DURATION, 1, onFadeInComplete);
            break;

        case SPLASH_STATE_WAIT:
            break;

        case SPLASH_STATE_FADE_OUT:
            screenFadeToBlack(g_mainScreen, FADE_DURATION, 0, onFadeOutComplete);
            break;
    }

    s_currentState = newState;
}

/*
 * Processes the current state
 */
void processState(void)
{
    switch (s_currentState)
    {
        case SPLASH_STATE_FADE_IN:
            break;

        case SPLASH_STATE_WAIT:
            if (s_uwDelay >= DELAY_DURATION)
            {
                changeState(SPLASH_STATE_FADE_OUT);
            }

            s_uwDelay++;
            break;

        case SPLASH_STATE_FADE_OUT:
            break;
    }
}

void onFadeInComplete(void)
{
    changeState(SPLASH_STATE_WAIT);
}

void onFadeOutComplete(void)
{
    stateChange(g_gameStateManager, &g_stateLangSelect);
}

void splashDestroy(void)
{
    logBlockEnd(STATE_NAME);
}

tState g_stateSplash = {
    .cbCreate = splashCreate,
    .cbLoop = splashProcess,
    .cbDestroy = splashDestroy,
};