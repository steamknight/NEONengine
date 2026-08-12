#include "scene_runtime.h"

namespace NEONengine
{
    void scene_runtime::load(data::scene const& scene)
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
