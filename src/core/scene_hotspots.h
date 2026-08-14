/**
 * @file scene_hotspots.h
 * @brief Runs a scene's HOTS interaction regions against the mouse.
 */

#ifndef __SCENE_HOTSPOTS_H__INCLUDED__
#define __SCENE_HOTSPOTS_H__INCLUDED__

#include "types.h"

#include <stdint.h>

#include <mtl/span.h>
#include <mtl/vector.h>

#include "core/game_data.h"
#include "core/mouse_pointer.h"
#include "core/scene_bobs.h"
#include "core/scene_sound_effects.h"
#include "game_data_format.h"

namespace NEONengine
{
    class scene_hotspots
    {
        public:  //////////////////////////////////////////////////////////////////////////////////
        scene_hotspots(game_data& data,
            scene_bobs&          bobs,
            scene_sound_effects& sound_effects,
            mouse_pointer&       pointer)
            : _data(data), _bobs(bobs), _sound_effects(sound_effects), _pointer(pointer)
        {
        }
        scene_hotspots(scene_hotspots const&)            = delete;
        scene_hotspots& operator=(scene_hotspots const&) = delete;

        /**
         * @brief Forgets the current scene's interaction set.
         *
         * Takes the hover caption down with it, so the bobs must outlive this.
         */
        ~scene_hotspots();

        /**
         * @brief Builds the interaction set for @p scene from its HOTS range.
         *
         * Seeds each hotspot's enabled state from its record and precomputes the
         * bounds enclosing them all, so a pointer outside the scene's interactive
         * area costs one test instead of a scan.
         */
        void load(data::scene const& scene);

        /**
         * @brief Tracks the pointer and dispatches a completed click.
         *
         * Call from the scene loop each frame. Hovering follows the topmost enabled
         * hotspot under the pointer; a press latches onto it, and releasing over
         * that same hotspot activates it, so dragging away cancels instead.
         * Activating runs the hotspot's click script and requests its scene
         * transition.
         */
        void update();

        /**
         * @brief Take the button as it stands next update, without reading an edge.
         *
         * Anything that stops this from running for a stretch has to call it before
         * play resumes. The button may have been pressed or released unseen in the
         * meantime, and the press that dismissed whatever was covering the scene
         * would otherwise read as a fresh click on whatever lies under the pointer.
         */
        void resync_button();

        /**
         * @brief Enables or disables the hotspot @p id, as HOTSPOT_ON/OFF do.
         *
         * @param id A global HOTS id; ids outside the current scene are ignored.
         * @param enabled True to let the hotspot respond to the pointer.
         */
        void set_enabled(u16 id, bool enabled);

        private:  /////////////////////////////////////////////////////////////////////////////////
        /** @brief Drops the interaction set and stops drawing the caption. */
        void unload();

        /**
         * @brief The hotspot records this scene owns.
         */
        mtl::span<data::hotspot> scene_records();

        /**
         * @brief The frame the hotspot @p index's state graphic should show.
         */
        hotspot_frame state_frame(u16 index);

        /**
         * @brief Pushes the hotspot @p index's current state to its graphic.
         *
         * Ignores NO_HOTSPOT so callers can pass a "previously hovered" slot
         * without checking it first.
         */
        void refresh_state_frame(u16 index);

        /**
         * @brief Run the hotspot @p index was clicked.
         *
         * The sound goes first so it is heard at all: the script may block on a
         * dialogue for as long as it likes, and a goto_scene tears this scene
         * down. Then the script runs, so it observes the state the click found,
         * and finally the record's own transition is requested; a concrete
         * goto_scene wins over whatever the script may have asked for.
         */
        void activate(u16 index);

        /// No hotspot: an index that names none, in either slot.
        static constexpr u16 NO_HOTSPOT = 0xFFFF;

        game_data&           _data;
        scene_bobs&          _bobs;
        scene_sound_effects& _sound_effects;
        mouse_pointer&       _pointer;

        u16 _first = 0;  ///< First HOTS id this scene owns.
        u16 _count = 0;  ///< How many it owns; 0 disables the whole set.

        /// One byte per hotspot, seeded from its record and flipped by scripts.
        mtl::vector<u8> _enabled;

        u16 _hovered = NO_HOTSPOT;  ///< Scene-local index under the pointer.
        u16 _pressed = NO_HOTSPOT;  ///< Scene-local index the press latched onto.
        bool _was_down     = false;  ///< Last frame's button state, for edges.
        bool _resyncButton = false;  ///< Adopt the button state, skip edges.

        /// Scene-space box enclosing every hotspot; maxima are exclusive.
        u16 _min_x = 0;
        u16 _min_y = 0;
        u16 _max_x = 0;
        u16 _max_y = 0;
    };
}  // namespace NEONengine

#endif  // __SCENE_HOTSPOTS_H__INCLUDED__
