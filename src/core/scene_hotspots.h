/**
 * @file scene_hotspots.h
 * @brief Runs a scene's HOTS interaction regions against the mouse.
 */

#ifndef __SCENE_HOTSPOTS_H__INCLUDED__
#define __SCENE_HOTSPOTS_H__INCLUDED__

#include "types.h"

#include <stdint.h>

#include "game_data_format.h"

namespace NEONengine
{
    /**
     * @brief Builds the interaction set for @p scene from its HOTS range.
     *
     * Seeds each hotspot's enabled state from its record and precomputes the
     * bounds enclosing them all, so a pointer outside the scene's interactive
     * area costs one test instead of a scan.
     */
    void scene_hotspots_load(data::scene const& scene);

    /**
     * @brief Forgets the current scene's interaction set.
     */
    void scene_hotspots_clear();

    /**
     * @brief Tracks the pointer and dispatches a completed click.
     *
     * Call from the scene loop each frame. Hovering follows the topmost enabled
     * hotspot under the pointer; a press latches onto it, and releasing over
     * that same hotspot activates it, so dragging away cancels instead.
     * Activating runs the hotspot's click script and requests its scene
     * transition.
     */
    void scene_hotspots_update();

    /**
     * @brief Enables or disables the hotspot @p id, as HOTSPOT_ON/OFF do.
     *
     * @param id A global HOTS id; ids outside the current scene are ignored.
     * @param enabled True to let the hotspot respond to the pointer.
     */
    void scene_hotspots_set_enabled(u16 id, bool enabled);
}  // namespace NEONengine

#endif  // __SCENE_HOTSPOTS_H__INCLUDED__
