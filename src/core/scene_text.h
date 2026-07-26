/**
 * @file scene_text.h
 * @brief Draws a scene's TXTR text regions into the screen backdrop.
 *
 * A text region is static: its box, color and string are fixed by the data, and
 * only a language change alters what it says. So the regions are painted into
 * the back buffer as part of the scene's backdrop rather than carried as bobs.
 * A bob would cost an undraw buffer the size of its box on every frame, and the
 * format lets a region cover the whole screen - one such region would want more
 * undraw space than the screen buffers themselves.
 *
 * Being backdrop puts them under every bob, which is what the format describes:
 * z_order runs between text regions, and a scene's bobs undraw against them
 * correctly because the backdrop is mirrored into both buffers before any bob
 * is built.
 */

#ifndef __SCENE_TEXT_H__INCLUDED__
#define __SCENE_TEXT_H__INCLUDED__

#include "game_data_format.h"

namespace NEONengine
{
    /**
     * @brief Draws every text region @p scene owns into the screen's back buffer.
     *
     * Renders each region's string in the language now in force, places it in
     * its box per the region's alignment bits, and blits it. Call while drawing
     * the backdrop, before the back buffer is mirrored to the front and before
     * any bob is registered.
     *
     * Allocates through ACE's font helpers and reads the active string table,
     * so call under a system-use guard. Nothing is retained: the rendered text
     * lives only for the blit.
     */
    void scene_text_draw(data::scene const& scene);
}  // namespace NEONengine

#endif  // __SCENE_TEXT_H__INCLUDED__
