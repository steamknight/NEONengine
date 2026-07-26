/**
 * @file scene_bobs.h
 * @brief Draws a scene's GFXI graphic instances and hotspot state graphics as ACE BOBs.
 *
 * ACE allows one bob set per manager - reset, then every bobInit(), then a
 * single bobReallocateBuffers() - so this module owns the whole scene's set
 * rather than splitting that sequence across callers. Instances animate on
 * graphic.frame_delay; hotspot graphics instead pick their frame from the
 * hotspot's state, which scene_hotspots reports through
 * scene_bobs_set_hotspot_frame().
 *
 * Scope of the current slice: instances are drawn in GFXI order, hotspot
 * graphics on top of them, and the hover caption above everything. Deliberately
 * not handled yet: z-order sorting and runtime move/visibility. ACE also does no
 * off-screen clipping and tracks bob counts/undraw size in 8-/16-bit fields, so
 * scenes must be validated/capped (editor side) before many or off-screen bobs
 * are used.
 */

#ifndef __SCENE_BOBS_H__INCLUDED__
#define __SCENE_BOBS_H__INCLUDED__

#include <stdint.h>

#include "game_data_format.h"

namespace NEONengine
{
    /// Frames a hotspot state graphic must supply, one per hotspot state.
    constexpr uint8_t HOTSPOT_STATE_FRAMES = 4;

    /// Which frame of a hotspot's state graphic each state draws.
    enum class hotspot_frame : uint8_t
    {
        NORMAL   = 0,
        HOVERED  = 1,
        PRESSED  = 2,
        DISABLED = 3,
    };

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
     * @brief Builds the bob set for @p scene: its GFXI instances and hotspot graphics.
     *
     * Loads each graphic's frame strip and mask into Chip RAM, overlays its
     * colors into its BOB palette-slot band (the caller must place the
     * background palette first), and initializes one bob per instance and per
     * hotspot that carries a state graphic. Hotspot graphics start on the
     * normal frame; scene_hotspots_load() then applies each hotspot's initial
     * state. Reads the pak, so call under a system-use guard.
     */
    void scene_bobs_load(data::scene const& scene);

    /**
     * @brief Shows @p frame of the hotspot @p index's state graphic.
     *
     * @param index Scene-local hotspot index, as scene_hotspots numbers them.
     *              Indices without a state graphic are ignored.
     */
    void scene_bobs_set_hotspot_frame(uint16_t index, hotspot_frame frame);

    /**
     * @brief Advances every visible graphic instance's looping frame animation.
     *
     * Call from the scene loop each frame, before scene_bobs_draw(). Static
     * graphics (frame_delay 0 or a single frame) are left untouched. Hotspot
     * graphics are driven by state, not time, so they are not advanced here.
     */
    void scene_bobs_update();

    /**
     * @brief Draws every bob into the current back buffer.
     *
     * Call from the scene loop each frame, before the screen is flipped.
     */
    void scene_bobs_draw();

    /**
     * @brief Shows the hotspot @p index's hover caption anchored near the pointer.
     *
     * @p anchorX and @p anchorY are the pointer position in display space; the
     * caption is centred on it and pushed inside the screen edges, flipping
     * above the pointer when it would not fit below. Hotspots without a hover
     * string, or whose string is empty, show nothing.
     *
     * @param index Scene-local hotspot index, as scene_hotspots numbers them.
     */
    void scene_bobs_show_caption(uint16_t index, uint16_t anchorX, uint16_t anchorY);

    /**
     * @brief Stops drawing the hover caption.
     */
    void scene_bobs_hide_caption();

    /**
     * @brief Re-renders every caption from the language now in force.
     *
     * Captions are rendered once per scene from the active string table, so a
     * language change must rebuild them. Reads nothing from disk but allocates
     * through ACE's font helpers, so call under a system-use guard.
     */
    void scene_bobs_reload_captions();
}  // namespace NEONengine

#endif  // __SCENE_BOBS_H__INCLUDED__
