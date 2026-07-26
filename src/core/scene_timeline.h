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
 * Scope of the current slice: the scene's tracks are resolved, the ones marked
 * auto-start begin at their own position zero, and every running track's
 * playhead is advanced once per frame. Deliberately not handled yet: firing
 * keyframes, and the ANIM_PLAY / ANIM_STOP / ANIM_WAIT opcodes.
 */

#ifndef __SCENE_TIMELINE_H__INCLUDED__
#define __SCENE_TIMELINE_H__INCLUDED__

#include <stdint.h>

#include <mtl/span.h>

#include "game_data_format.h"
#include "track_clock.h"

namespace NEONengine
{
    /**
     * @brief Resolves @p scene's timeline to the tracks it owns and starts them.
     *
     * A scene with no timeline, or whose timeline names a range this build
     * cannot address, simply ends up with no tracks and leaves the module
     * inert. Every track marked auto-start is left running at position 0, and
     * the rest wait to be played. Reads only the already-loaded game data, so it
     * needs no system-use guard.
     */
    void scene_timeline_load(data::scene const& scene);

    /**
     * @brief Forgets the current scene's tracks.
     */
    void scene_timeline_clear();

    /**
     * @brief Advances every running track's playhead by one frame.
     *
     * Call from the scene loop each frame. genericProcess runs the state loop
     * exactly once per PAL frame, so one call is one frame of track time; the
     * clock therefore counts frames processed rather than time passed, and a
     * blocking pak read stalls it rather than being skipped over.
     */
    void scene_timeline_update();

    /**
     * @brief The TRAK records the current scene owns, in timeline order.
     *
     * Empty when the scene has no timeline. The span points into the loaded
     * game data, so it stays valid until the next scene_timeline_load().
     */
    mtl::span<data::track> scene_timeline_tracks();

    /**
     * @brief The playhead of the track at scene-local @p index.
     *
     * @param index Must be within scene_timeline_tracks().
     */
    track_clock const& scene_timeline_clock(uint16_t index);
}  // namespace NEONengine

#endif  // __SCENE_TIMELINE_H__INCLUDED__
