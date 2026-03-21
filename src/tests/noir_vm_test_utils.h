#ifndef __NOIR_VM_TEST_UTILS_H__INCLUDED__
#define __NOIR_VM_TEST_UTILS_H__INCLUDED__

#ifdef ACE_TEST_RUNNER

#include "core/noir_opcodes.h"
#include "core/noir_vm.h"
#include "test_macros.h"

namespace NEONengine
{
    // ─── Mock bridge tracking ───────────────────────────────────────────────────
    //
    // Static state used by mock bridge functions so tests can verify which
    // bridge calls the VM made and with what arguments.

    struct mock_bridge_log
    {
        // Inventory
        uint16_t inv_added[16];
        uint16_t inv_removed[16];
        uint8_t inv_add_count;
        uint8_t inv_remove_count;
        bool inv_cleared;
        uint16_t inv_has_query;
        bool inv_has_result;
        uint16_t inv_count_result;

        // Scene
        uint16_t goto_scene_id;
        uint16_t hotspot_on_id;
        uint16_t hotspot_off_id;
        uint16_t set_pointer_type;
        uint16_t get_scene_result;
        bool goto_scene_called;

        // Dialogue / Audio
        uint16_t say_string_id;
        uint16_t say_speaker_id;
        uint16_t show_dialog_id;
        uint16_t show_text_id;
        uint16_t show_text_x;
        uint16_t show_text_y;
        uint16_t play_music_id;
        uint16_t play_sfx_id;
        bool say_called;
        bool show_dialog_called;
        bool clear_text_called;
        bool stop_music_called;

        // Screen
        uint16_t fade_out_frames;
        uint16_t fade_in_frames;
        uint16_t screen_clr_color;
        bool fade_out_called;
        bool fade_in_called;

        // Animation
        uint16_t anim_play_id;
        uint16_t anim_stop_id;
        uint16_t anim_wait_id;
        bool anim_wait_called;

        // System
        uint16_t set_lang_id;
        uint16_t save_slot;
        uint16_t load_slot;
        bool new_game_called;
        bool quit_game_called;

#ifdef GAME_DEBUG
        // Debug
        uint16_t debug_log_string_id;
        bool debug_log_called;
        char debug_print_buf[64];
        uint8_t debug_print_len;
        bool debug_print_called;
#endif

        void reset()
        {
            inv_add_count    = 0;
            inv_remove_count = 0;
            inv_cleared      = false;
            inv_has_query    = 0;
            inv_has_result   = false;
            inv_count_result = 0;

            goto_scene_id     = 0;
            hotspot_on_id     = 0;
            hotspot_off_id    = 0;
            set_pointer_type  = 0;
            get_scene_result  = 0;
            goto_scene_called = false;

            say_string_id      = 0;
            say_speaker_id     = 0;
            show_dialog_id     = 0;
            show_text_id       = 0;
            show_text_x        = 0;
            show_text_y        = 0;
            play_music_id      = 0;
            play_sfx_id        = 0;
            say_called         = false;
            show_dialog_called = false;
            clear_text_called  = false;
            stop_music_called  = false;

            fade_out_frames  = 0;
            fade_in_frames   = 0;
            screen_clr_color = 0;
            fade_out_called  = false;
            fade_in_called   = false;

            anim_play_id     = 0;
            anim_stop_id     = 0;
            anim_wait_id     = 0;
            anim_wait_called = false;

            set_lang_id      = 0;
            save_slot        = 0;
            load_slot        = 0;
            new_game_called  = false;
            quit_game_called = false;

#ifdef GAME_DEBUG
            debug_log_string_id = 0;
            debug_log_called    = false;
            debug_print_len     = 0;
            debug_print_called  = false;
            for (auto& b : debug_print_buf) b = 0;
#endif
        }
    };

    static mock_bridge_log g_mock_log;

    // ─── Mock bridge function implementations ───────────────────────────────────

    static void mock_inv_add(uint16_t id)
    {
        g_mock_log.inv_added[g_mock_log.inv_add_count++] = id;
    }
    static void mock_inv_remove(uint16_t id)
    {
        g_mock_log.inv_removed[g_mock_log.inv_remove_count++] = id;
    }
    static bool mock_inv_has(uint16_t id)
    {
        g_mock_log.inv_has_query = id;
        return g_mock_log.inv_has_result;
    }
    static uint16_t mock_inv_count()
    {
        return g_mock_log.inv_count_result;
    }
    static void mock_inv_clear()
    {
        g_mock_log.inv_cleared = true;
    }

    static void mock_goto_scene(uint16_t id)
    {
        g_mock_log.goto_scene_id     = id;
        g_mock_log.goto_scene_called = true;
    }
    static uint16_t mock_get_scene()
    {
        return g_mock_log.get_scene_result;
    }
    static void mock_hotspot_on(uint16_t id)
    {
        g_mock_log.hotspot_on_id = id;
    }
    static void mock_hotspot_off(uint16_t id)
    {
        g_mock_log.hotspot_off_id = id;
    }
    static void mock_set_pointer(uint16_t t)
    {
        g_mock_log.set_pointer_type = t;
    }

    static void mock_say(uint16_t s, uint16_t sp)
    {
        g_mock_log.say_string_id  = s;
        g_mock_log.say_speaker_id = sp;
        g_mock_log.say_called     = true;
    }
    static void mock_show_dialog(uint16_t id)
    {
        g_mock_log.show_dialog_id     = id;
        g_mock_log.show_dialog_called = true;
    }
    static void mock_show_text(uint16_t s, uint16_t x, uint16_t y)
    {
        g_mock_log.show_text_id = s;
        g_mock_log.show_text_x  = x;
        g_mock_log.show_text_y  = y;
    }
    static void mock_clear_text()
    {
        g_mock_log.clear_text_called = true;
    }
    static void mock_play_music(uint16_t id)
    {
        g_mock_log.play_music_id = id;
    }
    static void mock_stop_music()
    {
        g_mock_log.stop_music_called = true;
    }
    static void mock_play_sfx(uint16_t id)
    {
        g_mock_log.play_sfx_id = id;
    }

    static void mock_fade_out(uint16_t f)
    {
        g_mock_log.fade_out_frames = f;
        g_mock_log.fade_out_called = true;
    }
    static void mock_fade_in(uint16_t f)
    {
        g_mock_log.fade_in_frames = f;
        g_mock_log.fade_in_called = true;
    }
    static void mock_screen_clear(uint16_t c)
    {
        g_mock_log.screen_clr_color = c;
    }

    static void mock_anim_play(uint16_t id)
    {
        g_mock_log.anim_play_id = id;
    }
    static void mock_anim_stop(uint16_t id)
    {
        g_mock_log.anim_stop_id = id;
    }
    static void mock_anim_wait(uint16_t id)
    {
        g_mock_log.anim_wait_id     = id;
        g_mock_log.anim_wait_called = true;
    }

    static void mock_set_lang(uint16_t id)
    {
        g_mock_log.set_lang_id = id;
    }
    static void mock_save_game(uint16_t s)
    {
        g_mock_log.save_slot = s;
    }
    static void mock_load_game(uint16_t s)
    {
        g_mock_log.load_slot = s;
    }
    static void mock_new_game()
    {
        g_mock_log.new_game_called = true;
    }
    static void mock_quit_game()
    {
        g_mock_log.quit_game_called = true;
    }

#ifdef GAME_DEBUG
    static void mock_debug_log(uint16_t string_id)
    {
        g_mock_log.debug_log_string_id = string_id;
        g_mock_log.debug_log_called    = true;
    }
    static void mock_debug_print(char const* str, uint8_t len)
    {
        g_mock_log.debug_print_called = true;
        g_mock_log.debug_print_len    = len;
        uint8_t copy_len              = len < 63 ? len : 63;
        for (uint8_t i = 0; i < copy_len; ++i) g_mock_log.debug_print_buf[i] = str[i];
        g_mock_log.debug_print_buf[copy_len] = '\0';
    }
#endif

    // ─── Pre-built mock bridge instance ─────────────────────────────────────────

    static noir_bridge const g_mock_bridge = {
        .inv_add    = mock_inv_add,
        .inv_remove = mock_inv_remove,
        .inv_has    = mock_inv_has,
        .inv_count  = mock_inv_count,
        .inv_clear  = mock_inv_clear,

        .goto_scene  = mock_goto_scene,
        .get_scene   = mock_get_scene,
        .hotspot_on  = mock_hotspot_on,
        .hotspot_off = mock_hotspot_off,
        .set_pointer = mock_set_pointer,

        .say         = mock_say,
        .show_dialog = mock_show_dialog,
        .show_text   = mock_show_text,
        .clear_text  = mock_clear_text,

        .play_music = mock_play_music,
        .stop_music = mock_stop_music,
        .play_sfx   = mock_play_sfx,

        .fade_out     = mock_fade_out,
        .fade_in      = mock_fade_in,
        .screen_clear = mock_screen_clear,

        .anim_play = mock_anim_play,
        .anim_stop = mock_anim_stop,
        .anim_wait = mock_anim_wait,

        .set_lang  = mock_set_lang,
        .save_game = mock_save_game,
        .load_game = mock_load_game,
        .new_game  = mock_new_game,
        .quit_game = mock_quit_game,

#ifdef GAME_DEBUG
        .debug_log   = mock_debug_log,
        .debug_print = mock_debug_print,
#endif
    };

    // ─── Bytecode builder ───────────────────────────────────────────────────────
    //
    // Small helper to emit bytecode into a fixed buffer. Usage:
    //
    //   bytecode_builder bc;
    //   bc.emit(noir_op::NOP);
    //   bc.emit(noir_op::HALT);
    //   auto vm = bc.build_vm();
    //

    struct bytecode_builder
    {
        static constexpr uint16_t MAX_CODE  = 512;
        static constexpr uint16_t MAX_FUNCS = 8;

        uint8_t code[MAX_CODE];
        uint32_t toc[MAX_FUNCS];
        uint16_t pos;
        uint16_t func_count;

        // External state arrays provided to the VM
        uint8_t flags[16];  // 128 flags
        uint16_t values[32];

        bytecode_builder() : pos(0), func_count(0)
        {
            for (auto& b : code) b = 0;
            for (auto& t : toc) t = 0;
            for (auto& f : flags) f = 0;
            for (auto& v : values) v = 0;
        }

        // Start a new function at the current position
        uint16_t begin_func()
        {
            uint16_t id       = func_count;
            toc[func_count++] = pos;
            return id;
        }

        // Record the current position for computing offsets later
        uint16_t mark() { return pos; }

        void emit(uint8_t byte) { code[pos++] = byte; }

        void emit_u16(uint16_t val)
        {
            code[pos++] = static_cast<uint8_t>(val >> 8);
            code[pos++] = static_cast<uint8_t>(val & 0xFF);
        }

        void emit_s16(int16_t val) { emit_u16(static_cast<uint16_t>(val)); }

        // Emit a mode byte from dst and src modes
        void emit_mode(uint8_t dst, uint8_t src)
        {
            code[pos++]
                = static_cast<uint8_t>((dst << noir_mode::DST_SHIFT) | (src & noir_mode::SRC_MASK));
        }

        // Build a VM using this bytecode. Caller must keep this builder alive.
        noir_vm build_vm(uint16_t stack_size = 256)
        {
            g_mock_log.reset();
            return noir_vm(flags,
                sizeof(flags),
                values,
                sizeof(values) / sizeof(values[0]),
                code,
                pos,
                toc,
                func_count,
                &g_mock_bridge,
                stack_size);
        }

        // Run a single-function script to completion (up to max_updates frames)
        noir_status run_to_completion(noir_vm& vm, uint16_t func_id, uint16_t max_updates = 100)
        {
            auto status = vm.execute(func_id);
            if (status != noir_status::ok) return status;

            for (uint16_t i = 0; i < max_updates; ++i)
            {
                status = vm.update();
                if (status != noir_status::yielded) return status;
            }
            return status;
        }
    };

}  // namespace NEONengine

#endif  // ACE_TEST_RUNNER
#endif  // __NOIR_VM_TEST_UTILS_H__INCLUDED__
