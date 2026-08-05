/**
 * @file dialogue_portraits.h
 * @brief The speaker portraits one conversation can put on screen.
 *
 * A portrait is read from the pak, which blocks, so every distinct one a
 * dialogue's pages name is loaded when the conversation starts rather than as
 * each speaker takes their turn: the set is knowable from the pages, and a
 * pause as the box appears reads as normal where one mid-conversation reads as
 * a stall.
 *
 * Portrait colors live in their own palette band, the way a bob's colors live
 * in its slot: showing a portrait means putting its colors in the band and
 * drawing artwork whose indices point there. Portraits are authored at 32
 * colors with their own local indices, so each one is rebuilt at the screen's
 * depth with the band's base folded in when it is loaded - once per
 * conversation rather than once per blit. The band belongs to the scene's
 * reverse bob slots the rest of the time, so what was there is put back when
 * the conversation ends.
 *
 * One conversation at a time, like the state that draws it.
 */

#ifndef __DIALOGUE_PORTRAITS_H__INCLUDED__
#define __DIALOGUE_PORTRAITS_H__INCLUDED__

#include "types.h"

#include "core/game_data_format.h"

namespace NEONengine
{
    /// Every portrait is authored at this size; nothing scales on the Amiga.
    constexpr u16 PORTRAIT_WIDTH  = 96;
    constexpr u16 PORTRAIT_HEIGHT = 96;

    /**
     * @brief The palette band a portrait's colors are overlaid into.
     *
     * Indices 32..63, which are the scene's reverse bob slots 2..5 the rest of
     * the time. The portrait and the inventory both take this band, which is
     * why the two are never on screen together.
     */
    constexpr u16 PORTRAIT_PALETTE_FIRST  = 32;
    constexpr u16 PORTRAIT_PALETTE_COLORS = 32;

    /**
     * @brief Loads every distinct portrait the pages in @p page_range name.
     *
     * Reads the pak and takes system use itself, so it blocks; call it as the
     * conversation starts and not again. A portrait that cannot be read is
     * logged and left out, and its speaker then simply has none.
     *
     * Replaces whatever a previous conversation loaded.
     *
     * @param page_range The running dialogue's PAGS range.
     */
    void dialogue_portraits_load(data::range page_range);

    /**
     * @brief Releases every portrait and hands the palette band back.
     *
     * Safe to call when nothing was loaded, so the state can call it on every
     * way out of the box.
     */
    void dialogue_portraits_unload();

    /**
     * @brief Whether @p speaker_id has a portrait that can be drawn.
     *
     * False for a page with no speaker, a speaker authored without a portrait,
     * and a speaker whose portrait failed to load - the three cases where the
     * frame takes the whole width instead.
     */
    bool dialogue_portraits_has(u16 speaker_id);

    /**
     * @brief Draws @p speaker_id's portrait at @p x, @p y through its mask.
     *
     * Puts that portrait's colors in the band first, so only one portrait's
     * colors are ever live - which is what the band being 32 entries for a
     * whole conversation means. Does nothing when the speaker has no portrait.
     *
     * Takes scene-space coordinates, as screenBlitCopy does.
     */
    void dialogue_portraits_draw(u16 speaker_id, u16 x, u16 y);
}  // namespace NEONengine

#endif  // __DIALOGUE_PORTRAITS_H__INCLUDED__
