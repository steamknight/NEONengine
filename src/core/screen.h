#ifndef __SCREEN_H__INCLUDED__
#define __SCREEN_H__INCLUDED__

#include "types.h"

#include <ace/managers/viewport/simplebuffer.h>

#include "core/spatial.h"
#include "utils/fade.h"

typedef struct _tTextBitMap tTextBitMap;

namespace NEONengine
{

    constexpr int SCREEN_WIDTH  = 320;
    constexpr int SCREEN_HEIGHT = 200;

    /**
     * @brief The palette band the hardware pointer draws from.
     *
     * An attached sprite pair indexes a 16-aligned bank, here entries 16..31,
     * but it only reads the entries its art actually names. The cursor sheet is
     * authored in the top half, so 24..31 belong to the pointer alone: nothing
     * else may use them, and the fade leaves them lit so the cursor stays
     * readable while a load blocks the loop on a blacked-out screen.
     *
     * Text regions and the rest of the UI therefore have entries 0..23.
     */
    constexpr UWORD CURSOR_PALETTE_FIRST = 24;
    constexpr UWORD CURSOR_PALETTE_COUNT = 8;

    struct Screen;

    /**
     * @brief Chooses whether the pointer's palette band takes part in fades.
     *
     * A scene that shows the pointer holds the band out of the fade so the
     * cursor stays readable while a load blocks the loop on a blacked-out
     * screen. Because the fade then never writes those live entries again, this
     * refreshes them from the reference palette on the way in.
     *
     * A scene that shows no UI may author its background right across the band,
     * so there it fades with everything else and the cursor is not special.
     */
    void screenSetCursorBandExempt(Screen* screen, bool exempt);

    /**
     * @brief Create the a full screen view.
     * The screen will only be 200px tall and in PAL mode, it will be centered
     * vertically.
     *
     * @return Screen* The new screen.
     *
     * @see screenDestroy()
     */
    Screen* screenCreate();

    /**
     * @brief Destroyes the screen and associated resources.
     *
     * @param screen A valid screen
     *
     * @see screenCreate()
     */
    void screenDestroy(Screen* screen);

    /**
     * @brief Make the given screen the active one.
     *
     * @param screen The screen to activate
     */
    void screenLoad(Screen* screen);

    /**
     * @brief The screen's update loop. Must be called once per frame.
     *
     * @param screen The screen to update
     */
    void screenProcess(Screen* screen);

    /**
     * @brief Locks the display on the buffer that is on screen now.
     *
     * The two buffers stop alternating and every draw helper lands on the one
     * being displayed, so an overlay paints itself once instead of once per
     * buffer per frame. What was drawn last frame is left untouched in the
     * other buffer, which is what @ref screenRestoreRect paints back from.
     *
     * The pointer sprite and the palette are unaffected: only the buffer
     * manager stops, not the rest of the frame.
     *
     * @param screen The screen to hold still.
     *
     * @see screenResumeDisplay()
     */
    void screenFreezeDisplay(Screen* screen);

    /**
     * @brief Lets the two buffers alternate again.
     *
     * The next frame shows the buffer that was held back, so whatever the
     * overlay left behind must be painted over before this is called.
     *
     * @param screen The screen to release.
     *
     * @see screenFreezeDisplay()
     */
    void screenResumeDisplay(Screen* screen);

    /**
     * @brief Whether the display is being held on one buffer.
     */
    bool screenIsFrozen(Screen* screen);

    /**
     * @brief Paints a region of the held-back buffer over the displayed one.
     *
     * Undoes an overlay without a copy of the screen behind it: the buffer the
     * freeze held back still has the region as it was. It holds the frame
     * before the one on display, so it is only the same picture while nothing
     * is moving, which is why the world stops behind an overlay.
     *
     * Takes scene-space coordinates, as screenBlitCopy does.
     *
     * @param screen A frozen screen. Does nothing when it is not frozen.
     */
    void screenRestoreRect(Screen* screen, WORD wX, WORD wY, WORD wWidth, WORD wHeight);

    /**
     * @brief Clears the given screen with a color from the current color palette
     *
     * @param screen The screen to clear
     * @param ubColorIndex Index in the color palette
     */
    void screenClear(Screen* screen, UBYTE ubColorIndex);

    /**
     * @brief Whether the screen is part-way through fading out.
     *
     * A scene transition spans the script's fade-out and the load that follows,
     * with the two separated by frames in which nothing is pending. Callers that
     * must treat the whole span as one event can ask here instead of tracking
     * the script's progress themselves.
     *
     * @param screen The screen to query.
     */
    bool screenIsFadingOut(Screen* screen);

    /**
     * @brief Whether a fade will repaint the display palette this frame.
     *
     * The palette has two layers: screenGetPalette() is what the scene looks
     * like at full brightness, and the display holds that scaled by how bright
     * the screen currently is. A fade rebuilds the whole display layer from the
     * reference every frame it runs, so anything else that changes the
     * reference can leave the repaint to the fade rather than doing its own and
     * paying for a second copperlist palette rebuild.
     *
     * True only while a fade will actually paint: a fade that has run out and is
     * waiting to be noticed answers false, because it repaints nothing.
     *
     * @param screen The screen to query.
     */
    bool screenFadeOwnsPalette(Screen* screen);

    /**
     * @brief How bright the display currently stands, 0 to 255.
     *
     * The display holds the reference palette scaled by this. A fade leaves it
     * wherever it finished, so a screen that has faded down and been left there
     * still reads 0 long after the fade went idle. Anything writing the display
     * outside a fade has to scale by this, or it would paint its own entries at
     * a brightness the rest of the screen is not at.
     *
     * @param screen The screen to query.
     */
    UBYTE screenPaletteRatio(Screen* screen);

    /**
     * @brief Repaints display entries [@p first, @p first + @p count) from the reference.
     *
     * The palette has two layers, and writing screenGetPalette() only touches
     * the reference one; this is what carries a change through to the hardware.
     * Entries are scaled by @ref screenPaletteRatio on the way, so a band
     * written while the screen stands faded down does not light up on its own.
     *
     * @param screen The screen whose palette to publish.
     */
    void screenPublishPalette(Screen* screen, u16 first, u16 count);

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
    void screenFadeToBlack(Screen* screen,
                           UBYTE ubDuration,
                           UBYTE ubFadeMusic,
                           tCbFadeOnDone cbOnDone);

    /**
     * @brief Fades @p screen up from black.
     *
     * @param screen The screen to fade
     * @param ubDuration The duration time, in frames
     * @param ubFadeMusic If set to 1, it will fade the music
     * @param cbOnDone Callback called when done
     */
    void screenFadeFromBlack(Screen* screen,
                             UBYTE ubDuration,
                             UBYTE ubFadeMusic,
                             tCbFadeOnDone cbOnDone);

    /**
     * @brief Waits until the next vertical blank
     *
     * @param screen The screen to wait for
     */
    void screenVwait(Screen* screen);

    /**
     * @brief Sets the bounding box for the mouse to the whole screen.
     * It take into consideration the vertical offset of the screen in PAL mode.
     *
     * @param screen The screen to use
     */
    void screenBindMouse(Screen* screen);

    /**
     * @brief Shifts @p pRect from scene space down into display space.
     */
    void screenToScreenSpace(Screen* screen, tUwRect* pRect);

    tView* screenGetView(Screen* screen);

    tBitMap* screenGetBackBuffer(Screen* screen);

    tBitMap* screenGetFrontBuffer(Screen* screen);

    UWORD* screenGetPalette(Screen* screen);

    /**
     * @brief The offset that moves scene space into display space.
     *
     * Content drawn straight into a buffer, bobs for one, must add this.
     * screenBlitCopy already does. Only y is ever non-zero.
     */
    data::point<u16> screenGetOffset(Screen* screen);

    /**
     * @brief Copies the whole back buffer into the front buffer.
     *
     * Used after drawing a static background so both double-buffered frames hold
     * it, which the bob manager needs to undraw bobs correctly in either frame.
     *
     * @param screen A pointer to a Screen object.
     */
    void screenDuplicateBackToFront(Screen* screen);

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
    void screenBlitCopy(Screen* screen,
                        tBitMap const* pSrc,
                        WORD wSrcX,
                        WORD wSrcY,
                        WORD wDstX,
                        WORD wDstY,
                        WORD wWidth,
                        WORD wHeight,
                        UBYTE ubMinterm);

    /**
     * @brief Copies a region through @p pMsk, leaving masked-out pixels alone.
     *
     * The cookie-cut @ref screenBlitCopy cannot do: what the mask clears keeps
     * whatever was underneath it. The mask is read with the source's own
     * stride, so it has to carry its bits in every bitplane, which is how the
     * exported mask bitmaps are built.
     *
     * Takes scene-space coordinates, as screenBlitCopy does.
     */
    void screenBlitMask(Screen* screen,
                        tBitMap const* pSrc,
                        WORD wSrcX,
                        WORD wSrcY,
                        WORD wDstX,
                        WORD wDstY,
                        WORD wWidth,
                        WORD wHeight,
                        UBYTE const* pMsk);

    void screenTextCopy(Screen* screen,
                        tTextBitMap* pTextBitMap,
                        UWORD uwX,
                        UWORD uwY,
                        UBYTE ubColor,
                        UBYTE ubFlags);

}  // namespace NEONengine

#endif  //__SCREEN_H__INCLUDED__
