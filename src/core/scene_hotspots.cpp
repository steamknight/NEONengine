#include "scene_hotspots.h"

#include "neonengine.h"

#include <ace/managers/log.h>
#include <ace/managers/mouse.h>

#include <mtl/span.h>
#include <mtl/utility.h>
#include <mtl/vector.h>

#include "core/game_data.h"
#include "core/mouse_pointer.h"
#include "core/screen.h"

namespace NEONengine
{
    namespace
    {
        constexpr uint16_t NO_HOTSPOT      = 0xFFFF;
        constexpr uint8_t FLAG_ENABLED     = 1 << 0;  ///< hotspot.flags bit 0.

        uint16_t s_first = 0;  ///< First HOTS id this scene owns.
        uint16_t s_count = 0;  ///< How many it owns; 0 disables the whole module.

        /// One byte per hotspot, seeded from its record and flipped by scripts.
        mtl::vector<uint8_t> s_enabled;

        uint16_t s_hovered = NO_HOTSPOT;  ///< Scene-local index under the pointer.
        uint16_t s_pressed = NO_HOTSPOT;  ///< Scene-local index the press latched onto.
        bool s_was_down    = false;       ///< Last frame's button state, for edges.

        /// Scene-space box enclosing every hotspot; maxima are exclusive.
        uint16_t s_min_x = 0;
        uint16_t s_min_y = 0;
        uint16_t s_max_x = 0;
        uint16_t s_max_y = 0;

        /**
         * @brief The hotspot records this scene owns.
         */
        mtl::span<data::hotspot> scene_records()
        {
            auto const all = g_pEngine->data().hotspots();
            return mtl::span<data::hotspot>(all.data() + s_first, s_count);
        }

        /**
         * @brief True when scene-space @p x, @p y falls inside @p bounds.
         */
        constexpr bool contains(data::rect const& bounds, uint16_t x, uint16_t y) noexcept
        {
            return x >= bounds.pos.x && x < bounds.pos.x + bounds.width  //
                && y >= bounds.pos.y && y < bounds.pos.y + bounds.height;
        }

        /**
         * @brief Run the hotspot @p index was clicked.
         *
         * The script runs first so it observes the state the click found, then
         * the record's own transition is requested; a concrete goto_scene wins
         * over whatever the script may have asked for.
         */
        void activate(uint16_t index)
        {
            auto const& hotspot = scene_records()[index];

            NE_LOG("scene_hotspots: hotspot %hu clicked", mtl::to<UWORD>(s_first + index));

            if (hotspot.click_function_id != data::NO_ID)
            {
                g_pEngine->vm().execute(hotspot.click_function_id);
            }
            if (hotspot.goto_scene_id != data::NO_ID)
            {
                g_pEngine->request_scene(hotspot.goto_scene_id);
            }
        }
    }  // namespace

    void scene_hotspots_clear()
    {
        s_first   = 0;
        s_count   = 0;
        s_hovered = NO_HOTSPOT;
        s_pressed = NO_HOTSPOT;
        s_enabled.clear();
    }

    void scene_hotspots_load(data::scene const& scene)
    {
        scene_hotspots_clear();

        auto const all       = g_pEngine->data().hotspots();
        uint16_t const first = scene.hotspot_range.start;
        uint16_t const count = scene.hotspot_range.count;

        // An empty or out-of-bounds range simply leaves the scene inert.
        if (count == 0 || mtl::to<size_t>(first) + count > all.size()) return;

        s_first = first;
        s_count = count;
        s_enabled.reserve(count);

        uint16_t min_x = 0xFFFF;
        uint16_t min_y = 0xFFFF;
        uint16_t max_x = 0;
        uint16_t max_y = 0;

        for (auto const& hotspot : scene_records())
        {
            s_enabled.push_back((hotspot.flags & FLAG_ENABLED) != 0 ? 1 : 0);

            auto const& bounds = hotspot.bounds;
            min_x = MIN(min_x, bounds.pos.x);
            min_y = MIN(min_y, bounds.pos.y);
            max_x = MAX(max_x, mtl::to<uint16_t>(bounds.pos.x + bounds.width));
            max_y = MAX(max_y, mtl::to<uint16_t>(bounds.pos.y + bounds.height));
        }

        s_min_x = min_x;
        s_min_y = min_y;
        s_max_x = max_x;
        s_max_y = max_y;

        NE_LOG("scene_hotspots: %hu hotspot(s) loaded", s_count);
    }

    void scene_hotspots_set_enabled(uint16_t id, bool enabled)
    {
        // Scripts address hotspots by global id, and may name one belonging to
        // another scene; that is simply not ours to change.
        if (s_count == 0 || id < s_first || id >= s_first + s_count) return;

        auto const index = mtl::to<uint16_t>(id - s_first);
        s_enabled[index] = enabled ? 1 : 0;

        // A hotspot switched off mid-interaction must not stay hovered, nor
        // complete a click it can no longer respond to.
        if (!enabled)
        {
            if (s_hovered == index) s_hovered = NO_HOTSPOT;
            if (s_pressed == index) s_pressed = NO_HOTSPOT;
        }
    }

    void scene_hotspots_update()
    {
        if (s_count == 0) return;

        // Bounds are authored in scene space while the pointer reports display
        // space, so shift the one pointer rather than every hotspot.
        auto const offset_y = screenGetVerticalOffset(g_mainScreen);
        auto const pointer_x = mouseGetX(MOUSE_PORT_1);
        auto const pointer_y = mouseGetY(MOUSE_PORT_1);

        uint16_t hit = NO_HOTSPOT;
        if (pointer_y >= offset_y)
        {
            auto const x = pointer_x;
            auto const y = mtl::to<uint16_t>(pointer_y - offset_y);

            // One test rejects every frame the pointer spends away from the
            // scene's interactive area, which is most of them.
            if (x >= s_min_x && x < s_max_x && y >= s_min_y && y < s_max_y)
            {
                auto const records = scene_records();
                uint16_t best_z    = 0;
                for (uint16_t index = 0; index < s_count; ++index)
                {
                    if (s_enabled[index] == 0) continue;

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

        s_hovered = hit;

        // The pointer shows what the hotspot under it would do, and falls back
        // to the plain arrow over open scenery.
        auto const role = (hit != NO_HOTSPOT)
            ? mtl::to<mouse_pointer>(scene_records()[hit].cursor)
            : mouse_pointer::DEFAULT;
        mousePointerSwitch(role);

        auto const is_down       = mouseCheck(MOUSE_PORT_1, MOUSE_LMB) != 0;
        bool const pressed_edge  = is_down && !s_was_down;
        bool const released_edge = !is_down && s_was_down;
        s_was_down               = is_down;

        if (pressed_edge)
        {
            // Latches even on empty space, so a press that began off a hotspot
            // cannot complete on one the pointer merely drifted onto.
            s_pressed = hit;
        }
        else if (released_edge)
        {
            if (s_pressed != NO_HOTSPOT && s_pressed == hit) activate(hit);
            s_pressed = NO_HOTSPOT;
        }
    }
}  // namespace NEONengine
