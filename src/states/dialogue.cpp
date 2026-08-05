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

        /// No reply: no slot is highlighted, or the pointer is over none.
        constexpr u8 NO_CHOICE = 0xFF;

        /**
         * @brief The cursor a reply puts up.
         *
         * The engine's clickable role, reused rather than a hand of its own:
         * the roles are a closed set matching the authored cursor sheet.
         */
        constexpr mouse_pointer CHOICE_CURSOR = mouse_pointer::USE;

        /**
         * @brief One reply as it stands on screen.
         *
         * The rendered text is kept rather than the string it came from, so
         * moving the highlight costs one blit instead of laying the reply out
         * again. Its rectangle is what the pointer is tested against, and runs
         * the full width of the frame's contents so that a click beside a short
         * reply still takes it.
         */
        struct drawn_choice
        {
            ace::text_bitmap_ptr number{ nullptr };  ///< "1.", "2.", and so on.
            ace::text_bitmap_ptr text{ nullptr };    ///< What the reply says.

            /// Which of the session's available choices this is, or NO_SLOT for
            /// the "More..." that rotates to the next set.
            u8 session_slot;

            u16 x;       ///< Left edge of the number, in scene space.
            u16 text_x;  ///< Left edge of the text, shared by every reply.
            u16 y;
            u16 width;
            u16 height;
        };

        /// The "More..." row rotates rather than leading anywhere of its own.
        constexpr u8 MORE_SLOT = dialogue_session::NO_SLOT;

        static_assert(DIALOGUE_MAX_CHOICES_SHOWN == dialogue_session::MAX_CHOICES_SHOWN,
            "the numbering the box draws and the cap the session fits to are the same limit");

        drawn_choice s_choices[DIALOGUE_MAX_CHOICES_SHOWN];

        /// How many replies are on screen. Zero on a page that advances instead.
        u8 s_choice_count = 0;

        /// The reply about to be acted on, or NO_CHOICE when none is.
        u8 s_highlight = NO_CHOICE;

        /// Frames left of the pause that shows off a reply picked by its number.
        u8 s_hold_frames = 0;

        /// The reply that pause is showing off, taken when it runs out.
        u8 s_pending = NO_CHOICE;

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
         * @brief Drops the replies on screen, without erasing them.
         *
         * The frame they stand in is painted back separately, so this only lets
         * go of what was rendered for them.
         */
        void forget_choices()
        {
            for (u8 slot = 0; slot < s_choice_count; ++slot)
            {
                s_choices[slot].number.reset(nullptr);
                s_choices[slot].text.reset(nullptr);
            }

            s_choice_count = 0;
            s_highlight    = NO_CHOICE;
            s_hold_frames  = 0;
            s_pending      = NO_CHOICE;
        }

        /** @brief Puts the reply in @p slot on screen in @p color. */
        void draw_choice(u8 slot, u8 color)
        {
            auto& drawn = s_choices[slot];

            if (drawn.number)
            {
                screenTextCopy(
                    g_mainScreen, drawn.number.get(), drawn.x, drawn.y, color, FONT_COOKIE);
            }

            screenTextCopy(
                g_mainScreen, drawn.text.get(), drawn.text_x, drawn.y, color, FONT_COOKIE);
        }

        /**
         * @brief Moves the highlight to @p slot, or clears it with NO_CHOICE.
         *
         * Both replies are drawn again in their new colour rather than erased:
         * the glyphs land on the same pixels either way, so the colour is all
         * that changes.
         */
        void set_highlight(u8 slot)
        {
            if (slot == s_highlight) return;

            if (s_highlight != NO_CHOICE) draw_choice(s_highlight, DIALOGUE_CHOICE_COLOR);
            s_highlight = slot;
            if (s_highlight != NO_CHOICE)
            {
                draw_choice(s_highlight, DIALOGUE_CHOICE_HIGHLIGHT_COLOR);
            }
        }

        /** @brief Which reply the pointer stands on, or NO_CHOICE. */
        u8 choice_under_pointer()
        {
            // Replies are laid out in scene space while the pointer reports
            // display space, so the pointer is shifted rather than every reply.
            auto const offset_y = screenGetOffset(g_mainScreen).y;
            auto const pointerY = mouseGetY(MOUSE_PORT_1);
            if (pointerY < offset_y) return NO_CHOICE;

            u16 const x = to<u16>(mouseGetX(MOUSE_PORT_1));
            u16 const y = to<u16>(pointerY - offset_y);

            for (u8 slot = 0; slot < s_choice_count; ++slot)
            {
                auto const& drawn = s_choices[slot];
                if (x < drawn.x || x >= drawn.x + drawn.width) continue;
                if (y < drawn.y || y >= drawn.y + drawn.height) continue;
                return slot;
            }

            return NO_CHOICE;
        }

        /**
         * @brief The column every reply's text starts at, in frame space.
         *
         * The widest number the page could put up decides it, so a number
         * narrower than that still leaves the text beside it aligned - and a
         * reply that wraps hangs under its own text rather than its number.
         * Measured over every number the page could reach rather than the ones
         * the current set happens to draw, so rotating between sets of
         * different lengths does not shift the text sideways.
         */
        u16 choice_column(text_renderer* renderer, u16 allowance)
        {
            u8 numbered = s_session.choice_count();
            if (numbered > DIALOGUE_MAX_CHOICES_SHOWN) numbered = DIALOGUE_MAX_CHOICES_SHOWN;

            u16 numberWidth = 0;
            for (u8 slot = 0; slot < numbered; ++slot)
            {
                char label[3]      = { to<char>('1' + slot), '.', '\0' };
                u16 const measured = renderer->measure_text(label, allowance).width;
                if (measured > numberWidth) numberWidth = measured;
            }

            return to<u16>(DIALOGUE_CHOICE_INDENT + numberWidth + DIALOGUE_CHOICE_NUMBER_GAP);
        }

        /** @brief What "More..." says, in the player's language. */
        bstr_view more_text()
        {
            return g_pEngine->strings().more_choices();
        }

        /**
         * @brief Measures every reply the page offers, without drawing any.
         *
         * What the session fits its choice sets against. A reply the string
         * table has nothing for measures zero, which is how the session is told
         * to leave it out of every set: the numbers the player sees then run 1,
         * 2, 3 with no gaps and no dead slots.
         *
         * @param heights Room for one height per available reply.
         * @return What the tallest reply the page could show takes, which is
         *         what it keeps back for the floor under its text. The tallest
         *         rather than the one that happens to come first, so that
         *         whichever reply a set opens with has room for it; and the
         *         page's rather than the current set's, so that rotating
         *         between sets does not move the text above them.
         */
        u16 measure_choices(text_renderer* renderer, u16 choiceAllowance, u16* heights)
        {
            u8 const count = s_session.choice_count();
            u16 tallest    = 0;

            for (u8 slot = 0; slot < count; ++slot)
            {
                auto string
                    = g_pEngine->strings().get_string(s_session.choice_at(slot)->text_string_id);

                heights[slot] = string.is_empty()
                    ? u16(0)
                    : renderer->measure_text(string, choiceAllowance).height;

                if (heights[slot] == 0)
                {
                    NE_LOG("reply %hu has no text to show", s_session.choice_index(slot));
                    continue;
                }

                if (heights[slot] > tallest) tallest = heights[slot];
            }

            return tallest;
        }

        /** @brief Adds @p text as the next reply on screen, numbered where it lands. */
        void push_choice(text_renderer* renderer,
            u8 session_slot,
            ace::text_bitmap_ptr&& text,
            u16 column,
            u16 allowance,
            u16& widest,
            u16& height)
        {
            auto& drawn        = s_choices[s_choice_count];
            drawn.session_slot = session_slot;
            drawn.height       = text->uwActualHeight;
            drawn.text_x       = column;  // Made absolute once the frame is placed.
            if (text->uwActualWidth > widest) widest = text->uwActualWidth;

            // Numbered by where it is drawn, not by where the session holds it,
            // so every number the player can see belongs to a row that is there.
            char label[3] = { to<char>('1' + s_choice_count), '.', '\0' };
            drawn.number  = renderer->create_text(label, allowance, text_justify::LEFT);
            drawn.text    = mtl::move(text);

            if (s_choice_count) height = to<u16>(height + DIALOGUE_CHOICE_SPACING);
            height = to<u16>(height + drawn.height);
            ++s_choice_count;
        }

        /**
         * @brief Renders the set of replies the session settled on.
         *
         * Nothing is put on screen yet: the frame has to be sized around them
         * first, which is why this widens @p contentWidth when a reply is wider
         * than the page text. The last slot goes to "More..." when the replies
         * do not all fit at once, and picking it shows the next set.
         *
         * @return The height the replies add under the page text, zero for a
         *         page that shows none - which is the page that advances on a
         *         click instead.
         */
        u16 build_choices(
            text_renderer* renderer, u16 column, u16 allowance, u16 choiceAllowance,
            u16& contentWidth)
        {
            forget_choices();

            u16 widest = 0;
            u16 height = 0;

            u8 const shown = s_session.shown_count();
            for (u8 index = 0; index < shown; ++index)
            {
                u8 const slot = s_session.shown_slot(index);
                auto string
                    = g_pEngine->strings().get_string(s_session.choice_at(slot)->text_string_id);

                auto text = renderer->create_text(string, choiceAllowance, text_justify::LEFT);
                if (!text) continue;

                push_choice(
                    renderer, slot, mtl::move(text), column, allowance, widest, height);
            }

            if (s_session.has_more())
            {
                auto text = renderer->create_text(more_text(), choiceAllowance,
                    text_justify::LEFT);

                // Without it the sets after the first are out of reach, so this
                // is worth saying even though the string is the editor's to
                // provide and is always there in a sound pak.
                if (!text) { NE_LOG("no \"More...\" to show; the rest of the replies are lost"); }
                else
                {
                    push_choice(
                        renderer, MORE_SLOT, mtl::move(text), column, allowance, widest, height);
                }
            }

            if (s_choice_count == 0) return 0;

            if (column + widest > contentWidth) contentWidth = to<u16>(column + widest);

            return to<u16>(height + DIALOGUE_CHOICE_GAP);
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

            // The frame is pinned under the top margin and may not pass the
            // bottom one, so this is everything it can ever hold: the heading,
            // the page text, and the replies under them.
            u16 const contentSpace = to<u16>(
                SCREEN_HEIGHT - s_frameY - DIALOGUE_MARGIN - MESSAGE_MARGIN * 2);
            u16 const textSpace
                = contentSpace > headingHeight ? to<u16>(contentSpace - headingHeight) : u16(0);

            u16 const column          = choice_column(renderer, allowance);
            u16 const choiceAllowance = column < allowance ? to<u16>(allowance - column) : u16(0);

            if (s_session.choice_count() && !choiceAllowance)
            {
                NE_LOG("no room beside the reply numbers; showing none");
            }

            u16 heights[dialogue_session::MAX_CHOICES]{};
            u16 const tallestChoice
                = choiceAllowance ? measure_choices(renderer, choiceAllowance, heights) : u16(0);
            u16 const moreHeight
                = tallestChoice ? renderer->measure_text(more_text(), choiceAllowance).height
                                : u16(0);

            // Room for one reply and a "More..." kept back before the text is
            // placed. Without it a page whose text took everything would offer
            // nothing to pick and fall straight through to its next page,
            // making unreadable text look exactly like a page the author gave
            // nothing to pick. Sized to the tallest reply the page has, so
            // whichever one a set opens with fits under the text and the frame
            // never grows past the bottom margin.
            u16 const floorHeight = tallestChoice
                ? to<u16>(DIALOGUE_CHOICE_GAP + tallestChoice + DIALOGUE_CHOICE_SPACING
                    + moreHeight)
                : u16(0);
            u16 const textRoom
                = textSpace > floorHeight ? to<u16>(textSpace - floorHeight) : u16(0);

            if (text->uwActualHeight > textRoom)
            {
                NE_LOG("page %hu is taller than the box may be; its text is clipped to %hu",
                    s_session.current_page_index(),
                    textRoom);

                // The bitmap is drawn to the height it declares, so shortening
                // that is the clip.
                text->uwActualHeight = textRoom;
            }

            u16 choiceSpace = textSpace > text->uwActualHeight
                ? to<u16>(textSpace - text->uwActualHeight)
                : u16(0);
            choiceSpace = choiceSpace > DIALOGUE_CHOICE_GAP
                ? to<u16>(choiceSpace - DIALOGUE_CHOICE_GAP)
                : u16(0);

            s_session.fit_choices(mtl::span<u16>(heights, s_session.choice_count()),
                choiceSpace,
                DIALOGUE_CHOICE_SPACING,
                moreHeight);

            // Before the frame is sized: the replies live inside it, so what
            // they need is part of what it has to hold.
            u16 const choicesHeight
                = build_choices(renderer, column, allowance, choiceAllowance, contentWidth);

            s_frameWidth = to<u16>(contentWidth + MESSAGE_MARGIN * 2);
            s_frameHeight = to<u16>(
                headingHeight + text->uwActualHeight + choicesHeight + MESSAGE_MARGIN * 2);

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

            // Clipped to nothing when the replies took everything: the blitter
            // is asked for a rectangle, and one with no rows in it is not one.
            if (text->uwActualHeight)
            {
                screenTextCopy(g_mainScreen,
                    text.get(),
                    contentX,
                    to<u16>(contentY + headingHeight),
                    DIALOGUE_TEXT_COLOR,
                    FONT_COOKIE);
            }

            // Under the page text, indented and numbered from 1. The rectangles
            // are settled here rather than while measuring, because where the
            // frame ended up is what puts them on the screen.
            u16 choiceY = to<u16>(contentY + headingHeight + text->uwActualHeight
                + DIALOGUE_CHOICE_GAP);
            for (u8 slot = 0; slot < s_choice_count; ++slot)
            {
                auto& drawn  = s_choices[slot];
                drawn.x      = to<u16>(contentX + DIALOGUE_CHOICE_INDENT);
                drawn.text_x = to<u16>(contentX + drawn.text_x);
                drawn.y      = choiceY;

                // The whole width of the frame's contents, so a click beside a
                // short reply still lands on it.
                drawn.width = to<u16>(contentWidth - DIALOGUE_CHOICE_INDENT);

                draw_choice(slot, DIALOGUE_CHOICE_COLOR);
                choiceY = to<u16>(choiceY + drawn.height + DIALOGUE_CHOICE_SPACING);
            }

            return true;
        }

        /**
         * @brief Puts the page the conversation now stands on up, or closes the box.
         *
         * The way out of every navigation: whatever moved the conversation -
         * advancing, or a reply being taken - the screen catches up the same
         * way, and the box comes down when there is nothing left to show.
         */
        void show_current_page()
        {
            u16 const speaker      = current_speaker();
            bool const new_speaker = speaker != s_speaker;
            s_speaker              = speaker;

            forget_choices();

            // Both erased before either is drawn, so a frame that reclaims the
            // portrait's column finds nothing of the old portrait under it, and
            // a frame that shrinks leaves no part of the taller one behind.
            erase_frame();
            if (new_speaker) erase_portrait();

            if (s_session.has_ended() || !draw_current_page(new_speaker))
            {
                statePop(g_gameStateManager);
            }
        }

        /**
         * @brief Puts the set of replies the page now stands on up.
         *
         * What "More..." does. The page has not changed, so its portrait stays
         * where it is; the frame is sized to what it holds and every set is a
         * different height, so its rectangle is painted back and drawn again.
         */
        void show_current_set()
        {
            forget_choices();
            erase_frame();

            if (!draw_current_page(false)) statePop(g_gameStateManager);
        }

        /** @brief Takes the reply drawn in @p slot and shows where it leads. */
        void take_choice(u8 slot)
        {
            u8 const session_slot = s_choices[slot].session_slot;

            if (session_slot == MORE_SLOT)
            {
                s_session.rotate_choices();
                show_current_set();
                return;
            }

            s_session.take_choice(session_slot);
            show_current_page();
        }
    }  // namespace

    void dialogueCreate(void)
    {
        u16 const dialogue_id = g_pEngine->take_pending_dialogue();

        s_frameWidth     = 0;
        s_frameHeight    = 0;
        s_portraitWidth  = 0;
        s_portraitHeight = 0;
        forget_choices();

        s_session.create(g_pEngine->data().dialogues(),
            g_pEngine->data().pages(),
            g_pEngine->data().choices(),
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

        // The pointer takes a role of its own while the box is up - the
        // clickable one over a reply - so the scene's is put back on the way
        // out.
        mousePointerUpdate();

        // The press that started the script that spoke is still unspent - the
        // hotspots only ever look at it - and would advance the conversation on
        // the frame it opened. Spend it here so a fresh one is needed.
        (void)mouseUse(MOUSE_PORT_1, MOUSE_LMB);
        (void)keyUse(KEY_SPACE);
    }

    void dialogueProcess(void)
    {
        // The role the pointer shows is settled before the sprite moves: the
        // move applies the role's own click offset, so a role changed after it
        // would be a frame late.
        if (s_hold_frames == 0)
        {
            u8 const hovered = s_choice_count ? choice_under_pointer() : NO_CHOICE;
            set_highlight(hovered);
            mousePointerSwitch(hovered == NO_CHOICE ? mouse_pointer::DEFAULT : CHOICE_CURSOR);
        }

        // Nothing else runs while the box is up, so the sprite would sit where
        // it was until the box came down.
        mousePointerUpdate();

        if (s_hold_frames > 0)
        {
            // The reply the player picked with the keyboard is being shown to
            // them. Every press that arrives during it is spent rather than
            // left for the next page to act on.
            (void)mouseUse(MOUSE_PORT_1, MOUSE_LMB);
            (void)keyUse(KEY_SPACE);
            for (u8 key = KEY_1; key < KEY_1 + DIALOGUE_MAX_CHOICES_SHOWN; ++key)
            {
                (void)keyUse(key);
            }

            if (--s_hold_frames > 0) return;

            u8 const slot = s_pending;
            s_pending     = NO_CHOICE;
            take_choice(slot);
            return;
        }

        // A page with nothing to pick - written with no replies, or with every
        // one gated off or already taken - is what lets a character show the
        // player out, so it advances on a click anywhere or on space.
        if (s_choice_count == 0)
        {
            if (!mouseUse(MOUSE_PORT_1, MOUSE_LMB) && !keyUse(KEY_SPACE)) return;

            s_session.advance();
            show_current_page();
            return;
        }

        for (u8 slot = 0; slot < s_choice_count; ++slot)
        {
            if (!keyUse(to<u8>(KEY_1 + slot))) continue;

            // The highlight moves to what the number named before anything is
            // done about it, so it always marks the reply about to be acted on.
            set_highlight(slot);
            s_pending     = slot;
            s_hold_frames = DIALOGUE_CHOICE_HOLD_FRAMES;
            return;
        }

        if (mouseUse(MOUSE_PORT_1, MOUSE_LMB) && s_highlight != NO_CHOICE)
        {
            take_choice(s_highlight);
        }
    }

    void dialogueDestroy(void)
    {
        forget_choices();
        erase_frame();
        erase_portrait();

        // The box put the clickable role up over a reply; the scene decides
        // what the pointer shows from its own hotspots, and starts from here.
        mousePointerSwitch(mouse_pointer::DEFAULT);

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
