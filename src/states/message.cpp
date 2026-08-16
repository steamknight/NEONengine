#include "types.h"

#include "neonengine.h"

#include <ace/managers/blit.h>
#include <ace/managers/key.h>
#include <ace/managers/mouse.h>
#include <ace/managers/state.h>
#include <ace/macros.h>
#include <ace/utils/font.h>

#include <ace++/bitmap.h>

#include "core/logger.h"
#include "core/mouse_pointer.h"
#include "core/nine_patch.h"
#include "core/screen.h"
#include "core/text_render.h"

NE_LOG_DOMAIN(Text);

namespace NEONengine
{
    namespace
    {
        /// The box on screen now, in scene space.
        u16 s_boxX      = 0;
        u16 s_boxY      = 0;
        u16 s_boxWidth  = 0;
        u16 s_boxHeight = 0;

        /**
         * @brief Where the box's top edge goes, given how tall it turned out.
         *
         * A box taller than the screen is pinned to the top, since it is the
         * start of the text that has to be readable.
         */
        u16 top_edge_for(message_align align, u16 height)
        {
            if (height + MESSAGE_MARGIN * 2 >= SCREEN_HEIGHT) return 0;

            switch (align)
            {
                case message_align::top: return MESSAGE_MARGIN;
                case message_align::middle: return to<u16>((SCREEN_HEIGHT - height) / 2);
                case message_align::bottom:  // fallthrough
                default: return to<u16>(SCREEN_HEIGHT - height - MESSAGE_MARGIN);
            }
        }
    }  // namespace

    void messageCreate(void)
    {
        auto const request = g_pEngine->overlays().take_message();

        auto* renderer = g_pEngine->default_text_renderer();

        // The frame is drawn around the text, so what the text may use is what
        // the caller allowed less a margin on either side, and never more than
        // the screen holds.
        u16 const boxLimit = MIN(request.max_width, to<u16>(SCREEN_WIDTH));
        u16 const allowance
            = boxLimit > MESSAGE_MARGIN * 2 ? to<u16>(boxLimit - MESSAGE_MARGIN * 2) : u16(1);

        auto text = renderer->create_text(request.text, allowance, request.justify);
        if (!text)
        {
            // Reachable only with no font to draw with. Leaving the box up would
            // cost the player a click to dismiss something they cannot see.
            // statePush has already made this state current, so this unwinds it.
            NE_LOG("message has nothing to draw; dropping it");
            statePop(g_gameStateManager);
            return;
        }

        auto caption = request.caption.is_empty()
            ? ace::text_bitmap_ptr(nullptr)
            : renderer->create_text(request.caption, allowance, text_justify::CENTER);

        u16 contentWidth = text->uwActualWidth;
        if (caption && caption->uwActualWidth > contentWidth) contentWidth = caption->uwActualWidth;

        u16 const captionHeight
            = caption ? to<u16>(caption->uwActualHeight + MESSAGE_CAPTION_GAP) : u16(0);

        s_boxWidth  = to<u16>(contentWidth + MESSAGE_MARGIN * 2);
        s_boxHeight = to<u16>(captionHeight + text->uwActualHeight + MESSAGE_MARGIN * 2);

        // The frame can only be drawn well at certain widths, so the box takes
        // one of those and the text sits in the middle of whatever that leaves.
        auto* frame = g_pEngine->default_nine_patch();
        if (frame) s_boxWidth = frame->width_for(s_boxWidth);

        s_boxX = to<u16>((SCREEN_WIDTH - s_boxWidth) / 2);
        s_boxY = top_edge_for(request.align, s_boxHeight);

        // Held from here until the box comes down. Every draw below lands on the
        // buffer being displayed, and the one held back keeps the scene as it
        // was for messageDestroy to paint back.
        screenFreezeDisplay(g_mainScreen);

        if (frame)
        {
            if (auto art = frame->render(s_boxWidth, s_boxHeight, 0))
            {
                screenBlitCopy(g_mainScreen,
                    art.get(),
                    0,
                    0,
                    to<WORD>(s_boxX),
                    to<WORD>(s_boxY),
                    to<WORD>(s_boxWidth),
                    to<WORD>(s_boxHeight),
                    MINTERM_COOKIE);
            }
        }

        // Centred in the box rather than at a fixed margin: the frame's width is
        // whatever it could be drawn at, which is a little more than was asked.
        u16 const contentX = to<u16>(s_boxX + (s_boxWidth - contentWidth) / 2);
        u16 const contentY = to<u16>(s_boxY + MESSAGE_MARGIN);

        if (caption)
        {
            // Centred over the text rather than over its own width, which is
            // only as wide as the caption itself.
            u16 const captionX
                = to<u16>(contentX + (contentWidth - caption->uwActualWidth) / 2);
            screenTextCopy(
                g_mainScreen, caption.get(), captionX, contentY, request.caption_color, FONT_COOKIE);
        }

        screenTextCopy(g_mainScreen,
            text.get(),
            contentX,
            to<u16>(contentY + captionHeight),
            request.color,
            FONT_COOKIE);

        // The pointer keeps its own role while the box is up, so nothing has to
        // be put back on the way out.
        g_pEngine->pointer().update();

        // The press that started the script that spoke is still unspent - the
        // hotspots only ever look at it - and would close the box on the frame
        // it opened. Spend it here so a fresh one is needed.
        (void)mouseUse(MOUSE_PORT_1, MOUSE_LMB);
        (void)keyUse(KEY_SPACE);
    }

    void messageProcess(void)
    {
        // Nothing else runs while the box is up, so the sprite would sit where
        // it was until the box came down.
        g_pEngine->pointer().update();

        if (!mouseUse(MOUSE_PORT_1, MOUSE_LMB) && !keyUse(KEY_SPACE)) return;

        statePop(g_gameStateManager);
    }

    void messageDestroy(void)
    {
        screenRestoreRect(g_mainScreen,
            to<WORD>(s_boxX),
            to<WORD>(s_boxY),
            to<WORD>(s_boxWidth),
            to<WORD>(s_boxHeight));
        screenResumeDisplay(g_mainScreen);

        // SAY is a modal bridge call, so the script that asked for this box is
        // parked on the instruction after it and nothing else will start it
        // again. A box the engine put up for its own reasons parked nothing and
        // this is a no-op.
        g_pEngine->vm().resume_execution();

        s_boxWidth  = 0;
        s_boxHeight = 0;
    }

    tState g_stateMessage = {
        .cbCreate  = messageCreate,
        .cbLoop    = messageProcess,
        .cbDestroy = messageDestroy,
        .cbSuspend = nullptr,
        .cbResume  = nullptr,
        .pPrev     = nullptr,  // Set by statePush.
    };
}  // namespace NEONengine
