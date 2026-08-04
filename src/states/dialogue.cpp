#include "types.h"

#include "neonengine.h"

#include <ace/managers/blit.h>
#include <ace/managers/key.h>
#include <ace/managers/mouse.h>
#include <ace/managers/state.h>
#include <ace/macros.h>
#include <ace/utils/font.h>

#include <ace++/bitmap.h>

#include "core/dialogue_bridge.h"
#include "core/dialogue_session.h"
#include "core/logger.h"
#include "core/mouse_pointer.h"
#include "core/nine_patch.h"
#include "core/scene_hotspots.h"
#include "core/screen.h"
#include "core/text_render.h"

NE_LOG_DOMAIN(Dialogue);

namespace NEONengine
{
    namespace
    {
        /// The conversation the box is showing.
        dialogue_session s_session;

        /// The frame on screen now, in scene space. Zero-sized when none is up.
        u16 s_frameX      = 0;
        u16 s_frameY      = 0;
        u16 s_frameWidth  = 0;
        u16 s_frameHeight = 0;

        /**
         * @brief Paints the frame's rectangle back to the scene behind it.
         *
         * The freeze held a buffer back with the scene as it was, so the frame
         * leaves nothing where it stood. Doing nothing when no frame is up keeps
         * this safe to call before the first page and after the last.
         */
        void erase_frame()
        {
            if (!s_frameWidth || !s_frameHeight) return;

            screenRestoreRect(g_mainScreen,
                to<WORD>(s_frameX),
                to<WORD>(s_frameY),
                to<WORD>(s_frameWidth),
                to<WORD>(s_frameHeight));

            s_frameWidth  = 0;
            s_frameHeight = 0;
        }

        /**
         * @brief Draws the page on show in a frame sized to what it holds.
         *
         * Pinned to the top-left, since the portrait that will share that corner
         * is not drawn yet and the frame is where it will be either way.
         *
         * @return False when there was nothing to draw, which leaves the screen
         *         as it was and is the caller's cue to close the box.
         */
        bool draw_current_page()
        {
            auto const* page = s_session.current_page();
            if (!page) return false;

            // The page is burnt on arrival, before anything is laid out, so a
            // page that turns out to be undrawable has still been read. That is
            // the order the conversation's rules run in, and drawing is not
            // allowed a say in them.
            auto text_string = g_pEngine->strings().get_string(page->text_string_id);
            if (text_string.is_empty())
            {
                NE_LOG("page %hu has no text to show", s_session.current_page_index());
                return false;
            }

            auto* renderer = g_pEngine->default_text_renderer();

            // The frame is drawn around the text, so what the text may use is the
            // screen less the margin the frame is inset by and the margin
            // between the frame and its contents, on both sides.
            constexpr u16 CHROME = to<u16>(DIALOGUE_MARGIN * 2 + MESSAGE_MARGIN * 2);
            auto text = renderer->create_text(
                text_string, to<u16>(SCREEN_WIDTH - CHROME), text_justify::LEFT);
            if (!text)
            {
                // Reachable only with no font to draw with. A box the player
                // cannot read is worse than no box at all.
                NE_LOG("dialogue has nothing to draw");
                return false;
            }

            s_frameWidth  = to<u16>(text->uwActualWidth + MESSAGE_MARGIN * 2);
            s_frameHeight = to<u16>(text->uwActualHeight + MESSAGE_MARGIN * 2);

            // The frame can only be drawn well at certain widths, so it takes one
            // of those and the text sits in the middle of whatever that leaves.
            auto* frame = g_pEngine->default_nine_patch();
            if (frame) s_frameWidth = frame->width_for(s_frameWidth);

            s_frameX = DIALOGUE_MARGIN;
            s_frameY = DIALOGUE_MARGIN;

            if (frame)
            {
                if (auto art = frame->render(s_frameWidth, s_frameHeight, 0))
                {
                    screenBlitCopy(g_mainScreen,
                        art.get(),
                        0,
                        0,
                        to<WORD>(s_frameX),
                        to<WORD>(s_frameY),
                        to<WORD>(s_frameWidth),
                        to<WORD>(s_frameHeight),
                        MINTERM_COOKIE);
                }
            }

            // Centred in the frame rather than at a fixed margin: the frame's
            // width is whatever it could be drawn at, a little more than asked.
            u16 const textX = to<u16>(s_frameX + (s_frameWidth - text->uwActualWidth) / 2);
            screenTextCopy(g_mainScreen,
                text.get(),
                textX,
                to<u16>(s_frameY + MESSAGE_MARGIN),
                DIALOGUE_TEXT_COLOR,
                FONT_COOKIE);

            return true;
        }
    }  // namespace

    void dialogueCreate(void)
    {
        u16 const dialogue_id = g_pEngine->take_pending_dialogue();

        s_frameWidth  = 0;
        s_frameHeight = 0;

        s_session.create(g_pEngine->data().dialogues(),
            g_pEngine->data().pages(),
            &engine_dialogue_bridge());

        if (!s_session.begin(dialogue_id))
        {
            // Nothing loadable under that id. statePush has already made this
            // state current, so this unwinds it and the scene never sees a box.
            NE_LOG("SHOW_DIALOG %hu names no dialogue that can be shown", dialogue_id);
            statePop(g_gameStateManager);
            return;
        }

        // Held from here until the box comes down. Every draw below lands on the
        // buffer being displayed, and the one held back keeps the scene as it
        // was for erase_frame to paint back.
        screenFreezeDisplay(g_mainScreen);

        if (!draw_current_page())
        {
            statePop(g_gameStateManager);
            return;
        }

        // The pointer keeps its own role while the box is up, so nothing has to
        // be put back on the way out.
        mousePointerUpdate();

        // The press that started the script that spoke is still unspent - the
        // hotspots only ever look at it - and would advance the conversation on
        // the frame it opened. Spend it here so a fresh one is needed.
        (void)mouseUse(MOUSE_PORT_1, MOUSE_LMB);
        (void)keyUse(KEY_SPACE);
    }

    void dialogueProcess(void)
    {
        // Nothing else runs while the box is up, so the sprite would sit where
        // it was until the box came down.
        mousePointerUpdate();

        if (!mouseUse(MOUSE_PORT_1, MOUSE_LMB) && !keyUse(KEY_SPACE)) return;

        s_session.advance();

        // Erased before the next page is drawn, so a frame that shrinks leaves
        // no part of the taller one behind it.
        erase_frame();

        if (s_session.has_ended() || !draw_current_page())
        {
            statePop(g_gameStateManager);
        }
    }

    void dialogueDestroy(void)
    {
        erase_frame();
        screenResumeDisplay(g_mainScreen);

        // The scene has not seen the button since before the box went up, so
        // the press that closed it would read as a fresh click on whatever the
        // pointer happens to be over.
        scene_hotspots_resync_button();
    }

    tState g_stateDialogue = {
        .cbCreate  = dialogueCreate,
        .cbLoop    = dialogueProcess,
        .cbDestroy = dialogueDestroy,
    };
}  // namespace NEONengine
