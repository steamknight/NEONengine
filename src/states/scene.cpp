#include "scene.h"
#include "debug_view.h"

#include "neonengine.h"

#include <ace/managers/bob.h>
#include <ace/managers/copper.h>
#include <ace/managers/game.h>
#include <ace/managers/key.h>
#include <ace/managers/log.h>
#include <ace/managers/system.h>

#include <mtl/utility.h>

#include "core/logger.h"
#include "core/music.h"
#include "core/scene_backdrop.h"

NE_LOG_DOMAIN(Scene);

namespace NEONengine
{
    namespace
    {
        constexpr u32 MUSIC_KEEP    = 0xFFFFFFFF;
        constexpr u32 MUSIC_SILENCE = 0xFFFFFFFE;

        void scene_fade_done()
        {
            g_pEngine->vm().resume_execution();
        }

        void show_wait_pointer(engine& engine)
        {
            engine.pointer().switch_to(pointer_role::WAIT);
            engine.pointer().update();
            // Publish the sprite before disk access blocks the frame.
            copProcessBlocks();
        }

        LONG free_fast_size()
        {
            return to<LONG>(memGetFreeSize()) - to<LONG>(memGetFreeChipSize());
        }

        void log_scene_memory(u16 scene_id, LONG chip_before, LONG fast_before)
        {
            LONG const chip_after = to<LONG>(memGetFreeChipSize());
            LONG const fast_after = free_fast_size();

            NE_LOG("scene %hu memory: chip %ld KB free (used %ld KB), fast %ld KB free (used %ld KB)",
                scene_id, chip_after / 1024, (chip_before - chip_after) / 1024,
                fast_after / 1024, (fast_before - fast_after) / 1024);
        }
    }  // namespace

    scene_state::scene_state(engine& engine, u16 scene_id) noexcept
        : _engine(engine), _scene_id(scene_id),
          _bobs(engine.data(), engine.assets()),
          _sound_effects(engine.data(), engine.assets()),
          _hotspots(engine.data(), _bobs, _sound_effects, engine.pointer()),
          _timeline(engine.data(), _bobs, _sound_effects)
    {
    }

    mtl::expected<void, char const*> scene_state::load_scene()
    {
        auto& data = _engine.data();
        auto const scenes = data.scenes();
        if (_scene_id >= scenes.size()) return mtl::unexpected("Invalid scene id");

        auto const& scene = scenes[_scene_id];
        show_wait_pointer(_engine);
        _engine.assets().begin_load();

        LONG const chip_before = to<LONG>(memGetFreeChipSize());
        LONG const fast_before = free_fast_size();
        bool const silence = scene.music_hash == MUSIC_SILENCE;
        bool music_started = false;
        {
            mtl::system_use_guard guard;
            auto const backdrop = scene_backdrop_draw(_engine, scene);
            if (!backdrop) return backdrop;
            _engine.pointer().set_visible((scene.flags & data::SCENE_FLAG_SHOW_UI) != 0);

            _bobs.load(scene);
            _sound_effects.load(scene);
            _hotspots.load(scene);
            _timeline.load(scene);

            if (!silence && scene.music_hash != MUSIC_KEEP)
            {
                auto mod = data.open_asset_uncached(scene.music_hash);
                if (mod)
                {
                    musicLoad(mtl::move(mod));
                    music_started = true;
                }
            }
        }

        log_scene_memory(_scene_id, chip_before, fast_before);
        if (silence) musicFree();
        if (music_started) musicPlayCurrent(1);
        _rendered_language = _engine.current_language();
        return {};
    }

    void scene_state::start_entry_script()
    {
        auto const& scene = _engine.data().scenes()[_scene_id];
        _engine.vm().reset_execution();
        if (scene.on_enter_function_id != data::NO_ID
            && _engine.vm().execute(scene.on_enter_function_id))
        {
            return;
        }

        u16 const frames = _engine.data().config().default_fade_frames;
        _engine.main_screen().fade_from_black(
            to<UBYTE>(frames > 255 ? 255 : frames), 0, nullptr);
    }

    mtl::expected<void, char const*> scene_state::pushed()
    {
        logBlockBegin("State: Scene");
        _log_open = true;
        auto const result = load_scene();
        if (!result) return result;

        _engine.set_current_scene(_scene_id);
        _engine.bind_scene(*this);
        start_entry_script();
        return {};
    }

    void scene_state::start_pending_modal()
    {
        auto const request = _engine.take_pending_modal();
        u16 const frames = request.arg1 ? request.arg1 : _engine.data().config().default_fade_frames;
        auto const duration = to<UBYTE>(frames > 255 ? 255 : frames);
        switch (request.kind)
        {
            case modal_kind::fade_out:
                _engine.main_screen().fade_to_black(duration, 0, scene_fade_done);
                _engine.pointer().switch_to(pointer_role::WAIT);
                return;
            case modal_kind::fade_in:
                _engine.main_screen().fade_from_black(duration, 0, scene_fade_done);
                return;
            case modal_kind::show_dialog:
                _engine.overlays().post_dialogue(request.arg1);
                return;
            case modal_kind::none: return;
        }
    }

    bool scene_state::apply_system_request(state_control& control)
    {
        if (!_engine.has_pending_system()) return false;
        auto const request = _engine.take_pending_system();
        switch (request.kind)
        {
            case system_kind::load_scene:
                show_wait_pointer(_engine);
                control.replace<scene_state>(_engine, request.arg1);
                return true;
            case system_kind::save_game:
                NE_LOG("SAVE_GAME slot %hu: no save UI yet", request.arg1);
                _engine.vm().resume_execution();
                break;
            case system_kind::load_game:
                NE_LOG("LOAD_GAME slot %hu: no save format yet", request.arg1);
                break;
            case system_kind::new_game:
                NE_LOG("NEW_GAME: nothing to start yet");
                break;
            case system_kind::quit_game:
                NE_LOG("QUIT_GAME: no way out yet");
                break;
            case system_kind::none: break;
        }
        if (_engine.vm().terminated()) _engine.vm().reset_execution();
        return false;
    }

    bool scene_state::rebuild_for_language()
    {
        show_wait_pointer(_engine);
        mtl::system_use_guard guard;
        auto const& scene = _engine.data().scenes()[_scene_id];
        _bobs.reload_captions(scene);
        if (scene.text_region_range.count != 0)
        {
            _timeline.restore_palette();
            auto const result = scene_backdrop_draw(_engine, scene);
            if (!result)
            {
                NE_LOG_ALWAYS("scene %hu language reload failed: %s", _scene_id, result.error());
                gameExit();
                return false;
            }
            _engine.pointer().set_visible((scene.flags & data::SCENE_FLAG_SHOW_UI) != 0);
            // Saved bob backgrounds still contain the old text.
            bobDiscardUndraw();
        }
        _rendered_language = _engine.current_language();
        return true;
    }

    void scene_state::update(state_control& control)
    {
        bool const debug_requested = keyUse(KEY_F1);
        if (apply_system_request(control)) return;
        if (_engine.has_pending_modal()) start_pending_modal();
        auto& screen = _engine.main_screen();
        // Let a faded-out script request its next scene before decoding this one again.
        if (_rendered_language != _engine.current_language()
            && !screen.is_fading_out()
            && (screen.palette_ratio() != 0 || screen.fade_owns_palette()))
        {
            if (!rebuild_for_language()) return;
        }
        if (_engine.overlays().present(control, _engine.vm(), _engine))
        {
            _bobs.draw();
            return;
        }

        if (_engine.main_screen().is_fading_out())
            _engine.pointer().switch_to(pointer_role::WAIT);

        bool const language_ready = _rendered_language == _engine.current_language();
        if (!_engine.vm().blocked() && language_ready)
        {
            _timeline.update();
            if (apply_system_request(control)) return;
        }
        if (!_engine.vm().blocked() && language_ready)
        {
            _hotspots.update();
            if (apply_system_request(control)) return;
        }

        // One VM update per frame, including entry. Start its fade before the first display.
        if (!_engine.vm().blocked()) _engine.vm().update();
        if (apply_system_request(control)) return;
        if (_engine.has_pending_modal()) start_pending_modal();
        if (_rendered_language != _engine.current_language() && !screen.is_fading_out())
        {
            if (!rebuild_for_language()) return;
        }

        _engine.pointer().update();
        _bobs.update();
        _bobs.draw();
        if (_engine.overlays().present(control, _engine.vm(), _engine)) return;
        if (debug_requested)
        {
            control.push<debug_view_state>(_engine);
        }
    }

    scene_state::~scene_state()
    {
        if (_engine.unbind_scene(*this))
        {
            _engine.take_pending_modal();
            _engine.take_pending_system();
            _engine.overlays().clear();
            _engine.vm().reset_execution();
        }
        if (_log_open) logBlockEnd("State: Scene");
    }

    void scene_state::suspended()
    {
        // Overlays restore from the other buffer, so draw the same pose there.
        _bobs.draw();
    }

    void scene_state::resumed()
    {
        _hotspots.resync_button();
    }
}  // namespace NEONengine
