#include "neonengine.h"

#include "types.h"

#include "core/logger.h"
#include "core/noir_bridge.h"
#include "core/scene_runtime.h"

#include <ace/managers/key.h>
#include <ace/managers/mouse.h>
#include <ace/managers/ptplayer.h>
#include <ace/managers/state.h>

#include <ace++/bitmap.h>
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

        // A project without a frame still runs; boxes of text then draw their
        // words with nothing behind them, which is legible but plain.
        mtl::unique_ptr<nine_patch> uiFrame{ nullptr };
        auto const& ui = gd->ui_state();
        if (ui.nine_patch_bitmap_id != data::NO_ID
            && ui.nine_patch_bitmap_id < gd->bitmaps().size())
        {
            auto const& ref = gd->bitmaps()[ui.nine_patch_bitmap_id];
            auto frameArt   = ace::bitmapCreateFromFd(gd->open_asset(ref.bitmap_hash), 0);
            if (frameArt)
            {
                uiFrame = mtl::unique_ptr<nine_patch>(new (mtl::MemF::Fast) nine_patch(frameArt,
                    ui.nine_patch_left,
                    ui.nine_patch_top,
                    ui.nine_patch_right,
                    ui.nine_patch_bottom));
            }
            else
            {
                NE_LOG("Could not load the UI nine-patch (bitmap %hu)", ui.nine_patch_bitmap_id);
            }
        }
        else
        {
            NE_LOG("The project ships no UI nine-patch");
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
        result->_pNinePatch    = mtl::move(uiFrame);
        result->_pStrings      = mtl::move(strings.value());

        // The playthrough's memory of what the player has read and taken, sized
        // from the data just loaded. Nothing reads or writes it yet.
        result->_session.create_from(*result->_pGameData);
        result->_session.set_invalid_item_id_reporter(report_invalid_inventory_item_id);

        auto const scripts   = result->_pGameData->scripts();
        auto const functions = result->_pGameData->function_offsets();
        result->_vm          = noir_vm_ptr(new (mtl::MemF::Fast) noir_vm(  //
            result->_session.get_flags(),
            result->_session.get_values(),
            scripts.data(),
            scripts.size(),
            functions.data(),
            to<u16>(functions.size()),
            &engine_bridge(),
            result.get()));

        NE_LOG("VM ready - %lu bytecode bytes, %hu funcs, %hu flags, %hu values",
            to<ULONG>(scripts.size()),
            to<UWORD>(functions.size()),
            to<UWORD>(result->_session.get_flags().size()),
            to<UWORD>(result->_session.get_values().size()));

        return result;
    }

    void engine::apply_pending_language() noexcept
    {
        _language_changed = false;

        if (!_pending_language.has()) return;

        auto lang_id = _pending_language.take();

        if (_pStrings && _pStrings->language_id() == lang_id) return;

        // Load first, swap second: on failure the current table stays live.
        mtl::system_use_guard guard;

        auto strings = string_table::create(lang_id);
        if (!strings)
        {
            // TODO: tell the player, once there is a message for it.
            NE_LOG("Could not load language '%s'. Error %d.",
                language_code(lang_id), to<int>(strings.error()));
            return;
        }

        if (!matches_build(*_pGameData, *strings.value()))
        {
            // TODO: tell the player, once there is a message for it.
            NE_LOG("Language '%s' is from another build; keeping the current one.",
                language_code(lang_id));
            return;
        }

        _pStrings = mtl::move(strings.value());
        _language_changed = true;

        // The language's own name proves the table really swapped. Records
        // always carry a terminator, so the view goes straight to "%s".
        NE_LOG("language is now '%s' (%s)",
            language_code(lang_id), _pStrings->language_name().data());
    }

    void engine::update_input() noexcept
    {
        keyProcess();
        mouseProcess();
    }

    void engine::update_audio() noexcept
    {
        ptplayerProcess();
    }

    void destroy_scene_runtime(scene_runtime* runtime)
    {
        delete runtime;
    }

    void engine::open_scene() noexcept
    {
        _scene = scene_runtime_ptr(new (mtl::MemF::Fast) scene_runtime(data()));
    }

    void engine::close_scene() noexcept
    {
        _scene = nullptr;
    }

    void engine::frame() noexcept
    {        update_input();
        update_audio();
        apply_pending_language();
        _overlays.present(g_gameStateManager);
        stateProcess(g_gameStateManager);
        screenProcess(g_mainScreen);
    }
}  // namespace NEONengine
