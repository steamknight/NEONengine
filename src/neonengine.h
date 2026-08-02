#ifndef __NEONENGINE_H__INCLUDED__
#define __NEONENGINE_H__INCLUDED__

#include <ace/managers/state.h>
#include <ace/managers/system.h>

#include <ace++/font.h>

#include <mtl/expected.h>
#include <mtl/memory.h>
#include <mtl/vector.h>

#include "core/game_data.h"
#include "core/noir_vm.h"
#include "core/screen.h"
#include "core/string_table.h"
#include "core/text_render.h"
#include "types.h"

namespace NEONengine
{
    /**
     * @brief What a deferred modal bridge call asked the scene to put up.
     *
     * The kinds match the bridge functions that record one. @c none means the
     * request slot is empty.
     */
    enum class modal_kind : u8
    {
        none,
        say,          ///< a = string id, b = speaker id
        show_dialog,  ///< a = dialogue id
        fade_out,     ///< a = frames
        fade_in,      ///< a = frames
    };

    /** @brief One recorded modal request, with the opcode's D0 and D1. */
    struct modal_request
    {
        modal_kind kind{ modal_kind::none };
        u16 a{ 0 };
        u16 b{ 0 };
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
        u16 a{ 0 };
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

        game_data& data() noexcept { return *_pGameData; }
        noir_vm& vm() noexcept { return *_vm; }

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
        void request_system(system_kind kind, u16 a = 0) noexcept
        {
            _pending_system = system_request{ kind, a };
        }

        /** @brief True when something has asked for a system change. */
        bool has_pending_system() const noexcept
        {
            return _pending_system.kind != system_kind::none;
        }

        /** @brief What the pending system request asks for, or @c none. */
        system_kind pending_system_kind() const noexcept { return _pending_system.kind; }

        /** @brief Consume the pending system request, clearing it. */
        system_request take_pending_system() noexcept
        {
            system_request const taken = _pending_system;
            _pending_system            = system_request{};
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
            _pending_language     = lang;
            _has_pending_language = true;
        }

        /** @brief True when a script has requested a language change. */
        bool has_pending_language() const noexcept { return _has_pending_language; }

        /** @brief Consume the pending language request, clearing it. */
        language take_pending_language() noexcept
        {
            _has_pending_language = false;
            return _pending_language;
        }

        /**
         * @brief Post a modal requested by a script (SAY, FADE_OUT, and so on).
         *
         * The opcode drains the frame pool as it records this, so no session
         * runs again until the scene has put the modal up and taken it down.
         * One slot is enough: nothing else runs to record a second.
         */
        void request_modal(modal_kind kind, u16 a, u16 b = 0) noexcept
        {
            _pending_modal = modal_request{ kind, a, b };
        }

        /** @brief True when a script has requested a modal. */
        bool has_pending_modal() const noexcept { return _pending_modal.kind != modal_kind::none; }

        /** @brief Consume the pending modal request, clearing it. */
        modal_request take_pending_modal() noexcept
        {
            modal_request const taken = _pending_modal;
            _pending_modal            = modal_request{};
            return taken;
        }

        /** @brief The scene the runtime is currently presenting (for GET_SCENE). */
        u16 current_scene() const noexcept { return _current_scene; }
        void set_current_scene(u16 scene_id) noexcept { _current_scene = scene_id; }

        static result initialize();

        private:  //////////////////////////////////////////////////////////////////////////////////
        game_data_ptr _pGameData{
            nullptr
        };  ///< The one loaded .neon, owned for the process lifetime.
        ace::font_ptr _pUiFont{
            nullptr
        };  ///< The UI font, loaded from the pak via UIST; the engine's font.
        text_renderer_ptr _pTextRenderer{ nullptr };
        string_table_ptr _pStrings{
            nullptr
        };  ///< The active language's strings, swapped by set_language.

        noir_vm_ptr _vm{ nullptr };  ///< The one persistent script VM, over the buffers below.
        mtl::vector<u8> _flags;      ///< Runtime game flags, seeded from FLGS (mutable, savable).
        mtl::vector<u16> _values;    ///< Runtime game values, seeded from VALS (mutable, savable).
        language _pending_language{
            language::EN
        };  ///< Language requested by a script, awaiting a swap.
        bool _has_pending_language{ false };
        modal_request _pending_modal{};  ///< Modal requested by a script, awaiting the next frame.
        system_request _pending_system{};  ///< Scene change, save or new game, awaiting the next frame.
        u16 _current_scene{ 0 };           ///< Scene the runtime is presenting.
    };

    extern engine_ptr g_pEngine;

    extern tStateManager* g_gameStateManager;
    extern tState g_stateDebugView,  //
        g_stateScene,                //
        g_stateDialogueTest;

#ifdef ACE_TEST_RUNNER
    namespace tests
    {
        extern tState g_stateTestRunner;
    }
#endif

    extern Screen* g_mainScreen;

}  // namespace NEONengine

#endif  //__NEONENGINE_H__INCLUDED__
