#ifndef __NOIR_VM_TEST_UTILS_H__INCLUDED__
#define __NOIR_VM_TEST_UTILS_H__INCLUDED__

#ifdef ACE_TEST_RUNNER

#include "types.h"

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
        u16 inv_added[16];
        u16 inv_removed[16];
        u8 inv_add_count;
        u8 inv_remove_count;
        bool inv_clear_called;
        u16 inv_cleared_item_id;
        u16 inv_has_query;
        bool inv_has_result;
        u16 inv_count_query;
        u16 inv_count_result;

        // Scene
        u16 goto_scene_id;
        u16 hotspot_on_id;
        u16 hotspot_off_id;
        u16 set_pointer_type;
        u16 get_scene_result;
        bool goto_scene_called;

        // Dialogue / Audio
        u16 say_string_id;
        u16 say_speaker_id;
        u16 show_dialog_id;
        u16 show_text_id;
        u16 show_text_x;
        u16 show_text_y;
        u16 play_music_id;
        u16 play_sfx_id;
        bool say_called;
        u16 say_count;  ///< A deferred call must not be made twice by one park.
        bool show_dialog_called;
        bool clear_text_called;
        bool stop_music_called;

        // Screen
        u16 fade_out_frames;
        u16 fade_in_frames;
        u16 screen_clr_color;
        bool fade_out_called;
        bool fade_in_called;

        // Animation
        u16 anim_play_id;
        u16 anim_stop_id;

        // System
        u16 set_lang_id;
        u16 save_slot;
        u16 load_slot;
        bool new_game_called;
        bool quit_game_called;

        // Diagnostics
        char error_text[NOIR_ERROR_MESSAGE_SIZE];  ///< What the last death told the player

#ifdef GAME_DEBUG
        // Debug
        u16 debug_log_string_id;
        bool debug_log_called;
        char debug_print_buf[64];
        u8 debug_print_len;
        bool debug_print_called;
#endif

        void reset()
        {
            inv_add_count    = 0;
            inv_remove_count = 0;
            inv_clear_called = false;
            inv_cleared_item_id = 0;
            inv_has_query    = 0;
            inv_has_result   = false;
            inv_count_query  = 0;
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
            say_count          = 0;
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

            set_lang_id      = 0;
            save_slot        = 0;
            load_slot        = 0;
            new_game_called  = false;
            quit_game_called = false;

            error_text[0] = '\0';

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
    //
    // Each takes the log as its host, so the bridge reaches it the same way the
    // engine's own reaches the engine. g_mock_log is what the tests are handed,
    // and what they read their assertions from.

    /// Wraps a mock below as a slot the bridge table can hold.
    template<auto Fn>
    inline constexpr auto on_log = host_call<mock_bridge_log, Fn>;

    static void mock_inv_add(mock_bridge_log& log, u16 id)
    {
        log.inv_added[log.inv_add_count++] = id;
    }
    static void mock_inv_remove(mock_bridge_log& log, u16 id)
    {
        log.inv_removed[log.inv_remove_count++] = id;
    }
    static bool mock_inv_has(mock_bridge_log& log, u16 id)
    {
        log.inv_has_query = id;
        return log.inv_has_result;
    }
    static u16 mock_inv_count(mock_bridge_log& log, u16 id)
    {
        log.inv_count_query = id;
        return log.inv_count_result;
    }
    static void mock_inv_clear_item(mock_bridge_log& log, u16 id)
    {
        log.inv_clear_called = true;
        log.inv_cleared_item_id = id;
    }

    /// A fixed answer, so a test can tell INV_SELECTED apart from an empty D0.
    constexpr u16 MOCK_SELECTED_ITEM = 0x2A;
    static u16 mock_inv_selected_item(mock_bridge_log&) { return MOCK_SELECTED_ITEM; }

    static void mock_goto_scene(mock_bridge_log& log, u16 id)
    {
        log.goto_scene_id     = id;
        log.goto_scene_called = true;
    }
    static u16 mock_get_scene(mock_bridge_log& log)
    {
        return log.get_scene_result;
    }
    static void mock_hotspot_on(mock_bridge_log& log, u16 id)
    {
        log.hotspot_on_id = id;
    }
    static void mock_hotspot_off(mock_bridge_log& log, u16 id)
    {
        log.hotspot_off_id = id;
    }
    static void mock_set_pointer(mock_bridge_log& log, u16 t)
    {
        log.set_pointer_type = t;
    }

    static void mock_say(mock_bridge_log& log, u16 s, u16 sp)
    {
        log.say_string_id  = s;
        log.say_speaker_id = sp;
        log.say_called     = true;
        ++log.say_count;
    }
    static void mock_show_dialog(mock_bridge_log& log, u16 id)
    {
        log.show_dialog_id     = id;
        log.show_dialog_called = true;
    }
    static void mock_show_text(mock_bridge_log& log, u16 s, u16 x, u16 y)
    {
        log.show_text_id = s;
        log.show_text_x  = x;
        log.show_text_y  = y;
    }
    static void mock_clear_text(mock_bridge_log& log)
    {
        log.clear_text_called = true;
    }
    static void mock_play_music(mock_bridge_log& log, u16 id)
    {
        log.play_music_id = id;
    }
    static void mock_stop_music(mock_bridge_log& log)
    {
        log.stop_music_called = true;
    }
    static void mock_play_sfx(mock_bridge_log& log, u16 id)
    {
        log.play_sfx_id = id;
    }

    static void mock_fade_out(mock_bridge_log& log, u16 f)
    {
        log.fade_out_frames = f;
        log.fade_out_called = true;
    }
    static void mock_fade_in(mock_bridge_log& log, u16 f)
    {
        log.fade_in_frames = f;
        log.fade_in_called = true;
    }
    static void mock_screen_clear(mock_bridge_log& log, u16 c)
    {
        log.screen_clr_color = c;
    }

    static void mock_anim_play(mock_bridge_log& log, u16 id)
    {
        log.anim_play_id = id;
    }
    static void mock_anim_stop(mock_bridge_log& log, u16 id)
    {
        log.anim_stop_id = id;
    }

    static void mock_set_lang(mock_bridge_log& log, u16 id)
    {
        log.set_lang_id = id;
    }
    static void mock_save_game(mock_bridge_log& log, u16 s)
    {
        log.save_slot = s;
    }
    static void mock_load_game(mock_bridge_log& log, u16 s)
    {
        log.load_slot = s;
    }
    static void mock_new_game(mock_bridge_log& log)
    {
        log.new_game_called = true;
    }
    static void mock_quit_game(mock_bridge_log& log)
    {
        log.quit_game_called = true;
    }

    // The last death wins, so a test that provokes two reads the second - which
    // is what the VM's own account used to do.
    static void mock_report_error(mock_bridge_log& log, char const* message)
    {
        size_t i = 0;
        while (i + 1 < sizeof(log.error_text) && message[i] != '\0')
        {
            log.error_text[i] = message[i];
            ++i;
        }
        log.error_text[i] = '\0';
    }

#ifdef GAME_DEBUG
    static void mock_debug_log(mock_bridge_log& log, u16 string_id)
    {
        log.debug_log_string_id = string_id;
        log.debug_log_called    = true;
    }
    static void mock_debug_print(mock_bridge_log& log, char const* str, u8 len)
    {
        log.debug_print_called = true;
        log.debug_print_len    = len;
        u8 copy_len              = len < 63 ? len : 63;
        for (u8 i = 0; i < copy_len; ++i) log.debug_print_buf[i] = str[i];
        log.debug_print_buf[copy_len] = '\0';
    }
#endif

    // ─── Pre-built mock bridge instance ─────────────────────────────────────────

    static noir_bridge const g_mock_bridge = {
        .inv_add    = {on_log<mock_inv_add>},
        .inv_remove = {on_log<mock_inv_remove>},
        .inv_has    = {on_log<mock_inv_has>},
        .inv_count  = {on_log<mock_inv_count>},
        .inv_clear_item = {on_log<mock_inv_clear_item>},
        .inv_selected_item = {on_log<mock_inv_selected_item>},

        .goto_scene  = {on_log<mock_goto_scene>},
        .get_scene   = {on_log<mock_get_scene>},
        .hotspot_on  = {on_log<mock_hotspot_on>},
        .hotspot_off = {on_log<mock_hotspot_off>},
        .set_pointer = {on_log<mock_set_pointer>},

        .say         = {on_log<mock_say>},
        .show_dialog = {on_log<mock_show_dialog>},
        .show_text   = {on_log<mock_show_text>},
        .clear_text  = {on_log<mock_clear_text>},

        .play_music = {on_log<mock_play_music>},
        .stop_music = {on_log<mock_stop_music>},
        .play_sfx   = {on_log<mock_play_sfx>},

        .fade_out     = {on_log<mock_fade_out>},
        .fade_in      = {on_log<mock_fade_in>},
        .screen_clear = {on_log<mock_screen_clear>},

        .anim_play = {on_log<mock_anim_play>},
        .anim_stop = {on_log<mock_anim_stop>},

        .set_lang  = {on_log<mock_set_lang>},
        .save_game = {on_log<mock_save_game>},
        .load_game = {on_log<mock_load_game>},
        .new_game  = {on_log<mock_new_game>},
        .quit_game = {on_log<mock_quit_game>},

        .report_error = on_log<mock_report_error>,

#ifdef GAME_DEBUG
        .debug_log   = {on_log<mock_debug_log>},
        .debug_print = {on_log<mock_debug_print>},
#endif
    };

    // ─── Bytecode builder ───────────────────────────────────────────────────────
    //
    // Small helper to emit bytecode into a fixed buffer. Usage:
    //
    //   bytecode_builder bc;
    //   bc.emit(noir_op::NOP);
    //   bc.emit(noir_op::RET);
    //   auto vm = bc.build_vm();
    //

    struct bytecode_builder
    {
        static constexpr u16 MAX_CODE  = 1024;
        static constexpr u16 MAX_FUNCS = 16;

        u8 code[MAX_CODE];
        u32 toc[MAX_FUNCS];
        u16 pos;
        u16 func_count;

        // External state containers provided to the VM
        mtl::bitset<u32> flags{ 128 };
        mtl::vector<u16> values{ 32, 0 };

        bytecode_builder() : pos(0), func_count(0)
        {
            for (auto& b : code) b = 0;
            for (auto& t : toc) t = 0;
            flags.reset();
            for (auto& v : values) v = 0;
        }

        // Start a new function at the current position
        u16 begin_func()
        {
            u16 id       = func_count;
            toc[func_count++] = pos;
            return id;
        }

        // Record the current position for computing offsets later
        u16 mark() { return pos; }

        void emit(u8 byte) { code[pos++] = byte; }

        void emit_u16(u16 val)
        {
            code[pos++] = to<u8>(val >> 8);
            code[pos++] = to<u8>(val & 0xFF);
        }

        void emit_s16(i16 val) { emit_u16(to<u16>(val)); }

        // Emit a mode byte from dst and src modes
        void emit_mode(u8 dst, u8 src)
        {
            code[pos++]
                = to<u8>((dst << noir_mode::DST_SHIFT) | (src & noir_mode::SRC_MASK));
        }

        // Build a VM using this bytecode. Caller must keep this builder alive.
        noir_vm build_vm(u16 stack_size = 256, noir_bridge const* bridge = &g_mock_bridge)
        {
            g_mock_log.reset();
            return noir_vm(flags,
                values,
                code,
                pos,
                toc,
                func_count,
                bridge,
                &g_mock_log,
                stack_size);
        }

        // Run a single-function script to completion (up to max_updates frames)
        mtl::expected<noir_result, noir_error> run_to_completion(
            noir_vm& vm, u16 func_id, u16 max_updates = 100)
        {
            auto started = vm.execute(func_id);
            if (!started) return mtl::unexpected(started.error());

            mtl::expected<noir_result, noir_error> status = noir_result::ok;
            for (u16 i = 0; i < max_updates; ++i)
            {
                status = vm.update();
                if (status != noir_result::yielded) return status;
            }
            return status;
        }
    };

    /// True when @p status reports a death for @p reason rather than a result.
    template<typename T>
    inline bool failed_with(mtl::expected<T, noir_error> const& status, noir_error reason)
    {
        return !status.has_value() && status.error() == reason;
    }

    /// True when the message the player was shown mentions @p needle.
    inline bool message_has(char const* needle)
    {
        for (char const* start = g_mock_log.error_text; *start; ++start)
        {
            char const* a = start;
            char const* b = needle;
            while (*b && *a == *b) { ++a; ++b; }
            if (!*b) return true;
        }
        return *needle == '\0';
    }

    /// True when that message is @p expected and nothing besides.
    inline bool message_is(char const* expected)
    {
        char const* a = g_mock_log.error_text;
        while (*a && *a == *expected) { ++a; ++expected; }
        return *a == *expected;
    }

    /// True when no death has told the player anything.
    inline bool nothing_reported() { return g_mock_log.error_text[0] == '\0'; }

}  // namespace NEONengine

#endif  // ACE_TEST_RUNNER
#endif  // __NOIR_VM_TEST_UTILS_H__INCLUDED__
