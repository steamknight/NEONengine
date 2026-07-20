#include "neonengine.h"

#include <ace++/palette.h>

#include <mtl/utility.h>

namespace NEONengine
{
    namespace
    {
        constexpr UWORD UI_PALETTE_COLOR_COUNT = 32;  ///< UI colors, palette indices 0..31.
    }

    tStateManager* g_gameStateManager = nullptr;
    Screen* g_mainScreen              = nullptr;
    engine_ptr g_pEngine              = nullptr;

    engine::result engine::initialize()
    {
        auto gameData = game_data::load();
        if (!gameData)
        {
            NE_LOG("Could not load game data. Error %d", mtl::to<int>(gameData.error()));
            return mtl::make_error<engine_ptr, error_code>(error_code::GAME_DATA_LOAD_FAILED);
        }
        auto& gd = gameData.value();

        mtl::system_use_guard guard;  // systemUse()/systemUnuse() around all disk access below.

        auto uiFontFd = gd->open_asset(gd->uist().font_hash);
        if (!uiFontFd)
        {
            NE_LOG("Could not open UI font asset %08lx", mtl::to<ULONG>(gd->uist().font_hash));
            return mtl::make_error<engine_ptr, error_code>(error_code::UI_FONT_LOAD_FAILED);
        }
        auto uiFont = ace::fontCreateFromFd(mtl::move(uiFontFd));
        if (!uiFont)
        {
            NE_LOG("Could not create UI font from asset");
            return mtl::make_error<engine_ptr, error_code>(error_code::UI_FONT_LOAD_FAILED);
        }

        auto uiPaletteFd = gd->open_asset(gd->uist().ui_palette_hash);
        if (!uiPaletteFd)
        {
            NE_LOG("Could not open UI palette asset %08lx", mtl::to<ULONG>(gd->uist().ui_palette_hash));
            return mtl::make_error<engine_ptr, error_code>(error_code::UI_PALETTE_LOAD_FAILED);
        }
        ace::paletteLoadFromFd(
            mtl::move(uiPaletteFd), screenGetPalette(g_mainScreen), UI_PALETTE_COLOR_COUNT);

        NE_LOG("game_data: UI font %hux%hu, %hu chars; palette[0]=%08lx palette[1]=%08lx",
            mtl::to<UWORD>(uiFont->uwWidth),
            mtl::to<UWORD>(uiFont->uwHeight),
            mtl::to<UWORD>(uiFont->ubChars),
            mtl::to<ULONG>(mtl::force_to<ULONG const*>(screenGetPalette(g_mainScreen))[0]),
            mtl::to<ULONG>(mtl::force_to<ULONG const*>(screenGetPalette(g_mainScreen))[1]));

        auto textRenderer = text_renderer::create(uiFont.get());
        if (!textRenderer)
        {
            NE_LOG("Could not create text renderer. Error %d.",
                   mtl::to<int>(textRenderer.error()));
            return mtl::make_error<engine_ptr, error_code>(
                error_code::FAILED_TO_CREATE_TEXT_RENDERER);
        }

        auto result            = engine_ptr(new (mtl::MemF::Fast) engine());
        result->_pGameData     = mtl::move(gameData.value());
        result->_pUiFont       = mtl::move(uiFont);
        result->_pTextRenderer = mtl::move(textRenderer.value());

        return result;
    }
}  // namespace NEONengine