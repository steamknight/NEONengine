#include "scene_audio.h"

#include "neonengine.h"

#include <ace/managers/log.h>
#include <ace/managers/ptplayer.h>

#include <mtl/utility.h>
#include <mtl/vector.h>

#include "core/game_data.h"

namespace NEONengine
{
    namespace
    {
        constexpr uint16_t NO_TIMELINE = 0xFFFF;

        /// Neither volume nor priority is authored, so every effect shares one.
        /// ptplayer requires a nonzero priority; equal priorities mean the
        /// older sound gives way, which is what one-shots want.
        constexpr uint8_t SFX_VOLUME   = PTPLAYER_VOLUME_MAX;
        constexpr uint8_t SFX_PRIORITY = 1;

        /// One loaded effect, keyed by the hash its keyframes name it with.
        struct sfx_entry
        {
            uint32_t hash      = 0;
            tPtplayerSfx* data = nullptr;
        };

        mtl::vector<sfx_entry> s_sfx;

        /// @brief The loaded effect @p hash names, or nullptr.
        tPtplayerSfx* find_sfx(uint32_t hash)
        {
            for (auto const& entry : s_sfx)
            {
                if (entry.hash == hash) return entry.data;
            }
            return nullptr;
        }

        /**
         * @brief Loads the effect @p hash names, unless it is already loaded.
         *
         * Several keyframes naming one sound is the ordinary case, and a track
         * that loops names it again every cycle, so the table is keyed by hash
         * and each sound is read exactly once.
         */
        void require_sfx(uint32_t hash)
        {
            if (find_sfx(hash)) return;

            auto file = g_pEngine->data().open_asset(hash);
            if (!file)
            {
                NE_LOG("scene_audio: no sfx asset for hash %08lx", mtl::to<ULONG>(hash));
                return;
            }

            // Chip RAM, not Fast: ptplayer hands the sample straight to Paula,
            // which cannot see Fast. release() because the create closes it.
            tPtplayerSfx* sfx = ptplayerSfxCreateFromFd(file.release(), 0);
            if (!sfx)
            {
                NE_LOG("scene_audio: sfx %08lx would not load", mtl::to<ULONG>(hash));
                return;
            }

            s_sfx.push_back(sfx_entry{ hash, sfx });
        }

        /**
         * @brief Loads the effects named by the keyframes of one track.
         */
        void load_track_sfx(data::track const& track)
        {
            auto const all = g_pEngine->data().keyframes();
            if (mtl::to<size_t>(track.keyframe_range.start) + track.keyframe_range.count
                > all.size())
            {
                // scene_timeline reports this and refuses to play the track, so
                // it is passed over quietly rather than reported twice.
                return;
            }

            for (uint16_t offset = 0; offset < track.keyframe_range.count; ++offset)
            {
                auto const& key = all[mtl::to<size_t>(track.keyframe_range.start) + offset];
                if (mtl::to<data::keyframe_type>(key.type) != data::keyframe_type::SFX_PLAY)
                {
                    continue;
                }

                require_sfx(mtl::force_to<data::sfx_play_payload const*>(key.payload)->sfx_hash);
            }
        }
    }  // namespace

    void scene_audio_clear()
    {
        for (auto& entry : s_sfx)
        {
            if (entry.data) ptplayerSfxDestroy(entry.data);
        }
        s_sfx.clear();
    }

    void scene_audio_load(data::scene const& scene)
    {
        scene_audio_clear();

        if (scene.timeline_id == NO_TIMELINE) return;

        auto const timelines = g_pEngine->data().timelines();
        if (scene.timeline_id >= timelines.size()) return;  // scene_timeline reports it.

        auto const& time     = timelines[scene.timeline_id];
        auto const tracks    = g_pEngine->data().tracks();
        uint16_t const first = time.track_range.start;
        uint16_t const count = time.track_range.count;

        if (count == 0 || mtl::to<size_t>(first) + count > tracks.size()) return;

        for (uint16_t index = 0; index < count; ++index)
        {
            load_track_sfx(tracks[mtl::to<size_t>(first) + index]);
        }

        if (!s_sfx.empty())
        {
            NE_LOG("scene_audio: %lu sound effect(s)", mtl::to<ULONG>(s_sfx.size()));
        }
    }

    void scene_audio_play(uint32_t hash)
    {
        tPtplayerSfx* sfx = find_sfx(hash);

        // Already reported at load. Saying so again here would say it fifty
        // times a second on a looping track.
        if (!sfx) return;

        // The replayer picks the least busy channel and gives it back to the
        // music when the sound finishes, which is what leaves a four voice
        // module four voices for all but the few frames an effect lasts.
        ptplayerSfxPlay(sfx, PTPLAYER_SFX_CHANNEL_ANY, SFX_VOLUME, SFX_PRIORITY);
    }
}  // namespace NEONengine
