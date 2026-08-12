/**
 * @file scene_sound_effects.h
 * @brief Holds the sound effects the current scene can play.
 *
 * A sound effect has to be resident before it is asked for: ptplayer replays
 * samples straight out of Chip RAM, and neither a keyframe firing at 50Hz nor a
 * click can stop to read the pak. So every distinct sound the scene names, from
 * an SFX_PLAY keyframe or a hotspot's click, is loaded once when the scene loads
 * and freed when it ends, which is the same bargain scene_bobs makes with its
 * graphics.
 *
 * Music is not held here, which is why this is not the scene's audio. A module
 * is far larger than an effect and a scene may name several, so MUSIC_PLAY reads
 * its module when it fires and pays for it with a hitch, rather than every scene
 * paying up front for songs it might never reach.
 *
 * Nothing stops an effect. A one-shot is over in a few frames, so the replayer
 * is left to choose the channel and there is no channel state to keep.
 */

#ifndef __SCENE_SOUND_EFFECTS_H__INCLUDED__
#define __SCENE_SOUND_EFFECTS_H__INCLUDED__

#include "types.h"

#include <stdint.h>

#include <ace/managers/ptplayer.h>

#include <mtl/vector.h>

#include "core/game_data.h"
#include "game_data_format.h"

namespace NEONengine
{
    class scene_sound_effects
    {
        public:  //////////////////////////////////////////////////////////////////////////////////
        explicit scene_sound_effects(game_data& data) : _data(data) {}
        scene_sound_effects(scene_sound_effects const&)            = delete;
        scene_sound_effects& operator=(scene_sound_effects const&) = delete;

        /**
         * @brief Frees the effects held.
         *
         * Returns memory to the OS, so destroy this under a system-use guard.
         */
        ~scene_sound_effects();

        /**
         * @brief Loads every sound effect @p scene can play, dropping the last set.
         *
         * Covers both the scene's timeline keyframes and its hotspots' click
         * sounds. Reads the pak, so call under a system-use guard. An effect
         * that cannot be found or loaded is reported once here and then
         * silently does nothing, because the alternative is a keyframe
         * complaining fifty times a second.
         */
        void load(data::scene const& scene);

        /**
         * @brief Plays the sound effect @p hash names.
         *
         * The replayer picks the least busy channel and hands it back to the
         * music when the sound ends. Nothing stops an effect: one is over in a
         * few frames, long before anything could ask, so there is no channel to
         * name and nothing to keep track of.
         *
         * @param hash SFX pak-path hash, as the keyframe stores it.
         */
        void play(u32 hash);

        private:  /////////////////////////////////////////////////////////////////////////////////
        /// One loaded effect, keyed by the hash its keyframes name it with.
        struct entry
        {
            u32           hash{ 0 };
            tPtplayerSfx* data{ nullptr };
        };

        /// @brief The loaded effect @p hash names, or nullptr.
        tPtplayerSfx* find(u32 hash) const;

        /**
         * @brief Loads the effect @p hash names, unless it is already loaded.
         *
         * Several keyframes naming one sound is the ordinary case, and a track
         * that loops names it again every cycle, so the table is keyed by hash
         * and each sound is read exactly once.
         */
        void require(u32 hash);

        /** @brief Loads the effects named by the keyframes of one track. */
        void load_track(data::track const& track);

        /** @brief Loads the effects @p scene's timeline can play. */
        void load_timeline(data::scene const& scene);

        /**
         * @brief Loads the effects @p scene's hotspots play when clicked.
         *
         * A hotspot that starts disabled is loaded all the same: a script can
         * switch it on at any point, and the click that follows cannot stop to
         * read the pak any more than a keyframe can.
         */
        void load_hotspots(data::scene const& scene);

        /** @brief Frees every effect held and empties the table. */
        void unload();

        game_data&         _data;
        mtl::vector<entry> _effects;
    };
}  // namespace NEONengine

#endif  // __SCENE_SOUND_EFFECTS_H__INCLUDED__
