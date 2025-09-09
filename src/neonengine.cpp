#include "neonengine.h"

namespace NEONengine
{
    tStateManager* g_gameStateManager = nullptr;
    Screen* g_mainScreen              = nullptr;
    GameData* g_gameData              = nullptr;
    engine_ptr g_pEngine              = nullptr;

    engine::result engine::initialize(char const* szDefaultFontPath)
    {
        auto font = ace::fontCreateFromPath(szDefaultFontPath);
        if (!font)
        {
            NE_LOG("Could not load default font '%s'", szDefaultFontPath);
            return mtl::make_error<engine_ptr, error_code>(error_code::DEFAULT_FONT_NOT_FOUND);
        }

        auto textRenderer = text_renderer::create(font.get());
        if (!textRenderer)
        {
            NE_LOG("Could not create default text renderer. Error %d.",
                   mtl::to<int>(textRenderer.error()));
            return mtl::make_error<engine_ptr, error_code>(
                error_code::FAILED_TO_CREATE_DEFAULT_TEXT_RENDERER);
        }

        auto result                   = engine_ptr(new (mtl::MemF::Fast) engine());
        result->_pDefaultFont         = mtl::move(font);
        result->_pDefaultTextRenderer = mtl::move(textRenderer.value());

        return result;
    }
}  // namespace NEONengine