#ifndef __NEONENGINE_H__INCLUDED__
#define __NEONENGINE_H__INCLUDED__

#include <ace/managers/system.h>

#include <ace++/font.h>

#include <mtl/expected.h>
#include <mtl/memory.h>
#include <mtl/vector.h>

#include "core/asset_cache.h"
#include "core/game_data.h"
#include "core/game_session.h"
#include "core/mouse_pointer.h"
#include "core/nine_patch.h"
#include "core/noir_vm.h"
#include "core/overlay_system.h"
#include "core/screen.h"
#include "core/state_manager.h"
#include "core/string_table.h"
#include "core/text_render.h"
#include "types.h"
#include "utils/assert.h"

namespace NEONengine
{
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

    class engine;
    class scene_runtime;
    using engine_ptr = mtl::unique_ptr<engine>;
    using screen_ptr = mtl::unique_ptr<screen>;

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
            BOOT_SCENE_INVALID,
            ENGINE_ALLOCATION_FAILED,
        };

        using result = mtl::expected<engine_ptr, error_code>;

        public:  //////////////////////////////////////////////////////////////////////////////////
        ~engine() noexcept;
        
        /**
         * @brief Begins the game's execution by loading the boot scene.
         */
        void start();

        /**
         * @brief Begins the game's execution by loading a state of type @p T, constructed with @p args.
         * 
         * @tparam T The type of the state to start.
         * @param args Arguments to forward to the state's constructor.
        */
        template<state_type T, typename... Args>
        void start_state(Args&&... args)
        {
            _states.start<T>(mtl::forward<Args>(args)...);
        }

        tFont* default_font() noexcept { return _ui_font.get(); }
        text_renderer* default_text_renderer() noexcept { return _text_renderer.get(); }

        /**
         * @brief The frame every box of text is drawn in.
         *
         * Held for the process lifetime because a box goes up whenever a
         * character speaks, and decoding the source art blocks on the pak.
         *
         * @return Null when the project ships no nine-patch.
         */
        nine_patch* default_nine_patch() noexcept { return _nine_patch.get(); }

        /** @brief The bitmap hover captions are blitted through. Null when there is none. */
        tBitMap* caption_ink() noexcept { return _caption_ink.get(); }

        game_data& data() noexcept { return *_game_data; }

        /** @brief The Fast RAM cache asset reads go through. */
        asset_cache& assets() noexcept { return *_assets; }

        /** @brief The hardware pointer, shared by every state that draws. */
        mouse_pointer& pointer() noexcept { return *_pointer; }
        noir_vm& vm() noexcept { return *_vm; }

        void bind_scene_runtime(scene_runtime& runtime) noexcept
        {
            assert(!_scene_runtime, "a scene runtime is already bound");
            _scene_runtime = &runtime;
        }

        bool unbind_scene_runtime(scene_runtime& runtime) noexcept
        {
            if (_scene_runtime != &runtime) return false;
            _scene_runtime = nullptr;
            return true;
        }

        scene_runtime& scene() noexcept
        {
            assert(_scene_runtime, "no scene runtime is bound");
            return *_scene_runtime;
        }

        /**
         * @brief What the player has done, as opposed to what the data says.
         *
         * Created at boot and held for as long as the playthrough lasts.
         */
        game_session& session() noexcept { return _session; }

        /** @brief The active language's strings. */
        string_table& strings() noexcept { return *_strings; }

        /** @brief The language currently loaded. */
        language current_language() const noexcept { return _strings->language_id(); }

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
         * @brief The overlay waiting to go up, and the rule for when it may.
         *
         * Exposed rather than wrapped: an overlay's whole life - posted by a
         * bridge call, put on screen by the frame, taken by the state that
         * draws it - is the system's business, and forwarding each step through
         * the engine would only hide where it happens.
         */
        overlay_system& overlays() noexcept { return _overlays; }

        /** @brief The scene the runtime is currently presenting (for GET_SCENE). */
        u16 current_scene() const noexcept { return _current_scene; }
        void set_current_scene(u16 scene_id) noexcept { _current_scene = scene_id; }

        /**
         * @brief Runs one frame.
         */
        void process() noexcept;

        static result initialize();

        screen& main_screen() noexcept { return *_main_screen; }

        private: //////////////////////////////////////////////////////////////////////////////////

        /** @brief Reads this frame's keys and mouse. */
        void update_input() noexcept;

        /** @brief Gives the music player its slice of the frame. */
        void update_audio() noexcept;

        /**
         * @brief Swaps the string table when a script has asked for another.
         *
         * The new table is loaded before the old one is dropped, so a language
         * that will not load leaves the current one live and the game readable.
         */
        void apply_pending_language() noexcept;

        private:  //////////////////////////////////////////////////////////////////////////////////

        screen_ptr                       _main_screen{ nullptr };

        game_data_ptr                    _game_data{ nullptr };
        // Declared after the game data: it borrows that object's pak, so it has
        // to go down first.
        asset_cache_ptr                  _assets{ nullptr };
        mouse_pointer_ptr                _pointer{ nullptr };
        ace::font_ptr                    _ui_font{ nullptr };
        text_renderer_ptr                _text_renderer{ nullptr };
        mtl::unique_ptr<nine_patch>      _nine_patch{ nullptr };
        ace::bitmap_ptr                  _caption_ink{ nullptr };
        string_table_ptr                 _strings{ nullptr };
        noir_vm_ptr                      _vm{ nullptr };
        scene_runtime*                    _scene_runtime{ nullptr };

        // Current game session, overwriten on new/save/load game.
        game_session                     _session{};
        state_manager                    _states{};

        // Language requested by a script, awaiting a swap.
        pending_request<language>        _pending_language{};
        pending_request<modal_request>   _pending_modal{};
        pending_request<system_request>  _pending_system{};
        overlay_system                   _overlays{};

        u16                              _current_scene{ 0 };  ///< Scene the runtime is presenting.
        bool                             _language_changed{ false };
    };

    extern engine_ptr g_pEngine;

}  // namespace NEONengine

#endif  //__NEONENGINE_H__INCLUDED__
