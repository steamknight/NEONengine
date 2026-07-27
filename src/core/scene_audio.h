/**
 * @file scene_audio.h
 * @brief Holds the sound effects the current scene's timeline can play.
 *
 * A sound effect has to be resident before it is asked for: ptplayer replays
 * samples straight out of Chip RAM, and a keyframe firing at 50Hz cannot stop
 * to read the pak. So every distinct SFX_PLAY in the scene's timeline is loaded
 * once when the scene loads and freed when it ends, which is the same bargain
 * scene_bobs makes with its graphics.
 *
 * Music is not held here. A module is far larger than an effect and a scene
 * may name several, so MUSIC_PLAY reads its module when it fires and pays for
 * it with a hitch, rather than every scene paying up front for songs it might
 * never reach.
 *
 * Nothing stops an effect. A one-shot is over in a few frames, so the replayer
 * is left to choose the channel and there is no channel state to keep.
 */

#ifndef __SCENE_AUDIO_H__INCLUDED__
#define __SCENE_AUDIO_H__INCLUDED__

#include <stdint.h>

#include "game_data_format.h"

namespace NEONengine
{
    /**
     * @brief Loads every sound effect @p scene's timeline can play.
     *
     * Reads the pak, so call under a system-use guard. An effect that cannot be
     * found or loaded is reported once here and then silently does nothing,
     * because the alternative is a keyframe complaining fifty times a second.
     */
    void scene_audio_load(data::scene const& scene);

    /**
     * @brief Frees the current scene's sound effects.
     *
     * Reads no pak but returns memory to the OS, so call under a system-use
     * guard.
     */
    void scene_audio_clear();

    /**
     * @brief Plays the sound effect @p hash names.
     *
     * The replayer picks the least busy channel and hands it back to the music
     * when the sound ends. Nothing stops an effect: one is over in a few
     * frames, long before anything could ask, so there is no channel to name
     * and nothing to keep track of.
     *
     * @param hash SFX pak-path hash, as the keyframe stores it.
     */
    void scene_audio_play(uint32_t hash);
}  // namespace NEONengine

#endif  // __SCENE_AUDIO_H__INCLUDED__
