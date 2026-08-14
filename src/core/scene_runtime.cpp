#include "scene_runtime.h"

namespace NEONengine
{
    scene_runtime::scene_runtime(game_data& data,
        asset_cache&       cache,
        mouse_pointer&     pointer,
        data::scene const& scene)
        : _bobs(data, cache),
          _sound_effects(data, cache),
          _hotspots(data, _bobs, _sound_effects, pointer),
          _timeline(data, _bobs, _sound_effects)
    {
        // Bobs first: the hotspots apply each hotspot's initial state to its
        // graphic, and a track priming a movement reads where its instance
        // already stands, so both need the set registered.
        _bobs.load(scene);
        _sound_effects.load(scene);
        _hotspots.load(scene);
        _timeline.load(scene);
    }
}  // namespace NEONengine
