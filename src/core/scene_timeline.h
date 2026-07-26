/**
 * @file scene_timeline.h
 * @brief Owns the tracks of the current scene's timeline.
 *
 * A scene has at most one timeline, and a timeline is only a container: every
 * playable property lives on the TRAK records it owns. Each track is a named,
 * independently playable sequence of heterogeneous keyframes, with its own
 * duration, loop mode, loop count and auto-start bit. Tracks of one timeline
 * run concurrently on independent clocks, and a track started later begins at
 * its own time zero rather than joining one already running.
 *
 * Tracks cannot outlive their scene, so this module's state is rebuilt by
 * scene_timeline_load() and dropped by scene_timeline_clear().
 *
 * Scope of the current slice: the scene's tracks are resolved and logged, and
 * nothing else. Deliberately not handled yet: the clock, firing keyframes, and
 * the ANIM_PLAY / ANIM_STOP / ANIM_WAIT opcodes.
 */

#ifndef __SCENE_TIMELINE_H__INCLUDED__
#define __SCENE_TIMELINE_H__INCLUDED__

#include <stdint.h>

#include <mtl/span.h>

#include "game_data_format.h"

namespace NEONengine
{
    /**
     * @brief Resolves @p scene's timeline to the tracks it owns.
     *
     * A scene with no timeline, or whose timeline names a range this build
     * cannot address, simply ends up with no tracks and leaves the module
     * inert. Reads only the already-loaded game data, so it needs no
     * system-use guard.
     */
    void scene_timeline_load(data::scene const& scene);

    /**
     * @brief Forgets the current scene's tracks.
     */
    void scene_timeline_clear();

    /**
     * @brief The TRAK records the current scene owns, in timeline order.
     *
     * Empty when the scene has no timeline. The span points into the loaded
     * game data, so it stays valid until the next scene_timeline_load().
     */
    mtl::span<data::track> scene_timeline_tracks();
}  // namespace NEONengine

#endif  // __SCENE_TIMELINE_H__INCLUDED__
