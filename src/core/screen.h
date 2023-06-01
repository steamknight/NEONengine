#ifndef __SCREEN_H__INCLUDED__
#define __SCREEN_H__INCLUDED__

#include "utils/fade.h"

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 200

typedef struct _tSimpleBufferManager tSimpleBufferManager;

typedef struct _Screen
{
    tView *pView;
    tVPort *pViewport;
    tSimpleBufferManager *pBuffer;
    tFade *pFade;
    UWORD uwOffset;
} Screen;

/**
 * @brief Create the a full screen view.
 * The screen will only be 200px tall and in PAL mode, it will be centered
 * vertically.
 *
 * @return Screen* The new screen.
 *
 * @see screenDestroy()
 */
Screen *screenCreate(void);

/**
 * @brief Destroyes the screen and associated resources.
 *
 * @param pScreen A valid screen
 *
 * @see screenCreate()
 */
void screenDestroy(Screen *pScreen);

/**
 * @brief Make the given screen the active one.
 *
 * @param pScreen The screen to activate
 */
void screenLoad(Screen *pScreen);

/**
 * @brief The screen's update loop. Must be called once per frame.
 *
 * @param pScreen The screen to update
 */
void screenProcess(Screen *pScreen);

/**
 * @brief Clears the given screen with a color from the current color palette
 *
 * @param pScreen The screen to clear
 * @param ubColorIndex Index in the color palette
 */
void screenClear(Screen* pScreen, UBYTE ubColorIndex);

/**
 * @brief Fades the screen to black
 *
 * @param pScreen The screen to fade
 * @param ubDuration The duration time, in frames
 * @param ubFadeMusic If set to 1, it will fade the music
 * @param cbOnDone Callback called when done
 *
 * @see screenFadeFromBlack()
 */
void screenFadeToBlack(Screen *pScreen, UBYTE ubDuration, UBYTE ubFadeMusic, tCbFadeOnDone cbOnDone);

/**
 * @brief
 *
 * @param pScreen The screen to fade
 * @param ubDuration The duration time, in frames
 * @param ubFadeMusic If set to 1, it will fade the music
 * @param cbOnDone Callback called when done
 *
 * @see screenFadeToBlack()
 */
void screenFadeFromBlack(Screen *pScreen, UBYTE ubDuration, UBYTE ubFadeMusic, tCbFadeOnDone cbOnDone);

/**
 * @brief Waits until the next vertical blank
 *
 * @param pScreen The screen to wait for
 */
void screenVwait(Screen *pScreen);

/**
 * @brief Sets the bounding box for the mouse to the whole screen.
 * It take into consideration the vertical offset of the screen in PAL mode.
 *
 * @param pScreen The screen to use
 */
void screenBindMouse(Screen *pScreen);

#endif //__SCREEN_H__INCLUDED__