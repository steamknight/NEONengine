#ifndef __SCREEN_H__INCLUDED__
#define __SCREEN_H__INCLUDED__

#include <ace/managers/viewport/simplebuffer.h>

#include "utils/fade.h"

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 200


typedef struct Screen *Screen;

typedef struct _tTextBitMap tTextBitMap;

/**
 * @brief Create the a full screen view.
 * The screen will only be 200px tall and in PAL mode, it will be centered
 * vertically.
 *
 * @return Screen* The new screen.
 *
 * @see screenDestroy()
 */
Screen screenCreate(void);

/**
 * @brief Destroyes the screen and associated resources.
 *
 * @param screen A valid screen
 *
 * @see screenCreate()
 */
void screenDestroy(Screen screen);

/**
 * @brief Make the given screen the active one.
 *
 * @param screen The screen to activate
 */
void screenLoad(Screen screen);

/**
 * @brief The screen's update loop. Must be called once per frame.
 *
 * @param screen The screen to update
 */
void screenProcess(Screen screen);

/**
 * @brief Clears the given screen with a color from the current color palette
 *
 * @param screen The screen to clear
 * @param ubColorIndex Index in the color palette
 */
void screenClear(Screen screen, UBYTE ubColorIndex);

/**
 * @brief Fades the screen to black
 *
 * @param screen The screen to fade
 * @param ubDuration The duration time, in frames
 * @param ubFadeMusic If set to 1, it will fade the music
 * @param cbOnDone Callback called when done
 *
 * @see screenFadeFromBlack()
 */
void screenFadeToBlack(Screen screen, UBYTE ubDuration, UBYTE ubFadeMusic, tCbFadeOnDone cbOnDone);

/**
 * @brief
 *
 * @param screen The screen to fade
 * @param ubDuration The duration time, in frames
 * @param ubFadeMusic If set to 1, it will fade the music
 * @param cbOnDone Callback called when done
 *
 * @see screenFadeToBlack()
 */
void screenFadeFromBlack(Screen screen, UBYTE ubDuration, UBYTE ubFadeMusic, tCbFadeOnDone cbOnDone);

/**
 * @brief Waits until the next vertical blank
 *
 * @param screen The screen to wait for
 */
void screenVwait(Screen screen);

/**
 * @brief Sets the bounding box for the mouse to the whole screen.
 * It take into consideration the vertical offset of the screen in PAL mode.
 *
 * @param screen The screen to use
 */
void screenBindMouse(Screen screen);

/**
 * @brief Converts the coordinates of a rectangle from screen space to screen space.
 *
 * @param screen A pointer to a Screen object.
 * @param pRect A pointer to a tUwRect object.
 */
void screenToScreenSpace(Screen screen, tUwRect *pRect);

/**
 * @brief Returns a pointer to the view associated with the screen.
 *
 * @param screen A pointer to a Screen object.
 * @return tVPort* A pointer to the view.
 */
tView* screenGetView(Screen screen);

/**
 * @brief Returns a pointer to the back buffer associated with the screen.
 *
 * @param screen A pointer to a Screen object.
 * @return tBitMap* A pointer to the back buffer.
 */
tBitMap* screenGetBackBuffer(Screen screen);

/**
 * @brief Returns a pointer to the front buffer associated with the screen.
 *
 * @param screen A pointer to a Screen object.
 * @return tBitMap* A pointer to the front buffer.
 */
tBitMap* screenGetFrontBuffer(Screen screen);

/**
 * @brief Returns a pointer to the palette associated with the screen.
 *
 * @param screen A pointer to a Screen object.
 * @return UWORD* A pointer to the palette.
 */
UWORD* screenGetPalette(Screen screen);

/**
 * @brief Copies a rectangular region from the source bitmap to the back buffer.
 *
 * @param screen A pointer to a Screen object.
 * @param pSrc A pointer to the source tBitMap object.
 * @param wSrcX The top-left x-coordinate of the source rectangle.
 * @param wSrcY The top-left y-coordinate of the source rectangle.
 * @param wDstX The top-left x-coordinate of the destination rectangle.
 * @param wDstY The top-left y-coordinate of the destination rectangle.
 * @param wWidth The width of the rectangle.
 * @param wHeight The height of the rectangle.
 * @param ubMinterm Minterm to be used for blitter operation, usually MINTERM_COOKIE.
 */
void screenBlitCopy(Screen screen, const tBitMap *pSrc, WORD wSrcX, WORD wSrcY,
    WORD wDstX, WORD wDstY, WORD wWidth, WORD wHeight,
    UBYTE ubMinterm);

void screenTextCopy(Screen screen, tTextBitMap *pTextBitMap, UWORD uwX, UWORD uwY, 
    UBYTE ubColor, UBYTE ubFlags);

#endif //__SCREEN_H__INCLUDED__