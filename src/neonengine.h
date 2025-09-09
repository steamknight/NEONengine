#ifndef __NEONENGINE_H__INCLUDED__
#define __NEONENGINE_H__INCLUDED__

#include <ace/managers/state.h>
#include <ace/managers/system.h>

#include <ace++/font.h>

#include <mtl/expected.h>
#include <mtl/memory.h>

#include "core/game_data.h"
#include "core/screen.h"
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
            DEFAULT_FONT_NOT_FOUND,
            FAILED_TO_CREATE_DEFAULT_TEXT_RENDERER,
        };

        using result = mtl::expected<engine_ptr, error_code>;

        public:  //////////////////////////////////////////////////////////////////////////////////
        tFont* default_font() noexcept { return _pDefaultFont.get(); }
        text_renderer* default_text_renderer() noexcept { return _pDefaultTextRenderer.get(); }

        static result initialize(char const* szDefaultFontPath);

        private:  //////////////////////////////////////////////////////////////////////////////////
        ace::font_ptr _pDefaultFont{ nullptr };
        text_renderer_ptr _pDefaultTextRenderer{ nullptr };
    };

    extern engine_ptr g_pEngine;

    extern tStateManager* g_gameStateManager;
    extern tState g_stateFontTest,  //
        g_stateDebugView,           //
        g_stateSplash,              //
        g_stateLangSelect,          //
        g_stateDialogueTest,        //
        g_stateLangTest;

#ifdef ACE_TEST_RUNNER
    extern tState g_stateTestRunner;
#endif

    extern Screen* g_mainScreen;

    extern GameData* g_gameData;

#if !defined(SAFE_CB_CALL)
#define SAFE_CB_CALL(fn, ...) \
    if (fn) fn(__VA_ARGS__);
#endif  // !defined (SAFE_CB_CALL)

#ifdef ACE_DEBUG
#define NE_LOG(fmt, ...) logWrite("NEONengine: " fmt, ##__VA_ARGS__)
#else
#define NE_LOG(domain, fmt, ...)
#endif

}  // namespace NEONengine

#endif  //__NEONENGINE_H__INCLUDED__