/**
 * @file palette_cycle.h
 * @brief Rotates bands of the scene palette, and gets them onto the display.
 *
 * A palette cycle animates by moving colors the scene already has rather than
 * by computing new ones, so a step is a rotation of a band of entries and costs
 * no color arithmetic at all.
 *
 * Which band turns and how far is the timeline's business; this module owns
 * only the palette itself. It rotates the reference palette - what the scene
 * looks like at full brightness - rather than the display, so a fade composes
 * with a cycle for nothing: the fade already rebuilds the display from the
 * reference every frame, and picks up the rotation on its way past. When no
 * fade is running there is nothing else to carry the change across, so commit()
 * copies the turned bands over itself.
 *
 * The whole hardware palette is rebuilt by a single copperlist write, which is
 * far from free at 8 bitplanes, so a frame that turns nothing pays nothing and
 * a frame that turns several bands still only rebuilds once.
 */

#ifndef __PALETTE_CYCLE_H__INCLUDED__
#define __PALETTE_CYCLE_H__INCLUDED__

#include "types.h"

#include <stdint.h>

namespace NEONengine
{
    /**
     * @brief Rotates the reference palette band [@p first, @p last] by @p steps.
     *
     * Colors move towards the higher index, with @p last wrapping round to
     * @p first, which is the one direction the format can express. Rotating by
     * the width of the band leaves it exactly as it was, so a caller holding an
     * offset can always get back to the authored order by turning the rest of
     * the way round rather than by keeping a copy of it.
     *
     * The rotation is exact and in place: it moves the entries the band already
     * holds, so nothing accumulates and nothing has to be remembered here.
     *
     * @param first  First entry of the band.
     * @param last   Last entry of the band, inclusive; a band with last <= first
     *               is not a band and is ignored.
     * @param steps  How far to turn. Taken modulo the band width, so a caller
     *               may pass any value.
     */
    void palette_cycle_rotate(u8 first, u8 last, u16 steps);

    /**
     * @brief Puts this frame's rotations on the display.
     *
     * Copies every entry turned since the last commit from the reference
     * palette to the display and rebuilds the hardware palette once, or leaves
     * both to the fade when one is running. Does nothing at all if nothing
     * turned.
     */
    void palette_cycle_commit();
}  // namespace NEONengine

#endif  // __PALETTE_CYCLE_H__INCLUDED__
