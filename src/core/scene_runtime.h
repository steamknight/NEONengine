/**
 * @file scene_runtime.h
 * @brief What the current scene is made of, in one object.
 *
 * The scene's parts used to be file-static state behind free functions, with a
 * _load/_clear pair the scene state had to call in the right order by hand.
 * Here they are members: the constructor builds the set for one scene and the
 * destructor takes it down, so a scene change is one object replacing another.
 *
 * Member order is teardown order reversed. A part that another part talks to on
 * its way out must be declared before it.
 */

#ifndef __SCENE_RUNTIME_H__INCLUDED__
#define __SCENE_RUNTIME_H__INCLUDED__

#include <mtl/memory.h>

#include "core/asset_cache.h"
#include "core/game_data.h"
#include "core/game_data_format.h"
#include "core/scene_bobs.h"
#include "core/scene_hotspots.h"
#include "core/scene_sound_effects.h"
#include "core/scene_timeline.h"

namespace NEONengine
{
    class scene_runtime;
    using scene_runtime_ptr = mtl::unique_ptr<scene_runtime>;

    class scene_runtime
    {
        public:  //////////////////////////////////////////////////////////////////////////////////
        /**
         * @brief Builds every part of @p scene.
         *
         * Reads the pak, so construct this under a system-use guard. The
         * backdrop is laid down before it, because a bob undraws against it.
         */
        scene_runtime(game_data& data,
            asset_cache&        cache,
            mouse_pointer&      pointer,
            data::scene const&  scene);
        scene_runtime(scene_runtime const&)            = delete;
        scene_runtime& operator=(scene_runtime const&) = delete;

        /** @brief The bobs this scene draws. */
        scene_bobs& bobs() noexcept { return _bobs; }

        /** @brief The sound effects this scene can play. */
        scene_sound_effects& sound_effects() noexcept { return _sound_effects; }

        /** @brief The interaction regions this scene runs against the mouse. */
        scene_hotspots& hotspots() noexcept { return _hotspots; }

        /** @brief The tracks this scene plays. */
        scene_timeline& timeline() noexcept { return _timeline; }

        private:  /////////////////////////////////////////////////////////////////////////////////
        scene_bobs          _bobs;
        scene_sound_effects _sound_effects;
        scene_hotspots      _hotspots;
        scene_timeline      _timeline;
    };
}  // namespace NEONengine

#endif  // __SCENE_RUNTIME_H__INCLUDED__
