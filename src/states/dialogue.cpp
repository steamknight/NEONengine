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
#include "core/dialogue_portraits.h"
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
         * @brief The portrait on screen now, in scene space.
         *
         * Kept apart from the frame's rectangle because the two come and go at
         * different times: the frame is redrawn on every page, the portrait
         * only when the speaker changes. Zero-sized when none is up.
         */
        u16 s_portraitX      = 0;
        u16 s_portraitY      = 0;
        u16 s_portraitWidth  = 0;
        u16 s_portraitHeight = 0;

        /// Who the portrait on screen belongs to, or NO_ID when nobody speaks.
        u16 s_speaker = data::NO_ID;

        /** @brief Who the page on show is spoken by, or NO_ID. */
        u16 current_speaker()
        {
            auto const* page = s_session.current_page();
            return page ? page->speaker_id : data::NO_ID;
        }

        /** @brief What @p speaker_id is called, or an empty view when nothing. */
        bstr_view speaker_name(u16 speaker_id)
        {
            if (speaker_id == data::NO_ID) return bstr_view();

            auto const speakers = g_pEngine->data().speakers();
            if (speaker_id >= speakers.size()) return bstr_view();

            u16 const name_id = speakers[speaker_id].name_string_id;
            if (name_id == data::NO_ID) return bstr_view();

            return g_pEngine->strings().get_string(name_id);
        }

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
         * @brief Paints the portrait's rectangle back to the scene behind it.
         *
         * Called whenever the speaker changes, including to nobody: a page
         * without a portrait widens the frame across the column the last one
         * stood in, and the part of the column the frame does not reach would
         * otherwise still show the previous speaker.
         */
        void erase_portrait()
        {
            if (!s_portraitWidth || !s_portraitHeight) return;

            screenRestoreRect(g_mainScreen,
                to<WORD>(s_portraitX),
                to<WORD>(s_portraitY),
                to<WORD>(s_portraitWidth),
                to<WORD>(s_portraitHeight));

            s_portraitWidth  = 0;
            s_portraitHeight = 0;
        }

        /**
         * @brief Draws the page on show, and the speaker it belongs to.
         *
         * The portrait sits in the top-left corner and the frame beside it,
         * holding the speaker's name above the page text. A page whose speaker
         * has no portrait - or which has no speaker at all - gives the frame
         * that column too, which is what makes narrator prose full width.
         *
         * @param draw_portrait Whether the portrait has to be put on screen.
         *                      False when the speaker has not changed, in which
         *                      case theirs is already there and untouched: a
         *                      portrait costs far more to blit than a frame,
         *                      and only the frame changes on the common page.
         * @return False when there was nothing to draw, which leaves the screen
         *         as it was and is the caller's cue to close the box.
         */
        bool draw_current_page(bool draw_portrait)
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

            bool const has_portrait = dialogue_portraits_has(page->speaker_id);

            s_frameX = has_portrait
                ? to<u16>(DIALOGUE_MARGIN * 2 + PORTRAIT_WIDTH)
                : DIALOGUE_MARGIN;
            s_frameY = DIALOGUE_MARGIN;

            // The frame is drawn around the text, so what the text may use is
            // what is left of the screen beside the portrait, less the margin
            // the frame is inset by on the far side and the margin between the
            // frame and its contents on both.
            u16 const allowance
                = to<u16>(SCREEN_WIDTH - s_frameX - DIALOGUE_MARGIN - MESSAGE_MARGIN * 2);

            auto text = renderer->create_text(text_string, allowance, text_justify::LEFT);
            if (!text)
            {
                // Reachable only with no font to draw with. A box the player
                // cannot read is worse than no box at all.
                NE_LOG("dialogue has nothing to draw");
                return false;
            }

            auto const name = speaker_name(page->speaker_id);
            auto heading    = name.is_empty()
                   ? ace::text_bitmap_ptr(nullptr)
                   : renderer->create_text(name, allowance, text_justify::CENTER);

            u16 contentWidth = text->uwActualWidth;
            if (heading && heading->uwActualWidth > contentWidth)
            {
                contentWidth = heading->uwActualWidth;
            }

            u16 const headingHeight
                = heading ? to<u16>(heading->uwActualHeight + MESSAGE_CAPTION_GAP) : u16(0);

            s_frameWidth  = to<u16>(contentWidth + MESSAGE_MARGIN * 2);
            s_frameHeight = to<u16>(headingHeight + text->uwActualHeight + MESSAGE_MARGIN * 2);

            // The frame can only be drawn well at certain widths, so it takes one
            // of those and the text sits in the middle of whatever that leaves.
            auto* frame = g_pEngine->default_nine_patch();
            if (frame) s_frameWidth = frame->width_for(s_frameWidth);

            if (draw_portrait && has_portrait)
            {
                s_portraitX      = DIALOGUE_MARGIN;
                s_portraitY      = DIALOGUE_MARGIN;
                s_portraitWidth  = PORTRAIT_WIDTH;
                s_portraitHeight = PORTRAIT_HEIGHT;
                dialogue_portraits_draw(page->speaker_id, s_portraitX, s_portraitY);
            }

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
            u16 const contentX = to<u16>(s_frameX + (s_frameWidth - contentWidth) / 2);
            u16 const contentY = to<u16>(s_frameY + MESSAGE_MARGIN);

            if (heading)
            {
                // Centred over the text rather than over its own width, which is
                // only as wide as the name itself.
                u16 const headingX
                    = to<u16>(contentX + (contentWidth - heading->uwActualWidth) / 2);
                screenTextCopy(g_mainScreen,
                    heading.get(),
                    headingX,
                    contentY,
                    DIALOGUE_NAME_COLOR,
                    FONT_COOKIE);
            }

            screenTextCopy(g_mainScreen,
                text.get(),
                contentX,
                to<u16>(contentY + headingHeight),
                DIALOGUE_TEXT_COLOR,
                FONT_COOKIE);

            return true;
        }
    }  // namespace

    void dialogueCreate(void)
    {
        u16 const dialogue_id = g_pEngine->take_pending_dialogue();

        s_frameWidth     = 0;
        s_frameHeight    = 0;
        s_portraitWidth  = 0;
        s_portraitHeight = 0;

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

        // Every portrait the conversation could reach, in one go: each one
        // reads the pak and blocks, and a pause as the box appears reads as
        // normal where one between two pages would read as a stall.
        dialogue_portraits_load(s_session.page_range());

        // Held from here until the box comes down. Every draw below lands on the
        // buffer being displayed, and the one held back keeps the scene as it
        // was for erase_frame to paint back.
        screenFreezeDisplay(g_mainScreen);

        s_speaker = current_speaker();

        if (!draw_current_page(true))
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

        u16 const speaker      = current_speaker();
        bool const new_speaker = speaker != s_speaker;
        s_speaker              = speaker;

        // Both erased before either is drawn, so a frame that reclaims the
        // portrait's column finds nothing of the old portrait under it, and a
        // frame that shrinks leaves no part of the taller one behind.
        erase_frame();
        if (new_speaker) erase_portrait();

        if (s_session.has_ended() || !draw_current_page(new_speaker))
        {
            statePop(g_gameStateManager);
        }
    }

    void dialogueDestroy(void)
    {
        erase_frame();
        erase_portrait();

        // After the erase: it hands the palette band back to the scene's bobs,
        // and the pixels underneath have to be the scene's again by then.
        dialogue_portraits_unload();
        s_speaker = data::NO_ID;

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
