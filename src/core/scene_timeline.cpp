#include "scene_timeline.h"

#include "neonengine.h"

#include <ace/managers/log.h>

#include <mtl/utility.h>

#include "core/game_data.h"

namespace NEONengine
{
    namespace
    {
        constexpr uint16_t NO_TIMELINE = 0xFFFF;

        uint16_t s_first = 0;  ///< First TRAK id this scene owns.
        uint16_t s_count = 0;  ///< How many it owns; 0 disables the whole module.

        /**
         * @brief Names @p mode the way the editor and the format docs do.
         */
        char const* loop_mode_name(uint8_t mode)
        {
            switch (mode)
            {
                case 0: return "None";
                case 1: return "Restart";
                case 2: return "PingPong";
                default: return "?";
            }
        }
    }  // namespace

    void scene_timeline_clear()
    {
        s_first = 0;
        s_count = 0;
    }

    mtl::span<data::track> scene_timeline_tracks()
    {
        auto const all = g_pEngine->data().tracks();
        return mtl::span<data::track>(all.data() + s_first, s_count);
    }

    void scene_timeline_load(data::scene const& scene)
    {
        scene_timeline_clear();

        if (scene.timeline_id == NO_TIMELINE) return;

        auto const timelines = g_pEngine->data().timelines();
        if (scene.timeline_id >= timelines.size())
        {
            NE_LOG("timeline: scene names timeline %hu of %lu",
                scene.timeline_id,
                mtl::to<ULONG>(timelines.size()));
            return;
        }

        auto const& time     = timelines[scene.timeline_id];
        auto const all       = g_pEngine->data().tracks();
        uint16_t const first = time.track_range.start;
        uint16_t const count = time.track_range.count;

        // An empty or out-of-bounds range simply leaves the scene inert.
        if (count == 0 || mtl::to<size_t>(first) + count > all.size())
        {
            if (count != 0)
            {
                NE_LOG("timeline: track range %hu+%hu exceeds %lu tracks",
                    first,
                    count,
                    mtl::to<ULONG>(all.size()));
            }
            return;
        }

        s_first = first;
        s_count = count;

        NE_LOG("timeline: %hu, %hu track(s)", scene.timeline_id, count);
        for (uint16_t index = 0; index < count; ++index)
        {
            auto const& track = all[mtl::to<size_t>(first) + index];
            NE_LOG("  track %hu: name %hu, keys %hu+%hu, %hu frames, %s x%hu, %s",
                index,
                track.name_id,
                track.keyframe_range.start,
                track.keyframe_range.count,
                track.duration,
                loop_mode_name(track.loop_mode),
                mtl::to<uint16_t>(track.loop_count),
                (track.flags & data::TRACK_FLAG_AUTO_START) ? "auto" : "on cue");
        }
    }
}  // namespace NEONengine
