/**
 * @file scene_timeline.h
 * @brief Owns the tracks of the current scene's timeline.
 *
 * A scene has at most one timeline, and a timeline is only a container: every
 * playable property lives on the TRAK records it owns. Each track is a named,
 * independently playable sequence of heterogeneous keyframes, with its own
 * duration, loop mode, loop count and auto-start bit. Tracks of one timeline
 * run concurrently on independent clocks, and a track started later begins at
 * its own time zero rather than joining one already running.
 *
 * Tracks cannot outlive their scene, so this state is rebuilt by load() and
 * dropped with the object.
 *
 * Unknown keyframe types are logged once at load and skipped.
 *
 * A movement in progress belongs to the graphic instance rather than to the
 * track that started it, because an instance has one position: two tracks
 * moving the same instance are settled by which of them wrote last, in track
 * order, and not by remembering both.
 *
 * The palette bands a track turns are turned here too. A cycle animates by
 * moving colors the scene already has rather than by computing new ones, so a
 * step is a rotation of a band of entries and costs no color arithmetic at all.
 * The rotation is applied to the reference palette - what the scene looks like
 * at full brightness - rather than to the display, so a fade composes with a
 * cycle for nothing: the fade already rebuilds the display from the reference
 * every frame, and picks up the rotation on its way past. When no fade is
 * running there is nothing else to carry the change across, so commit_palette()
 * copies the turned bands over itself.
 */

#ifndef __SCENE_TIMELINE_H__INCLUDED__
#define __SCENE_TIMELINE_H__INCLUDED__

#include "types.h"

#include <stdint.h>

#include <mtl/span.h>
#include <mtl/vector.h>

#include "core/game_data.h"
#include "core/scene_bobs.h"
#include "core/scene_sound_effects.h"
#include "core/track_clock.h"
#include "game_data_format.h"

namespace NEONengine
{
    class scene_timeline
    {
        public:  //////////////////////////////////////////////////////////////////////////////////
        scene_timeline(game_data& data, scene_bobs& bobs, scene_sound_effects& sound_effects)
            : _data(data), _bobs(bobs), _sound_effects(sound_effects)
        {
        }
        scene_timeline(scene_timeline const&)            = delete;
        scene_timeline& operator=(scene_timeline const&) = delete;

        /**
         * @brief Forgets the current scene's tracks.
         */
        ~scene_timeline();

        /**
         * @brief Resolves @p scene's timeline to the tracks it owns and starts them.
         *
         * A scene with no timeline, or whose timeline names a range this build
         * cannot address, simply ends up with no tracks and leaves this inert.
         * Every track marked auto-start is left running at position 0, and the
         * rest wait to be played. Reads only the already-loaded game data, so it
         * needs no system-use guard.
         */
        void load(data::scene const& scene);

        /**
         * @brief Puts every palette band this scene turns back as it was authored.
         *
         * A rotation is a permutation, so turning each band the rest of the way
         * round restores it exactly, and the bands are then held to be untouched.
         *
         * Call this before anything replaces the reference palette. A cycle's
         * record of how far it has turned describes the palette as it actually
         * stands, so loading a new one over the top of a turned band would strand
         * that record against colors it no longer describes: the band would be
         * corrected only by the difference from then on, and never brought back
         * into agreement.
         */
        void restore_palette();

        /**
         * @brief Advances every running track's playhead by one frame.
         *
         * Call from the scene loop each frame. genericProcess runs the state loop
         * exactly once per PAL frame, so one call is one frame of track time; the
         * clock therefore counts frames processed rather than time passed, and a
         * blocking pak read stalls it rather than being skipped over.
         *
         * A track fires whatever its playhead is standing on before moving, so the
         * first frame of a run plays position zero rather than stepping over it.
         */
        void update();

        /**
         * @brief Plays the current scene's track called @p name_id from its start.
         *
         * Backs the ANIM_PLAY opcode. A track name is resolved within the current
         * scene and nowhere else, because tracks cannot outlive their scene and a
         * script is not owned by one: the same function may run in several scenes,
         * and each has its own tracks. Naming a track this scene does not have is
         * therefore not a build error, only a script doing nothing.
         *
         * A track already running is left alone. This begins a *run*, which rewinds
         * the playhead and offers every fire-once keyframe again, and a run already
         * under way cannot be begun a second time without deciding what becomes of
         * everything the first one has done. A script that means to start over stops
         * the track first.
         */
        void play(u16 name_id);

        /**
         * @brief Halts the current scene's track called @p name_id where it stands.
         *
         * Backs the ANIM_STOP opcode. Only play() starts a track again, and it
         * rewinds, so there is no resume. Stopping a track that is not running is
         * the ordinary way to be sure of its state before playing it, and passes
         * quietly.
         */
        void stop(u16 name_id);

        /**
         * @brief The TRAK records the current scene owns, in timeline order.
         *
         * Empty when the scene has no timeline. The span points into the loaded
         * game data, so it stays valid until the next load().
         */
        mtl::span<data::track> tracks();

        private:  /////////////////////////////////////////////////////////////////////////////////
        /// No graphic instance of that id in this scene.
        static constexpr u16 NO_INSTANCE = 0xFFFF;

        /// No neighbouring keyframe on that side.
        static constexpr u16 NO_KEY = 0xFFFF;

        /// No palette entry has been turned since the last commit.
        static constexpr u16 NO_ENTRY = 0xFFFF;

        /**
         * @brief A GFX_MOVE interpolation under way for one graphic instance.
         *
         * Held per instance rather than per track because an instance has one
         * position: two tracks moving the same one are settled by which wrote
         * last, not by both being remembered. @c track names the playhead the
         * span is read against, since every track keeps its own.
         */
        struct glide
        {
            data::point<u16> from{ 0, 0 };  ///< Where the span begins.
            data::point<u16> to{ 0, 0 };    ///< Where it ends.
            u16 t0    = 0;                  ///< Playhead position of @c from.
            u16 t1    = 0;                  ///< Playhead position of @c to; always > t0.
            u16 track = 0;                  ///< Scene-local track driving it.
            bool active = false;            ///< Whether the instance is on a span at all.
        };

        /**
         * @brief A band of palette entries one track turns while its playhead
         *        stands inside a span.
         *
         * A cycle is a span, not a pair of events. Reading it off the playhead's
         * position rather than firing it on a crossing is what makes it behave
         * on a ping-pong track: the return leg walks the positions backwards, so
         * the band turns backwards with it, and it re-enters the span at exactly
         * the offset it left on, so the whole out-and-back is continuous. Fired
         * as events, a reverse leg would meet the END first and leave the band
         * turning everywhere except inside its own span.
         *
         * @c offset is the only state that has to be kept, because it describes
         * the palette as it actually stands: a rotation is a permutation, so
         * turning the rest of the way round restores the authored order exactly
         * and no copy of it is needed.
         */
        struct cycle_span
        {
            u16 track  = 0;  ///< Scene-local track whose playhead drives it.
            u16 t0     = 0;  ///< Position the span opens at.
            u16 t1     = 0;  ///< Position it closes at; always > t0.
            u16 delay  = 1;  ///< Frames each step of the turn is held for.
            u16 width  = 0;  ///< Entries in the band; always >= 2.
            u16 offset = 0;  ///< How far the band currently stands turned.
            u8 first   = 0;  ///< First palette entry of the band.
            u8 last    = 0;  ///< Last entry of the band, inclusive.

            /// Whether the band turns for one traversal of its span only.
            bool fire_once = false;

            /// Whether the playhead stood inside the span last frame.
            bool inside = false;

            /// Whether a fire_once band has had its one traversal.
            bool spent = false;
        };

        /**
         * @brief Where a keyframe sits, for the ones that need to see their neighbours.
         *
         * A GFX_MOVE is only half a movement: the other half is the move before
         * or after it in the same track, so it cannot be run from the record
         * alone the way a script keyframe can.
         */
        struct fire_context
        {
            mtl::span<data::keyframe> keys;       ///< The owning track's keyframes.
            data::track const* record = nullptr;  ///< The owning track's TRAK record.
            u32 base       = 0;                   ///< _fired index of keys[0].
            u16 offset     = 0;                   ///< This keyframe's index in keys.
            u16 track      = 0;                   ///< Scene-local track index.
            bool reversing = false;               ///< Whether the playhead heads back to 0.
        };

        /** @brief Drops every track, playhead, movement and band of this scene. */
        void unload();

        /**
         * @brief The scene-local slot for global GFXI @p id.
         *
         * @return NO_INSTANCE when the id names an instance this scene does not
         *         own, which the load reports once rather than every frame.
         */
        u16 find_instance(u16 id);

        /// @brief @p track's keyframes, in stored order.
        mtl::span<data::keyframe> track_keys(data::track const& track);

        /// @brief Whether the fire_once key at scene-local @p index has already fired.
        bool has_fired(u32 index);

        /// @brief Records that the fire_once key at scene-local @p index has fired.
        void mark_fired(u32 index);

        /**
         * @brief Offers every fire_once key of the track at @p index again.
         *
         * A run is the unit a fire_once keyframe fires once within, and play()
         * always begins a new run, so the marks from the previous one are wiped
         * rather than carried over.
         */
        void clear_fired(u16 index, data::track const& track);

        /**
         * @brief Whether @p track's keyframes really lie inside the KEYS chunk.
         *
         * Playing a track whose range points past the chunk would read whatever
         * follows it, so a track that fails this is never started: not at load,
         * and not by ANIM_PLAY either.
         */
        bool keys_in_range(data::track const& track);

        /**
         * @brief The scene-local index of the track called @p name_id.
         *
         * @return NO_TRACK when this scene has no such track, which is an
         *         ordinary outcome: a script is not owned by a scene, so it can
         *         legitimately name a track that only exists in another one.
         */
        u16 find_track(u16 name_id);

        /**
         * @brief The nearest GFX_MOVE for @p instance on one side of ctx.offset.
         *
         * A spent fire_once key is passed over rather than found: it no longer
         * places its instance, so it cannot bracket a movement either.
         *
         * @param forward Which side to look on, in time order.
         * @return An index into ctx.keys, or NO_KEY when that side holds none.
         */
        u16 neighbour_move(fire_context const& ctx, u16 instance, bool forward);

        /// @brief Takes the instance at @p slot off whatever span it was on.
        void clear_glide(u16 slot);

        /**
         * @brief Puts the instance at @p slot on the span @p t0 to @p t1.
         *
         * The ends are given in time order however the playhead is travelling,
         * which is what lets one expression serve a reverse leg: the position is
         * read off the span rather than accumulated along it.
         */
        void set_glide(u16 slot,
            u16 track,
            data::point<u16> from,
            u16 t0,
            data::point<u16> dest,
            u16 t1);

        /**
         * @brief Places every gliding instance where its track's playhead says.
         *
         * Computed from the playhead rather than stepped along, so a span costs
         * nothing to re-enter, gathers no drift however long it is, lands exactly
         * on its far end, and reads the same backwards as forwards. The divide
         * this costs is some 44 cycles against a frame's ~280,000; the search it
         * replaces was the expensive part, and the cached span removes that.
         */
        void update_glides();

        /**
         * @brief Turns every palette band to where its track's playhead says.
         *
         * Read off the playhead for the same reasons the glides are, and with
         * one more: a ping-pong's return leg walks the positions backwards, so
         * the band simply turns backwards with it, and re-enters its span on
         * exactly the offset it left on.
         *
         * Only the difference from where the band already stands is applied, so
         * the usual frame turns it by one step or not at all, and a playhead
         * that jumped costs one pass over the band rather than one per step.
         */
        void update_cycles();

        /**
         * @brief Leaves every band of this scene standing as it was authored.
         *
         * A rotation is a permutation, so turning each band the rest of the way
         * round restores it exactly. Worth doing on the way out because a scene
         * without a background of its own loads no palette and would otherwise
         * inherit the last scene's bands mid-turn.
         */
        void restore_cycles();

        /**
         * @brief Offers the bands of the track at @p index their traversal again.
         *
         * The counterpart to clear_fired(): a run is the unit a fire_once band
         * turns once within. The offset is left alone, because it describes the
         * palette as it actually stands rather than a policy; the band picks up
         * from there when the playhead next reaches the span.
         */
        void clear_cycles(u16 index);

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
        void rotate_palette_band(u8 first, u8 last, u16 steps);

        /**
         * @brief Puts this frame's rotations on the display.
         *
         * Copies every entry turned since the last commit from the reference
         * palette to the display and rebuilds the hardware palette once, or leaves
         * both to the fade when one is running. Does nothing at all if nothing
         * turned.
         *
         * The whole hardware palette is rebuilt by a single copperlist write,
         * which is far from free at 8 bitplanes, so a frame that turns nothing
         * pays nothing and a frame that turns several bands still only rebuilds
         * once.
         */
        void commit_palette();

        /**
         * @brief Runs @p key as a GFX_MOVE and works out what comes next.
         *
         * The instance lands exactly on the keyframe's position, and the span
         * the playhead is now entering is cached so that no later frame has to
         * search for it.
         */
        bool fire_move(data::keyframe const& key, fire_context const& ctx);

        bool fire_visibility(data::keyframe const& key);

        bool fire_sfx(data::keyframe const& key);

        bool fire_music(data::keyframe const& key);

        bool fire_script(data::keyframe const& key, u16 track_index);

        /**
         * @brief Runs @p key, if this slice knows how to.
         *
         * @return Whether the keyframe actually fired. A keyframe that did not
         *         fire has not used up its fire_once, so a looping track will
         *         offer it again rather than losing it for the rest of the run.
         */
        bool fire(data::keyframe const& key, fire_context const& ctx);

        /**
         * @brief Sets up the spans a cycle of @p track begins already on.
         *
         * A move that no earlier move leads up to has nowhere to start from but
         * where its instance already stands, so its span is established here
         * rather than at a crossing: the instance has to be moving before the
         * playhead arrives, and the crossing is where it arrives.
         *
         * Run at the head of every cycle rather than only of every run, so that
         * a looping track's second cycle leads into its first move exactly as
         * its first cycle did instead of teleporting there.
         */
        void prime_glides(u16 track_index, data::track const& track);

        /**
         * @brief Collects the palette bands the track at @p index turns.
         *
         * A START and its END are paired here rather than at fire time, so that
         * a band whose records do not make a span is complained about once at
         * load instead of fifty times a second. The two are always in the same
         * track, which is why an END can name nothing but a cycle id and still
         * be unambiguous.
         */
        void collect_cycles(u16 track_index, data::track const& track);

        /**
         * @brief Fires whatever the track at @p track_index has under its playhead.
         */
        void fire_keyframes(u16 track_index, data::track const& track);

        game_data&           _data;
        scene_bobs&          _bobs;
        scene_sound_effects& _sound_effects;

        u16 _first = 0;  ///< First TRAK id this scene owns.
        u16 _count = 0;  ///< How many it owns; 0 leaves the scene without a timeline.

        u16 _gfxi_first = 0;  ///< First GFXI id this scene owns.
        u16 _gfxi_count = 0;  ///< How many it owns.

        mtl::vector<track_clock> _clocks;  ///< One playhead per owned track.

        /// Scene-local index of each track's first keyframe, for indexing _fired.
        mtl::vector<u32> _key_base;

        /// One bit per scene-local keyframe: whether a fire_once key has fired.
        mtl::vector<u8> _fired;

        /// One slot per graphic instance the scene owns.
        mtl::vector<glide> _glides;

        /**
         * @brief Every band this scene's timeline turns, in no particular order.
         *
         * Two bands of one track may not overlap, which Build enforces. Two
         * bands of different tracks may, because their clocks are independent
         * and Build cannot know where either playhead will be. Overlapping
         * rotations do not commute, so an authored overlap scrambles both bands
         * and neither offset describes the palette any more. That is left to
         * happen: it costs nothing that matters - the next scene load replaces
         * the palette outright - and correcting it would mean undoing and
         * reapplying every band on every frame any one of them stepped.
         */
        mtl::vector<cycle_span> _cycles;

        /// Lowest and highest palette entry turned since the last commit, inclusive.
        u16 _palette_dirty_first = NO_ENTRY;
        u16 _palette_dirty_last  = 0;
    };
}  // namespace NEONengine

#endif  // __SCENE_TIMELINE_H__INCLUDED__
