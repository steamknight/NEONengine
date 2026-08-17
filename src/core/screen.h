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

    /**
     * @brief Owns the viewport, palette and fade state for the engine.
     */
    class Screen
    {
        public:  //////////////////////////////////////////////////////////////////////////////
        Screen();
        ~Screen();

        /**
         * @brief Run the screen's per-frame update logic.
         */
        void process();
        /**
         * @brief Clear the screen with a palette index.
         */
        void clear(UBYTE ubColorIndex);

        /**
         * @brief Freeze the live display while an overlay draws over the scene.
         */
        void freeze_display();
        /**
         * @brief Resume the live display after a frozen overlay is done.
         */
        void resume_display();

        /**
         * @brief Restore a scene rectangle from the held back buffer.
         */
        void restore_rect(WORD wX, WORD wY, WORD wWidth, WORD wHeight);
        /**
         * @brief Copy the back buffer over the front buffer.
         */
        void duplicate_back_to_front();

        /**
         * @brief Copy a bitmap into the screen space at the current display offset.
         */
        void blit_copy(tBitMap const* pSrc,
            WORD wSrcX,
            WORD wSrcY,
            WORD wDstX,
            WORD wDstY,
            WORD wWidth,
            WORD wHeight,
            UBYTE ubMinterm);
        /**
         * @brief Copy a masked bitmap into the screen space at the current display offset.
         */
        void blit_copy_mask(tBitMap const* pSrc,
            WORD wSrcX,
            WORD wSrcY,
            WORD wDstX,
            WORD wDstY,
            WORD wWidth,
            WORD wHeight,
            UBYTE const* pMsk);
        /**
         * @brief Copy a text bitmap into screen space.
         */
        void text_copy(tTextBitMap* pTextBitMap,
            UWORD uwX,
            UWORD uwY,
            UBYTE ubColor,
            UBYTE ubFlags);
        /**
         * @brief Whether the screen is fading out.
         */
        bool is_fading_out() const;
        /**
         * @brief Whether the active fade owns the palette output.
         */
        bool fade_owns_palette() const;
        /**
         * @brief The current fade ratio from 0 to 255.
         */
        UBYTE palette_ratio() const;

        /**
         * @brief Publish only the palette band a caller owns.
         */
        void publish_palette(u16 first, u16 count);
        /**
         * @brief Exempt the cursor palette band from fade updates.
         */
        void set_cursor_band_exempt(bool exempt);
        /**
         * @brief Wait for the next vertical blank.
         */
        void vwait();
        /**
         * @brief Set the mouse bounds to the active screen area.
         */
        void bind_mouse();

        /**
         * @brief Load this screen as the active display.
         */
        void load();
        /**
         * @brief Fade the screen to black.
         */
        void fade_to_black(UBYTE ubDuration,
            UBYTE ubFadeMusic,
            tCbFadeOnDone cbOnDone);
        /**
         * @brief Fade the screen up from black.
         */
        void fade_from_black(UBYTE ubDuration,
            UBYTE ubFadeMusic,
            tCbFadeOnDone cbOnDone);

        /**
         * @brief Access the active view for this screen.
         */
        tView* view() const;
        /**
         * @brief Access the scene-to-display vertical offset.
         */
        UWORD offset() const;
        /**
         * @brief Access the back buffer bitmap.
         */
        tBitMap* back_buffer() const;
        /**
         * @brief Access the front buffer bitmap.
         */
        tBitMap* front_buffer() const;
        /**
         * @brief Access the live palette data.
         */
        UWORD* palette();
        /**
         * @brief Access the live palette data read-only.
         */
        UWORD const* palette() const;

        private:  ///////////////////////////////////////////////////////////////////////////
        tView* pView;
        tVPort* pViewport;
        tSimpleBufferManager* pBuffer;
        tFade* pFade;
        UWORD uwOffset;
        tBitMap* pHeldBack;
    };

    /**
     * @brief The engine's single display owner.
     */
    extern Screen* g_mainScreen;

    /**
     * @brief Holds the display still for as long as the object lives.
     */
    class freezes_display
    {
        protected: ////////////////////////////////////////////////////////////////////////////////
        freezes_display() noexcept { g_mainScreen->freeze_display(); }
        ~freezes_display() noexcept { g_mainScreen->resume_display(); }
    };

    /**
     * @brief Create the full 200px PAL screen view.
     *
     * @return The new screen.
     */
    Screen* screenCreate();

    /**
     * @brief Destroy the screen and its ACE resources.
     *
     * @param screen A valid screen.
     */
    void screenDestroy(Screen* screen);



}  // namespace NEONengine

#endif  //__SCREEN_H__INCLUDED__
