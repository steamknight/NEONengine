/**
 * @file overlay_system.h
 * @brief The one thing that may go up over the scene in a frame.
 *
 * An overlay is a box the game puts in front of whatever is running - a line of
 * speech, a script's dying words, a conversation. Two cannot share a frame. The
 * freeze that holds a buffer back does not nest, so the second box up would
 * restore its rectangle from a buffer that never held the scene under it.
 *
 * This system is where that rule lives, along with the slot that makes it true:
 * one value waiting, so there is nothing to arbitrate.
 */

#ifndef __OVERLAY_SYSTEM_H__INCLUDED__
#define __OVERLAY_SYSTEM_H__INCLUDED__

#include <ace/managers/state.h>

#include "core/text_render.h"
#include "types.h"
#include "utils/bstr_view.h"

namespace NEONengine
{
    /// Widest a message box may be, in pixels, unless its caller asks for less.
    constexpr u16 MESSAGE_MAX_WIDTH = 256;

    /// Gap between the nine-patch border and the text inside it, in pixels.
    constexpr u16 MESSAGE_MARGIN = 8;

    /// Gap between a message's caption and its text, in pixels.
    constexpr u16 MESSAGE_CAPTION_GAP = 4;

    /// Palette index a message's text uses unless its caller asks for another.
    constexpr u8 MESSAGE_TEXT_COLOR = 1;

    /// Palette index a dead script's report uses.
    constexpr u8 MESSAGE_ERROR_COLOR = 3;

    /**
     * @brief Longest message text the system keeps, in bytes.
     *
     * Comfortably more than a box 256 pixels wide can show, and more than four
     * times the longest line the shipped tables hold. Text past it is cut.
     */
    constexpr size_t MESSAGE_TEXT_CAPACITY = 512;

    /// Longest message caption the system keeps, in bytes. @see MESSAGE_TEXT_CAPACITY
    constexpr size_t MESSAGE_CAPTION_CAPACITY = 32;

    /** @brief Where a message box sits on the screen. */
    enum class message_align : u8
    {
        top,
        middle,
        bottom,
    };

    /**
     * @brief One box of text waiting to be put on screen.
     *
     * Neither string is copied here. @ref overlay_system copies both as it
     * takes the request, so a caller only has to keep them alive for the
     * length of the post.
     */
    struct message_request
    {
        bstr_view text{};
        bstr_view caption{};  ///< Drawn above the text. Empty when there is none.
        u16 max_width{ MESSAGE_MAX_WIDTH };
        text_justify justify{ text_justify::LEFT };
        u8 color{ MESSAGE_TEXT_COLOR };
        u8 caption_color{ MESSAGE_TEXT_COLOR };
        message_align align{ message_align::bottom };
    };

    /** @brief What is waiting to go up over the scene, if anything. */
    enum class overlay_kind : u8
    {
        none,
        message,
        dialogue,
    };

    /**
     * @brief Holds the next overlay, and puts it on screen when it may go.
     *
     * The slot owns the message text it is given. Every message comes from a
     * buffer its poster does not control - the string table's payload, which a
     * language change frees, or the line the VM builds in a buffer it reuses -
     * so copying here is what lets the text outlive the call that posted it.
     *
     * Which states draw an overlay is told to the system at boot rather than
     * known by it, so the engine it belongs to stays free of any knowledge of
     * which states a build has.
     */
    class overlay_system
    {
        public:  //////////////////////////////////////////////////////////////////////////////////
        /**
         * @brief Names the states that draw each kind of overlay.
         *
         * Until this is called nothing is put on screen, which is what a build
         * with no overlay states - the test runner - wants.
         */
        void bind(tState* message_state, tState* dialogue_state) noexcept
        {
            _message_state  = message_state;
            _dialogue_state = dialogue_state;
        }

        /**
         * @brief Records a box of text, unless something is already waiting.
         *
         * The first request stands until it has been shown. A script dies
         * without ending the walk it was part of, so a session later in that
         * same walk can speak, or die in turn, before anything looks at the
         * slot; the death that stopped the first one is the more useful of the
         * two to read.
         *
         * A request with no text is not a message and is ignored. Text longer
         * than the slot holds is cut short.
         */
        void post_message(message_request const& request) noexcept;

        /** @brief Records a conversation, unless something is already waiting. */
        void post_dialogue(u16 dialogue_id) noexcept;

        /**
         * @brief Puts what is waiting on screen, when it may go up.
         *
         * Pushed from here rather than by whoever asked for it. A bridge call
         * runs deep inside the running state's own callback, and pushing from
         * there would draw the box and then let the rest of that callback paint
         * over it.
         *
         * Never over an overlay that is already up, either: a box closing on
         * top would resume the display out from under the one beneath. A script
         * that died inside a conversation says so once the conversation has come
         * down, which is the frame the scene has the screen back.
         */
        void present(tStateManager* states) noexcept;

        /** @brief What is waiting, or @c none. */
        overlay_kind kind() const noexcept { return _kind; }

        /**
         * @brief Takes the waiting message and empties the slot.
         *
         * The strings it carries point into this system, and stay good until
         * the next post. That is long enough: an overlay is drawn as it is
         * created, and nothing runs to post another until it has come down.
         *
         * @return An empty request when a message is not what is waiting.
         */
        message_request take_message() noexcept;

        /**
         * @brief Takes the waiting conversation and empties the slot.
         *
         * @return 0 when a conversation is not what is waiting.
         */
        u16 take_dialogue() noexcept;

        /** @brief Drops whatever is waiting, unshown. */
        void clear() noexcept { _kind = overlay_kind::none; }

        private: //////////////////////////////////////////////////////////////////////////////////
        message_request _message{};
        u16             _dialogue{ 0 };
        overlay_kind    _kind{ overlay_kind::none };
        char            _text[MESSAGE_TEXT_CAPACITY]{};
        char            _caption[MESSAGE_CAPTION_CAPACITY]{};

        tState*         _message_state{ nullptr };
        tState*         _dialogue_state{ nullptr };
    };
}  // namespace NEONengine

#endif  // __OVERLAY_SYSTEM_H__INCLUDED__
