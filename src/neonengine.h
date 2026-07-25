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

namespace NEONengine
{
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

        /** @brief Post a scene change requested by a script (GOTO_SCENE). */
        void request_scene(uint16_t scene_id) noexcept
        {
            _pending_scene     = scene_id;
            _has_pending_scene = true;
        }

        /** @brief True when a script has requested a scene change. */
        bool has_pending_scene() const noexcept { return _has_pending_scene; }

        /** @brief Consume the pending scene request, clearing it. */
        uint16_t take_pending_scene() noexcept
        {
            _has_pending_scene = false;
            return _pending_scene;
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

        /** @brief The scene the runtime is currently presenting (for GET_SCENE). */
        uint16_t current_scene() const noexcept { return _current_scene; }
        void set_current_scene(uint16_t scene_id) noexcept { _current_scene = scene_id; }

        static result initialize();

        private:  //////////////////////////////////////////////////////////////////////////////////
        game_data_ptr _pGameData{ nullptr };  ///< The one loaded .neon, owned for the process lifetime.
        ace::font_ptr _pUiFont{ nullptr };    ///< The UI font, loaded from the pak via UIST; the engine's font.
        text_renderer_ptr _pTextRenderer{ nullptr };
        string_table_ptr _pStrings{ nullptr };  ///< The active language's strings, swapped by set_language.

        noir_vm_ptr _vm{ nullptr };     ///< The one persistent script VM, over the buffers below.
        mtl::vector<uint8_t> _flags;    ///< Runtime game flags, seeded from FLGS (mutable, savable).
        mtl::vector<uint16_t> _values;  ///< Runtime game values, seeded from VALS (mutable, savable).
        uint16_t _pending_scene{ 0 };   ///< Scene requested by a script, awaiting a transition.
        bool _has_pending_scene{ false };
        language _pending_language{ language::EN };  ///< Language requested by a script, awaiting a swap.
        bool _has_pending_language{ false };
        uint16_t _current_scene{ 0 };   ///< Scene the runtime is presenting.
    };

    extern engine_ptr g_pEngine;

    extern tStateManager* g_gameStateManager;
    extern tState g_stateFontTest,  //
        g_stateDebugView,           //
        g_stateSplash,              //
        g_stateScene,               //
        g_stateDialogueTest;

#ifdef ACE_TEST_RUNNER
    namespace tests
    {
        extern tState g_stateTestRunner;
    }
#endif

    extern Screen* g_mainScreen;

#if !defined(SAFE_CB_CALL)
#define SAFE_CB_CALL(fn, ...) \
    if (fn) fn(__VA_ARGS__);
#endif  // !defined (SAFE_CB_CALL)

#ifdef ACE_DEBUG
#define NE_LOG(fmt, ...) logWrite("NEONengine: " fmt, ##__VA_ARGS__)
#else
#define NE_LOG(fmt, ...)
#endif

}  // namespace NEONengine

#endif  //__NEONENGINE_H__INCLUDED__