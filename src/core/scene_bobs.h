/**
 * @file scene_bobs.h
 * @brief Draws a scene's GFXI graphic instances as ACE BOBs.
 *
 * Scope of the current slice: each visible instance is drawn in GFXI order and
 * plays its graphic's looping frame animation (graphic.frame_delay).
 * Deliberately not handled yet: z-order sorting and runtime move/visibility.
 * ACE also does no off-screen clipping and tracks bob counts/undraw size in
 * 8-/16-bit fields, so scenes must be validated/capped (editor side) before
 * many or off-screen bobs are used.
 */

#ifndef __SCENE_BOBS_H__INCLUDED__
#define __SCENE_BOBS_H__INCLUDED__

#include "game_data_format.h"

namespace NEONengine
{
    /**
     * @brief Creates the ACE bob manager over the screen's double buffer.
     *
     * Call once when the scene state is created, after the screen exists.
     * scene_bobs_destroy() releases it.
     */
    void scene_bobs_create();

    /**
     * @brief Destroys the bob manager and frees all per-scene bob assets.
     */
    void scene_bobs_destroy();

    /**
     * @brief Builds the bob set for @p scene from its GFXI instances.
     *
     * Loads each instanced graphic's frame strip and mask into Chip RAM,
     * overlays the graphic's colors into its BOB palette-slot band (the caller
     * must place the background palette first), and initializes one bob per
     * instance at frame 0. Reads the pak, so call under a system-use guard.
     */
    void scene_bobs_load(data::scene const& scene);

    /**
     * @brief Advances every visible graphic's looping frame animation.
     *
     * Call from the scene loop each frame, before scene_bobs_draw(). Static
     * graphics (frame_delay 0 or a single frame) are left untouched.
     */
    void scene_bobs_update();

    /**
     * @brief Draws every bob into the current back buffer.
     *
     * Call from the scene loop each frame, before the screen is flipped.
     */
    void scene_bobs_draw();
}  // namespace NEONengine

#endif  // __SCENE_BOBS_H__INCLUDED__
