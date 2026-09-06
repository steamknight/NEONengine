#include "neonengine.h"

#include "types.h"

#include <ace/managers/game.h>
#include <ace/managers/key.h>
#include <ace/managers/mouse.h>
#include <ace/managers/ptplayer.h>

#include <ace++/bitmap.h>
#include <ace++/palette.h>

#include <mtl/utility.h>

#include "core/logger.h"
#include "core/music.h"
#include "core/noir_bridge.h"
#include "core/scene_bobs.h"

#include "states/scene.h"
#include "states/message.h"

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

        /** @brief Reports what is left of each kind of memory. */
        void log_free_memory()
        {
            auto chipSize = memGetFreeChipSize();
            auto fastSize = AvailMem(MEMF_FAST);
            auto anySize  = AvailMem(MEMF_ANY);

            NE_LOG("Chip: %ld.%3ld KB, Fast: %ld.%3ld KB, Any:  %ld.%3ld KB ",
                chipSize >> 10,
                chipSize & 0x3FF,
                fastSize >> 10,
                fastSize & 0x3FF,
                anySize >> 10,
                anySize & 0x3FF);
        }
    }  // namespace

    engine_ptr g_pEngine = nullptr;

    void initialize_ace()
    {
        log_init();

        keyCreate();
        mouseCreate(MOUSE_PORT_1);
        ptplayerCreate(systemIsPal());
    }

    void uninitialize_ace()
    {
        musicFree();
        ptplayerDestroy();
        mouseDestroy();
        keyDestroy();
    }

    engine::~engine() noexcept
    {
        clear_state_request();
        _states.clear();
        uninitialize_ace();
    }

    void engine::start()
    {
        _states.start<scene_state>(*this, _game_data->config().boot_scene_id);

    }

    engine::result engine::initialize()
    {
        mtl::system_use_guard guard;

        initialize_ace();

        auto result = mtl::make_unique<engine>();
        if (!result)
        {
            uninitialize_ace();
            return mtl::unexpected(error_code::ENGINE_ALLOCATION_FAILED);
        }

        auto main_screen = mtl::make_unique<screen>();
        main_screen->load();
        main_screen->bind_mouse();
        main_screen->set_cursor_band_exempt(true);

        auto gameData = game_data::load();
        if (!gameData)
        {
            NE_LOG("Could not load game data. Error %d", to<int>(gameData.error()));
            return mtl::unexpected(error_code::GAME_DATA_LOAD_FAILED);
        }
        auto& gd = gameData.value();

        // Nothing runs without a scene: the scene state builds its runtime from
        // the boot scene and every caller reads that runtime unchecked.
        if (gd->config().boot_scene_id >= gd->scenes().size())
        {
            NE_LOG("boot scene %hu does not exist (%lu scenes)",
                gd->config().boot_scene_id,
                to<ULONG>(gd->scenes().size()));
            return mtl::unexpected(error_code::BOOT_SCENE_INVALID);
        }

        auto assets = mtl::make_unique<asset_cache>(gd->pak());

        auto uiFontFd = assets->open(gd->ui_state().font_hash);
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

        auto uiPaletteFd = assets->open(gd->ui_state().ui_palette_hash);
        if (!uiPaletteFd)
        {
            NE_LOG("Could not open UI palette asset %08lx", to<ULONG>(gd->ui_state().ui_palette_hash));
            return mtl::unexpected(error_code::UI_PALETTE_LOAD_FAILED);
        }
        ace::paletteLoadFromFd(
            mtl::move(uiPaletteFd), main_screen->palette(), UI_PALETTE_COLOR_COUNT);

        NE_LOG("UI font %hux%hu, %hu chars; palette[0]=%08lx palette[1]=%08lx",
            to<UWORD>(uiFont->uwWidth),
            to<UWORD>(uiFont->uwHeight),
            to<UWORD>(uiFont->ubChars),
            to<ULONG>(force_to<ULONG const*>(main_screen->palette())[0]),
            to<ULONG>(force_to<ULONG const*>(main_screen->palette())[1]));

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
            auto frameArt   = ace::bitmapCreateFromFd(assets->open(ref.bitmap_hash), 0);
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

        auto pointer = mtl::make_unique<mouse_pointer>(*main_screen, *gd, *assets);

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

        result->_main_screen   = mtl::move(main_screen);
        result->_game_data     = mtl::move(gameData.value());
        result->_assets        = mtl::move(assets);
        result->_pointer       = mtl::move(pointer);
        result->_ui_font       = mtl::move(uiFont);
        result->_text_renderer = mtl::move(textRenderer.value());
        result->_nine_patch    = mtl::move(uiFrame);
        result->_caption_ink   = create_caption_ink(result->_ui_font.get(), result->_main_screen->back_buffer()->Depth);
        result->_strings      = mtl::move(strings.value());

        // The playthrough's memory of what the player has read and taken, sized
        // from the data just loaded. Nothing reads or writes it yet.
        result->_session.create_from(*result->_game_data);
        result->_session.set_invalid_item_id_reporter(report_invalid_inventory_item_id);

        auto const scripts   = result->_game_data->scripts();
        auto const functions = result->_game_data->function_offsets();
        result->_vm          = mtl::make_unique<noir_vm>(  //
            result->_session.get_flags(),
            result->_session.get_values(),
            scripts.data(),
            scripts.size(),
            functions.data(),
            to<u16>(functions.size()),
            &engine_bridge(),
            result.get());

        NE_LOG("VM ready - %lu bytecode bytes, %hu funcs, %hu flags, %hu values",
            to<ULONG>(scripts.size()),
            to<UWORD>(functions.size()),
            to<UWORD>(result->_session.get_flags().size()),
            to<UWORD>(result->_session.get_values().size()));

        // Log the what the engine startup left in the asset cache before we load any scenes.
        result->_assets->log(*result->_game_data);

        return result;
    }

    void engine::apply_pending_language() noexcept
    {
        _language_changed = false;

        if (!_pending_language.has()) return;

        auto lang_id = _pending_language.take();

        if (_strings && _strings->language_id() == lang_id) return;

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

        if (!matches_build(*_game_data, *strings.value()))
        {
            // TODO: tell the player, once there is a message for it.
            NE_LOG("Language '%s' is from another build; keeping the current one.",
                language_code(lang_id));
            return;
        }

        _strings = mtl::move(strings.value());
        _language_changed = true;

        // The language's own name proves the table really swapped. Records
        // always carry a terminator, so the view goes straight to "%s".
        NE_LOG("language is now '%s' (%s)",
            language_code(lang_id), _strings->language_name().data());
    }

    void engine::update_input() noexcept
    {
        keyProcess();
        mouseProcess();

        // Process the debug keys
        //
        // F2: Log the free memory in each kind of RAM
        // F3: Log the asset cache contents
        // F4: Clear the asset cache
        // F5: Toggle muting of ACE's log output
        // F9: Shut down the engine and exit the game

        if (keyUse(KEY_F2)) log_free_memory();

        if (keyUse(KEY_F3)) _assets->log(*_game_data);

        if (keyUse(KEY_F4))
        {
            g_pEngine->assets().clear();
            NE_LOG_ALWAYS("asset cache cleared on request");
        }

        if (keyUse(KEY_F5))
        {
            log_mute_ace(!log_ace_muted());
            NE_LOG_ALWAYS("ACE logging %s", log_ace_muted() ? "muted" : "on");
        }

        if (keyUse(KEY_F9))
        {
            NE_LOG_ALWAYS("F9 pressed: shutting down the engine and exiting the game");
            gameExit();
        }
    }

    void engine::update_audio() noexcept
    {
        ptplayerProcess();
    }

    void engine::process() noexcept
    {
        update_input();
        update_audio();
        apply_pending_language();

        auto const result = update_states(_states);
        if (result != state_update_result::ok)
        {
            if (result == state_update_result::error)
                NE_LOG_ALWAYS("Fatal state error: %s", _states.error());
            _states.clear();
            clear_state_request();

            gameExit();
            return;
        }

        _main_screen->process();
    }

    void engine::skip_modal_wait(noir_vm& vm) noexcept
    {
        NE_LOG("modal request skipped: empty text or a state is already pending");
        if (_state_wait.owns(vm)) return;
        _skipped_wait = modal_wait(&vm);
        _skipped_owner = _scene;
    }

    void engine::request_message(noir_vm& vm, message_request const& request)
    {
        if (request.text.is_empty())
        {
            if (request.modal_wait) skip_modal_wait(vm);
            return;
        }
        if (!_pending_state && request.text.length() >= MESSAGE_TEXT_CAPACITY)
            NE_LOG("message of %lu bytes cut to %lu",
                to<ULONG>(request.text.length()), to<ULONG>(MESSAGE_TEXT_CAPACITY - 1));
        request_state<message_state>(request.modal_wait ? &vm : nullptr, vm, request);
    }

    void engine::clear_state_request() noexcept
    {
        _pending_state.reset(nullptr);
        _state_owner = nullptr;
        _state_wait = {};
        _skipped_wait = {};
        _skipped_owner = nullptr;
    }

    void engine::discard_scene_requests(scene_state& scene) noexcept
    {
        if (_state_owner == &scene) clear_state_request();
        if (_system_owner == &scene)
        {
            _pending_system.clear();
            _system_owner = nullptr;
        }
        if (_fade_owner == &scene)
        {
            _fade_wait = {};
            _fade_owner = nullptr;
        }
        if (_pending_fade_owner == &scene)
        {
            _pending_modal.clear();
            _pending_fade_wait = {};
            _pending_fade_owner = nullptr;
        }
        if (_skipped_owner == &scene)
        {
            _skipped_wait = {};
            _skipped_owner = nullptr;
        }
    }

    void engine::start_pending_fade() noexcept
    {
        if (!_pending_modal.has()) return;
        auto const request = _pending_modal.take();
        _fade_wait = _pending_fade_wait;
        _fade_owner = _pending_fade_owner;
        _pending_fade_wait = {};
        _pending_fade_owner = nullptr;
        u16 const frames = request.arg1 ? request.arg1 : data().config().default_fade_frames;
        auto const duration = to<UBYTE>(frames > 255 ? 255 : frames);
        auto const done = []() { g_pEngine->finish_fade(); };
        if (request.kind == modal_kind::fade_out)
        {
            main_screen().fade_to_black(duration, 0, done);
            pointer().switch_to(pointer_role::WAIT);
            pointer().update();
        }
        else if (request.kind == modal_kind::fade_in)
            main_screen().fade_from_black(duration, 0, done);
    }

    void engine::apply_system_request(state_manager& states, bool applied)
    {
        if (!_pending_system.has() || (_scene && states.current() != _scene)) return;
        if (_pending_system.peek().kind == system_kind::load_scene)
        {
            if (applied || states.has_transition()) return;
            auto const request = _pending_system.take();
            clear_state_request();
            state_ptr replacement(new (mtl::MemF::Fast) scene_state(*this, request.arg1));
            states.replace(replacement);
            return;
        }

        auto const request = _pending_system.take();
        switch (request.kind)
        {
            case system_kind::save_game:
                NE_LOG("SAVE_GAME slot %hu: no save UI yet", request.arg1);
                vm().resume_execution();
                break;
            case system_kind::load_game:
                NE_LOG("LOAD_GAME slot %hu: no save format yet", request.arg1);
                break;
            case system_kind::new_game: NE_LOG("NEW_GAME: nothing to start yet"); break;
            case system_kind::quit_game: NE_LOG("QUIT_GAME: no way out yet"); break;
            default: break;
        }
        if (vm().terminated()) vm().reset_execution();
    }

    state_update_result engine::update_states(state_manager& states)
    {
        if (_request_failed) states.fail("Cannot allocate requested state");
        bool const applied = states.has_transition();
        auto result = states.apply_transition();
        if (result != state_update_result::ok) return result;
        if (_request_failed)
        {
            states.fail("Cannot allocate requested state");
            return state_update_result::error;
        }

        _skipped_wait.complete();
        apply_system_request(states, applied);
        if (_pending_system.has() && (!_scene || states.current() == _scene))
            return state_update_result::ok;
        if (states.has_transition())
        {
            result = states.apply_transition();
            if (result != state_update_result::ok) return result;
        }
        else if (_pending_state && !main_screen().display_frozen()
            && (!_scene || states.current() == _scene))
        {
            if (applied) return state_update_result::ok;
            if (_scene)
            {
                auto const prepared = _scene->prepare_suspend();
                if (!prepared)
                {
                    states.fail(prepared.error());
                    return state_update_result::error;
                }
            }
            _state_wait = {};
            _state_owner = nullptr;
            states.push(_pending_state);
            result = states.apply_transition();
            if (result != state_update_result::ok) return result;
        }

        if (_request_failed)
        {
            states.fail("Cannot allocate requested state");
            return state_update_result::error;
        }
        if (!_pending_system.has()) start_pending_fade();
        result = states.update_current();
        // The VM blocked before the bridge call; answer skipped waits only
        // after the state and bridge callbacks have returned.
        _skipped_wait.complete();
        if (!_pending_system.has()) start_pending_fade();
        if (_request_failed) states.fail("Cannot allocate requested state");
        return _request_failed ? state_update_result::error : result;
    }
}  // namespace NEONengine
