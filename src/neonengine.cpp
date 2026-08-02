#include "neonengine.h"

#include "types.h"

#include "core/logger.h"
#include "core/noir_bridge.h"

#include <ace++/palette.h>

#include <mtl/utility.h>

NE_LOG_DOMAIN(Core);

namespace NEONengine
{
    namespace
    {
        constexpr UWORD UI_PALETTE_COLOR_COUNT = 32;    ///< UI colors, palette indices 0..31.

        /**
         * @brief True when @p strings was produced by the same build as the pak.
         *
         * Every file of one build shares a {period, number} tuple. A table from
         * another build may still parse, but its catalog order would map string
         * ids to unrelated text, so it must be refused rather than displayed.
         */
        bool matches_build(game_data const& gd, string_table const& strings) noexcept
        {
            auto const& hd = gd.header();
            return strings.build_period() == hd.build_period
                && strings.build_number() == hd.build_number;
        }
    }

    tStateManager* g_gameStateManager = nullptr;
    Screen* g_mainScreen              = nullptr;
    engine_ptr g_pEngine              = nullptr;

    engine::result engine::initialize()
    {
        auto gameData = game_data::load();
        if (!gameData)
        {
            NE_LOG("Could not load game data. Error %d", to<int>(gameData.error()));
            return mtl::unexpected(error_code::GAME_DATA_LOAD_FAILED);
        }
        auto& gd = gameData.value();

        mtl::system_use_guard guard;  // systemUse()/systemUnuse() around all disk access below.

        auto uiFontFd = gd->open_asset(gd->ui_state().font_hash);
        if (!uiFontFd)
        {
            NE_LOG("Could not open UI font asset %08lx", to<ULONG>(gd->ui_state().font_hash));
            return mtl::unexpected(error_code::UI_FONT_LOAD_FAILED);
        }
        auto uiFont = ace::fontCreateFromFd(mtl::move(uiFontFd));
        if (!uiFont)
        {
            NE_LOG("Could not create UI font from asset");
            return mtl::unexpected(error_code::UI_FONT_LOAD_FAILED);
        }

        auto uiPaletteFd = gd->open_asset(gd->ui_state().ui_palette_hash);
        if (!uiPaletteFd)
        {
            NE_LOG("Could not open UI palette asset %08lx", to<ULONG>(gd->ui_state().ui_palette_hash));
            return mtl::unexpected(error_code::UI_PALETTE_LOAD_FAILED);
        }
        ace::paletteLoadFromFd(
            mtl::move(uiPaletteFd), screenGetPalette(g_mainScreen), UI_PALETTE_COLOR_COUNT);

        NE_LOG("UI font %hux%hu, %hu chars; palette[0]=%08lx palette[1]=%08lx",
            to<UWORD>(uiFont->uwWidth),
            to<UWORD>(uiFont->uwHeight),
            to<UWORD>(uiFont->ubChars),
            to<ULONG>(force_to<ULONG const*>(screenGetPalette(g_mainScreen))[0]),
            to<ULONG>(force_to<ULONG const*>(screenGetPalette(g_mainScreen))[1]));

        auto textRenderer = text_renderer::create(uiFont.get());
        if (!textRenderer)
        {
            NE_LOG("Could not create text renderer. Error %d.",
                   to<int>(textRenderer.error()));
            return mtl::unexpected(
                error_code::FAILED_TO_CREATE_TEXT_RENDERER);
        }

        // English is the one language the format guarantees is present, so it is
        // the boot language; set_language() swaps to another once one is chosen.
        auto strings = string_table::create(language::EN);
        if (!strings)
        {
            NE_LOG("Could not load the English string table. Error %d.",
                   to<int>(strings.error()));
            return mtl::unexpected(error_code::STRING_TABLE_LOAD_FAILED);
        }

        if (!matches_build(*gd, *strings.value()))
        {
            NE_LOG("The English string table is from another build than the pak.");
            return mtl::unexpected(error_code::STRING_TABLE_LOAD_FAILED);
        }

        auto result            = engine_ptr(new (mtl::MemF::Fast) engine());
        result->_pGameData     = mtl::move(gameData.value());
        result->_pUiFont       = mtl::move(uiFont);
        result->_pTextRenderer = mtl::move(textRenderer.value());
        result->_pStrings      = mtl::move(strings.value());

        // Seed the runtime game-state buffers from the loaded initial state, then
        // build the one persistent script VM over them. FLAGS/VALUES are copied so
        // the on-disk image stays pristine and the runtime state is separately
        // savable; the VM borrows these engine-owned buffers for its lifetime.
        auto const initialFlags = result->_pGameData->initial_flags();
        result->_flags.resize(initialFlags.size());
        for (u32 i = 0; i < initialFlags.size(); ++i)
        {
            result->_flags.data()[i] = initialFlags[i];
        }

        auto const initialValues = result->_pGameData->initial_values();
        result->_values.resize(initialValues.size());
        for (u32 i = 0; i < initialValues.size(); ++i)
        {
            result->_values.data()[i] = to<u16>(initialValues[i]);
        }

        auto const scripts   = result->_pGameData->scripts();
        auto const functions = result->_pGameData->function_offsets();
        result->_vm          = noir_vm_ptr(new (mtl::MemF::Fast) noir_vm(  //
            result->_flags.data(),
            to<u16>(result->_flags.size()),
            result->_values.data(),
            to<u16>(result->_values.size()),
            scripts.data(),
            scripts.size(),
            functions.data(),
            to<u16>(functions.size()),
            &engine_bridge()));

        NE_LOG("VM ready - %lu bytecode bytes, %hu funcs, %hu flag bytes, %hu values",
            to<ULONG>(scripts.size()),
            to<UWORD>(functions.size()),
            to<UWORD>(result->_flags.size()),
            to<UWORD>(result->_values.size()));

        return result;
    }

    bool engine::set_language(language lang)
    {
        if (_pStrings && _pStrings->language_id() == lang) return true;

        // Load first, swap second: on failure the current table stays live.
        mtl::system_use_guard guard;  // systemUse()/systemUnuse() around the disk access.

        auto strings = string_table::create(lang);
        if (!strings)
        {
            NE_LOG("Could not load language '%s'. Error %d.",
                language_code(lang), to<int>(strings.error()));
            return false;
        }

        if (!matches_build(*_pGameData, *strings.value()))
        {
            NE_LOG("Language '%s' is from another build; keeping the current one.",
                language_code(lang));
            return false;
        }

        _pStrings = mtl::move(strings.value());
        return true;
    }
}  // namespace NEONengine
