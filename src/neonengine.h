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

    /**
     * @brief Longest message text the engine keeps, in bytes.
     *
     * Comfortably more than a box 256 pixels wide can show, and more than four
     * times the longest line the shipped tables hold. Text past it is cut.
     */
    constexpr size_t MESSAGE_TEXT_CAPACITY = 512;

    /// Longest message caption the engine keeps, in bytes. @see MESSAGE_TEXT_CAPACITY
    constexpr size_t MESSAGE_CAPTION_CAPACITY = 32;

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
     * @brief How long a reply is held highlighted, in frames.
     *
     * Applies only when the player picks a reply with the keyboard. Using the mouse already shows
     * which choice is about to be picked.
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
     * Neither string is copied here. @ref pending_overlay copies both as it
     * takes the request, so a caller only has to keep them alive for the
     * length of the post.
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

    /** @brief What is waiting to go up over the scene, if anything. */
    enum class overlay_kind : u8
    {
        none,
        message,
        dialogue,
    };

    /**
     * @brief The one thing that may go up over the scene in a frame.
     *
     * A message and a conversation cannot share a frame. The freeze that holds
     * a buffer back does not nest, so the second box up would restore its
     * rectangle from a buffer that never held the scene under it. One slot
     * makes that a property of the store, rather than a rule spelled out again
     * wherever overlays are put on screen.
     *
     * The slot owns the message text it is given. Every message comes from a
     * buffer its poster does not control - the string table's payload, which a
     * language change frees, or the line the VM builds in a buffer it reuses -
     * so copying here is what lets the text outlive the call that posted it.
     */
    class pending_overlay
    {
        public:  //////////////////////////////////////////////////////////////////////////////////
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

        /** @brief What is waiting, or @c none. */
        overlay_kind kind() const noexcept { return _kind; }

        /**
         * @brief Takes the waiting message and empties the slot.
         *
         * The strings it carries point into this slot, and stay good until the
         * next post. That is long enough: an overlay is drawn as it is created,
         * and nothing runs to post another until it has come down.
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
    };

    class engine;
    using engine_ptr = mtl::unique_ptr<engine>;    class engine
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

        enum class request_failure
        {
            NONE,
            LANGUAGE_LOAD_FAILED,
            LANGUAGE_MISMATCH,
        };

        using request_result = mtl::expected<void, request_failure>;

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


        /** @brief The active language's strings. */
        string_table& strings() noexcept { return *_pStrings; }

        /** @brief The language currently loaded. */
        language current_language() const noexcept { return _pStrings->language_id(); }

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
         * @brief Post a language change request, applied on the next frame.
         */
        void request_language(language lang) noexcept
        {
            _pending_language.post(lang);
        }

        /** @brief True when a script has requested a language change. */
        bool has_pending_language() const noexcept { return _pending_language.has(); }

        /** @brief True for the one frame in which the string table actually swapped. */
        bool language_changed() const noexcept { return _language_changed; }

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
         * Deferred rather than drawn where it is asked for: a bridge call runs
         * deep inside the running state's own callback, and a box drawn there
         * would be painted over by the rest of that callback.
         *
         * @see pending_overlay::post_message for what happens to a second one.
         */
        void show_message(message_request const& request) noexcept
        {
            _pending_overlay.post_message(request);
        }

        /** @brief Consume the waiting message, clearing the slot. */
        message_request take_pending_message() noexcept
        {
            return _pending_overlay.take_message();
        }

        /**
         * @brief Post a conversation for the dialogue state to put on screen.
         *
         * Deferred for the same reason a message is: the scene answers
         * SHOW_DIALOG from inside its own loop callback.
         */
        void request_dialogue(u16 dialogue_id) noexcept
        {
            _pending_overlay.post_dialogue(dialogue_id);
        }

        /** @brief Consume the waiting conversation, clearing the slot. */
        u16 take_pending_dialogue() noexcept
        {
            return _pending_overlay.take_dialogue();
        }

        /** @brief What is waiting to go up over the scene, or @c none. */
        overlay_kind pending_overlay_kind() const noexcept { return _pending_overlay.kind(); }

        /** @brief The scene the runtime is currently presenting (for GET_SCENE). */
        u16 current_scene() const noexcept { return _current_scene; }
        void set_current_scene(u16 scene_id) noexcept { _current_scene = scene_id; }

        void update() noexcept;

        static result initialize();

        private: //////////////////////////////////////////////////////////////////////////////////

        /** @brief Apply the pending language change, if any. */
        request_result apply_pending_language() noexcept;

        private:  //////////////////////////////////////////////////////////////////////////////////

        game_data_ptr                    _pGameData{ nullptr };
        ace::font_ptr                    _pUiFont{ nullptr };
        text_renderer_ptr                _pTextRenderer{ nullptr };
        mtl::unique_ptr<nine_patch>      _pNinePatch{ nullptr };
        string_table_ptr                 _pStrings{ nullptr };
        noir_vm_ptr                      _vm{ nullptr };

        // Current game session, overwriten on new/save/load game.
        game_session                     _session{};

        // Language requested by a script, awaiting a swap.
        pending_request<language>        _pending_language{};
        pending_request<modal_request>   _pending_modal{};
        pending_request<system_request>  _pending_system{};
        pending_overlay                  _pending_overlay{};

        u16                              _current_scene{ 0 };  ///< Scene the runtime is presenting.
        bool                             _language_changed{ false };
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
