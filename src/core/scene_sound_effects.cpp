#include "scene_sound_effects.h"

#include "types.h"

#include "neonengine.h"

#include <ace/managers/ptplayer.h>

#include <mtl/utility.h>

#include "core/game_data.h"
#include "core/logger.h"

NE_LOG_DOMAIN(Audio);

namespace NEONengine
{
    namespace
    {
        /// Neither volume nor priority is authored, so every effect shares one.
        /// ptplayer requires a nonzero priority; equal priorities mean the
        /// older sound gives way, which is what one-shots want.
        constexpr u8 SFX_VOLUME   = PTPLAYER_VOLUME_MAX;
        constexpr u8 SFX_PRIORITY = 1;
    }  // namespace

    scene_sound_effects::~scene_sound_effects()
    {
        unload();
    }

    tPtplayerSfx* scene_sound_effects::find(u32 hash) const
    {
        for (auto const& held : _effects)
        {
            if (held.hash == hash) return held.data;
        }
        return nullptr;
    }

    void scene_sound_effects::require(u32 hash)
    {
        if (find(hash)) return;

        auto file = _data.open_asset(hash);
        if (!file)
        {
            NE_LOG("no sfx asset for hash %08lx", to<ULONG>(hash));
            return;
        }

        // Chip RAM, not Fast: ptplayer hands the sample straight to Paula,
        // which cannot see Fast. release() because the create closes it.
        tPtplayerSfx* sfx = ptplayerSfxCreateFromFd(file.release(), 0);
        if (!sfx)
        {
            NE_LOG("sfx %08lx would not load", to<ULONG>(hash));
            return;
        }

        _effects.push_back(entry{ hash, sfx });
    }

    void scene_sound_effects::load_track(data::track const& track)
    {
        auto const all = _data.keyframes();
        if (to<size_t>(track.keyframe_range.start) + track.keyframe_range.count > all.size())
        {
            // scene_timeline reports this and refuses to play the track, so it
            // is passed over quietly rather than reported twice.
            return;
        }

        for (u16 offset = 0; offset < track.keyframe_range.count; ++offset)
        {
            auto const& key = all[to<size_t>(track.keyframe_range.start) + offset];
            if (to<data::keyframe_type>(key.type) != data::keyframe_type::SFX_PLAY) continue;

            require(force_to<data::sfx_play_payload const*>(key.payload)->sfx_hash);
        }
    }

    void scene_sound_effects::load_timeline(data::scene const& scene)
    {
        if (scene.timeline_id == data::NO_ID) return;

        auto const timelines = _data.timelines();
        if (scene.timeline_id >= timelines.size()) return;  // scene_timeline reports it.

        auto const& time  = timelines[scene.timeline_id];
        auto const tracks = time.track_range.of(_data.tracks());

        for (auto const& track : tracks) load_track(track);
    }

    void scene_sound_effects::load_hotspots(data::scene const& scene)
    {
        // scene_hotspots reports a bad range and leaves the scene inert.
        auto const hotspots = scene.hotspot_range.of(_data.hotspots());

        for (auto const& hotspot : hotspots)
        {
            if (hotspot.click_sfx_hash != data::NO_ASSET) require(hotspot.click_sfx_hash);
        }
    }

    void scene_sound_effects::unload()
    {
        for (auto& held : _effects)
        {
            if (held.data) ptplayerSfxDestroy(held.data);
        }
        _effects.clear();
    }

    void scene_sound_effects::load(data::scene const& scene)
    {
        unload();

        load_timeline(scene);
        load_hotspots(scene);

        if (!_effects.empty())
        {
            NE_LOG("%lu sound effect(s)", to<ULONG>(_effects.size()));
        }
    }

    void scene_sound_effects::play(u32 hash)
    {
        tPtplayerSfx* sfx = find(hash);

        // Already reported at load. Saying so again here would say it fifty
        // times a second on a looping track.
        if (!sfx) return;

        // The replayer picks the least busy channel and gives it back to the
        // music when the sound finishes, which is what leaves a four voice
        // module four voices for all but the few frames an effect lasts.
        ptplayerSfxPlay(sfx, PTPLAYER_SFX_CHANNEL_ANY, SFX_VOLUME, SFX_PRIORITY);
    }
}  // namespace NEONengine
