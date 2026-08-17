#include "scene_hotspots.h"

#include "types.h"

#include "neonengine.h"

#include <ace/managers/mouse.h>

#include <mtl/span.h>
#include <mtl/utility.h>
#include <mtl/vector.h>

#include "core/game_data.h"
#include "core/logger.h"
#include "core/mouse_pointer.h"
#include "core/scene_bobs.h"
#include "core/screen.h"

NE_LOG_DOMAIN(Hotspot);

namespace NEONengine
{
    namespace
    {
        constexpr u8 FLAG_ENABLED = 1 << 0;  ///< hotspot.flags bit 0.

        /**
         * @brief True when scene-space @p x, @p y falls inside @p bounds.
         */
        constexpr bool contains(data::rect const& bounds, u16 x, u16 y) noexcept
        {
            return x >= bounds.pos.x && x < bounds.pos.x + bounds.width  //
                && y >= bounds.pos.y && y < bounds.pos.y + bounds.height;
        }
    }  // namespace

    scene_hotspots::~scene_hotspots()
    {
        unload();
    }

    mtl::span<data::hotspot> scene_hotspots::scene_records()
    {
        auto const all = _data.hotspots();
        return mtl::span<data::hotspot>(all.data() + _first, _count);
    }

    hotspot_frame scene_hotspots::state_frame(u16 index)
    {
        if (_enabled[index] == 0) return hotspot_frame::DISABLED;
        // A press only reads as pressed while the pointer is still over the
        // hotspot it latched onto, matching the rule that decides the click.
        if (_pressed == index && _hovered == index) return hotspot_frame::PRESSED;
        if (_hovered == index) return hotspot_frame::HOVERED;
        return hotspot_frame::NORMAL;
    }

    void scene_hotspots::refresh_state_frame(u16 index)
    {
        if (index == NO_HOTSPOT) return;
        _bobs.set_hotspot_frame(index, state_frame(index));
    }

    void scene_hotspots::activate(u16 index)
    {
        auto const& hotspot = scene_records()[index];

        NE_LOG("hotspot %hu clicked", to<UWORD>(_first + index));

        if (hotspot.click_sfx_hash != data::NO_ASSET)
        {
            _sound_effects.play(hotspot.click_sfx_hash);
        }
        if (hotspot.click_function_id != data::NO_ID)
        {
            g_pEngine->vm().execute(hotspot.click_function_id);
        }
        if (hotspot.goto_scene_id != data::NO_ID)
        {
            g_pEngine->request_system(system_kind::load_scene, hotspot.goto_scene_id);
        }
    }

    void scene_hotspots::unload()
    {
        _first   = 0;
        _count   = 0;
        _hovered = NO_HOTSPOT;
        _pressed = NO_HOTSPOT;
        // The loop was blocked through the load, so whatever the button did in
        // that time went unseen; the next update resamples instead of guessing.
        _resync_button = true;
        _enabled.clear();
        _bobs.hide_caption();
    }

    void scene_hotspots::load(data::scene const& scene)
    {
        unload();

        // An empty or out-of-bounds range simply leaves the scene inert.
        auto const hotspots = scene.hotspot_range.of(_data.hotspots());
        if (hotspots.is_empty()) return;

        _first = scene.hotspot_range.start;
        _count = to<u16>(hotspots.size());
        _enabled.reserve(hotspots.size());

        u16 min_x = 0xFFFF;
        u16 min_y = 0xFFFF;
        u16 max_x = 0;
        u16 max_y = 0;

        for (auto const& hotspot : scene_records())
        {
            _enabled.push_back((hotspot.flags & FLAG_ENABLED) != 0 ? 1 : 0);

            auto const& bounds = hotspot.bounds;
            min_x = MIN(min_x, bounds.pos.x);
            min_y = MIN(min_y, bounds.pos.y);
            max_x = MAX(max_x, to<u16>(bounds.pos.x + bounds.width));
            max_y = MAX(max_y, to<u16>(bounds.pos.y + bounds.height));
        }

        _min_x = min_x;
        _min_y = min_y;
        _max_x = max_x;
        _max_y = max_y;

        // Bobs register on the normal frame, so a hotspot that starts disabled
        // needs its state applied before the first draw.
        for (u16 index = 0; index < _count; ++index)
        {
            refresh_state_frame(index);
        }

        NE_LOG("%hu hotspot(s) loaded", _count);
    }

    void scene_hotspots::set_enabled(u16 id, bool enabled)
    {
        // Scripts address hotspots by global id, and may name one belonging to
        // another scene; that is simply not ours to change.
        if (_count == 0 || id < _first || id >= _first + _count) return;

        auto const index = to<u16>(id - _first);
        _enabled[index]  = enabled ? 1 : 0;

        // A hotspot switched off mid-interaction must not stay hovered, nor
        // complete a click it can no longer respond to.
        if (!enabled)
        {
            auto const was_hovered = _hovered;
            auto const was_pressed = _pressed;
            if (_hovered == index) _hovered = NO_HOTSPOT;
            if (_pressed == index) _pressed = NO_HOTSPOT;
            // The hotspot that lost the press may be a different one from the
            // hotspot just disabled, and it too returns to its resting frame.
            refresh_state_frame(was_hovered);
            refresh_state_frame(was_pressed);
        }

        refresh_state_frame(index);
    }

    void scene_hotspots::resync_button()
    {
        _resync_button = true;
    }

    void scene_hotspots::update()
    {
        auto const is_down = mouseCheck(MOUSE_PORT_1, MOUSE_LMB) != 0;

        // A transition blocks or skips this update for many frames, so the
        // button may have moved unseen. Adopt its state without inventing an
        // edge, or a press begun during a load would activate in the new scene.
        if (_resync_button)
        {
            _resync_button = false;
            _was_down     = is_down;
        }

        // A scene with nothing interactive still has to release the pointer:
        // the transition that loaded it left the WAIT role showing, and nothing
        // below runs to put the default back.
        if (_count == 0)
        {
            _bobs.hide_caption();
            _pointer.switch_to(pointer_role::DEFAULT);
            _was_down = is_down;
            return;
        }

        // Bounds are authored in scene space while the pointer reports display
        // space, so shift the one pointer rather than every hotspot.
        auto const offset_y = g_mainScreen->offset();
        auto const pointer_x = mouseGetX(MOUSE_PORT_1);
        auto const pointer_y = mouseGetY(MOUSE_PORT_1);

        u16 hit = NO_HOTSPOT;
        if (pointer_y >= offset_y)
        {
            auto const x = pointer_x;
            auto const y = to<u16>(pointer_y - offset_y);

            // One test rejects every frame the pointer spends away from the
            // scene's interactive area, which is most of them.
            if (x >= _min_x && x < _max_x && y >= _min_y && y < _max_y)
            {
                auto const records = scene_records();
                u16 best_z    = 0;
                for (u16 index = 0; index < _count; ++index)
                {
                    if (_enabled[index] == 0) continue;

                    auto const& hotspot = records[index];
                    if (!contains(hotspot.bounds, x, y)) continue;

                    // Highest z-order wins; the earlier record breaks a tie.
                    if (hit == NO_HOTSPOT || hotspot.z_order > best_z)
                    {
                        best_z = hotspot.z_order;
                        hit    = index;
                    }
                }
            }
        }

        auto const prev_hovered = _hovered;
        _hovered = hit;

        // The caption names what the pointer is over, so it follows the pointer
        // for as long as the hover lasts and disappears with it.
        if (hit != NO_HOTSPOT)
        {
            _bobs.show_caption(hit, pointer_x, pointer_y);
        }
        else
        {
            _bobs.hide_caption();
        }

        // The pointer shows what the hotspot under it would do, and falls back
        // to the plain arrow over open scenery.
        auto const role = (hit != NO_HOTSPOT)
            ? to<pointer_role>(scene_records()[hit].cursor)
            : pointer_role::DEFAULT;
        _pointer.switch_to(role);

        bool const pressed_edge  = is_down && !_was_down;
        bool const released_edge = !is_down && _was_down;
        _was_down                = is_down;

        auto const prev_pressed = _pressed;
        u16 activated      = NO_HOTSPOT;

        if (pressed_edge)
        {
            // Latches even on empty space, so a press that began off a hotspot
            // cannot complete on one the pointer merely drifted onto.
            _pressed = hit;
        }
        else if (released_edge)
        {
            if (_pressed != NO_HOTSPOT && _pressed == hit) activated = hit;
            _pressed = NO_HOTSPOT;
        }

        // Repaint only what moved. At most one hotspot is hovered and one
        // pressed, so the four slots cover every transition; repeats simply
        // recompute the same frame, and NO_HOTSPOT is ignored.
        if (_hovered != prev_hovered || _pressed != prev_pressed)
        {
            refresh_state_frame(prev_hovered);
            refresh_state_frame(prev_pressed);
            refresh_state_frame(_hovered);
            refresh_state_frame(_pressed);
        }

        // Activate last so the click's own effects land on top: the script may
        // call HOTSPOT_ON/OFF, which repaints the hotspots it touches, and may
        // request a scene that rebuilds the set entirely.
        if (activated != NO_HOTSPOT) activate(activated);
    }
}  // namespace NEONengine
