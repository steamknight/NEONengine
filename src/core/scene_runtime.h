/**
 * @file scene_runtime.h
 * @brief What the current scene is made of, in one object.
 *
 * The scene's parts used to be file-static state behind free functions, with a
 * _load/_clear pair the scene state had to call in the right order by hand.
 * Here they are members: load() builds the set for a scene, and the destructor
 * is what _clear used to be.
 *
 * Member order is teardown order reversed. A part that another part talks to on
 * its way out must be declared before it.
 */

#ifndef __SCENE_RUNTIME_H__INCLUDED__
#define __SCENE_RUNTIME_H__INCLUDED__

#include "core/game_data.h"
#include "core/game_data_format.h"
#include "core/scene_bobs.h"
#include "core/scene_hotspots.h"
#include "core/scene_sound_effects.h"
#include "core/scene_timeline.h"

namespace NEONengine
{
    class scene_runtime
    {
        public:  //////////////////////////////////////////////////////////////////////////////////
        explicit scene_runtime(game_data& data)
            : _bobs(data),
              _sound_effects(data),
              _hotspots(data, _bobs, _sound_effects),
              _timeline(data, _bobs, _sound_effects)
        {
        }
        scene_runtime(scene_runtime const&)            = delete;
        scene_runtime& operator=(scene_runtime const&) = delete;

        /**
         * @brief Builds every part of @p scene, replacing the previous one.
         *
         * Reads the pak, so call under a system-use guard. The backdrop is laid
         * down by the scene state before this, because a bob undraws against it.
         */
        void load(data::scene const& scene);

        /** @brief The bobs this scene draws. */
        scene_bobs& bobs() noexcept { return _bobs; }

        /** @brief The sound effects this scene can play. */
        scene_sound_effects& sound_effects() noexcept { return _sound_effects; }

        /** @brief The interaction regions this scene runs against the mouse. */
        scene_hotspots& hotspots() noexcept { return _hotspots; }

        /** @brief The tracks this scene plays. */
        scene_timeline& timeline() noexcept { return _timeline; }

        private:  /////////////////////////////////////////////////////////////////////////////////
        // Declaration order is teardown order reversed, and it is load-bearing.
        // Members go down in reverse: timeline, hotspots, sound_effects, bobs.
        // ~scene_hotspots takes its hover caption down through the bobs, so the
        // bobs have to outlive it. It is also the order sceneDestroy called the
        // _clear functions in by hand.
        scene_bobs          _bobs;
        scene_sound_effects _sound_effects;
        scene_hotspots      _hotspots;
        scene_timeline      _timeline;
    };
}  // namespace NEONengine

#endif  // __SCENE_RUNTIME_H__INCLUDED__
