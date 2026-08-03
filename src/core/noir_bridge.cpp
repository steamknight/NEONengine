#include "core/noir_bridge.h"

#include "types.h"

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
        // ── Screen ───────────────────────────────────────────────────────────
        // Fades are deferred modals: the scene starts them at the top of the
        // next frame and holds the VM until they finish.
        void nb_fade_out(u16 frames)
        {
            if (g_pEngine) g_pEngine->request_modal(modal_kind::fade_out, frames);
        }
        void nb_fade_in(u16 frames)
        {
            if (g_pEngine) g_pEngine->request_modal(modal_kind::fade_in, frames);
        }
        void nb_screen_clear(u16) {}

        // ── Scenes ───────────────────────────────────────────────────────────
        void nb_goto_scene(u16 scene_id)
        {
            if (g_pEngine) g_pEngine->request_system(system_kind::load_scene, scene_id);
        }
        u16 nb_get_scene() { return g_pEngine ? g_pEngine->current_scene() : u16(0); }
        void nb_hotspot_on(u16 id) { scene_hotspots_set_enabled(id, true); }
        void nb_hotspot_off(u16 id) { scene_hotspots_set_enabled(id, false); }

        void nb_set_pointer(u16 type)
        {
            if (type < to<u16>(mouse_pointer::COUNT))
            {
                mousePointerSwitch(to<mouse_pointer>(type));
            }
        }

        // ── Inventory ────────────────────────────────────────────────────────
        void nb_inv_add(u16) {}
        void nb_inv_remove(u16) {}
        bool nb_inv_has(u16) { return false; }
        u16 nb_inv_count() { return 0; }
        void nb_inv_clear() {}

        // ── Dialogue / Audio ─────────────────────────────────────────────────
        void nb_say(u16 string_id, u16)
        {
            if (!g_pEngine) return;

            g_pEngine->show_message(message_request{
                .text = g_pEngine->strings().get_string(string_id),
            });
        }
        void nb_show_dialog(u16 dialogue_id)
        {
            if (g_pEngine) g_pEngine->request_modal(modal_kind::show_dialog, dialogue_id);
        }
        void nb_show_text(u16, u16, u16) {}
        void nb_clear_text() {}
        void nb_play_music(u16) {}
        void nb_stop_music() {}
        void nb_play_sfx(u16) {}

        // ── Timeline ─────────────────────────────────────────────────────────
        // A track name means a track in the current scene, so naming one that
        // another scene owns does nothing here.
        void nb_anim_play(u16 name_id) { scene_timeline_play(name_id); }
        void nb_anim_stop(u16 name_id) { scene_timeline_stop(name_id); }

        // ── System ───────────────────────────────────────────────────────────
        // Records the request; the state loop applies it at a safe point. Doing
        // it here would read from disk inside a VM step.
        void nb_set_lang(u16 lang)
        {
            if (lang >= to<u16>(language::COUNT))
            {
                NE_LOG("SET_LANG %hu is not a known language id", lang);
                return;
            }

            NE_LOG("SET_LANG %hu ('%s') requested", lang,
                language_code(to<language>(lang)));
            if (g_pEngine) g_pEngine->request_language(to<language>(lang));
        }
        void nb_save_game(u16 slot)
        {
            if (g_pEngine) g_pEngine->request_system(system_kind::save_game, slot);
        }

        // Terminal: the VM stops the moment one of these runs and only starts
        // again once the scene has torn it down.
        void nb_load_game(u16 slot)
        {
            if (g_pEngine) g_pEngine->request_system(system_kind::load_game, slot);
        }
        void nb_new_game()
        {
            if (g_pEngine) g_pEngine->request_system(system_kind::new_game);
        }
        void nb_quit_game()
        {
            if (g_pEngine) g_pEngine->request_system(system_kind::quit_game);
        }

        // ── Diagnostics ──────────────────────────────────────────────────────
        /// The one message text the engine does not already hold. @see nb_report_error
        char s_error_text[NOIR_ERROR_MESSAGE_SIZE];

        /**
         * @brief Puts what killed a script in front of the player.
         *
         * The VM builds the line in a buffer of its own that it reuses, so
         * unlike every other message this text has to be copied to outlive the
         * call. Whether the message is wanted is settled before the copy: a
         * script dying does not end the walk it was part of, so a second one
         * can die before the first message has been drawn, and that second
         * death must not rewrite the text the waiting request points at.
         */
        void nb_report_error(char const* message)
        {
            if (!g_pEngine || !message || g_pEngine->has_pending_message()) return;

            size_t length = 0;
            while (length + 1 < sizeof(s_error_text) && message[length] != '\0')
            {
                s_error_text[length] = message[length];
                ++length;
            }
            s_error_text[length] = '\0';

            g_pEngine->show_message(message_request{
                .text          = bstr_view(s_error_text, length),
                .caption       = "Script Error",
                .color         = MESSAGE_ERROR_COLOR,
                .caption_color = MESSAGE_ERROR_COLOR,
                .align         = message_align::middle,
            });
        }

#ifdef GAME_DEBUG
        void nb_debug_log(u16) {}
        void nb_debug_print(char const*, u8) {}
#endif

        noir_bridge const g_engine_bridge = {
            .inv_add    = {nb_inv_add},
            .inv_remove = {nb_inv_remove},
            .inv_has    = {nb_inv_has},
            .inv_count  = {nb_inv_count},
            .inv_clear  = {nb_inv_clear},

            .goto_scene  = {nb_goto_scene},
            .get_scene   = {nb_get_scene},
            .hotspot_on  = {nb_hotspot_on},
            .hotspot_off = {nb_hotspot_off},
            .set_pointer = {nb_set_pointer},

            .say         = {nb_say},
            .show_dialog = {nb_show_dialog},
            .show_text   = {nb_show_text},
            .clear_text  = {nb_clear_text},

            .play_music = {nb_play_music},
            .stop_music = {nb_stop_music},
            .play_sfx   = {nb_play_sfx},

            .fade_out     = {nb_fade_out},
            .fade_in      = {nb_fade_in},
            .screen_clear = {nb_screen_clear},

            .anim_play = {nb_anim_play},
            .anim_stop = {nb_anim_stop},

            .set_lang  = {nb_set_lang},
            .save_game = {nb_save_game},
            .load_game = {nb_load_game},
            .new_game  = {nb_new_game},
            .quit_game = {nb_quit_game},

            .report_error = nb_report_error,

#ifdef GAME_DEBUG
            .debug_log   = {nb_debug_log},
            .debug_print = {nb_debug_print},
#endif
        };
    }  // namespace

    noir_bridge const& engine_bridge() noexcept { return g_engine_bridge; }
}  // namespace NEONengine
