#ifndef __OVERLAY_STATE_TESTS_H__INCLUDED__
#define __OVERLAY_STATE_TESTS_H__INCLUDED__

#ifdef ACE_TEST_RUNNER

#include <ace/managers/blit.h>
#include <ace/managers/key.h>
#include <ace/managers/mouse.h>
#include <ace++/memory_file.h>
#include <ace++/palette.h>

#include "core/overlay_system.h"
#include "core/state_manager.h"
#include "neonengine.h"
#include "states/dialogue.h"
#include "states/message.h"
#include "tests/noir_vm_test_utils.h"
#include "utils/adler32.h"

namespace NEONengine
{
    namespace
    {
        struct overlay_fixture
        {
            bytecode_builder _code;
            overlay_system _overlays;
            bstr_view _say_text{ "owned message" };
            noir_bridge _bridge{};
            tBitMap* _front{ g_pEngine->main_screen().front_buffer() };
            tBitMap* _back{ g_pEngine->main_screen().back_buffer() };
            u8 _space{ g_sKeyManager.pStates[KEY_SPACE] };
            u8 _numbers[DIALOGUE_MAX_CHOICES_SHOWN]{};
            u8 _button{ g_sMouseManager.pMice[MOUSE_PORT_1].pButtonStates[MOUSE_LMB] };
            u16 _mouse_x{ mouseGetX(MOUSE_PORT_1) };
            u16 _mouse_y{ mouseGetY(MOUSE_PORT_1) };
            bool _terminal_requested{ false };
            u16 _terminal_scene{ data::NO_ID };

            overlay_fixture()
            {
                keySetState(KEY_SPACE, KEY_NACTIVE);
                for (u8 i = 0; i < DIALOGUE_MAX_CHOICES_SHOWN; ++i)
                {
                    _numbers[i] = g_sKeyManager.pStates[KEY_1 + i];
                    keySetState(to<u8>(KEY_1 + i), KEY_NACTIVE);
                }
                mouseSetButton(MOUSE_PORT_1, MOUSE_LMB, MOUSE_NACTIVE);
                mouseSetPosition(MOUSE_PORT_1, SCREEN_WIDTH - 1,
                    to<u16>(g_pEngine->main_screen().offset() + SCREEN_HEIGHT - 1));
                // The runner's report is the backdrop for these overlays.
                g_pEngine->main_screen().duplicate_back_to_front();
                _bridge.say = { [](void* host, u16, u16) {
                    auto& fixture = *static_cast<overlay_fixture*>(host);
                    fixture._overlays.post_message(message_request{
                        .text = fixture._say_text, .modal_wait = true });
                } };
                _bridge.show_dialog = { [](void* host, u16 id) {
                    static_cast<overlay_fixture*>(host)->_overlays.post_dialogue(id);
                } };
                _bridge.report_error = [](void* host, char const* text) {
                    static_cast<overlay_fixture*>(host)->_overlays.post_message(
                        message_request{ .text = text });
                };
                _bridge.fade_out = { [](void*, u16) {} };
                _bridge.goto_scene = { [](void* host, u16 id) {
                    auto& fixture = *static_cast<overlay_fixture*>(host);
                    fixture._terminal_requested = true;
                    fixture._terminal_scene = id;
                } };
            }

            ~overlay_fixture()
            {
                keySetState(KEY_SPACE, _space);
                for (u8 i = 0; i < DIALOGUE_MAX_CHOICES_SHOWN; ++i)
                    keySetState(to<u8>(KEY_1 + i), _numbers[i]);
                mouseSetButton(MOUSE_PORT_1, MOUSE_LMB, _button);
                mouseSetPosition(MOUSE_PORT_1, _mouse_x, _mouse_y);
            }

            bool frozen() const
            {
                return g_pEngine->main_screen().front_buffer() == _front
                    && g_pEngine->main_screen().back_buffer() == _front;
            }

            bool drawn() const
            {
                // The CPU must not compare buffers while the blitter is writing them.
                blitWait();
                return memcmp(_front->Planes[0], _back->Planes[0],
                    to<u32>(_front->BytesPerRow) * _front->Rows) != 0;
            }

            bool restored() const
            {
                return g_pEngine->main_screen().front_buffer() == _front
                    && g_pEngine->main_screen().back_buffer() == _back && !drawn();
            }

            u32 pixels() const
            {
                // Hash the completed image, not pixels still being drawn.
                blitWait();
                u32 hash = 5381;
                u32 const size = to<u32>(_front->BytesPerRow) * _front->Rows;
                for (u32 i = 0; i < size; ++i) hash = (hash * 33) ^ _front->Planes[0][i];
                return hash;
            }

            u16 script(u8 opcode)
            {
                u16 const id = _code.begin_func();
                if (opcode == noir_op::SHOW_DIALOG)
                {
                    _code.emit(noir_op::MOV16);
                    _code.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
                    _code.emit_u16(data::NO_ID);
                }
                _code.emit(opcode);
                _code.emit(noir_op::MOV16);
                _code.emit_mode(noir_mode::REG_D3, noir_mode::IMM16);
                _code.emit_u16(1234);
                _code.emit(noir_op::RET);
                return id;
            }

            noir_vm make_vm()
            {
                return noir_vm(_code.flags, _code.values, _code.code, _code.pos,
                    _code.toc, _code.func_count, &_bridge, this, 256);
            }
        };

        struct dialogue_progress_fixture
        {
            game_session _saved;
            ULONG _colors[256]{};

            dialogue_progress_fixture()
            {
                mtl::system_use_guard guard;
                _saved = g_pEngine->session();
                g_pEngine->session().create_from(g_pEngine->data());
                auto const* palette = force_to<ULONG const*>(g_pEngine->main_screen().palette());
                for (u16 i = 0; i < 256; ++i) _colors[i] = palette[i];
            }

            ~dialogue_progress_fixture()
            {
                mtl::system_use_guard guard;
                g_pEngine->session() = mtl::move(_saved);
            }

            bool colors_restored() const
            {
                auto const* palette = force_to<ULONG const*>(g_pEngine->main_screen().palette());
                return memcmp(_colors, palette, sizeof(_colors)) == 0;
            }

            u16 dialogue_id() const
            {
                auto const dialogues = g_pEngine->data().dialogues();
                auto const pages = g_pEngine->data().pages();
                for (u16 i = 0; i < dialogues.size(); ++i)
                {
                    u16 const first = dialogues[i].first_page_id;
                    if (!dialogues[i].page_range.has(first) || first >= pages.size()) continue;
                    auto const& page = pages[first];
                    if (!(page.flags & data::PAGE_FLAG_ENABLED)) continue;
                    if (page.check_flag_id != data::NO_ID
                        && !g_pEngine->session().get_flags().test(page.check_flag_id)) continue;
                    if (!g_pEngine->strings().get_string(page.text_string_id).is_empty()) return i;
                }
                return data::NO_ID;
            }
        };

        struct bound_dialogue_fixture
        {
            game_session _progress;
            data::dialogue _dialogue{ { 0, 2 }, 0, 0 };
            data::page _pages[2]{};
            data::choice _choice{};
            dialogue_session _session;

            bound_dialogue_fixture()
            {
                mtl::system_use_guard guard;
                _progress.create_for_test(2, 1);
                for (auto& page : _pages)
                {
                    page.speaker_id = data::NO_ID;
                    page.text_string_id = data::NO_ID;
                    page.set_flag_id = data::NO_ID;
                    page.clear_flag_id = data::NO_ID;
                    page.check_flag_id = data::NO_ID;
                    page.next_page_id = data::NO_ID;
                    page.display_function_id = data::NO_ID;
                    page.flags = data::PAGE_FLAG_ENABLED;
                }
                _choice.set_flag_id = data::NO_ID;
                _choice.clear_flag_id = data::NO_ID;
                _choice.check_flag_id = data::NO_ID;
                _choice.goto_page_id = 1;
                _choice.click_function_id = data::NO_ID;
                _choice.flags = data::CHOICE_FLAG_ENABLED;
            }

            ~bound_dialogue_fixture()
            {
                mtl::system_use_guard guard;
                _progress = {};
            }

            void bind(dialogue_context& context)
            {
                _session.create(mtl::span<data::dialogue>(&_dialogue, 1),
                    mtl::span<data::page>(_pages, 2), mtl::span<data::choice>(&_choice, 1),
                    &engine_dialogue_bridge(), &context);
            }
        };

        struct dialogue_strings_fixture
        {
            string_table _saved;
            bool _replaced{ false };

            ~dialogue_strings_fixture()
            {
                mtl::system_use_guard guard;
                if (_replaced) g_pEngine->strings() = mtl::move(_saved);
            }

            bool replace(language lang)
            {
                mtl::system_use_guard guard;
                auto table = string_table::create(lang);
                if (!table) return false;
                auto const& current = g_pEngine->strings();
                if (table.value()->size() != current.size()
                    || table.value()->build_period() != current.build_period()
                    || table.value()->build_number() != current.build_number()) return false;
                if (!save()) return false;
                g_pEngine->strings() = mtl::move(*table.value());
                return true;
            }

            bool long_choices()
            {
                mtl::system_use_guard guard;
                auto& strings = g_pEngine->strings();
                mtl::vector<u8> bytes;
                bytes.resize(36);
                for (u16 id = 0; id < strings.size(); ++id)
                {
                    bstr_view text = strings.get_string(id);
                    bool choice_text = false;
                    for (auto const& choice : g_pEngine->data().choices())
                        if (choice.text_string_id == id) choice_text = true;
                    bstr_view const padding = choice_text
                        ? bstr_view("\na\na\na\na\na\na\na\na\na\na\na\na\na\na\na") : bstr_view{};
                    u32 const length = to<u32>(text.length() + padding.length());
                    for (i16 shift = 24; shift >= 0; shift -= 8)
                        bytes.push_back(to<u8>(length >> shift));
                    for (auto c : text) bytes.push_back(to<u8>(c));
                    for (auto c : padding) bytes.push_back(to<u8>(c));
                    for (u8 i = 0; i < 4 - (length & 3); ++i) bytes.push_back(0);
                }
                put(bytes, 0, 0x4E4F4952);
                put(bytes, 4, (u32(2) << 16) | strings.build_period());
                put(bytes, 8, (u32(36) << 16) | to<u16>(strings.language_id()));
                put(bytes, 12, 0);
                put(bytes, 16, bytes.size());
                put(bytes, 20, strings.size());
                put(bytes, 24, bytes.size() - 36);
                put(bytes, 28, strings.build_number());
                put(bytes, 32, adler32(bytes.data() + 36, bytes.size() - 36));
                auto table = string_table::create_from_fd(
                    ace::file_ptr(ace::memoryFileOpen(bytes.data(), bytes.size())));
                if (!table) return false;
                if (!save()) return false;
                g_pEngine->strings() = mtl::move(*table.value());
                return true;
            }

            private:
            bool save()
            {
                if (_replaced) return true;
                auto copy = string_table::create(g_pEngine->current_language());
                if (!copy) return false;
                _saved = mtl::move(*copy.value());
                _replaced = true;
                return true;
            }

            static void put(mtl::vector<u8>& bytes, u32 offset, u32 value)
            {
                for (u8 i = 0; i < 4; ++i)
                    bytes[offset + i] = to<u8>(value >> (24 - i * 8));
            }
        };

        u16 dialogue_first_choice(u16 page_id)
        {
            auto& engine = *g_pEngine;
            auto const& page = engine.data().pages()[page_id];
            for (u16 id = page.choice_range.start;
                id < page.choice_range.end() && id < engine.data().choices().size(); ++id)
            {
                auto const& choice = engine.data().choices()[id];
                if (!(choice.flags & data::CHOICE_FLAG_ENABLED)) continue;
                if (choice.check_flag_id != data::NO_ID
                    && !engine.session().get_flags().test(choice.check_flag_id)) continue;
                if ((choice.flags & data::CHOICE_FLAG_ONE_TIME) && engine.session().is_choice_taken(id)) continue;
                if (!engine.strings().get_string(choice.text_string_id).is_empty()) return id;
            }
            return data::NO_ID;
        }

        char const* dialogue_to_choices(state_manager& manager, u16 id, u16& page_id)
        {
            auto const dialogue = g_pEngine->data().dialogues()[id];
            page_id = dialogue.first_page_id;
            for (u16 visited = 0; visited < dialogue.page_range.count; ++visited)
            {
                TEST_ASSERT(dialogue.page_range.has(page_id), "the fixture needs a reachable choice page");
                TEST_ASSERT(page_id < g_pEngine->data().pages().size(), "the fixture page must exist");
                auto const& page = g_pEngine->data().pages()[page_id];
                TEST_ASSERT(page.display_function_id == data::NO_ID, "fixture navigation must not run authored scripts");
                TEST_ASSERT(g_pEngine->session().is_page_read(page_id), "navigation must mark each displayed page read");
                if (dialogue_first_choice(page_id) != data::NO_ID) return nullptr;
                keySetState(KEY_SPACE, KEY_ACTIVE);
                TEST_ASSERT(manager.update() == state_update_result::ok, "space should advance the dialogue");
                page_id = page.next_page_id;
            }
            return "the fixture needs a choice page before the dialogue ends";
        }

        class overlay_presenter : public state
        {
            public:
            overlay_presenter(overlay_system& overlays, noir_vm& vm)
                : _overlays(overlays), _vm(vm) {}

            void update(state_control& control) override { _overlays.present(control, _vm, *g_pEngine); }

            private:
            overlay_system& _overlays;
            noir_vm& _vm;
        };

        struct overlay_text_fixture
        {
            ace::text_bitmap_ptr _text{ nullptr };
            ace::text_bitmap_ptr _caption{ nullptr };

            explicit overlay_text_fixture(message_request const& request)
            {
                mtl::system_use_guard guard;
                auto& renderer = *g_pEngine->default_text_renderer();
                u16 const width = to<u16>(request.max_width - MESSAGE_MARGIN * 2);
                _text = renderer.create_text(request.text, width, request.justify);
                _caption = renderer.create_text(request.caption, width, text_justify::CENTER);
            }

            ~overlay_text_fixture()
            {
                mtl::system_use_guard guard;
                _text.reset(nullptr);
                _caption.reset(nullptr);
            }
        };

        u8 overlay_pixel(tBitMap const* bitmap, u16 x, u16 y)
        {
            u8 value = 0;
            u8 const mask = to<u8>(0x80 >> (x & 7));
            for (u8 plane = 0; plane < bitmap->Depth; ++plane)
            {
                if (bitmap->Planes[plane][to<u32>(y) * bitmap->BytesPerRow + (x >> 3)] & mask)
                    value |= to<u8>(1 << plane);
            }
            return value;
        }

        bool message_glyphs_match(tTextBitMap const& text, u16 x, u16 y, u8 color)
        {
            // The blitter may still be writing either bitmap the CPU will inspect.
            blitWait();
            auto& screen = g_pEngine->main_screen();
            bool has_ink = false;
            for (u16 row = 0; row < text.uwActualHeight; ++row)
            {
                for (u16 column = 0; column < text.uwActualWidth; ++column)
                {
                    if (!overlay_pixel(text.pBitMap, column, row)) continue;
                    has_ink = true;
                    if (overlay_pixel(screen.front_buffer(),
                            to<u16>(x + column), to<u16>(screen.offset() + y + row)) != color)
                        return false;
                }
            }
            return has_ink;
        }

        char const* overlay_round_trip(u8 opcode, overlay_kind kind)
        {
            overlay_fixture fixture;
            u16 const script = fixture.script(opcode);
            auto vm = fixture.make_vm();
            TEST_ASSERT(vm.execute(script), "the modal script should start");
            vm.update();
            TEST_ASSERT(vm.blocked(), "the VM should latch after its bridge callback");

            // A second request cannot own or release the first request's wait.
            if (kind == overlay_kind::message) fixture._overlays.post_dialogue(99);
            else fixture._overlays.post_message(
                message_request{ .text = "dropped", .modal_wait = true });
            TEST_ASSERT(fixture._overlays.kind() == kind, "the first overlay should keep its slot");

            state_manager manager;
            manager.start<overlay_presenter>(fixture._overlays, vm);
            TEST_ASSERT(manager.update() == state_update_result::ok, "presentation should queue one push");
            TEST_ASSERT(fixture._overlays.kind() == kind, "construction should not consume the slot");
            TEST_ASSERT(vm.blocked(), "request time must not release the accepted modal");
            TEST_ASSERT(manager.update() == state_update_result::ok, "the overlay should enter");
            TEST_ASSERT(fixture._overlays.kind() == overlay_kind::none, "entry should consume the slot");
            TEST_ASSERT(vm.blocked() && vm.session_count() == 1, "entry must not resume or reset the VM");
            if (kind == overlay_kind::message)
            {
                TEST_ASSERT(fixture.frozen() && fixture.drawn(), "message entry must paint the frozen display");
                TEST_ASSERT(manager.update() == state_update_result::ok, "the message should wait for input");
                TEST_ASSERT(vm.blocked() && fixture.frozen(), "idle updates must keep the message and wait");
                keySetState(KEY_SPACE, KEY_ACTIVE);
                TEST_ASSERT(manager.update() == state_update_result::ok, "space should request a pop");
                TEST_ASSERT(vm.blocked() && fixture.frozen(), "the pop request must not restore or resume yet");
            }
            TEST_ASSERT(manager.update() == state_update_result::ok, "actual pop should return to the presenter");
            TEST_ASSERT(fixture.restored(), "actual pop must restore both scene buffers");
            TEST_ASSERT(!vm.blocked(), "only destruction of the entered state should release the wait");
            TEST_ASSERT(vm.update() == noir_result::completed, "the parked script should finish");
            TEST_ASSERT(vm.reg_d(3) == 1234, "the opcode after the modal should run");
            TEST_SUCCESS;
        }

        char const* overlay_terminal_survives_pop(u8 opcode)
        {
            overlay_fixture fixture;
            u16 const modal = fixture.script(opcode);
            u16 const terminal = fixture.script(noir_op::GOTO_SCENE);
            auto vm = fixture.make_vm();
            TEST_ASSERT(vm.execute(modal), "the modal script should start");
            vm.update();
            state_manager manager;
            manager.start<overlay_presenter>(fixture._overlays, vm);
            TEST_ASSERT(manager.update() == state_update_result::ok, "the overlay should queue");
            TEST_ASSERT(manager.update() == state_update_result::ok, "the overlay should enter");
            vm.execute_now(terminal);
            TEST_ASSERT(vm.terminated(), "an immediate terminal call should latch the VM");
            if (opcode == noir_op::SAY)
            {
                mouseSetButton(MOUSE_PORT_1, MOUSE_LMB, MOUSE_ACTIVE);
                TEST_ASSERT(manager.update() == state_update_result::ok, "click should request a pop");
                TEST_ASSERT(fixture.frozen(), "the pop request must leave the display frozen");
            }
            TEST_ASSERT(manager.update() == state_update_result::ok, "the entered overlay should pop");
            TEST_ASSERT(fixture.restored(), "terminal pop must still restore the scene");
            TEST_ASSERT(vm.terminated(), "overlay destruction must not lift the terminal latch");
            TEST_ASSERT(vm.session_count() == 1, "overlay destruction must not reset sessions");
            TEST_ASSERT(vm.update() == noir_result::terminated, "the parked session must not resume");
            TEST_SUCCESS;
        }

        char const* diagnostic_before_modal(u8 opcode)
        {
            overlay_fixture fixture;
            u16 const diagnostic = fixture.script(noir_op::HALT);
            u16 const modal = fixture.script(opcode);
            auto vm = fixture.make_vm();
            TEST_ASSERT(vm.execute(diagnostic) && vm.execute(modal), "both sessions should start");
            vm.update();
            TEST_ASSERT(vm.blocked(), "the rejected modal still latches after the callback");
            TEST_ASSERT(fixture._overlays.kind() == overlay_kind::message, "the diagnostic should win");
            TEST_ASSERT(vm.session_count() == 1, "only the modal session should survive HALT");

            state_manager manager;
            manager.start<overlay_presenter>(fixture._overlays, vm);
            TEST_ASSERT(manager.update() == state_update_result::ok, "only the diagnostic should queue");
            TEST_ASSERT(!vm.blocked(), "presentation should answer the dropped modal after it latched");
            TEST_ASSERT(manager.update() == state_update_result::ok, "the diagnostic should enter");
            TEST_ASSERT(fixture.frozen() && fixture.drawn(), "the diagnostic must render");
            keySetState(KEY_SPACE, KEY_ACTIVE);
            TEST_ASSERT(manager.update() == state_update_result::ok, "space should request diagnostic dismissal");
            TEST_ASSERT(manager.update() == state_update_result::ok, "the diagnostic should pop");
            TEST_ASSERT(fixture.restored(), "the diagnostic should leave no pixels behind");
            TEST_ASSERT(vm.update() == noir_result::completed, "the dropped modal must not strand its caller");
            TEST_ASSERT(vm.reg_d(3) == 1234, "the caller should continue after the dropped call");
            TEST_SUCCESS;
        }
    }

    TEST_IMPL(test_pending_message_leaves_slot_and_vm)
    {
        overlay_fixture fixture;
        u16 const script = fixture.script(noir_op::SAY);
        auto vm = fixture.make_vm();
        TEST_ASSERT(vm.execute(script), "the script should start");
        vm.update();
        state_manager manager;
        manager.start<message_state>(*g_pEngine, vm, fixture._overlays,
            message_request{ .text = "never entered", .modal_wait = true });
        manager.clear();
        TEST_ASSERT(fixture._overlays.kind() == overlay_kind::message, "pending destruction must not consume the slot");
        TEST_ASSERT(vm.blocked() && vm.session_count() == 1, "pending destruction must not resume or reset the VM");
        TEST_ASSERT(fixture.restored(), "pending destruction must not freeze or paint the display");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_pending_dialogue_leaves_slot_and_vm)
    {
        overlay_fixture fixture;
        u16 const script = fixture.script(noir_op::SHOW_DIALOG);
        auto vm = fixture.make_vm();
        TEST_ASSERT(vm.execute(script), "the script should start");
        vm.update();
        state_manager manager;
        manager.start<dialogue_state>(*g_pEngine, vm, fixture._overlays, u16(0xFFFF));
        manager.clear();
        TEST_ASSERT(fixture._overlays.kind() == overlay_kind::dialogue, "pending destruction must not consume the slot");
        TEST_ASSERT(vm.blocked() && vm.session_count() == 1, "pending destruction must not resume or reset the VM");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_message_first_wins_and_releases_on_pop)
    {
        return overlay_round_trip(noir_op::SAY, overlay_kind::message);
    }

    TEST_IMPL(test_dialogue_first_wins_and_releases_on_pop)
    {
        return overlay_round_trip(noir_op::SHOW_DIALOG, overlay_kind::dialogue);
    }

    TEST_IMPL(test_message_pop_preserves_terminal_latch)
    {
        return overlay_terminal_survives_pop(noir_op::SAY);
    }

    TEST_IMPL(test_dialogue_pop_preserves_terminal_latch)
    {
        return overlay_terminal_survives_pop(noir_op::SHOW_DIALOG);
    }

    TEST_IMPL(test_diagnostic_does_not_release_fade)
    {
        overlay_fixture fixture;
        u16 const fade = fixture.script(noir_op::FADE_OUT);
        auto vm = fixture.make_vm();
        TEST_ASSERT(vm.execute(fade), "the fade script should start");
        vm.update();
        fixture._overlays.post_message(message_request{ .text = "diagnostic during fade" });
        state_manager manager;
        manager.start<overlay_presenter>(fixture._overlays, vm);
        TEST_ASSERT(manager.update() == state_update_result::ok, "the diagnostic should queue");
        TEST_ASSERT(manager.update() == state_update_result::ok, "the diagnostic should enter");
        TEST_ASSERT(fixture.frozen() && fixture.drawn(), "a diagnostic during a fade must render");
        mouseSetButton(MOUSE_PORT_1, MOUSE_LMB, MOUSE_ACTIVE);
        TEST_ASSERT(manager.update() == state_update_result::ok, "click should request dismissal");
        TEST_ASSERT(vm.blocked() && fixture.frozen(), "the pop request must not release the fade or display");
        TEST_ASSERT(manager.update() == state_update_result::ok, "the diagnostic should pop");
        TEST_ASSERT(fixture.restored(), "the diagnostic should restore the display");
        TEST_ASSERT(vm.blocked(), "a diagnostic must not release the fade's wait");
        TEST_ASSERT(vm.update() == noir_result::blocked, "the fade's caller must stay parked");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_empty_say_releases_after_vm_latches)
    {
        overlay_fixture fixture;
        fixture._say_text = {};
        u16 const script = fixture.script(noir_op::SAY);
        auto vm = fixture.make_vm();
        TEST_ASSERT(vm.execute(script), "the script should start");
        vm.update();
        TEST_ASSERT(vm.blocked(), "even empty SAY latches after its callback returns");
        TEST_ASSERT(fixture._overlays.kind() == overlay_kind::none, "empty text should not own a slot");
        state_manager manager;
        manager.start<overlay_presenter>(fixture._overlays, vm);
        TEST_ASSERT(manager.update() == state_update_result::ok, "presentation should answer without a push");
        TEST_ASSERT(!vm.blocked(), "the dropped empty SAY should release its wait");
        TEST_ASSERT(manager.update() == state_update_result::ok, "a second presentation should have no transition");
        TEST_ASSERT(vm.update() == noir_result::completed, "the caller should continue");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_diagnostic_then_say_does_not_strand_vm)
    {
        return diagnostic_before_modal(noir_op::SAY);
    }

    TEST_IMPL(test_diagnostic_then_dialogue_does_not_strand_vm)
    {
        return diagnostic_before_modal(noir_op::SHOW_DIALOG);
    }

    TEST_IMPL(test_message_owns_request_after_construction)
    {
        overlay_fixture fixture;
        u16 const script = fixture.script(noir_op::SAY);
        auto vm = fixture.make_vm();
        u32 expected_pixels;
        {
            state_manager reference;
            reference.start<message_state>(*g_pEngine, vm, fixture._overlays,
                message_request{ .text = "copied state payload", .caption = "copied caption" });
            TEST_ASSERT(reference.update() == state_update_result::ok, "the reference message should render");
            TEST_ASSERT(fixture.frozen() && fixture.drawn(), "the reference must paint real pixels");
            expected_pixels = fixture.pixels();
        }
        TEST_ASSERT(fixture.restored(), "clearing an entered message must restore the display");
        TEST_ASSERT(vm.execute(script), "the script should start");
        vm.update();
        state_manager manager;
        {
            char text[] = "copied state payload";
            char caption[] = "copied caption";
            message_request request{ .text = text, .caption = caption, .modal_wait = true };
            manager.start<message_state>(*g_pEngine, vm, fixture._overlays, request);
            memset(text, 0, sizeof(text));
            memset(caption, 0, sizeof(caption));
            request.modal_wait = false;
        }
        TEST_ASSERT(manager.update() == state_update_result::ok, "entry should use the owned payload");
        TEST_ASSERT(fixture.frozen() && fixture.pixels() == expected_pixels,
            "body and caption pixels must match the original strings");
        TEST_ASSERT(vm.blocked(), "entry must retain the copied modal ownership");
        keySetState(KEY_SPACE, KEY_ACTIVE);
        TEST_ASSERT(manager.update() == state_update_result::ok, "dismissal should queue a pop");
        TEST_ASSERT(vm.blocked(), "dismissal must not release the modal before destruction");
        TEST_ASSERT(manager.update() == state_update_result::finished, "the only state should pop");
        TEST_ASSERT(fixture.restored(), "the copied message should restore its backdrop");
        TEST_ASSERT(!vm.blocked(), "changing the source request must not change modal ownership");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_pending_message_survives_slot_reuse)
    {
        overlay_fixture fixture;
        char text[] = "copied slot payload";
        fixture._say_text = text;
        u16 const script = fixture.script(noir_op::SAY);
        auto vm = fixture.make_vm();
        u32 expected_pixels;
        {
            state_manager reference;
            reference.start<message_state>(*g_pEngine, vm, fixture._overlays,
                message_request{ .text = "copied slot payload" });
            TEST_ASSERT(reference.update() == state_update_result::ok, "the reference should render");
            TEST_ASSERT(fixture.frozen() && fixture.drawn(), "the reference must paint real pixels");
            expected_pixels = fixture.pixels();
        }
        TEST_ASSERT(vm.execute(script), "the script should start");
        vm.update();
        memset(text, 0, sizeof(text));
        state_manager manager;
        manager.start<overlay_presenter>(fixture._overlays, vm);
        TEST_ASSERT(manager.update() == state_update_result::ok, "the message should be constructed");
        fixture._overlays.clear();
        fixture._overlays.post_message(message_request{ .text = "replacement diagnostic" });
        TEST_ASSERT(manager.update() == state_update_result::ok, "entry should use its own copy, not the reused slot");
        TEST_ASSERT(fixture.frozen() && fixture.pixels() == expected_pixels,
            "slot and state copies must preserve the original message pixels");
        TEST_ASSERT(vm.blocked(), "slot reuse must not change the pending state's modal ownership");
        keySetState(KEY_SPACE, KEY_ACTIVE);
        TEST_ASSERT(manager.update() == state_update_result::ok, "dismissal should queue a pop");
        TEST_ASSERT(manager.update() == state_update_result::ok, "the message should pop");
        TEST_ASSERT(fixture.restored(), "the original message should restore its backdrop");
        TEST_ASSERT(!vm.blocked(), "the original modal should release its caller");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_message_consumes_opening_press_and_waits)
    {
        overlay_fixture fixture;
        u16 const script = fixture.script(noir_op::SAY);
        auto vm = fixture.make_vm();
        TEST_ASSERT(vm.execute(script), "the script should start");
        vm.update();
        state_manager manager;
        manager.start<overlay_presenter>(fixture._overlays, vm);
        TEST_ASSERT(manager.update() == state_update_result::ok, "the message should queue");
        keySetState(KEY_SPACE, KEY_ACTIVE);
        mouseSetButton(MOUSE_PORT_1, MOUSE_LMB, MOUSE_ACTIVE);
        TEST_ASSERT(manager.update() == state_update_result::ok, "the message should enter");
        TEST_ASSERT(fixture.frozen() && fixture.drawn(), "entry must render despite both opening presses");
        TEST_ASSERT(!keyUse(KEY_SPACE) && !mouseUse(MOUSE_PORT_1, MOUSE_LMB),
            "entry must consume both opening presses");
        ULONG const chip_free = memGetFreeChipSize();
        ULONG const total_free = memGetFreeSize();
        for (u8 i = 0; i < 3; ++i)
        {
            TEST_ASSERT(manager.update() == state_update_result::ok, "held input should not dismiss the message");
            TEST_ASSERT(vm.blocked() && fixture.frozen(), "the message should keep its wait and display");
        }
        TEST_ASSERT(memGetFreeChipSize() == chip_free && memGetFreeSize() == total_free,
            "idle message updates must not allocate text");
        mouseSetButton(MOUSE_PORT_1, MOUSE_LMB, MOUSE_NACTIVE);
        mouseSetButton(MOUSE_PORT_1, MOUSE_LMB, MOUSE_ACTIVE);
        TEST_ASSERT(manager.update() == state_update_result::ok, "a fresh click should request a pop");
        TEST_ASSERT(vm.blocked() && fixture.frozen(), "a pop request must keep the wait and pixels");
        TEST_ASSERT(manager.update() == state_update_result::ok, "the next update should finish the pop");
        TEST_ASSERT(!vm.blocked() && fixture.restored(), "exit should release the modal and restore the display");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_empty_message_enters_and_closes_normally)
    {
        overlay_fixture fixture;
        u16 const script = fixture.script(noir_op::SAY);
        auto vm = fixture.make_vm();
        TEST_ASSERT(vm.execute(script), "the script should start");
        vm.update();
        state_manager manager;
        manager.start<message_state>(*g_pEngine, vm, fixture._overlays,
            message_request{ .modal_wait = true });
        TEST_ASSERT(manager.update() == state_update_result::ok, "empty entry should succeed and request a pop");
        TEST_ASSERT(fixture._overlays.kind() == overlay_kind::none, "empty entry should consume the slot");
        TEST_ASSERT(fixture.restored(), "an empty message must not freeze or draw");
        TEST_ASSERT(vm.blocked(), "empty entry must not resume before actual exit");
        TEST_ASSERT(manager.update() == state_update_result::finished, "empty messages should close without input");
        TEST_ASSERT(!vm.blocked() && fixture.restored(), "empty exit should release only its wait");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_clearing_visible_message_restores_and_releases)
    {
        overlay_fixture fixture;
        u16 const script = fixture.script(noir_op::SAY);
        auto vm = fixture.make_vm();
        TEST_ASSERT(vm.execute(script), "the script should start");
        vm.update();
        state_manager manager;
        manager.start<overlay_presenter>(fixture._overlays, vm);
        TEST_ASSERT(manager.update() == state_update_result::ok, "the message should queue");
        TEST_ASSERT(manager.update() == state_update_result::ok, "the message should enter");
        TEST_ASSERT(fixture.frozen() && fixture.drawn(), "the message must paint before clearing");
        manager.clear();
        TEST_ASSERT(fixture.restored(), "clear should erase and unfreeze a visible message");
        TEST_ASSERT(!vm.blocked() && vm.session_count() == 1, "clear should release the wait without resetting sessions");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_message_layout_and_ink)
    {
        overlay_fixture fixture;
        auto vm = fixture.make_vm();
        message_align const aligns[] = {
            message_align::top, message_align::middle, message_align::bottom };
        text_justify const justifications[] = {
            text_justify::LEFT, text_justify::CENTER, text_justify::RIGHT };
        for (u8 i = 0; i < 3; ++i)
        {
            message_request request{
                .text = "Short\nText wraps over several lines inside this message.",
                .caption = "Caption",
                .max_width = 128,
                .justify = justifications[i],
                .color = 7,
                .caption_color = 11,
                .align = aligns[i] };
            overlay_text_fixture expected(request);
            TEST_ASSERT(expected._text && expected._caption, "expected glyph bitmaps should allocate");
            u16 const content_width = MAX(expected._text->uwActualWidth, expected._caption->uwActualWidth);
            u16 box_width = to<u16>(content_width + MESSAGE_MARGIN * 2);
            if (auto* frame = g_pEngine->default_nine_patch()) box_width = frame->width_for(box_width);
            u16 const caption_height = to<u16>(expected._caption->uwActualHeight + MESSAGE_CAPTION_GAP);
            u16 const box_height = to<u16>(expected._text->uwActualHeight + caption_height + MESSAGE_MARGIN * 2);
            TEST_ASSERT(box_height + MESSAGE_MARGIN * 2 < SCREEN_HEIGHT, "the layout sample should fit the screen");
            u16 const x = to<u16>((SCREEN_WIDTH - box_width) / 2 + (box_width - content_width) / 2);
            u16 const y = to<u16>((i == 0 ? MESSAGE_MARGIN
                : i == 1 ? (SCREEN_HEIGHT - box_height) / 2
                         : SCREEN_HEIGHT - box_height - MESSAGE_MARGIN) + MESSAGE_MARGIN);

            state_manager manager;
            manager.start<message_state>(*g_pEngine, vm, fixture._overlays, request);
            TEST_ASSERT(manager.update() == state_update_result::ok, "the layout sample should enter");
            TEST_ASSERT(fixture.frozen() && fixture.drawn(), "the layout sample must paint pixels");
            TEST_ASSERT(message_glyphs_match(*expected._text, x, to<u16>(y + caption_height), request.color),
                "wrapped, justified body glyphs should use the requested position and color");
            TEST_ASSERT(message_glyphs_match(*expected._caption,
                to<u16>(x + (content_width - expected._caption->uwActualWidth) / 2), y, request.caption_color),
                "caption glyphs should center above the body in their own color");
            manager.clear();
            TEST_ASSERT(fixture.restored(), "each layout must restore the full painted rectangle");
        }
        TEST_SUCCESS;
    }

    TEST_IMPL(test_tall_message_stays_inside_scene)
    {
        overlay_fixture fixture;
        auto vm = fixture.make_vm();
        state_manager manager;
        manager.start<message_state>(*g_pEngine, vm, fixture._overlays,
            message_request{
                .text = "a\na\na\na\na\na\na\na\na\na\na\na\na\na\na\na",
                .caption = "b\nb\nb\nb\nb\nb\nb\nb\nb\nb\nb\nb\nb\nb\nb\nb" });
        TEST_ASSERT(manager.update() == state_update_result::ok, "a tall message should still enter");
        TEST_ASSERT(fixture.frozen() && fixture.drawn(), "the tall message must render");
        u16 const offset = g_pEngine->main_screen().offset();
        for (u16 row = 0; row < fixture._front->Rows; ++row)
        {
            if (row >= offset && row < offset + SCREEN_HEIGHT) continue;
            u32 const position = to<u32>(row) * fixture._front->BytesPerRow;
            TEST_ASSERT(memcmp(fixture._front->Planes[0] + position,
                fixture._back->Planes[0] + position, fixture._front->BytesPerRow) == 0,
                "a tall message must not change pixels outside the scene");
        }
        manager.clear();
        TEST_ASSERT(fixture.restored(), "a tall message must restore every painted pixel");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_renders_consumes_input_and_restores)
    {
        overlay_fixture fixture;
        dialogue_progress_fixture progress;
        u16 const id = progress.dialogue_id();
        TEST_ASSERT(id != data::NO_ID, "the pak should contain a dialogue with a readable entry page");
        u16 const modal = fixture.script(noir_op::SHOW_DIALOG);
        auto vm = fixture.make_vm();
        TEST_ASSERT(vm.execute(modal), "the modal caller should start");
        vm.update();
        state_manager manager;
        manager.start<dialogue_state>(*g_pEngine, vm, fixture._overlays, id);
        keySetState(KEY_SPACE, KEY_ACTIVE);
        keySetState(KEY_1, KEY_ACTIVE);
        mouseSetButton(MOUSE_PORT_1, MOUSE_LMB, MOUSE_ACTIVE);
        TEST_ASSERT(manager.update() == state_update_result::ok, "the authored dialogue should enter");
        TEST_ASSERT(fixture.frozen() && fixture.drawn(), "entry must paint the real display");
        TEST_ASSERT(vm.blocked(), "entry must not release the caller");
        TEST_ASSERT(!keyUse(KEY_SPACE) && !keyUse(KEY_1) && !mouseUse(MOUSE_PORT_1, MOUSE_LMB),
            "entry must consume opening clicks, space and number keys");
        u16 const page = g_pEngine->data().dialogues()[id].first_page_id;
        TEST_ASSERT(g_pEngine->session().is_page_read(page), "the drawn entry page must be marked read");
        for (u8 i = 0; i < 3; ++i)
            TEST_ASSERT(manager.update() == state_update_result::ok, "held opening input must not advance");
        TEST_ASSERT(fixture.frozen() && vm.blocked(), "idle dialogue must retain display and modal ownership");
        manager.clear();
        TEST_ASSERT(fixture.restored() && progress.colors_restored(), "clear must restore all pixels and palette entries");
        TEST_ASSERT(!vm.blocked(), "entered dialogue destruction must release its caller");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_visible_dialogue_terminal_keeps_diagnostic)
    {
        overlay_fixture fixture;
        dialogue_progress_fixture progress;
        u16 const id = progress.dialogue_id();
        TEST_ASSERT(id != data::NO_ID, "the pak should contain a readable dialogue");
        u16 const modal = fixture.script(noir_op::SHOW_DIALOG);
        auto vm = fixture.make_vm();
        TEST_ASSERT(vm.execute(modal), "the modal caller should start");
        vm.update();
        state_manager manager;
        manager.start<dialogue_state>(*g_pEngine, vm, fixture._overlays, id);
        TEST_ASSERT(manager.update() == state_update_result::ok, "the dialogue should enter");
        TEST_ASSERT(fixture.frozen() && fixture.drawn(), "terminal coverage must start with a visible dialogue");
        fixture._overlays.post_message(message_request{ .text = "keep until dialogue closes" });
        // Replace only the fixture's bytecode after all entry scripts have returned.
        fixture._code.code[fixture._code.toc[modal]] = noir_op::GOTO_SCENE;
        TEST_ASSERT(vm.execute_now(modal), "the bound VM should accept a terminal call");
        TEST_ASSERT(vm.terminated() && fixture._terminal_requested, "the fixture VM must own the terminal request");
        TEST_ASSERT(manager.update() == state_update_result::ok, "terminal dialogue should request a pop");
        TEST_ASSERT(fixture.frozen() && vm.terminated(), "requesting pop must retain display ownership and termination");
        TEST_ASSERT(manager.update() == state_update_result::finished, "the next update should pop the dialogue");
        TEST_ASSERT(fixture.restored() && progress.colors_restored(), "terminal exit must restore pixels and palette");
        TEST_ASSERT(vm.terminated() && fixture._terminal_requested, "exit must preserve the terminal latch and request");
        TEST_ASSERT(fixture._overlays.kind() == overlay_kind::message, "exit must not clear queued diagnostics");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_terminal_before_entry_draws_nothing)
    {
        overlay_fixture fixture;
        dialogue_progress_fixture progress;
        u16 const id = progress.dialogue_id();
        TEST_ASSERT(id != data::NO_ID, "the pak should contain a readable dialogue");
        u16 const terminal = fixture.script(noir_op::GOTO_SCENE);
        auto vm = fixture.make_vm();
        TEST_ASSERT(vm.execute_now(terminal), "the terminal script should run");
        state_manager manager;
        manager.start<dialogue_state>(*g_pEngine, vm, fixture._overlays, id);
        TEST_ASSERT(manager.update() == state_update_result::ok, "terminal entry should close normally");
        TEST_ASSERT(fixture.restored() && progress.colors_restored(), "terminal entry must not acquire the display");
        u16 const page = g_pEngine->data().dialogues()[id].first_page_id;
        TEST_ASSERT(!g_pEngine->session().is_page_read(page), "terminal entry must not read a page or run its script");
        TEST_ASSERT(manager.update() == state_update_result::finished, "terminal entry should pop next frame");
        TEST_ASSERT(vm.terminated() && fixture._terminal_requested, "terminal destruction must preserve the request");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_page_script_uses_bound_vm)
    {
        overlay_fixture fixture;
        bound_dialogue_fixture dialogue;
        u16 const terminal = fixture.script(noir_op::GOTO_SCENE);
        auto vm = fixture.make_vm();
        dialogue_context context{ dialogue._progress, vm };
        dialogue.bind(context);
        dialogue._pages[0].display_function_id = terminal;
        bool const engine_terminated = g_pEngine->vm().terminated();
        TEST_ASSERT(!dialogue._session.begin(0), "an entry script that terminates must end the dialogue");
        TEST_ASSERT(dialogue._progress.is_page_read(0), "the page must be read before its script runs");
        TEST_ASSERT(vm.terminated() && fixture._terminal_requested, "the supplied VM must run the immediate script");
        TEST_ASSERT(g_pEngine->vm().terminated() == engine_terminated, "the engine VM must remain untouched");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_choice_terminal_abandons_goto)
    {
        overlay_fixture fixture;
        bound_dialogue_fixture dialogue;
        u16 const terminal = fixture.script(noir_op::GOTO_SCENE);
        auto vm = fixture.make_vm();
        dialogue_context context{ dialogue._progress, vm };
        dialogue.bind(context);
        dialogue._pages[0].choice_range = { 0, 1 };
        dialogue._choice.click_function_id = terminal;
        TEST_ASSERT(dialogue._session.begin(0), "the page should offer its choice");
        dialogue._session.take_choice(0);
        TEST_ASSERT(dialogue._progress.is_choice_taken(0), "selection must mark the choice taken");
        TEST_ASSERT(dialogue._session.has_ended() && vm.terminated(), "the terminal script must end the dialogue");
        TEST_ASSERT(!dialogue._progress.is_page_read(1), "terminal selection must abandon goto_page_id");
        TEST_ASSERT(fixture._terminal_requested, "the terminal request must survive navigation");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_script_error_stays_queued_through_navigation)
    {
        overlay_fixture fixture;
        bound_dialogue_fixture dialogue;
        u16 const modal = fixture.script(noir_op::SAY);
        auto vm = fixture.make_vm();
        dialogue_context context{ dialogue._progress, vm };
        dialogue.bind(context);
        dialogue._pages[0].display_function_id = modal;
        dialogue._pages[0].next_page_id = 1;
        TEST_ASSERT(dialogue._session.begin(0), "an immediate modal error must not end the dialogue");
        TEST_ASSERT(!vm.terminated() && !vm.blocked(), "a rejected immediate modal must not park the VM");
        TEST_ASSERT(fixture._overlays.kind() == overlay_kind::message, "the script error should queue a diagnostic");
        dialogue._session.advance();
        TEST_ASSERT(dialogue._session.current_page_index() == 1, "navigation must continue after a script error");
        dialogue._session.advance();
        TEST_ASSERT(dialogue._session.has_ended(), "the next page should end normally");
        TEST_ASSERT(fixture._overlays.kind() == overlay_kind::message, "navigation and end must preserve the diagnostic");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_message_width_boundaries_restore_all_pixels)
    {
        overlay_fixture fixture;
        auto vm = fixture.make_vm();
        u16 const widths[] = { 0, 1, MESSAGE_MARGIN * 2, MESSAGE_MARGIN * 2 + 1, 31, 305, 319, 320, 321, 65535 };
        for (auto width : widths)
        {
            state_manager manager;
            manager.start<message_state>(*g_pEngine, vm, fixture._overlays, message_request{
                .text = "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
                .max_width = width });
            TEST_ASSERT(manager.update() == state_update_result::ok, "boundary widths should enter safely");
            auto* frame = g_pEngine->default_nine_patch();
            TEST_ASSERT(!frame || frame->width_for(MIN(width, u16(SCREEN_WIDTH))) <= SCREEN_WIDTH,
                "word-aligned frame art must not round a clamped width beyond the screen");
            manager.clear();
            TEST_ASSERT(fixture.restored(), "boundary widths must erase every pixel without wrapping coordinates");
        }
        TEST_SUCCESS;
    }

    TEST_IMPL(test_message_without_measured_ink_closes_without_display)
    {
        overlay_fixture fixture;
        u16 const script = fixture.script(noir_op::SAY);
        auto vm = fixture.make_vm();
        TEST_ASSERT(vm.execute(script), "the modal caller should start");
        vm.update();
        state_manager manager;
        manager.start<message_state>(*g_pEngine, vm, fixture._overlays,
            message_request{ .text = "\n\n", .modal_wait = true });
        TEST_ASSERT(manager.update() == state_update_result::ok, "line breaks without glyphs should close normally");
        TEST_ASSERT(fixture.restored() && vm.blocked(), "no-ink entry must leave pixels alone and retain its wait until pop");
        TEST_ASSERT(manager.update() == state_update_result::finished, "a no-ink message should pop without input");
        TEST_ASSERT(fixture.restored() && !vm.blocked(), "no-ink cleanup must release only its modal wait");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_keyboard_hold_takes_only_selected_choice)
    {
        overlay_fixture fixture;
        dialogue_progress_fixture progress;
        u16 const id = progress.dialogue_id();
        TEST_ASSERT(id != data::NO_ID, "the pak should contain a readable dialogue");
        auto vm = fixture.make_vm();
        state_manager manager;
        manager.start<dialogue_state>(*g_pEngine, vm, fixture._overlays, id);
        TEST_ASSERT(manager.update() == state_update_result::ok, "the dialogue should enter");
        u16 page;
        auto const fault = dialogue_to_choices(manager, id, page);
        TEST_ASSERT(!fault, fault);
        u16 const choice = dialogue_first_choice(page);
        keySetState(KEY_1, KEY_ACTIVE);
        TEST_ASSERT(manager.update() == state_update_result::ok, "number one should highlight the first choice");
        for (u8 frame = 1; frame < DIALOGUE_CHOICE_HOLD_FRAMES; ++frame)
        {
            keySetState(KEY_2, KEY_ACTIVE);
            keySetState(KEY_SPACE, KEY_ACTIVE);
            mouseSetButton(MOUSE_PORT_1, MOUSE_LMB, MOUSE_ACTIVE);
            TEST_ASSERT(manager.update() == state_update_result::ok, "the hold should consume later input");
            TEST_ASSERT(!g_pEngine->session().is_choice_taken(choice), "a highlighted choice is not taken yet");
        }
        TEST_ASSERT(manager.update() == state_update_result::ok, "the final hold frame should select");
        TEST_ASSERT(g_pEngine->session().is_choice_taken(choice), "the first choice should become taken after the hold");
        u16 const destination = g_pEngine->data().choices()[choice].goto_page_id;
        TEST_ASSERT(destination != data::NO_ID && g_pEngine->session().is_page_read(destination),
            "the fixture choice should read its destination");
        TEST_ASSERT(!keyUse(KEY_2) && !keyUse(KEY_SPACE) && !mouseUse(MOUSE_PORT_1, MOUSE_LMB),
            "hold input must not act on the new page");
        manager.clear();
        TEST_ASSERT(fixture.restored() && progress.colors_restored(), "navigation must restore pixels and speaker colors");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_rotates_long_choices_without_taking_them)
    {
        overlay_fixture fixture;
        dialogue_progress_fixture progress;
        dialogue_strings_fixture strings;
        TEST_ASSERT(strings.long_choices(), "the independent long-choice string table should load");
        u16 const id = progress.dialogue_id();
        TEST_ASSERT(id != data::NO_ID, "the pak should contain a readable dialogue");
        auto vm = fixture.make_vm();
        state_manager manager;
        manager.start<dialogue_state>(*g_pEngine, vm, fixture._overlays, id);
        TEST_ASSERT(manager.update() == state_update_result::ok, "the dialogue should enter");
        u16 page;
        auto const fault = dialogue_to_choices(manager, id, page);
        TEST_ASSERT(!fault, fault);
        u16 const first = dialogue_first_choice(page);
        u32 const original_pixels = fixture.pixels();
        keySetState(KEY_2, KEY_ACTIVE);
        for (u8 frame = 0; frame <= DIALOGUE_CHOICE_HOLD_FRAMES; ++frame)
            TEST_ASSERT(manager.update() == state_update_result::ok, "the second slot should rotate after its hold");
        TEST_ASSERT(fixture.frozen() && fixture.pixels() != original_pixels, "the next choice set must draw different glyphs");
        for (u16 i = 0; i < g_pEngine->data().choices().size(); ++i)
            TEST_ASSERT(!g_pEngine->session().is_choice_taken(i), "rotation must not take any choice");
        keySetState(KEY_1, KEY_ACTIVE);
        for (u8 frame = 0; frame <= DIALOGUE_CHOICE_HOLD_FRAMES; ++frame)
            TEST_ASSERT(manager.update() == state_update_result::ok, "number one should take the rotated choice");
        TEST_ASSERT(!g_pEngine->session().is_choice_taken(first), "rotation must not retain the old slot's target");
        bool taken = false;
        for (u16 i = to<u16>(first + 1); i < g_pEngine->data().choices().size(); ++i)
            taken = taken || g_pEngine->session().is_choice_taken(i);
        TEST_ASSERT(taken, "a choice from the next set must become taken");
        manager.clear();
        TEST_ASSERT(fixture.restored() && progress.colors_restored(), "rotated long text must stay inside its erased bounds");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_drawn_choices_survive_string_table_release)
    {
        overlay_fixture fixture;
        dialogue_progress_fixture progress;
        dialogue_strings_fixture strings;
        u16 const id = progress.dialogue_id();
        TEST_ASSERT(id != data::NO_ID, "the pak should contain a readable dialogue");
        auto vm = fixture.make_vm();
        state_manager manager;
        manager.start<dialogue_state>(*g_pEngine, vm, fixture._overlays, id);
        TEST_ASSERT(manager.update() == state_update_result::ok, "the dialogue should enter");
        u16 page;
        auto const fault = dialogue_to_choices(manager, id, page);
        TEST_ASSERT(!fault, fault);
        u32 const drawn = fixture.pixels();
        language const next = g_pEngine->current_language() == language::IT ? language::EN : language::IT;
        TEST_ASSERT(strings.replace(next), "the alternate shipped language must load");
        TEST_ASSERT(g_pEngine->current_language() == next, "the public string table must expose its new language");
        TEST_ASSERT(manager.update() == state_update_result::ok, "a table change must leave the current page up");
        TEST_ASSERT(fixture.pixels() == drawn, "releasing the original table must not redraw the current glyphs");
        u16 const choice = dialogue_first_choice(page);
        u16 const destination = g_pEngine->data().choices()[choice].goto_page_id;
        TEST_ASSERT(destination != data::NO_ID, "the fixture choice should have a page destination");
        keySetState(KEY_1, KEY_ACTIVE);
        for (u8 frame = 0; frame <= DIALOGUE_CHOICE_HOLD_FRAMES; ++frame)
            TEST_ASSERT(manager.update() == state_update_result::ok, "selection must still use the owned choice glyphs");
        TEST_ASSERT(g_pEngine->session().is_page_read(destination), "navigation must reach the next page");
        TEST_ASSERT(fixture.pixels() != drawn, "navigation must paint the new page");
        auto const& target = g_pEngine->data().pages()[destination];
        TEST_ASSERT(target.speaker_id == data::NO_ID && !target.choice_range.count,
            "the fixture destination should be plain text without a portrait or choices");
        message_request request{ .text = g_pEngine->strings().get_string(target.text_string_id),
            .max_width = u16(SCREEN_WIDTH - DIALOGUE_MARGIN * 2) };
        overlay_text_fixture expected(request);
        TEST_ASSERT(expected._text, "the new language's page glyphs should allocate");
        u16 width = to<u16>(expected._text->uwActualWidth + MESSAGE_MARGIN * 2);
        if (auto* frame = g_pEngine->default_nine_patch()) width = frame->width_for(width);
        TEST_ASSERT(message_glyphs_match(*expected._text,
            to<u16>(DIALOGUE_MARGIN + (width - expected._text->uwActualWidth) / 2),
            DIALOGUE_MARGIN + MESSAGE_MARGIN, DIALOGUE_TEXT_COLOR),
            "the next page must use the newly loaded language's actual glyphs");
        manager.clear();
        TEST_ASSERT(fixture.restored() && progress.colors_restored(), "language navigation must restore the scene");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_mouse_hover_and_click_select_immediately)
    {
        overlay_fixture fixture;
        dialogue_progress_fixture progress;
        u16 const id = progress.dialogue_id();
        TEST_ASSERT(id != data::NO_ID, "the pak must contain a readable dialogue");
        auto vm = fixture.make_vm();
        state_manager manager;
        manager.start<dialogue_state>(*g_pEngine, vm, fixture._overlays, id);
        TEST_ASSERT(manager.update() == state_update_result::ok, "the dialogue should enter");
        u16 page_id;
        auto const fault = dialogue_to_choices(manager, id, page_id);
        TEST_ASSERT(!fault, fault);
        auto const& page = g_pEngine->data().pages()[page_id];
        auto const speakers = g_pEngine->data().speakers();
        TEST_ASSERT(page.speaker_id < speakers.size()
            && speakers[page.speaker_id].portrait_bitmap_id != data::NO_ID,
            "the fixture choice page should have a portrait and speaker");
        u16 const frame_x = DIALOGUE_MARGIN * 2 + PORTRAIT_WIDTH;
        u16 width = to<u16>(SCREEN_WIDTH - frame_x - DIALOGUE_MARGIN);
        if (auto* frame = g_pEngine->default_nine_patch())
        {
            u16 const aligned = frame->width_for(width);
            width = aligned > width ? to<u16>(aligned - 16) : aligned;
        }
        auto& renderer = *g_pEngine->default_text_renderer();
        u16 const allowance = to<u16>(width - MESSAGE_MARGIN * 2);
        auto const heading = renderer.measure_text(
            g_pEngine->strings().get_string(speakers[page.speaker_id].name_string_id), allowance);
        auto const body = renderer.measure_text(g_pEngine->strings().get_string(page.text_string_id), allowance);
        u16 const x = frame_x + MESSAGE_MARGIN + DIALOGUE_CHOICE_INDENT + 16;
        u16 const y = to<u16>(g_pEngine->main_screen().offset() + DIALOGUE_MARGIN + MESSAGE_MARGIN
            + heading.height + MESSAGE_CAPTION_GAP + body.height + DIALOGUE_CHOICE_GAP);
        u16 const choice = dialogue_first_choice(page_id);
        u32 const plain = fixture.pixels();
        mouseSetPosition(MOUSE_PORT_1, x, y);
        TEST_ASSERT(manager.update() == state_update_result::ok, "the pointer should hover the first choice");
        TEST_ASSERT(fixture.pixels() != plain && !g_pEngine->session().is_choice_taken(choice),
            "hover must recolor glyphs without taking the choice");
        mouseSetPosition(MOUSE_PORT_1, SCREEN_WIDTH - 1,
            to<u16>(g_pEngine->main_screen().offset() + SCREEN_HEIGHT - 1));
        TEST_ASSERT(manager.update() == state_update_result::ok, "leaving the choice should remove highlight");
        TEST_ASSERT(fixture.pixels() == plain, "unhighlighting must restore the original glyph colors");
        mouseSetPosition(MOUSE_PORT_1, x, y);
        mouseSetButton(MOUSE_PORT_1, MOUSE_LMB, MOUSE_ACTIVE);
        TEST_ASSERT(manager.update() == state_update_result::ok, "click should select without a keyboard hold");
        TEST_ASSERT(g_pEngine->session().is_choice_taken(choice), "mouse selection must take the hovered choice immediately");
        TEST_ASSERT(progress.colors_restored(), "the fixture's speakerless destination must release the portrait and text colors");
        manager.clear();
        TEST_ASSERT(fixture.restored(), "mouse navigation must leave no dialogue pixels");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_dialogue_speaker_change_publishes_and_restores_portrait_colors)
    {
        overlay_fixture fixture;
        dialogue_progress_fixture progress;
        u16 const id = progress.dialogue_id();
        TEST_ASSERT(id != data::NO_ID, "the pak must contain a readable dialogue");
        auto vm = fixture.make_vm();
        state_manager manager;
        manager.start<dialogue_state>(*g_pEngine, vm, fixture._overlays, id);
        TEST_ASSERT(manager.update() == state_update_result::ok, "the dialogue should enter");
        u16 page_id = g_pEngine->data().dialogues()[id].first_page_id;
        u16 previous_speaker = data::NO_ID;
        for (u8 visit = 0; visit < 2; ++visit)
        {
            auto const& page = g_pEngine->data().pages()[page_id];
            TEST_ASSERT(!page.choice_range.count && page.speaker_id != previous_speaker,
                "the fixture needs two plain pages with different speakers");
            auto const speakers = g_pEngine->data().speakers();
            TEST_ASSERT(page.speaker_id < speakers.size(), "the fixture speaker must exist");
            auto const& speaker = speakers[page.speaker_id];
            TEST_ASSERT(speaker.portrait_bitmap_id < g_pEngine->data().bitmaps().size(),
                "the fixture speaker must name a portrait");
            ULONG expected[PORTRAIT_PALETTE_COLORS]{};
            {
                mtl::system_use_guard guard;
                auto const hash = g_pEngine->data().bitmaps()[speaker.portrait_bitmap_id].palette_hash;
                auto file = g_pEngine->assets().open(hash);
                TEST_ASSERT(file, "the fixture portrait palette must load");
                ace::paletteLoadFromFd(mtl::move(file), force_to<UWORD*>(expected), PORTRAIT_PALETTE_COLORS);
            }
            auto const* palette = force_to<ULONG const*>(g_pEngine->main_screen().palette());
            TEST_ASSERT(palette[DIALOGUE_TEXT_COLOR] == speaker.dialogue_color >> 8,
                "the page must publish the current speaker's text color");
            TEST_ASSERT(memcmp(palette + PORTRAIT_PALETTE_FIRST, expected, sizeof(expected)) == 0,
                "the page must publish the current portrait's palette band");
            previous_speaker = page.speaker_id;
            if (!visit)
            {
                page_id = page.next_page_id;
                TEST_ASSERT(page_id < g_pEngine->data().pages().size(), "the fixture must have its second page");
                keySetState(KEY_SPACE, KEY_ACTIVE);
                TEST_ASSERT(manager.update() == state_update_result::ok, "space should change speaker");
            }
        }
        manager.clear();
        TEST_ASSERT(fixture.restored() && progress.colors_restored(), "clear must restore the original portrait band and pixels");
        TEST_SUCCESS;
    }

    TEST_SUITE_BEGIN(overlay_state)
        TEST(test_pending_message_leaves_slot_and_vm)
        TEST(test_pending_dialogue_leaves_slot_and_vm)
        TEST(test_message_first_wins_and_releases_on_pop)
        TEST(test_dialogue_first_wins_and_releases_on_pop)
        TEST(test_message_pop_preserves_terminal_latch)
        TEST(test_dialogue_pop_preserves_terminal_latch)
        TEST(test_diagnostic_does_not_release_fade)
        TEST(test_empty_say_releases_after_vm_latches)
        TEST(test_diagnostic_then_say_does_not_strand_vm)
        TEST(test_diagnostic_then_dialogue_does_not_strand_vm)
        TEST(test_message_owns_request_after_construction)
        TEST(test_pending_message_survives_slot_reuse)
        TEST(test_message_consumes_opening_press_and_waits)
        TEST(test_empty_message_enters_and_closes_normally)
        TEST(test_clearing_visible_message_restores_and_releases)
        TEST(test_message_layout_and_ink)
        TEST(test_tall_message_stays_inside_scene)
        TEST(test_dialogue_renders_consumes_input_and_restores)
        TEST(test_visible_dialogue_terminal_keeps_diagnostic)
        TEST(test_dialogue_terminal_before_entry_draws_nothing)
        TEST(test_dialogue_page_script_uses_bound_vm)
        TEST(test_dialogue_choice_terminal_abandons_goto)
        TEST(test_dialogue_script_error_stays_queued_through_navigation)
        TEST(test_message_width_boundaries_restore_all_pixels)
        TEST(test_message_without_measured_ink_closes_without_display)
        TEST(test_dialogue_keyboard_hold_takes_only_selected_choice)
        TEST(test_dialogue_rotates_long_choices_without_taking_them)
        TEST(test_dialogue_drawn_choices_survive_string_table_release)
        TEST(test_dialogue_mouse_hover_and_click_select_immediately)
        TEST(test_dialogue_speaker_change_publishes_and_restores_portrait_colors)
    TEST_SUITE_END
}

#endif
#endif
