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

        /// Scene-local index of each track's first keyframe, for indexing s_fired.
        mtl::vector<uint32_t> s_key_base;

        /// One bit per scene-local keyframe: whether a fire_once key has fired.
        mtl::vector<uint8_t> s_fired;

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

        /**
         * @brief Names @p type the way the editor and the format docs do.
         */
        char const* keyframe_type_name(uint8_t type)
        {
            switch (mtl::to<data::keyframe_type>(type))
            {
                case data::keyframe_type::GFX_MOVE: return "GFX_MOVE";
                case data::keyframe_type::GFX_VISIBILITY: return "GFX_VISIBILITY";
                case data::keyframe_type::PALETTE_CYCLE_START: return "PALETTE_CYCLE_START";
                case data::keyframe_type::PALETTE_CYCLE_END: return "PALETTE_CYCLE_END";
                case data::keyframe_type::SFX_PLAY: return "SFX_PLAY";
                case data::keyframe_type::SFX_STOP: return "SFX_STOP";
                case data::keyframe_type::MUSIC_PLAY: return "MUSIC_PLAY";
                case data::keyframe_type::MUSIC_STOP: return "MUSIC_STOP";
                case data::keyframe_type::EXECUTE_SCRIPT: return "EXECUTE_SCRIPT";
                default: return "?";
            }
        }

        /// @brief Whether the fire_once key at scene-local @p index has already fired.
        bool has_fired(uint32_t index)
        {
            return (s_fired[index >> 3] & mtl::to<uint8_t>(1 << (index & 7))) != 0;
        }

        /// @brief Records that the fire_once key at scene-local @p index has fired.
        void mark_fired(uint32_t index)
        {
            s_fired[index >> 3] |= mtl::to<uint8_t>(1 << (index & 7));
        }

        /**
         * @brief Runs @p key, if this slice knows how to.
         *
         * @return Whether the keyframe actually fired. A keyframe that did not
         *         fire has not used up its fire_once, so a looping track will
         *         offer it again rather than losing it for the rest of the run.
         */
        bool fire(data::keyframe const& key, uint16_t track_index)
        {
            if (mtl::to<data::keyframe_type>(key.type) != data::keyframe_type::EXECUTE_SCRIPT)
            {
                // Every other type is reported once at load and ignored here, so
                // that an unimplemented keyframe on a looping track cannot log
                // 50 times a second.
                return false;
            }

            auto const& payload = *mtl::force_to<data::execute_script_payload const*>(key.payload);
            auto& vm            = g_pEngine->vm();

            // execute() saves the registers of a re-entered call but not the
            // VM's state or its blocking and wait counters, so starting a script
            // over a blocked one would strand the counter and release the outer
            // script early - taking ANIM_WAIT with it. The same test means only
            // the first script keyframe of a frame runs, which is deliberate:
            // two execute() calls in one frame would run in reverse order.
            if (vm.state() != noir_state::idle)
            {
                NE_LOG("timeline: track %hu cannot run script %hu, the VM is busy",
                    track_index,
                    payload.function_id);
                return false;
            }

            vm.execute(payload.function_id);
            NE_LOG("timeline: track %hu ran script %hu", track_index, payload.function_id);
            return true;
        }

        /**
         * @brief Fires whatever the track at @p track_index has under its playhead.
         */
        void fire_keyframes(uint16_t track_index, data::track const& track)
        {
            if (track.keyframe_range.count == 0) return;

            auto const all = g_pEngine->data().keyframes();
            mtl::span<data::keyframe> const keys(all.data() + track.keyframe_range.start,
                track.keyframe_range.count);

            auto const& clock = s_clocks[track_index];
            auto const run    = clock.keys_at(keys);
            if (run.count == 0) return;

            uint32_t const base = s_key_base[track_index];
            for (uint16_t step = 0; step < run.count; ++step)
            {
                // A track played backwards is played backwards all the way down,
                // so keys sharing a position run in reverse on the return leg.
                uint16_t const offset =
                    clock.is_reversing() ? mtl::to<uint16_t>(run.first + run.count - 1 - step)
                                         : mtl::to<uint16_t>(run.first + step);

                auto const& key       = keys[offset];
                bool const fire_once  = (key.flags & data::KEYFRAME_FLAG_FIRE_ONCE) != 0;
                uint32_t const marker = base + offset;

                if (fire_once && has_fired(marker)) continue;
                if (fire(key, track_index) && fire_once) mark_fired(marker);
            }
        }
    }  // namespace

    void scene_timeline_clear()
    {
        s_first = 0;
        s_count = 0;
        s_clocks.clear();
        s_key_base.clear();
        s_fired.clear();
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
        s_key_base.reserve(count);

        uint32_t total_keys      = 0;
        uint16_t unimplemented   = 0;  ///< Bitmask over data::keyframe_type.
        auto const all_keyframes = g_pEngine->data().keyframes();

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

            bool const keys_in_range = mtl::to<size_t>(track.keyframe_range.start)
                    + track.keyframe_range.count
                <= all_keyframes.size();

            s_key_base.push_back(total_keys);
            total_keys += track.keyframe_range.count;

            track_clock clock;
            if (!keys_in_range)
            {
                // Playing a track whose keys point outside the chunk would read
                // whatever follows it, so the track is left permanently idle.
                NE_LOG("  track %hu keys %hu+%hu exceed %lu keyframes; it will not play",
                    index,
                    track.keyframe_range.start,
                    track.keyframe_range.count,
                    mtl::to<ULONG>(all_keyframes.size()));
            }
            else
            {
                for (uint16_t offset = 0; offset < track.keyframe_range.count; ++offset)
                {
                    auto const& key = all_keyframes[mtl::to<size_t>(track.keyframe_range.start) + offset];
                    if (mtl::to<data::keyframe_type>(key.type) != data::keyframe_type::EXECUTE_SCRIPT
                        && key.type < data::KEYFRAME_TYPE_COUNT)
                    {
                        unimplemented |= mtl::to<uint16_t>(1 << key.type);
                    }
                }

                if (track.flags & data::TRACK_FLAG_AUTO_START) clock.start();
            }
            s_clocks.push_back(clock);
        }

        // Reported once here rather than at fire time, where an unimplemented
        // keyframe on a looping track would log 50 times a second.
        for (uint8_t type = 0; type < data::KEYFRAME_TYPE_COUNT; ++type)
        {
            if (unimplemented & (1 << type))
            {
                NE_LOG("  %s keyframes are not implemented yet and will be skipped",
                    keyframe_type_name(type));
            }
        }

        s_fired.reserve((total_keys + 7) / 8);
        for (uint32_t byte = 0; byte < (total_keys + 7) / 8; ++byte) s_fired.push_back(0);
    }

    void scene_timeline_update()
    {
        if (s_count == 0) return;

        auto const tracks = scene_timeline_tracks();
        for (uint16_t index = 0; index < s_count; ++index)
        {
            auto& clock = s_clocks[index];
            if (!clock.is_running()) continue;

            // Fire before ticking, so a run's very first frame plays whatever
            // sits at position 0 rather than stepping straight over it.
            fire_keyframes(index, tracks[index]);

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
