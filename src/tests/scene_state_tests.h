#ifndef __SCENE_STATE_TESTS_H__INCLUDED__
#define __SCENE_STATE_TESTS_H__INCLUDED__

#ifdef ACE_TEST_RUNNER

#include <string.h>

#include "core/noir_bridge.h"
#include "neonengine.h"
#include "states/debug_view.h"
#include "states/dialogue.h"
#include "states/scene.h"
#include "tests/noir_vm_test_utils.h"
#include "tests/overlay_state_tests.h"

namespace NEONengine
{
    namespace
    {
        struct failed_scene_callbacks
        {
            u16 _destroyed{ 0 };
            u16 _suspended{ 0 };
            u16 _resumed{ 0 };
        };

        class failed_scene_parent : public state
        {
            public:
            failed_scene_parent(engine& engine, failed_scene_callbacks& callbacks, bool replace)
                : _engine(engine), _callbacks(callbacks), _replace(replace) {}

            ~failed_scene_parent() override { ++_callbacks._destroyed; }
            void suspended() override { ++_callbacks._suspended; }
            void resumed() override { ++_callbacks._resumed; }
            void update(state_control& control) override
            {
                if (_replace) control.replace<scene_state>(_engine, data::NO_ID);
                else control.push<scene_state>(_engine, data::NO_ID);
            }

            private:
            engine& _engine;
            failed_scene_callbacks& _callbacks;
            bool _replace;
        };

        u32 scene_test_pixels(tBitMap const* bitmap)
        {
            // Hash the completed image, not pixels still being drawn.
            blitWait();
            u32 hash = 5381;
            u32 const size = to<u32>(bitmap->BytesPerRow) * bitmap->Rows;
            for (u32 i = 0; i < size; ++i) hash = (hash * 33) ^ bitmap->Planes[0][i];
            return hash;
        }

        struct scene_observation
        {
            scene_state* _scene{ nullptr };
            u16 _suspended{ 0 };
            u16 _resumed{ 0 };
            u16 _destroyed{ 0 };
            u16 _updates{ 0 };
            ULONG _chip_on_destroy{ 0 };
            u32 _pixels{ 0 };
            ULONG _colors[256]{};
            bool _buffers_ready{ false };
            bool _restored_on_resume{ false };
            bool _restored_on_destroy{ false };
            bool _terminal_on_destroy{ false };

            bool restored() const
            {
                auto& screen = g_pEngine->main_screen();
                return &g_pEngine->scene() == _scene
                    && screen.front_buffer() != screen.back_buffer()
                    && scene_test_pixels(screen.front_buffer()) == _pixels
                    && memcmp(_colors, screen.palette(), sizeof(_colors)) == 0;
            }
        };

        class observed_scene : public scene_state
        {
            public:
            observed_scene(engine& engine, u16 id, scene_observation& observation)
                : scene_state(engine, id), _observation(observation) {}

            mtl::expected<void, char const*> pushed() override
            {
                auto result = scene_state::pushed();
                if (result) _observation._scene = &g_pEngine->scene();
                return result;
            }

            void update(state_control& control) override
            {
                ++_observation._updates;
                scene_state::update(control);
            }

            void suspended() override
            {
                scene_state::suspended();
                ++_observation._suspended;
                auto& screen = g_pEngine->main_screen();
                _observation._pixels = scene_test_pixels(screen.front_buffer());
                _observation._buffers_ready = _observation._pixels == scene_test_pixels(screen.back_buffer());
                memcpy(_observation._colors, screen.palette(), sizeof(_observation._colors));
            }

            void resumed() override
            {
                ++_observation._resumed;
                _observation._restored_on_resume = _observation.restored();
                scene_state::resumed();
            }

            ~observed_scene() override
            {
                ++_observation._destroyed;
                _observation._chip_on_destroy = memGetFreeChipSize();
                if (_observation._scene && _observation._suspended)
                    _observation._restored_on_destroy = _observation.restored();
                _observation._terminal_on_destroy = g_pEngine->vm().terminated();
            }

            private:
            scene_observation& _observation;
        };

        struct loaded_scene_fixture
        {
            overlay_fixture _input;
            dialogue_progress_fixture _progress;
            noir_vm _saved_vm{ mtl::move(g_pEngine->vm()) };
            ace::bitmap_ptr _report{ nullptr };
            u16 _scene_id{ g_pEngine->current_scene() };
            u8 _f1{ g_sKeyManager.pStates[KEY_F1] };

            loaded_scene_fixture(u16 dialogue, u16 destination)
            {
                mtl::system_use_guard guard;
                auto& screen = g_pEngine->main_screen();
                auto* source = screen.back_buffer();
                _report = ace::bitmapCreate(SCREEN_WIDTH, source->Rows, source->Depth, BMF_INTERLEAVED);
                if (_report)
                    blitCopy(source, 0, 0, _report.get(), 0, 0, SCREEN_WIDTH, source->Rows, MINTERM_COPY);
                keySetState(KEY_F1, KEY_NACTIVE);
                auto& code = _input._code;
                for (u16 id = 0; id < bytecode_builder::MAX_FUNCS; ++id)
                {
                    code.begin_func();
                    if (id < 2)
                    {
                        code.emit(noir_op::MOV16);
                        code.emit_mode(noir_mode::REG_D0, noir_mode::IMM16);
                        code.emit_u16(id ? destination : dialogue);
                        code.emit(id ? noir_op::GOTO_SCENE : noir_op::SHOW_DIALOG);
                    }
                    code.emit(noir_op::RET);
                }
                g_pEngine->vm() = noir_vm(code.flags, code.values, code.code, code.pos,
                    code.toc, code.func_count, &engine_bridge(), g_pEngine.get(), 256);
            }

            ~loaded_scene_fixture()
            {
                auto& engine = *g_pEngine;
                auto& screen = engine.main_screen();
                engine.vm().reset_execution();
                engine.take_pending_modal();
                engine.take_pending_system();
                engine.clear_state_request();
                engine.set_current_scene(_scene_id);
                if (_report)
                {
                    blitCopy(_report.get(), 0, 0, screen.back_buffer(), 0, 0,
                        SCREEN_WIDTH, _report->Rows, MINTERM_COPY);
                    screen.duplicate_back_to_front();
                }
                // Finish restoring pixels before changing their displayed colors.
                blitWait();
                memcpy(screen.palette(), _progress._colors, sizeof(_progress._colors));
                screen.publish_palette(0, 256);
                screen.set_cursor_band_exempt(true);
                screen.bind_mouse();
                engine.pointer().set_visible(true);
                engine.pointer().switch_to(pointer_role::DEFAULT);
                screen.fade_from_black(1, 0, nullptr);
                screen.process();
                keySetState(KEY_F1, _f1);
                mtl::system_use_guard guard;
                engine.vm() = mtl::move(_saved_vm);
                _report.reset(nullptr);
            }

            static u16 scene_id(bool text = false)
            {
                auto const scenes = g_pEngine->data().scenes();
                for (u16 id = 0; id < scenes.size(); ++id)
                {
                    auto const& scene = scenes[id];
                    if (scene.on_enter_function_id != data::NO_ID
                        || !(scene.flags & data::SCENE_FLAG_SHOW_UI)) continue;
                    if (text && scene.text_region_range.count && scene.music_hash == 0xFFFFFFFE) return id;
                    if (!text && scene.graphic_instance_range.count && scene.music_hash == 0xFFFFFFFF) return id;
                }
                return data::NO_ID;
            }

            static u16 empty_scene_id()
            {
                auto const scenes = g_pEngine->data().scenes();
                for (u16 id = 0; id < scenes.size(); ++id)
                {
                    auto const& scene = scenes[id];
                    if (scene.music_hash == 0xFFFFFFFF && scene.background_bitmap_id == data::NO_ID
                        && scene.timeline_id == data::NO_ID && scene.on_enter_function_id == data::NO_ID
                        && !scene.graphic_instance_range.count && !scene.hotspot_range.count
                        && !scene.text_region_range.count) return id;
                }
                return data::NO_ID;
            }

            state_update_result tick(state_manager& manager)
            {
                auto const result = g_pEngine->update_states(manager);
                if (result == state_update_result::ok) g_pEngine->main_screen().process();
                return result;
            }

            char const* load(state_manager& manager, scene_observation& observation, u16 scene)
            {
                TEST_ASSERT(_report, "the test report snapshot must allocate");
                TEST_ASSERT(scene != data::NO_ID, "the pak must contain the requested scriptless scene fixture");
                manager.start<observed_scene>(*g_pEngine, scene, observation);
                TEST_ASSERT(tick(manager) == state_update_result::ok, "the production scene must load");
                TEST_ASSERT(&g_pEngine->scene() == observation._scene, "entry must bind the scene state");
                TEST_SUCCESS;
            }

            char const* open(state_manager& manager, scene_observation& observation, u16 scene)
            {
                auto const fault = load(manager, observation, scene);
                TEST_ASSERT(!fault, fault);
                TEST_ASSERT(g_pEngine->vm().execute(0), "SHOW_DIALOG should start on the scene VM");
                TEST_ASSERT(tick(manager) == state_update_result::ok, "the scene should queue its dialogue");
                TEST_ASSERT(g_pEngine->vm().blocked(), "SHOW_DIALOG must latch before the overlay enters");
                TEST_ASSERT(tick(manager) == state_update_result::ok, "the dialogue should enter above the scene");
                TEST_ASSERT(observation._suspended == 1 && observation._buffers_ready,
                    "scene suspension must paint the displayed bob pose into the restoration buffer");
                TEST_ASSERT(g_pEngine->main_screen().front_buffer() == g_pEngine->main_screen().back_buffer(),
                    "the production dialogue must freeze the display");
                TEST_ASSERT(&g_pEngine->scene() == observation._scene, "the suspended scene must remain registered");
                TEST_SUCCESS;
            }
        };

        struct mixed_scene_fixture
        {
            // The loaded image is mutable storage exposed through read-only views.
            // Borrow the authored flags for TextTest without changing the pak.
            data::scene& _scene;
            data::scene _saved;

            mixed_scene_fixture(u16 text_scene, u16 graphic_scene)
                : _scene(const_cast<data::scene&>(g_pEngine->data().scenes()[text_scene])),
                  _saved(_scene)
            {
                auto const& graphics = g_pEngine->data().scenes()[graphic_scene];
                _scene.graphic_range = graphics.graphic_range;
                _scene.graphic_instance_range = graphics.graphic_instance_range;
            }

            ~mixed_scene_fixture() { _scene = _saved; }
        };

        char const* rotate_to_dialogue_end(state_manager& manager, u16 dialogue_id)
        {
            u16 page;
            auto const fault = dialogue_to_choices(manager, dialogue_id, page);
            TEST_ASSERT(!fault, fault);
            auto const range = g_pEngine->data().pages()[page].choice_range;
            for (u16 id = range.start; id < range.end(); ++id)
            {
                auto const& choice = g_pEngine->data().choices()[id];
                if (!(choice.flags & data::CHOICE_FLAG_ENABLED)) continue;
                if (choice.check_flag_id != data::NO_ID
                    && !g_pEngine->session().get_flags().test(choice.check_flag_id)) continue;
                if (g_pEngine->strings().get_string(choice.text_string_id).is_empty()) continue;
                if (choice.goto_page_id == data::NO_ID) return nullptr;
                keySetState(KEY_2, KEY_ACTIVE);
                for (u8 frame = 0; frame <= DIALOGUE_CHOICE_HOLD_FRAMES; ++frame)
                    TEST_ASSERT(manager.update() == state_update_result::ok, "the long-choice set should rotate");
            }
            return "the authored choice page needs an available exit choice";
        }
    }

    TEST_IMPL(test_pending_scene_and_debug_leave_active_scene)
    {
        loaded_scene_fixture fixture(data::NO_ID, data::NO_ID);
        scene_observation observation;
        state_manager active;
        auto const fault = fixture.load(active, observation, loaded_scene_fixture::scene_id());
        TEST_ASSERT(!fault, fault);
        auto& engine = *g_pEngine;
        u16 const scene_id = engine.current_scene();
        auto* front = engine.main_screen().front_buffer();
        auto* back = engine.main_screen().back_buffer();
        ULONG const chip_free = memGetFreeChipSize();
        u32 const pixels = scene_test_pixels(front);
        ULONG colors[256];
        memcpy(colors, engine.main_screen().palette(), sizeof(colors));
        TEST_ASSERT(engine.vm().execute(0), "an authored function should queue without executing");
        engine.request_system(system_kind::load_scene, 17);
        engine.request_modal(modal_kind::fade_out, 37);
        engine.request_message(engine.vm(), message_request{ .text = "leave pending" });
        {
            state_manager pending;
            pending.start<scene_state>(engine, scene_id);
            pending.clear();
        }
        {
            state_manager pending;
            pending.start<debug_view_state>(engine);
            pending.clear();
        }
        TEST_ASSERT(&engine.scene() == observation._scene, "unentered states must leave the active scene bound");
        TEST_ASSERT(memGetFreeChipSize() == chip_free, "pending cancellation must not release active bob buffers");
        TEST_ASSERT(scene_test_pixels(front) == pixels
            && memcmp(colors, engine.main_screen().palette(), sizeof(colors)) == 0,
            "unloaded helpers must not change the active pixels or palette");
        TEST_ASSERT(engine.current_scene() == scene_id, "an unentered scene must not change the scene id");
        TEST_ASSERT(engine.vm().session_count() == 1, "unentered states must not reset the active VM");
        TEST_ASSERT(engine.has_pending_system() && engine.has_pending_modal(), "unentered states must leave requests intact");
        TEST_ASSERT(engine.take_pending_system().arg1 == 17, "the scene request payload must survive");
        TEST_ASSERT(engine.take_pending_modal().arg1 == 37, "the fade request payload must survive");
        TEST_ASSERT(engine.has_pending_state(), "unentered states must not clear the request");
        TEST_ASSERT(engine.main_screen().front_buffer() == front && engine.main_screen().back_buffer() == back,
            "unentered debug state must not freeze or resume the display");
        engine.scene().bobs().draw();
        TEST_SUCCESS;
    }

    TEST_IMPL(test_invalid_scene_entry_leaves_active_scene)
    {
        loaded_scene_fixture fixture(data::NO_ID, data::NO_ID);
        scene_observation observation;
        state_manager active;
        auto const fault = fixture.load(active, observation, loaded_scene_fixture::scene_id());
        TEST_ASSERT(!fault, fault);
        auto& engine = *g_pEngine;
        ULONG const chip_free = memGetFreeChipSize();
        u16 const scene_id = engine.current_scene();
        u32 const pixels = scene_test_pixels(engine.main_screen().front_buffer());
        ULONG colors[256];
        memcpy(colors, engine.main_screen().palette(), sizeof(colors));
        TEST_ASSERT(engine.vm().execute(0), "an authored function should queue");
        engine.request_modal(modal_kind::fade_out, 11);
        engine.request_system(system_kind::load_scene, 23);
        engine.request_modal_state<dialogue_state>(engine.vm(), u16(9));
        state_manager manager;
        manager.start<scene_state>(engine, data::NO_ID);
        TEST_ASSERT(manager.update() == state_update_result::error, "invalid scene entry must be fatal");
        TEST_ASSERT(strcmp(manager.error(), "Invalid scene id") == 0, "the manager should retain the scene's error");
        TEST_ASSERT(&engine.scene() == observation._scene, "failed entry must not unbind a scene it does not own");
        TEST_ASSERT(engine.current_scene() == scene_id && memGetFreeChipSize() == chip_free,
            "failed entry must leave the active scene id and bob buffers intact");
        TEST_ASSERT(scene_test_pixels(engine.main_screen().front_buffer()) == pixels
            && memcmp(colors, engine.main_screen().palette(), sizeof(colors)) == 0,
            "failed entry must not change the active pixels or palette");
        TEST_ASSERT(engine.vm().session_count() == 1, "failed unowned entry must not reset another VM session");
        TEST_ASSERT(engine.has_pending_modal() && engine.has_pending_system(), "failed unowned entry must not consume requests");
        TEST_ASSERT(engine.has_pending_state(), "failed unowned entry must not consume the request");
        manager.clear();
        TEST_ASSERT(manager.update() == state_update_result::error, "clear must preserve the fatal result");
        engine.scene().bobs().draw();
        TEST_SUCCESS;
    }

    TEST_IMPL(test_failed_scene_push_never_resumes_parent)
    {
        failed_scene_callbacks callbacks;
        state_manager manager;
        manager.start<failed_scene_parent>(*g_pEngine, callbacks, false);
        TEST_ASSERT(manager.update() == state_update_result::ok, "the parent should queue the real scene");
        TEST_ASSERT(callbacks._suspended == 0, "request time must not suspend the parent");
        TEST_ASSERT(manager.update() == state_update_result::error, "the real scene should reject its invalid id");
        TEST_ASSERT(strcmp(manager.error(), "Invalid scene id") == 0, "the real scene error should survive destruction");
        TEST_ASSERT(callbacks._suspended == 1 && callbacks._resumed == 0, "failed scene entry must not resume its parent");
        manager.clear();
        TEST_ASSERT(callbacks._destroyed == 1 && callbacks._resumed == 0, "fatal cleanup should destroy without resuming");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_failed_scene_replacement_preserves_fatal_result)
    {
        failed_scene_callbacks callbacks;
        state_manager manager;
        manager.start<failed_scene_parent>(*g_pEngine, callbacks, true);
        TEST_ASSERT(manager.update() == state_update_result::ok, "replacement construction should be deferred from entry");
        TEST_ASSERT(callbacks._destroyed == 0, "request time must leave the outgoing state alive");
        TEST_ASSERT(manager.update() == state_update_result::error, "the real replacement should fail at entry");
        TEST_ASSERT(callbacks._destroyed == 1, "replacement should destroy the outgoing state");
        TEST_ASSERT(strcmp(manager.error(), "Invalid scene id") == 0, "the manager should retain the real scene error");
        TEST_ASSERT(callbacks._suspended == 0 && callbacks._resumed == 0, "replacement failure must not resume anything");
        manager.clear();
        TEST_ASSERT(manager.update() == state_update_result::error, "clear must not turn failure into success");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_bridge_uses_suspended_scene)
    {
        dialogue_progress_fixture progress;
        u16 const dialogue_id = progress.dialogue_id();
        TEST_ASSERT(dialogue_id != data::NO_ID, "the pak must contain a readable dialogue");
        loaded_scene_fixture fixture(dialogue_id, data::NO_ID);
        scene_observation observation;
        auto& engine = *g_pEngine;
        state_manager manager;
        auto const fault = fixture.open(manager, observation, loaded_scene_fixture::scene_id());
        TEST_ASSERT(!fault, fault);

        bytecode_builder code;
        u16 const script = code.begin_func();
        code.emit(noir_op::HOTSPOT_ON);
        code.emit(noir_op::ANIM_STOP);
        code.emit(noir_op::GET_SCENE);
        code.emit(noir_op::RET);
        noir_vm vm(code.flags, code.values, code.code, code.pos, code.toc, code.func_count,
            &engine_bridge(), &engine, 256);
        auto const result = vm.execute_now(script);
        TEST_ASSERT(result && to<u16>(result.value()) == engine.current_scene(),
            "real bridge calls must reach the borrowed scene while an overlay is entered");
        TEST_ASSERT(&engine.scene() == observation._scene && observation._suspended == 1,
            "bridge calls must keep the suspended scene bound");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_clearing_pending_scene_replacement_keeps_outgoing_resources)
    {
        loaded_scene_fixture fixture(data::NO_ID, data::NO_ID);
        scene_observation observation;
        state_manager manager;
        u16 const scene_id = loaded_scene_fixture::scene_id();
        auto const fault = fixture.load(manager, observation, scene_id);
        TEST_ASSERT(!fault, fault);
        ULONG const chip_free = memGetFreeChipSize();
        g_pEngine->request_system(system_kind::load_scene, scene_id);
        TEST_ASSERT(g_pEngine->has_pending_system(), "the engine should retain the replacement request");
        TEST_ASSERT(&g_pEngine->scene() == observation._scene && observation._destroyed == 0,
            "a pending replacement must leave the outgoing scene bound and alive");
        TEST_ASSERT(memGetFreeChipSize() == chip_free, "requesting replacement must not acquire scene resources");
        manager.clear();
        TEST_ASSERT(observation._destroyed == 1 && observation._chip_on_destroy == chip_free,
            "clear must discard the request without freeing outgoing bob buffers before scene destruction");
        TEST_ASSERT(!g_pEngine->vm().session_count(), "outgoing scene cleanup must reset its VM");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_scene_applies_dialogue_terminal_before_diagnostic)
    {
        dialogue_progress_fixture progress;
        u16 const dialogue_id = progress.dialogue_id();
        u16 const destination = loaded_scene_fixture::empty_scene_id();
        TEST_ASSERT(dialogue_id != data::NO_ID && destination != data::NO_ID,
            "the pak must contain a readable dialogue and an empty replacement scene");
        loaded_scene_fixture fixture(dialogue_id, destination);
        scene_observation observation;
        state_manager manager;
        auto const fault = fixture.open(manager, observation, loaded_scene_fixture::scene_id());
        TEST_ASSERT(!fault, fault);
        g_pEngine->request_message(g_pEngine->vm(), message_request{ .text = "queued during dialogue" });

        bound_dialogue_fixture script;
        dialogue_context context{ g_pEngine->session(), g_pEngine->vm() };
        script.bind(context);
        script._pages[0].display_function_id = 1;
        TEST_ASSERT(!script._session.begin(0), "a real bound display script must end after GOTO_SCENE");
        TEST_ASSERT(g_pEngine->vm().terminated()
            && g_pEngine->pending_system_kind() == system_kind::load_scene,
            "the engine VM and real engine bridge must retain the terminal scene request");
        u16 const updates = observation._updates;
        TEST_ASSERT(fixture.tick(manager) == state_update_result::ok, "the visible dialogue should request pop");
        TEST_ASSERT(observation._resumed == 0 && &g_pEngine->scene() == observation._scene,
            "the scene must remain suspended and alive until dialogue cleanup");
        TEST_ASSERT(fixture.tick(manager) == state_update_result::ok, "pop should restore and resume the real scene");
        TEST_ASSERT(observation._resumed == 1 && observation._restored_on_resume,
            "dialogue cleanup must restore pixels and palette before scene resume");
        TEST_ASSERT(observation._updates == updates,
            "engine arbitration must not run resumed scene gameplay before replacement");
        TEST_ASSERT(g_pEngine->has_pending_system() && g_pEngine->vm().terminated(),
            "the engine must retain the request after applying the dialogue pop");
        TEST_ASSERT(g_pEngine->has_pending_state() && observation._suspended == 1,
            "the queued diagnostic must not win over scene replacement");
        TEST_ASSERT(fixture.tick(manager) == state_update_result::ok, "the real replacement should load");
        TEST_ASSERT(observation._destroyed == 1 && observation._terminal_on_destroy,
            "the old scene must own the terminated VM until destruction");
        TEST_ASSERT(g_pEngine->current_scene() == destination && !g_pEngine->vm().terminated(),
            "the replacement must bind its scene and reset old execution");
        TEST_ASSERT(!g_pEngine->has_pending_state(),
            "old scene cleanup must discard its diagnostic rather than display it in the new scene");
        manager.clear();
        TEST_SUCCESS;
    }

    TEST_IMPL(test_scene_dialogue_diagnostic_restores_between_overlays)
    {
        dialogue_progress_fixture progress;
        u16 const dialogue_id = progress.dialogue_id();
        TEST_ASSERT(dialogue_id != data::NO_ID, "the pak must contain a readable dialogue");
        loaded_scene_fixture fixture(dialogue_id, data::NO_ID);
        dialogue_strings_fixture strings;
        TEST_ASSERT(strings.long_choices(), "the long-choice table must load");
        scene_observation observation;
        state_manager manager;
        auto const fault = fixture.open(manager, observation, loaded_scene_fixture::scene_id());
        TEST_ASSERT(!fault, fault);
        g_pEngine->request_message(g_pEngine->vm(), message_request{ .text = "after the dialogue" });
        TEST_ASSERT(fixture.tick(manager) == state_update_result::ok, "idle dialogue should leave the diagnostic queued");
        TEST_ASSERT(g_pEngine->has_pending_state() && observation._suspended == 1,
            "a visible dialogue must not present a nested message");
        auto const navigation = rotate_to_dialogue_end(manager, dialogue_id);
        TEST_ASSERT(!navigation, navigation);
        keySetState(KEY_1, KEY_ACTIVE);
        for (u8 frame = 0; frame <= DIALOGUE_CHOICE_HOLD_FRAMES; ++frame)
            TEST_ASSERT(manager.update() == state_update_result::ok, "the exit choice should request pop after its hold");
        TEST_ASSERT(g_pEngine->vm().blocked(), "the pop request must retain the modal wait");
        TEST_ASSERT(fixture.tick(manager) == state_update_result::ok, "pop should restore the scene and queue its message");
        TEST_ASSERT(observation._resumed == 1 && observation._restored_on_resume,
            "the scene must resume once after full dialogue restoration");
        TEST_ASSERT(!g_pEngine->vm().blocked() && observation._suspended == 1,
            "the modal should release before the diagnostic enters on the next update");
        TEST_ASSERT(fixture.tick(manager) == state_update_result::ok, "the diagnostic should now enter");
        TEST_ASSERT(observation._suspended == 2 && observation._buffers_ready,
            "the second overlay must prepare the real scene buffers again");
        TEST_ASSERT(!g_pEngine->has_pending_state(),
            "only diagnostic entry should consume its slot");
        TEST_ASSERT(fixture.tick(manager) == state_update_result::ok,
            "the consumed dialogue key must not dismiss the new diagnostic");
        mouseSetButton(MOUSE_PORT_1, MOUSE_LMB, MOUSE_ACTIVE);
        TEST_ASSERT(fixture.tick(manager) == state_update_result::ok, "a new click should request message dismissal");
        TEST_ASSERT(observation._resumed == 1, "requesting pop must not resume the scene early");
        TEST_ASSERT(fixture.tick(manager) == state_update_result::ok, "message destruction should resume the scene");
        TEST_ASSERT(observation._resumed == 2 && observation._restored_on_resume,
            "each overlay must restore before exactly one scene resume");
        TEST_ASSERT(!mouseUse(MOUSE_PORT_1, MOUSE_LMB), "the dismissal click must not reach resumed hotspots");
        manager.clear();
        TEST_SUCCESS;
    }

    TEST_IMPL(test_clearing_scene_with_dialogue_restores_before_scene_destroy)
    {
        dialogue_progress_fixture progress;
        u16 const dialogue_id = progress.dialogue_id();
        TEST_ASSERT(dialogue_id != data::NO_ID, "the pak must contain a readable dialogue");
        loaded_scene_fixture fixture(dialogue_id, data::NO_ID);
        scene_observation observation;
        state_manager manager;
        auto const fault = fixture.open(manager, observation, loaded_scene_fixture::scene_id());
        TEST_ASSERT(!fault, fault);
        manager.clear();
        TEST_ASSERT(observation._destroyed == 1 && observation._resumed == 0,
            "clear must destroy the scene without resuming gameplay");
        TEST_ASSERT(observation._restored_on_destroy,
            "dialogue cleanup must restore pixels and palette while the scene remains registered");
        TEST_ASSERT(!g_pEngine->vm().blocked() && !g_pEngine->vm().session_count(),
            "scene destruction must reset execution after the overlay releases its wait");
        scene_observation next_observation;
        state_manager next;
        auto const next_fault = fixture.load(next, next_observation, loaded_scene_fixture::scene_id());
        TEST_ASSERT(!next_fault, next_fault);
        TEST_ASSERT(&g_pEngine->scene() == next_observation._scene,
            "scene destruction must allow a new scene and bob registration");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_scene_reconciles_language_after_dialogue_pop)
    {
        dialogue_progress_fixture progress;
        u16 const dialogue_id = progress.dialogue_id();
        TEST_ASSERT(dialogue_id != data::NO_ID, "the pak must contain a readable dialogue");
        loaded_scene_fixture fixture(dialogue_id, data::NO_ID);
        dialogue_strings_fixture strings;
        TEST_ASSERT(strings.long_choices(), "the long-choice table must load");
        scene_observation observation;
        state_manager manager;
        auto const fault = fixture.open(manager, observation, loaded_scene_fixture::scene_id(true));
        TEST_ASSERT(!fault, fault);
        auto const navigation = rotate_to_dialogue_end(manager, dialogue_id);
        TEST_ASSERT(!navigation, navigation);
        u32 const dialogue_pixels = scene_test_pixels(g_pEngine->main_screen().front_buffer());
        language const next = g_pEngine->current_language() == language::IT ? language::EN : language::IT;
        TEST_ASSERT(strings.replace(next), "the alternate shipped language must load");
        TEST_ASSERT(manager.update() == state_update_result::ok, "the dialogue should remain on its drawn choice set");
        TEST_ASSERT(scene_test_pixels(g_pEngine->main_screen().front_buffer()) == dialogue_pixels,
            "a language change must not disturb the already drawn dialogue");
        keySetState(KEY_1, KEY_ACTIVE);
        for (u8 frame = 0; frame <= DIALOGUE_CHOICE_HOLD_FRAMES; ++frame)
            TEST_ASSERT(manager.update() == state_update_result::ok, "the old drawn exit choice must still work");
        TEST_ASSERT(fixture.tick(manager) == state_update_result::ok, "the scene should resume and reconcile language");
        TEST_ASSERT(observation._restored_on_resume && observation._resumed == 1,
            "the old backdrop must restore before the scene rebuilds it");
        u32 const translated = scene_test_pixels(g_pEngine->main_screen().front_buffer());
        TEST_ASSERT(translated != observation._pixels,
            "the real scene must repaint localized text after the suspended table swap");
        TEST_ASSERT(fixture.tick(manager) == state_update_result::ok, "the reconciled scene should keep running");
        TEST_ASSERT(scene_test_pixels(g_pEngine->main_screen().front_buffer()) == translated,
            "both buffers must retain the translated scene after the next flip");
        manager.clear();
        TEST_SUCCESS;
    }

    TEST_IMPL(test_scene_defers_language_during_fade_out_then_rebuilds_black)
    {
        loaded_scene_fixture fixture(data::NO_ID, data::NO_ID);
        dialogue_strings_fixture strings;
        scene_observation observation;
        state_manager manager;
        auto const fault = fixture.load(manager, observation, loaded_scene_fixture::scene_id(true));
        TEST_ASSERT(!fault, fault);
        auto& engine = *g_pEngine;
        auto& screen = engine.main_screen();
        u32 const original = scene_test_pixels(screen.front_buffer());
        language const next = engine.current_language() == language::IT ? language::EN : language::IT;
        TEST_ASSERT(strings.replace(next), "the alternate shipped language must load");

        screen.fade_to_black(2, 0, nullptr);
        TEST_ASSERT(manager.update() == state_update_result::ok, "the fading scene should keep updating");
        TEST_ASSERT(scene_test_pixels(screen.front_buffer()) == original
            && scene_test_pixels(screen.back_buffer()) == original,
            "neither scheduling point should repaint while fading out");
        screen.process();
        screen.process();
        TEST_ASSERT(screen.palette_ratio() == 0 && !screen.fade_owns_palette(),
            "the fade must finish at black before the next scene update");

        TEST_ASSERT(manager.update() == state_update_result::ok, "the scene should rebuild after scripts at black");
        u32 const translated = scene_test_pixels(screen.front_buffer());
        TEST_ASSERT(translated != original && scene_test_pixels(screen.back_buffer()) == translated,
            "a scene that stays must repaint both buffers without waiting for fade-in");
        TEST_ASSERT(screen.palette_ratio() == 0, "rebuilding must not start a fade");
        TEST_ASSERT(fixture.tick(manager) == state_update_result::ok, "the translated scene should keep updating");
        TEST_ASSERT(scene_test_pixels(screen.front_buffer()) == translated,
            "the next buffer flip must retain the translated backdrop");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_scene_black_terminal_skips_language_rebuild)
    {
        u16 const destination = loaded_scene_fixture::empty_scene_id();
        TEST_ASSERT(destination != data::NO_ID, "the pak must contain an empty replacement scene");
        loaded_scene_fixture fixture(data::NO_ID, destination);
        dialogue_strings_fixture strings;
        scene_observation observation;
        state_manager manager;
        auto const fault = fixture.load(manager, observation, loaded_scene_fixture::scene_id(true));
        TEST_ASSERT(!fault, fault);
        auto& engine = *g_pEngine;
        auto& screen = engine.main_screen();
        screen.fade_to_black(1, 0, nullptr);
        screen.process();
        TEST_ASSERT(screen.palette_ratio() == 0 && !screen.fade_owns_palette(), "the scene should be black");
        u32 const original = scene_test_pixels(screen.front_buffer());
        language const next = engine.current_language() == language::IT ? language::EN : language::IT;
        TEST_ASSERT(strings.replace(next), "the alternate shipped language must load");
        TEST_ASSERT(engine.vm().execute(1), "the scene-changing script should queue");

        TEST_ASSERT(engine.update_states(manager) == state_update_result::ok, "the VM should request scene replacement");
        TEST_ASSERT(engine.vm().terminated() && engine.has_pending_system(),
            "the scene must leave the VM's terminal request for engine selection");
        TEST_ASSERT(&engine.scene() == observation._scene && observation._destroyed == 0,
            "the outgoing scene must stay alive until the replacement enters");
        TEST_ASSERT(scene_test_pixels(screen.front_buffer()) == original
            && scene_test_pixels(screen.back_buffer()) == original,
            "neither scheduling point should rebuild the outgoing scene");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_scene_rebuilds_language_before_overlay_at_fade_in_start)
    {
        loaded_scene_fixture fixture(data::NO_ID, data::NO_ID);
        dialogue_strings_fixture strings;
        scene_observation observation;
        state_manager manager;
        auto const fault = fixture.load(manager, observation, loaded_scene_fixture::scene_id(true));
        TEST_ASSERT(!fault, fault);
        auto& engine = *g_pEngine;
        auto& screen = engine.main_screen();
        screen.fade_to_black(1, 0, nullptr);
        screen.process();
        u32 const original = scene_test_pixels(screen.front_buffer());
        language const next = engine.current_language() == language::IT ? language::EN : language::IT;
        TEST_ASSERT(strings.replace(next), "the alternate shipped language must load");
        screen.fade_from_black(2, 0, nullptr);
        TEST_ASSERT(screen.palette_ratio() == 0 && screen.fade_owns_palette(),
            "fade-in should own the palette while still black");
        engine.request_message(engine.vm(), message_request{ .text = "after language rebuild" });

        TEST_ASSERT(engine.update_states(manager) == state_update_result::ok, "the engine should enter the message");
        TEST_ASSERT(observation._pixels != original && observation._buffers_ready,
            "suspension must reconcile language before saving the message background");
        TEST_ASSERT(observation._suspended == 1 && !engine.has_pending_state(),
            "the engine should consume the request once after scene preparation");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_language_rebuild_before_message_preserves_bob_undraw)
    {
        loaded_scene_fixture fixture(data::NO_ID, data::NO_ID);
        dialogue_strings_fixture strings;
        u16 const text_scene = loaded_scene_fixture::scene_id(true);
        u16 const graphic_scene = loaded_scene_fixture::scene_id();
        TEST_ASSERT(text_scene != data::NO_ID && graphic_scene != data::NO_ID,
            "the pak must contain the authored text and graphic scenes");
        mixed_scene_fixture mixed(text_scene, graphic_scene);
        scene_observation observation;
        state_manager manager;
        auto const fault = fixture.load(manager, observation, text_scene);
        TEST_ASSERT(!fault, fault);
        auto& engine = *g_pEngine;
        auto& screen = engine.main_screen();
        auto& bobs = engine.scene().bobs();
        for (auto const& track : engine.data().tracks()) engine.scene().timeline().stop(track.name_id);
        for (u16 id = mixed._scene.hotspot_range.start; id < mixed._scene.hotspot_range.end(); ++id)
            engine.scene().hotspots().set_enabled(id, false);

        auto const instances = mixed._scene.graphic_instance_range.of(engine.data().graphic_instances());
        u16 visible = data::NO_ID;
        for (u16 id = 0; id < instances.size(); ++id)
        {
            bobs.set_instance_visible(id, false);
            auto const& instance = instances[id];
            auto const& graphic = engine.data().graphics()[instance.graphic_id];
            if ((instance.flags & 1) && (graphic.frame_count == 1 || graphic.frame_delay == 0)
                && instance.pos.y + graphic.frame_height <= SCREEN_HEIGHT / 2
                && instance.pos.x + graphic.frame_width < SCREEN_WIDTH)
                visible = id;
        }
        TEST_ASSERT(visible != data::NO_ID, "the fixture needs a static visible bob above the message");
        data::point<u16> position;
        TEST_ASSERT(bobs.instance_pos(visible, position), "the authored bob must load");
        language const original_language = engine.current_language();
        language const next = original_language == language::IT ? language::EN : language::IT;
        TEST_ASSERT(strings.replace(next), "the alternate shipped language must load");
        for (u8 frame = 0; frame < 2; ++frame)
            TEST_ASSERT(fixture.tick(manager) == state_update_result::ok, "draw the translated backdrop alone");
        u32 const backdrop = scene_test_pixels(screen.front_buffer());
        bobs.set_instance_visible(visible, true);
        for (u8 frame = 0; frame < 2; ++frame)
            TEST_ASSERT(fixture.tick(manager) == state_update_result::ok, "draw the translated bob pose normally");
        u32 const translated_pose = scene_test_pixels(screen.front_buffer());
        TEST_ASSERT(translated_pose != backdrop && scene_test_pixels(screen.back_buffer()) == translated_pose,
            "the production bob draw must change pixels in both buffers");

        TEST_ASSERT(strings.replace(original_language), "restore the original language before the request");
        for (u8 frame = 0; frame < 2; ++frame)
            TEST_ASSERT(fixture.tick(manager) == state_update_result::ok, "draw the original language in both buffers");
        TEST_ASSERT(scene_test_pixels(screen.front_buffer()) != translated_pose,
            "the fixture must contain text that changes with language");
        screen.fade_to_black(1, 0, nullptr);
        screen.process();
        TEST_ASSERT(strings.replace(next), "queue translation immediately before the message");
        screen.fade_from_black(2, 0, nullptr);
        TEST_ASSERT(screen.palette_ratio() == 0 && screen.fade_owns_palette(),
            "fade-in must own the still-black palette");
        auto* front = screen.front_buffer();
        auto* back = screen.back_buffer();
        u16 const updates = observation._updates;
        engine.request_message(engine.vm(), message_request{
            .text = "translated scene", .align = message_align::bottom });
        TEST_ASSERT(engine.update_states(manager) == state_update_result::ok, "the message must enter");
        TEST_ASSERT(observation._suspended == 1 && observation._buffers_ready
            && observation._pixels == translated_pose,
            "suspension must retain translated text and the same bob pose in both buffers");
        TEST_ASSERT(observation._updates == updates, "preparing suspension must not advance scene animation");
        TEST_ASSERT(screen.front_buffer() == front && screen.back_buffer() == front,
            "the message must display the prepared front buffer");
        blitWait();
        TEST_ASSERT(memcmp(front->Planes[0], back->Planes[0],
            to<u32>(front->BytesPerRow) * (screen.offset() + SCREEN_HEIGHT / 2)) == 0,
            "the visible bob above the message must match the saved scene");
        TEST_ASSERT(scene_test_pixels(front) != translated_pose, "the message itself must draw");

        keySetState(KEY_SPACE, KEY_ACTIVE);
        TEST_ASSERT(engine.update_states(manager) == state_update_result::ok, "space must request message close");
        TEST_ASSERT(engine.update_states(manager) == state_update_result::ok, "close must restore the scene");
        TEST_ASSERT(observation._resumed == 1 && observation._restored_on_resume
            && scene_test_pixels(front) == translated_pose && scene_test_pixels(back) == translated_pose,
            "message close must restore the translated bob pose before gameplay");
        auto const& graphic = engine.data().graphics()[instances[visible].graphic_id];
        position.x = to<u16>(SCREEN_WIDTH - graphic.frame_width - 16);
        bobs.set_instance_pos(visible, position);
        for (u8 frame = 0; frame < 2; ++frame)
            TEST_ASSERT(fixture.tick(manager) == state_update_result::ok, "move the bob through both buffers");
        TEST_ASSERT(scene_test_pixels(front) == scene_test_pixels(back)
            && scene_test_pixels(front) != translated_pose, "both buffers must show the moved bob");
        bobs.set_instance_visible(visible, false);
        for (u8 frame = 0; frame < 2; ++frame)
            TEST_ASSERT(fixture.tick(manager) == state_update_result::ok, "undraw the moved bob in both buffers");
        TEST_ASSERT(scene_test_pixels(front) == backdrop && scene_test_pixels(back) == backdrop,
            "both undraw records must erase old and moved bob positions without trails");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_hotspot_terminal_skips_vm_update)
    {
        loaded_scene_fixture fixture(data::NO_ID, data::NO_ID);
        scene_observation observation;
        auto& engine = *g_pEngine;
        u16 scene_id = data::NO_ID;
        u16 hotspot_id = data::NO_ID;
        auto const scenes = engine.data().scenes();
        auto const hotspots = engine.data().hotspots();
        for (u16 id = 0; id < scenes.size() && scene_id == data::NO_ID; ++id)
        {
            auto const& scene = scenes[id];
            if (scene.on_enter_function_id != data::NO_ID
                || !(scene.flags & data::SCENE_FLAG_SHOW_UI)
                || (scene.music_hash != 0xFFFFFFFF && scene.music_hash != 0xFFFFFFFE)) continue;
            for (u16 h = scene.hotspot_range.start; h < scene.hotspot_range.end(); ++h)
            {
                auto const& hotspot = hotspots[h];
                if (hotspot.goto_scene_id == data::NO_ID || !hotspot.bounds.width
                    || !hotspot.bounds.height || hotspot.bounds.pos.x >= SCREEN_WIDTH
                    || hotspot.bounds.pos.y >= SCREEN_HEIGHT) continue;
                scene_id = id;
                hotspot_id = h;
                break;
            }
        }
        TEST_ASSERT(hotspot_id != data::NO_ID, "the pak needs a scriptless scene with a scene-changing hotspot");
        state_manager manager;
        auto const fault = fixture.load(manager, observation, scene_id);
        TEST_ASSERT(!fault, fault);
        for (auto const& track : engine.data().tracks()) engine.scene().timeline().stop(track.name_id);
        auto const range = scenes[scene_id].hotspot_range;
        for (u16 h = range.start; h < range.end(); ++h)
            engine.scene().hotspots().set_enabled(h, h == hotspot_id);
        auto const& hotspot = hotspots[hotspot_id];
        mouseSetPosition(MOUSE_PORT_1, hotspot.bounds.pos.x,
            to<u16>(hotspot.bounds.pos.y + engine.main_screen().offset()));
        mouseSetButton(MOUSE_PORT_1, MOUSE_LMB, MOUSE_ACTIVE);
        TEST_ASSERT(engine.update_states(manager) == state_update_result::ok, "the hotspot should observe the press");
        TEST_ASSERT(engine.vm().execute(2), "the probe session should queue before release");
        mouseSetButton(MOUSE_PORT_1, MOUSE_LMB, MOUSE_NACTIVE);
        TEST_ASSERT(engine.update_states(manager) == state_update_result::ok, "release should post the scene request");
        TEST_ASSERT(engine.pending_system_kind() == system_kind::load_scene,
            "the hotspot should leave terminal selection to the engine");
        TEST_ASSERT(engine.vm().session_count() != 0,
            "a hotspot terminal must stop the scene before the queued VM session runs");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_engine_starts_vm_fade_before_display)
    {
        loaded_scene_fixture fixture(data::NO_ID, data::NO_ID);
        scene_observation observation;
        state_manager manager;
        auto const fault = fixture.load(manager, observation, loaded_scene_fixture::scene_id());
        TEST_ASSERT(!fault, fault);
        auto& engine = *g_pEngine;
        auto& code = fixture._input._code;
        code.code[code.toc[2]] = noir_op::FADE_OUT;
        TEST_ASSERT(engine.vm().execute(2), "the scene VM should queue FADE_OUT");
        TEST_ASSERT(engine.update_states(manager) == state_update_result::ok, "the VM frame should return");
        TEST_ASSERT(engine.vm().blocked() && engine.main_screen().is_fading_out(),
            "the engine must start the fade before screen.process");
        engine.request_message(engine.vm(), message_request{ .text = "diagnostic during real fade" });
        TEST_ASSERT(engine.update_states(manager) == state_update_result::ok, "the diagnostic should enter");
        TEST_ASSERT(engine.main_screen().display_frozen(), "the diagnostic should own the saved background");
        keySetState(KEY_SPACE, KEY_ACTIVE);
        TEST_ASSERT(engine.update_states(manager) == state_update_result::ok, "space should request pop");
        TEST_ASSERT(engine.update_states(manager) == state_update_result::ok, "the diagnostic should restore");
        TEST_ASSERT(engine.vm().blocked() && engine.main_screen().is_fading_out(),
            "diagnostic cleanup must not finish a fade that has not processed a frame");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_old_fade_cannot_answer_pending_fade)
    {
        loaded_scene_fixture fixture(data::NO_ID, data::NO_ID);
        scene_observation observation;
        state_manager manager;
        auto const fault = fixture.load(manager, observation, loaded_scene_fixture::scene_id());
        TEST_ASSERT(!fault, fault);
        auto& engine = *g_pEngine;
        auto& code = fixture._input._code;
        code.code[code.toc[2]] = noir_op::FADE_OUT;
        TEST_ASSERT(engine.vm().execute(2), "the first fade should queue");
        engine.vm().update();
        engine.request_modal(modal_kind::fade_out, 1);
        TEST_ASSERT(engine.update_states(manager) == state_update_result::ok, "the first fade should start");
        engine.vm().reset_execution();
        TEST_ASSERT(engine.vm().execute(2), "new execution should request another fade");
        engine.vm().update();
        TEST_ASSERT(engine.has_pending_modal() && engine.vm().blocked(), "the new fade must still await dispatch");
        engine.main_screen().process();
        TEST_ASSERT(engine.vm().blocked() && engine.has_pending_modal(),
            "the active fade's completion must not answer the pending fade's wait");
        TEST_ASSERT(engine.update_states(manager) == state_update_result::ok, "the pending fade should start later");
        TEST_ASSERT(engine.vm().blocked(), "starting the new fade must retain its wait");
        TEST_SUCCESS;
    }

    TEST_SUITE_BEGIN(scene_state)
        TEST(test_old_fade_cannot_answer_pending_fade)
        TEST(test_hotspot_terminal_skips_vm_update)
        TEST(test_engine_starts_vm_fade_before_display)
        TEST(test_pending_scene_and_debug_leave_active_scene)
        TEST(test_invalid_scene_entry_leaves_active_scene)
        TEST(test_failed_scene_push_never_resumes_parent)
        TEST(test_failed_scene_replacement_preserves_fatal_result)
        TEST(test_bridge_uses_suspended_scene)
        TEST(test_clearing_pending_scene_replacement_keeps_outgoing_resources)
        TEST(test_scene_applies_dialogue_terminal_before_diagnostic)
        TEST(test_scene_dialogue_diagnostic_restores_between_overlays)
        TEST(test_clearing_scene_with_dialogue_restores_before_scene_destroy)
        TEST(test_scene_reconciles_language_after_dialogue_pop)
        TEST(test_scene_defers_language_during_fade_out_then_rebuilds_black)
        TEST(test_scene_black_terminal_skips_language_rebuild)
        TEST(test_scene_rebuilds_language_before_overlay_at_fade_in_start)
        TEST(test_language_rebuild_before_message_preserves_bob_undraw)
    TEST_SUITE_END
}

#endif
#endif
