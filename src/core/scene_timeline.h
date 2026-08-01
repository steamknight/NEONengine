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
 * Unknown keyframe types are logged once at load and skipped.
 *
 * A movement in progress belongs to the graphic instance rather than to the
 * track that started it, because an instance has one position: two tracks
 * moving the same instance are settled by which of them wrote last, in track
 * order, and not by remembering both.
 */

#ifndef __SCENE_TIMELINE_H__INCLUDED__
#define __SCENE_TIMELINE_H__INCLUDED__

#include "types.h"

#include <stdint.h>

#include <mtl/span.h>

#include "game_data_format.h"

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
     * @brief Puts every palette band this scene turns back as it was authored.
     *
     * A rotation is a permutation, so turning each band the rest of the way
     * round restores it exactly, and the bands are then held to be untouched.
     *
     * Call this before anything replaces the reference palette. A cycle's
     * record of how far it has turned describes the palette as it actually
     * stands, so loading a new one over the top of a turned band would strand
     * that record against colors it no longer describes: the band would be
     * corrected only by the difference from then on, and never brought back
     * into agreement.
     */
    void scene_timeline_restore_palette();

    /**
     * @brief Advances every running track's playhead by one frame.
     *
     * Call from the scene loop each frame. genericProcess runs the state loop
     * exactly once per PAL frame, so one call is one frame of track time; the
     * clock therefore counts frames processed rather than time passed, and a
     * blocking pak read stalls it rather than being skipped over.
     *
     * A track fires whatever its playhead is standing on before moving, so the
     * first frame of a run plays position zero rather than stepping over it.
     */
    void scene_timeline_update();

    /**
     * @brief Plays the current scene's track called @p name_id from its start.
     *
     * Backs the ANIM_PLAY opcode. A track name is resolved within the current
     * scene and nowhere else, because tracks cannot outlive their scene and a
     * script is not owned by one: the same function may run in several scenes,
     * and each has its own tracks. Naming a track this scene does not have is
     * therefore not a build error, only a script doing nothing.
     *
     * A track already running is left alone. This begins a *run*, which rewinds
     * the playhead and offers every fire-once keyframe again, and a run already
     * under way cannot be begun a second time without deciding what becomes of
     * everything the first one has done. A script that means to start over stops
     * the track first.
     */
    void scene_timeline_play(u16 name_id);

    /**
     * @brief Halts the current scene's track called @p name_id where it stands.
     *
     * Backs the ANIM_STOP opcode. Only scene_timeline_play() starts a track
     * again, and it rewinds, so there is no resume. Stopping a track that is
     * not running is the ordinary way to be sure of its state before playing
     * it, and passes quietly.
     */
    void scene_timeline_stop(u16 name_id);

    /**
     * @brief The TRAK records the current scene owns, in timeline order.
     *
     * Empty when the scene has no timeline. The span points into the loaded
     * game data, so it stays valid until the next scene_timeline_load().
     */
    mtl::span<data::track> scene_timeline_tracks();
}  // namespace NEONengine

#endif  // __SCENE_TIMELINE_H__INCLUDED__
