#include "core/noir_bridge.h"

#include "neonengine.h"
#include "core/logger.h"
#include "core/mouse_pointer.h"
#include "core/scene_hotspots.h"
#include "core/scene_timeline.h"
#include "core/screen.h"
#include "core/string_table.h"

#include <mtl/utility.h>

NE_LOG_DOMAIN(Script);

namespace NEONengine
{
    namespace
    {
        // Fires when a bridge-initiated fade finishes: the VM's blocking counter
        // drops and the paused script resumes on the next update().
        void nb_fade_done()
        {
            if (g_pEngine) g_pEngine->vm().decrement_blocking();
        }

        // ── Screen ───────────────────────────────────────────────────────────
        // A bare FADE_IN/FADE_OUT (no MOV D0, #frames) arrives as 0 frames; fall
        // back to the project's configured default so scripts can omit the count.
        uint16_t nb_resolve_fade_frames(uint16_t frames)
        {
            if (frames) return frames;
            return g_pEngine ? g_pEngine->data().config().default_fade_frames : uint16_t(0);
        }

        void nb_fade_out(uint16_t frames)
        {
            screenFadeToBlack(g_mainScreen, mtl::to<UBYTE>(nb_resolve_fade_frames(frames)), 0, nb_fade_done);
        }
        void nb_fade_in(uint16_t frames)
        {
            screenFadeFromBlack(g_mainScreen, mtl::to<UBYTE>(nb_resolve_fade_frames(frames)), 0, nb_fade_done);
        }
        void nb_screen_clear(uint16_t) {}

        // ── Scenes ───────────────────────────────────────────────────────────
        void nb_goto_scene(uint16_t scene_id)
        {
            if (g_pEngine) g_pEngine->request_scene(scene_id);
        }
        uint16_t nb_get_scene() { return g_pEngine ? g_pEngine->current_scene() : uint16_t(0); }
        void nb_hotspot_on(uint16_t id) { scene_hotspots_set_enabled(id, true); }
        void nb_hotspot_off(uint16_t id) { scene_hotspots_set_enabled(id, false); }

        void nb_set_pointer(uint16_t type)
        {
            if (type < mtl::to<uint16_t>(mouse_pointer::COUNT))
            {
                mousePointerSwitch(mtl::to<mouse_pointer>(type));
            }
        }

        // ── Inventory ────────────────────────────────────────────────────────
        void nb_inv_add(uint16_t) {}
        void nb_inv_remove(uint16_t) {}
        bool nb_inv_has(uint16_t) { return false; }
        uint16_t nb_inv_count() { return 0; }
        void nb_inv_clear() {}

        // ── Dialogue / Audio ─────────────────────────────────────────────────
        void nb_say(uint16_t, uint16_t) {}
        void nb_show_dialog(uint16_t) {}
        void nb_show_text(uint16_t, uint16_t, uint16_t) {}
        void nb_clear_text() {}
        void nb_play_music(uint16_t) {}
        void nb_stop_music() {}
        void nb_play_sfx(uint16_t) {}

        // ── Timeline ─────────────────────────────────────────────────────────
        // Track names are resolved against the current scene, so a script that
        // names a track another scene owns simply does nothing here.
        void nb_anim_play(uint16_t name_id) { scene_timeline_play(name_id); }
        void nb_anim_stop(uint16_t name_id) { scene_timeline_stop(name_id); }
        void nb_anim_wait(uint16_t name_id) { scene_timeline_wait(name_id); }

        // ── System ───────────────────────────────────────────────────────────
        // Records the request; the state loop applies it at a safe point. Doing
        // it here would read from disk inside a VM step.
        void nb_set_lang(uint16_t lang)
        {
            if (lang >= mtl::to<uint16_t>(language::COUNT))
            {
                NE_LOG("SET_LANG %hu is not a known language id", lang);
                return;
            }

            NE_LOG("SET_LANG %hu ('%s') requested", lang,
                language_code(mtl::to<language>(lang)));
            if (g_pEngine) g_pEngine->request_language(mtl::to<language>(lang));
        }
        void nb_save_game(uint16_t) {}
        void nb_load_game(uint16_t) {}
        void nb_new_game() {}
        void nb_quit_game() {}

#ifdef GAME_DEBUG
        void nb_debug_log(uint16_t) {}
        void nb_debug_print(char const*, uint8_t) {}
#endif

        noir_bridge const g_engine_bridge = {
            .inv_add    = nb_inv_add,
            .inv_remove = nb_inv_remove,
            .inv_has    = nb_inv_has,
            .inv_count  = nb_inv_count,
            .inv_clear  = nb_inv_clear,

            .goto_scene  = nb_goto_scene,
            .get_scene   = nb_get_scene,
            .hotspot_on  = nb_hotspot_on,
            .hotspot_off = nb_hotspot_off,
            .set_pointer = nb_set_pointer,

            .say         = nb_say,
            .show_dialog = nb_show_dialog,
            .show_text   = nb_show_text,
            .clear_text  = nb_clear_text,

            .play_music = nb_play_music,
            .stop_music = nb_stop_music,
            .play_sfx   = nb_play_sfx,

            .fade_out     = nb_fade_out,
            .fade_in      = nb_fade_in,
            .screen_clear = nb_screen_clear,

            .anim_play = nb_anim_play,
            .anim_stop = nb_anim_stop,
            .anim_wait = nb_anim_wait,

            .set_lang  = nb_set_lang,
            .save_game = nb_save_game,
            .load_game = nb_load_game,
            .new_game  = nb_new_game,
            .quit_game = nb_quit_game,

#ifdef GAME_DEBUG
            .debug_log   = nb_debug_log,
            .debug_print = nb_debug_print,
#endif
        };
    }  // namespace

    noir_bridge const& engine_bridge() noexcept { return g_engine_bridge; }
}  // namespace NEONengine
