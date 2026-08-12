#include "core/noir_bridge.h"

#include "neonengine.h"

#include <stdio.h>

#include <mtl/utility.h>

#include "core/logger.h"
#include "core/mouse_pointer.h"
#include "core/scene_runtime.h"
#include "core/screen.h"
#include "core/string_table.h"
#include "types.h"

NE_LOG_DOMAIN(Script);

namespace NEONengine
{
    namespace
    {
        /// Wraps an implementation below as a slot the bridge table can hold.
        template<auto Fn>
        inline constexpr auto on_engine = host_call<engine, Fn>;

        // ── Screen ───────────────────────────────────────────────────────────
        // Fades are deferred modals: the scene starts them at the top of the
        // next frame and holds the VM until they finish.
        void nb_fade_out(engine& e, u16 frames)
        {
            e.request_modal(modal_kind::fade_out, frames);
        }
        void nb_fade_in(engine& e, u16 frames)
        {
            e.request_modal(modal_kind::fade_in, frames);
        }
        void nb_screen_clear(engine&, u16) {}

        // ── Scenes ───────────────────────────────────────────────────────────
        void nb_goto_scene(engine& e, u16 scene_id)
        {
            e.request_system(system_kind::load_scene, scene_id);
        }
        u16 nb_get_scene(engine& e) { return e.current_scene(); }
        void nb_hotspot_on(engine& e, u16 id) { e.scene().hotspots().set_enabled(id, true); }
        void nb_hotspot_off(engine& e, u16 id) { e.scene().hotspots().set_enabled(id, false); }

        void nb_set_pointer(engine&, u16 type)
        {
            if (type < to<u16>(mouse_pointer::COUNT))
            {
                mousePointerSwitch(to<mouse_pointer>(type));
            }
        }

        // ── Diagnostics ──────────────────────────────────────────────────────
        /**
         * @brief Puts what killed a script in front of the player.
         *
         * The VM builds the line in a buffer of its own that it reuses, so this
         * text is only good for the length of the call. The message slot copies
         * what it is given, which is what lets it outlive that.
         */
        void nb_report_error(engine& e, char const* message)
        {
            if (!message) return;

            e.overlays().post_message(message_request{
                .text          = bstr_view(message),
                .caption       = "Script Error",
                .color         = MESSAGE_ERROR_COLOR,
                .caption_color = MESSAGE_ERROR_COLOR,
                .align         = message_align::middle,
            });
        }

        // ── Inventory ────────────────────────────────────────────────────────
        void nb_inv_add(engine& e, u16 item_id) { e.session().add_item(item_id); }
        void nb_inv_remove(engine& e, u16 item_id) { e.session().remove_item(item_id); }
        bool nb_inv_has(engine& e, u16 item_id) { return e.session().has(item_id); }
        u16 nb_inv_count(engine& e, u16 item_id) { return e.session().count(item_id); }
        void nb_inv_clear_item(engine& e, u16 item_id) { e.session().clear_item(item_id); }
        u16 nb_inv_selected_item(engine& e)
        {
            return e.session().get_selected_inventory_item();
        }

        // ── Dialogue / Audio ─────────────────────────────────────────────────
        void nb_say(engine& e, u16 string_id, u16)
        {
            e.overlays().post_message(message_request{
                .text = e.strings().get_string(string_id),
            });
        }
        void nb_show_dialog(engine& e, u16 dialogue_id)
        {
            e.request_modal(modal_kind::show_dialog, dialogue_id);
        }
        void nb_show_text(engine&, u16, u16, u16) {}
        void nb_clear_text(engine&) {}
        void nb_play_music(engine&, u16) {}
        void nb_stop_music(engine&) {}
        void nb_play_sfx(engine&, u16) {}

        // ── Timeline ─────────────────────────────────────────────────────────
        // A track name means a track in the current scene, so naming one that
        // another scene owns does nothing here.
        void nb_anim_play(engine& e, u16 name_id) { e.scene().timeline().play(name_id); }
        void nb_anim_stop(engine& e, u16 name_id) { e.scene().timeline().stop(name_id); }

        // ── System ───────────────────────────────────────────────────────────
        // Records the request; the state loop applies it at a safe point. Doing
        // it here would read from disk inside a VM step.
        void nb_set_lang(engine& e, u16 lang)
        {
            if (lang >= to<u16>(language::COUNT))
            {
                NE_LOG("SET_LANG %hu is not a known language id", lang);
                return;
            }

            NE_LOG("SET_LANG %hu ('%s') requested", lang, language_code(to<language>(lang)));
            e.request_language(to<language>(lang));
        }
        void nb_save_game(engine& e, u16 slot)
        {
            e.request_system(system_kind::save_game, slot);
        }

        // Terminal: the VM stops the moment one of these runs and only starts
        // again once the scene has torn it down.
        void nb_load_game(engine& e, u16 slot)
        {
            e.request_system(system_kind::load_game, slot);
        }
        void nb_new_game(engine& e) { e.request_system(system_kind::new_game); }
        void nb_quit_game(engine& e) { e.request_system(system_kind::quit_game); }

#ifdef GAME_DEBUG
        void nb_debug_log(engine&, u16) {}
        void nb_debug_print(engine&, char const*, u8) {}
#endif

        noir_bridge const g_engine_bridge = {
            .inv_add           = { on_engine<nb_inv_add> },
            .inv_remove        = { on_engine<nb_inv_remove> },
            .inv_has           = { on_engine<nb_inv_has> },
            .inv_count         = { on_engine<nb_inv_count> },
            .inv_clear_item    = { on_engine<nb_inv_clear_item> },
            .inv_selected_item = { on_engine<nb_inv_selected_item> },

            .goto_scene  = { on_engine<nb_goto_scene> },
            .get_scene   = { on_engine<nb_get_scene> },
            .hotspot_on  = { on_engine<nb_hotspot_on> },
            .hotspot_off = { on_engine<nb_hotspot_off> },
            .set_pointer = { on_engine<nb_set_pointer> },

            .say         = { on_engine<nb_say> },
            .show_dialog = { on_engine<nb_show_dialog> },
            .show_text   = { on_engine<nb_show_text> },
            .clear_text  = { on_engine<nb_clear_text> },

            .play_music = { on_engine<nb_play_music> },
            .stop_music = { on_engine<nb_stop_music> },
            .play_sfx   = { on_engine<nb_play_sfx> },

            .fade_out     = { on_engine<nb_fade_out> },
            .fade_in      = { on_engine<nb_fade_in> },
            .screen_clear = { on_engine<nb_screen_clear> },

            .anim_play = { on_engine<nb_anim_play> },
            .anim_stop = { on_engine<nb_anim_stop> },

            .set_lang  = { on_engine<nb_set_lang> },
            .save_game = { on_engine<nb_save_game> },
            .load_game = { on_engine<nb_load_game> },
            .new_game  = { on_engine<nb_new_game> },
            .quit_game = { on_engine<nb_quit_game> },

            .report_error = on_engine<nb_report_error>,

#ifdef GAME_DEBUG
            .debug_log   = { on_engine<nb_debug_log> },
            .debug_print = { on_engine<nb_debug_print> },
#endif
        };
    }  // namespace

    void report_invalid_inventory_item_id(u16 item_id) noexcept
    {
#ifdef GAME_DEBUG
        // game_session reports through a plain u16 hook with no receiver, so
        // this one still reaches for the global. It goes when that hook does.
        if (!g_pEngine) return;

        char message[NOIR_ERROR_MESSAGE_SIZE];
        snprintf(message, sizeof(message), "inventory item %hu is out of range", item_id);
        nb_report_error(*g_pEngine, message);
#else
        (void)item_id;
#endif
    }

    noir_bridge const& engine_bridge() noexcept { return g_engine_bridge; }
}  // namespace NEONengine
