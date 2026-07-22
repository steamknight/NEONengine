#include "neonengine.h"

#include "core/noir_bridge.h"

#include <ace++/palette.h>

#include <mtl/utility.h>

namespace NEONengine
{
    namespace
    {
        constexpr UWORD UI_PALETTE_COLOR_COUNT = 32;  ///< UI colors, palette indices 0..31.
        constexpr uint16_t NOIR_STACK_SIZE     = 512;  ///< Script VM stack size, in bytes.
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

        auto uiFontFd = gd->open_asset(gd->ui_state().font_hash);
        if (!uiFontFd)
        {
            NE_LOG("Could not open UI font asset %08lx", mtl::to<ULONG>(gd->ui_state().font_hash));
            return mtl::make_error<engine_ptr, error_code>(error_code::UI_FONT_LOAD_FAILED);
        }
        auto uiFont = ace::fontCreateFromFd(mtl::move(uiFontFd));
        if (!uiFont)
        {
            NE_LOG("Could not create UI font from asset");
            return mtl::make_error<engine_ptr, error_code>(error_code::UI_FONT_LOAD_FAILED);
        }

        auto uiPaletteFd = gd->open_asset(gd->ui_state().ui_palette_hash);
        if (!uiPaletteFd)
        {
            NE_LOG("Could not open UI palette asset %08lx", mtl::to<ULONG>(gd->ui_state().ui_palette_hash));
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

        // Seed the runtime game-state buffers from the loaded initial state, then
        // build the one persistent script VM over them. FLAGS/VALUES are copied so
        // the on-disk image stays pristine and the runtime state is separately
        // savable; the VM borrows these engine-owned buffers for its lifetime.
        auto const initialFlags = result->_pGameData->initial_flags();
        result->_flags.resize(initialFlags.size());
        for (uint32_t i = 0; i < initialFlags.size(); ++i)
        {
            result->_flags.data()[i] = initialFlags[i];
        }

        auto const initialValues = result->_pGameData->initial_values();
        result->_values.resize(initialValues.size());
        for (uint32_t i = 0; i < initialValues.size(); ++i)
        {
            result->_values.data()[i] = mtl::to<uint16_t>(initialValues[i]);
        }

        auto const scripts   = result->_pGameData->scripts();
        auto const functions = result->_pGameData->function_offsets();
        result->_vm          = noir_vm_ptr(new (mtl::MemF::Fast) noir_vm(  //
            result->_flags.data(),
            mtl::to<uint16_t>(result->_flags.size()),
            result->_values.data(),
            mtl::to<uint16_t>(result->_values.size()),
            scripts.data(),
            scripts.size(),
            functions.data(),
            mtl::to<uint16_t>(functions.size()),
            &engine_bridge(),
            NOIR_STACK_SIZE));

        NE_LOG("game_data: VM ready - %lu bytecode bytes, %hu funcs, %hu flag bytes, %hu values",
            mtl::to<ULONG>(scripts.size()),
            mtl::to<UWORD>(functions.size()),
            mtl::to<UWORD>(result->_flags.size()),
            mtl::to<UWORD>(result->_values.size()));

        return result;
    }
}  // namespace NEONengine