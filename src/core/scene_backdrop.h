#ifndef __SCENE_BACKDROP_H__INCLUDED__
#define __SCENE_BACKDROP_H__INCLUDED__

#include <mtl/expected.h>

#include "game_data_format.h"

namespace NEONengine
{
    class engine;

    /**
     * Draws the background and localized text, restores UI colors and cursor
     * palette exemption, then copies the back buffer to the front.
     * Without a background bitmap, skips clearing pixels and loading its palette.
     * Call under a system-use guard, before registering or drawing bobs.
     * Retains no resources and does not change pointer visibility.
     */
    mtl::expected<void, char const*> scene_backdrop_draw(engine& engine, data::scene const& scene);
}  // namespace NEONengine

#endif  // __SCENE_BACKDROP_H__INCLUDED__
