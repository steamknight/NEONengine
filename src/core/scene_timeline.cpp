#include "scene_timeline.h"

#include "neonengine.h"

#include <ace/managers/log.h>

#include <mtl/utility.h>
#include <mtl/vector.h>

#include "core/game_data.h"

namespace NEONengine
{
    namespace
    {
        constexpr uint16_t NO_TIMELINE = 0xFFFF;

        /// Cycle boundaries logged per track before the module falls silent.
        constexpr uint16_t LOGGED_CYCLES = 3;

        uint16_t s_first = 0;  ///< First TRAK id this scene owns.
        uint16_t s_count = 0;  ///< How many it owns; 0 disables the whole module.

        mtl::vector<track_clock> s_clocks;  ///< One playhead per owned track.

        /**
         * @brief Names @p mode the way the editor and the format docs do.
         */
        char const* loop_mode_name(uint8_t mode)
        {
            switch (mtl::to<data::loop_mode>(mode))
            {
                case data::loop_mode::NONE: return "None";
                case data::loop_mode::RESTART: return "Restart";
                case data::loop_mode::PING_PONG: return "PingPong";
                default: return "?";
            }
        }
    }  // namespace

    void scene_timeline_clear()
    {
        s_first = 0;
        s_count = 0;
        s_clocks.clear();
    }

    mtl::span<data::track> scene_timeline_tracks()
    {
        auto const all = g_pEngine->data().tracks();
        return mtl::span<data::track>(all.data() + s_first, s_count);
    }

    track_clock const& scene_timeline_clock(uint16_t index)
    {
        return s_clocks[index];
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
        s_clocks.reserve(count);

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

            // The editor rejects this, so it means the pak was built by
            // something else. track_clock plays it as if it were 1 frame long
            // rather than refusing, but the data is still wrong.
            if (track.duration == 0)
            {
                NE_LOG("  track %hu has no duration; playing it as 1 frame", index);
            }

            track_clock clock;
            if (track.flags & data::TRACK_FLAG_AUTO_START) clock.start();
            s_clocks.push_back(clock);
        }
    }

    void scene_timeline_update()
    {
        if (s_count == 0) return;

        auto const tracks = scene_timeline_tracks();
        for (uint16_t index = 0; index < s_count; ++index)
        {
            auto& clock = s_clocks[index];
            switch (clock.tick(tracks[index]))
            {
                case tick_result::LOOPED:
                    // A short looping track turns over dozens of times a second,
                    // which would bury everything else in the log, so only the
                    // first few of each track's cycles are reported.
                    if (clock.cycles_done() <= LOGGED_CYCLES)
                    {
                        NE_LOG("timeline: track %hu completed cycle %hu", index, clock.cycles_done());
                    }
                    else if (clock.cycles_done() == LOGGED_CYCLES + 1)
                    {
                        NE_LOG("timeline: track %hu is looping; further cycles are not logged", index);
                    }
                    break;

                case tick_result::FINISHED:
                    NE_LOG("timeline: track %hu finished after %hu cycle(s)", index, clock.cycles_done());
                    break;

                case tick_result::IDLE:
                case tick_result::RUNNING:
                    break;
            }
        }
    }
}  // namespace NEONengine
