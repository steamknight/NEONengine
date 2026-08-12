/**
 * @file debug_keys.h
 * @brief The function keys that report on, or poke at, a running build.
 *
 * These belong to whoever is working on the engine, not to the game, and none
 * of them is part of a frame. They are kept apart from the frame for that
 * reason: the loop should read as what the game does, with the developer's
 * instruments off to one side.
 */

#ifndef __DEBUG_KEYS_H__INCLUDED__
#define __DEBUG_KEYS_H__INCLUDED__

namespace NEONengine
{
    /**
     * @brief Acts on whichever of the debug keys was pressed.
     *
     * Runs after the frame it reports on, so what it shows is what the player
     * is looking at, and so a key that pushes a state acts on the stack the
     * frame left behind.
     *
     * F1 stats overlay, F2 free memory, F3 asset cache report, F4 empty the
     * asset cache, F5 mute ACE's own logging.
     */
    void debug_keys_process();
}  // namespace NEONengine

#endif  // __DEBUG_KEYS_H__INCLUDED__
