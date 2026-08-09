#ifndef __NEONENGINE_H__INCLUDED__
#define __NEONENGINE_H__INCLUDED__

#include <ace/managers/state.h>
#include <ace/managers/system.h>

#include <ace++/font.h>

#include <mtl/expected.h>
#include <mtl/memory.h>
#include <mtl/vector.h>

#include "core/game_data.h"
#include "core/game_session.h"
#include "core/nine_patch.h"
#include "core/noir_vm.h"
#include "core/screen.h"
#include "core/string_table.h"
#include "core/text_render.h"
#include "types.h"

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

    /// Gap between the edge of the screen and the dialogue frame, in pixels.
    constexpr u16 DIALOGUE_MARGIN = 8;

    /// Palette index used for dialogue text. Changed to the speaker's color or the default text color when there's no speaker.

    constexpr u8 DIALOGUE_TEXT_COLOR = 2;

    /// Palette index a dialogue speaker's name uses, as a message caption does.
    constexpr u8 DIALOGUE_NAME_COLOR = MESSAGE_TEXT_COLOR;

    /// Palette index a reply uses while the player is not about to pick it.
    constexpr u8 DIALOGUE_CHOICE_COLOR = MESSAGE_TEXT_COLOR;

    /// Palette index the reply about to be acted on uses.
    constexpr u8 DIALOGUE_CHOICE_HIGHLIGHT_COLOR = 8;

    /// How far a reply is indented past the page text, in pixels.
    constexpr u16 DIALOGUE_CHOICE_INDENT = 8;

    /// Gap between a reply's number and its text, in pixels.
    constexpr u16 DIALOGUE_CHOICE_NUMBER_GAP = 4;

    /// Gap between the page text and the first reply under it, in pixels.
    constexpr u16 DIALOGUE_CHOICE_GAP = 6;

    /// Gap between one reply and the next, in pixels.
    constexpr u16 DIALOGUE_CHOICE_SPACING = 2;

    /// Most replies one page can show, which is as many as the numbering allows.
    constexpr u8 DIALOGUE_MAX_CHOICES_SHOWN = 9;

    /**
     * @brief How long a reply picked by its number is held highlighted, in frames.
     *
     * Long enough for the player to read what they picked before the
     * conversation moves on, short enough not to feel like a stall. A reply
     * picked with the mouse needs none of this: it was highlighted for as long
     * as the pointer sat on it.
     */
    constexpr u8 DIALOGUE_CHOICE_HOLD_FRAMES = 12;

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
     * A line a character speaks and a dead script's report differ only in what
     * these fields hold, so everything that has something to say fills one of
     * these in and one state draws them all.
     *
     * Neither string is copied. Whoever posts a request has to keep both alive
     * until the box is drawn, which is the next time the main loop runs.
     */
    struct message_request
    {
        bstr_view text;
        bstr_view caption;  ///< Drawn above the text. Empty when there is none.
        u16 max_width{ MESSAGE_MAX_WIDTH };
        text_justify justify{ text_justify::LEFT };
        u8 color{ MESSAGE_TEXT_COLOR };
        u8 caption_color{ MESSAGE_TEXT_COLOR };
        message_align align{ message_align::bottom };
    };

    /**
     * @brief What a deferred modal bridge call asked the scene to put up.
     *
     * The kinds match the bridge functions that record one. @c none means the
     * request slot is empty.
     */
    enum class modal_kind : u8
    {
        none,
        show_dialog,  ///< a = dialogue id
        fade_out,     ///< a = frames
        fade_in,      ///< a = frames
    };

    /** @brief One recorded modal request, with the opcode's D0 and D1. */
    struct modal_request
    {
        modal_kind kind{ modal_kind::none };
        u16 arg1{ 0 };
        u16 arg2{ 0 };
    };

    /**
     * @brief A request to change which game is being played.
     *
     * Separate from a modal because it says what the scene must do, not what it
     * must draw. Most of these come from a deferred terminal call, which stops
     * the VM until the teardown they ask for has happened; @c save_game comes
     * from a modal one, and the session that asked for it lives on.
     */
    enum class system_kind : u8
    {
        none,
        load_scene,  ///< a = scene id — GOTO_SCENE, or a hotspot with a target
        new_game,    ///< NEW_GAME
        load_game,   ///< a = slot — LOAD_GAME
        save_game,   ///< a = slot — SAVE_GAME
        quit_game,   ///< QUIT_GAME
    };

    /** @brief One recorded system request, with the opcode's D0. */
    struct system_request
    {
        system_kind kind{ system_kind::none };
        u16 arg1{ 0 };
    };

    /** @brief A slot for a pending engine request. */
    template<typename T>
    class pending_request
    {
        public:  ///////////////////////////////////////////////////////////////////////////////////
        /**
         * @brief Records @p request, replacing anything already waiting.
         *
         * The right default for a request whose sender stops the VM as it posts:
         * nothing runs to post a second, and if something did, the newer one is
         * the one with a caller still waiting on it.
         */
        void post(T request) noexcept { _request = mtl::move(request); _valid = true; }

        /**
         * @brief Records @p request only when the slot is free.
         *
         * For requests whose senders are not serialised, and where the first is
         * the one worth keeping.
         */
        void post_if_empty(T request) noexcept
        {
            if (_valid) return;
            post(mtl::move(request));
        }

        /** @brief True when something is waiting to be acted on. */
        bool has() const noexcept { return _valid; }

        /**
         * @brief The waiting request, without consuming it.
         *
         * For a reader that must decide something from the request but is not
         * the one that will answer it - the scene reading a system kind to know
         * it is mid-transition, say.
         *
         * @return A default-constructed @c T when the slot is empty.
         */
        T const& peek() const noexcept { return _request; }

        /**
         * @brief Takes the waiting request and empties the slot.
         *
         * @return A default-constructed @c T when the slot is empty, so an
         *         unguarded take is harmless rather than undefined.
         */
        T take() noexcept
        {
            T const taken = _request;
            clear();
            return taken;
        }

        /** @brief Drops whatever is waiting, unanswered. */
        void clear() noexcept
        {
            _request = T{};
            _valid   = false;
        }

        private: //////////////////////////////////////////////////////////////////////////////////
        T    _request{};
        bool _valid{ false };
    };

    class engine;
    using engine_ptr = mtl::unique_ptr<engine>;

    class engine
    {
        public:  //////////////////////////////////////////////////////////////////////////////////
        enum class error_code
        {
            GAME_DATA_LOAD_FAILED,
            UI_FONT_LOAD_FAILED,
            UI_PALETTE_LOAD_FAILED,
            FAILED_TO_CREATE_TEXT_RENDERER,
            STRING_TABLE_LOAD_FAILED,
        };

        using result = mtl::expected<engine_ptr, error_code>;

        public:  //////////////////////////////////////////////////////////////////////////////////
        tFont* default_font() noexcept { return _pUiFont.get(); }
        text_renderer* default_text_renderer() noexcept { return _pTextRenderer.get(); }

        /**
         * @brief The frame every box of text is drawn in.
         *
         * Held for the process lifetime because a box goes up whenever a
         * character speaks, and decoding the source art blocks on the pak.
         *
         * @return Null when the project ships no nine-patch.
         */
        nine_patch* default_nine_patch() noexcept { return _pNinePatch.get(); }

        game_data& data() noexcept { return *_pGameData; }
        noir_vm& vm() noexcept { return *_vm; }

        /**
         * @brief What the player has done, as opposed to what the data says.
         *
         * Created at boot and held for as long as the playthrough lasts.
         */
        game_session& session() noexcept { return _session; }

        /**
         * @brief The current value of game state flag @p flag_id.
         *
         * Reads the buffer the VM borrows, so scripts and authored data see the
         * same flag. An id past the end of that buffer is false, because a flag
         * the data never declared has never been set.
         */
        bool get_flag(u16 flag_id) const noexcept
        {
            u32 const byte = to<u32>(flag_id) >> 3;
            if (byte >= _flags.size()) return false;
            return (_flags[byte] & to<u8>(1 << (flag_id & 7))) != 0;
        }

        /**
         * @brief Writes game state flag @p flag_id.
         *
         * Writes the buffer the VM borrows, so a flag authored data sets is the
         * same flag a script goes on to read. An id past the end of that buffer
         * is dropped: the data never declared it, and growing the buffer here
         * would move the memory the VM holds.
         */
        void set_flag(u16 flag_id, bool value) noexcept
        {
            u32 const byte = to<u32>(flag_id) >> 3;
            if (byte >= _flags.size()) return;

            u8 const mask = to<u8>(1 << (flag_id & 7));
            if (value)
                _flags[byte] = to<u8>(_flags[byte] | mask);
            else
                _flags[byte] = to<u8>(_flags[byte] & ~mask);
        }

        /** @brief The active language's strings. */
        string_table& strings() noexcept { return *_pStrings; }

        /** @brief The language currently loaded. */
        language current_language() const noexcept { return _pStrings->language_id(); }

        /**
         * @brief Switch to @p lang, keeping the current strings if it fails.
         *
         * The new table is loaded before the old one is released, so a missing
         * or corrupt language file never leaves the game without strings.
         *
         * @param lang The language to load.
         * @return True when @p lang is now active.
         */
        bool set_language(language lang);

        /**
         * @brief Post a system request - a scene change, a save, a new game.
         *
         * One slot is enough. Every call that records one either stops the VM
         * outright or drains the frame pool, so nothing runs to record a second
         * before the scene has answered this one.
         */
        void request_system(system_kind kind, u16 arg1 = 0) noexcept
        {
            _pending_system.post(system_request{ kind, arg1 });
        }

        /** @brief True when something has asked for a system change. */
        bool has_pending_system() const noexcept
        {
            return _pending_system.has();
        }

        /** @brief What the pending system request asks for, or @c none. */
        system_kind pending_system_kind() const noexcept { return _pending_system.peek().kind; }
        
        /** @brief Consume the pending system request, clearing it. */
        system_request take_pending_system() noexcept
        {
            system_request const taken = _pending_system.take();
            return taken;
        }

        /**
         * @brief Post a language change requested by a script (SET_LANG).
         *
         * Deferred rather than applied on the spot: SET_LANG runs inside a VM
         * step, and swapping the table reads from disk. The state loop applies
         * it at the same safe point it applies a pending scene.
         */
        void request_language(language lang) noexcept
        {
            _pending_language.post(lang);
        }

        /** @brief True when a script has requested a language change. */
        bool has_pending_language() const noexcept { return _pending_language.has(); }

        /** @brief Consume the pending language request, clearing it. */
        language take_pending_language() noexcept
        {
            auto const taken = _pending_language.take();
            return taken;
        }

        /**
         * @brief Post a modal requested by a script (SAY, FADE_OUT, and so on).
         *
         * The opcode spends the rest of the frame budget as it records this, so
         * no session runs again until the scene has put the modal up and taken
         * it down. One slot is enough: nothing else runs to record a second.
         */
        void request_modal(modal_kind kind, u16 arg1, u16 arg2 = 0) noexcept
        {
            _pending_modal.post(modal_request{ kind, arg1, arg2 });
        }

        /** @brief True when a script has requested a modal. */
        bool has_pending_modal() const noexcept { return _pending_modal.has(); }

        /** @brief Consume the pending modal request, clearing it. */
        modal_request take_pending_modal() noexcept
        {
            return _pending_modal.take();
        }

        /**
         * @brief Post a box of text for the message state to put on screen.
         *
         * The first request stands until it has been shown. A script dies
         * without ending the walk it was part of, so a session later in that
         * same walk can speak, or die in turn, before anything looks at the
         * slot; the death that stopped the first one is the more useful of the
         * two to read.
         *
         * A request with no text is not a message and is ignored.
         */
        void show_message(message_request const& request) noexcept
        {
            if (has_pending_message()) return;
            _pending_message.post_if_empty(request);
        }

        /** @brief True when something has a box of text waiting to be shown. */
        bool has_pending_message() const noexcept { return _pending_message.has(); }

        /** @brief Consume the waiting message, clearing the slot. */
        message_request take_pending_message() noexcept
        {
            return _pending_message.take();
        }

        /**
         * @brief Post a conversation for the dialogue state to put on screen.
         *
         * Deferred for the same reason a message is: the scene answers
         * SHOW_DIALOG from inside its own loop callback, and a state pushed
         * there would draw the box and then let the rest of that callback paint
         * over it. The main loop pushes it instead, before any state runs.
         *
         * One slot is enough. The opcode that asks for one parks its session and
         * spends the frame budget, and the scene holds the VM until the box has
         * come down, so nothing runs to ask for a second.
         */
        void request_dialogue(u16 dialogue_id) noexcept
        {
            _pending_dialogue.post(dialogue_id);
        }

        /** @brief True when a conversation is waiting to be put on screen. */
        bool has_pending_dialogue() const noexcept { return _pending_dialogue.has(); }

        /** @brief Consume the waiting conversation, clearing the slot. */
        u16 take_pending_dialogue() noexcept
        {
            return _pending_dialogue.take();
        }

        /** @brief The scene the runtime is currently presenting (for GET_SCENE). */
        u16 current_scene() const noexcept { return _current_scene; }
        void set_current_scene(u16 scene_id) noexcept { _current_scene = scene_id; }

        static result initialize();

        private:  //////////////////////////////////////////////////////////////////////////////////

        game_data_ptr                    _pGameData{ nullptr };
        ace::font_ptr                    _pUiFont{ nullptr };
        text_renderer_ptr                _pTextRenderer{ nullptr };
        mtl::unique_ptr<nine_patch>      _pNinePatch{ nullptr };
        string_table_ptr                 _pStrings{ nullptr };
        noir_vm_ptr                      _vm{ nullptr };

        // Current game session, overwriten on new/save/load game.
        game_session                     _session{};

        // TODO: Move in to game session, and have the VM borrow it from there. 
        mtl::vector<u8>                  _flags;

        // Language requested by a script, awaiting a swap.
        pending_request<language>        _pending_language{};
        pending_request<modal_request>   _pending_modal{};
        pending_request<system_request>  _pending_system{};
        pending_request<message_request> _pending_message{};
        pending_request<u16>             _pending_dialogue{};

        u16 _current_scene{ 0 };  ///< Scene the runtime is presenting.
    };

    extern engine_ptr g_pEngine;

    extern tStateManager* g_gameStateManager;
    extern tState g_stateDebugView,  //
        g_stateScene,                //
        g_stateDialogue,             //
        g_stateMessage;

#ifdef ACE_TEST_RUNNER
    namespace tests
    {
        extern tState g_stateTestRunner;
    }
#endif

    extern Screen* g_mainScreen;

}  // namespace NEONengine

#endif  //__NEONENGINE_H__INCLUDED__
