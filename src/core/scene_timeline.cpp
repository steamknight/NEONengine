#include "scene_timeline.h"

#include "neonengine.h"


#include <mtl/utility.h>
#include <mtl/vector.h>

#include "core/game_data.h"
#include "core/logger.h"
#include "core/music.h"
#include "core/palette_cycle.h"
#include "core/scene_audio.h"
#include "core/scene_bobs.h"

NE_LOG_DOMAIN(Timeline);

namespace NEONengine
{
    namespace
    {
        constexpr uint16_t NO_TIMELINE = 0xFFFF;

        /// No track of that name in this scene.
        constexpr uint16_t NO_TRACK = 0xFFFF;

        /// No graphic instance of that id in this scene.
        constexpr uint16_t NO_INSTANCE = 0xFFFF;

        /// No neighbouring keyframe on that side.
        constexpr uint16_t NO_KEY = 0xFFFF;

        /// Cycle boundaries logged per track before the module falls silent.
        constexpr uint16_t LOGGED_CYCLES = 3;

        uint16_t s_first = 0;  ///< First TRAK id this scene owns.
        uint16_t s_count = 0;  ///< How many it owns; 0 disables the whole module.

        uint16_t s_gfxi_first = 0;  ///< First GFXI id this scene owns.
        uint16_t s_gfxi_count = 0;  ///< How many it owns.

        mtl::vector<track_clock> s_clocks;  ///< One playhead per owned track.

        /// Scene-local index of each track's first keyframe, for indexing s_fired.
        mtl::vector<uint32_t> s_key_base;

        /// One bit per scene-local keyframe: whether a fire_once key has fired.
        mtl::vector<uint8_t> s_fired;

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
            data::point<uint16_t> from{ 0, 0 };  ///< Where the span begins.
            data::point<uint16_t> to{ 0, 0 };    ///< Where it ends.
            uint16_t t0    = 0;                  ///< Playhead position of @c from.
            uint16_t t1    = 0;                  ///< Playhead position of @c to; always > t0.
            uint16_t track = 0;                  ///< Scene-local track driving it.
            bool active    = false;              ///< Whether the instance is on a span at all.
        };

        /// One slot per graphic instance the scene owns.
        mtl::vector<glide> s_glides;

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
            uint16_t track  = 0;  ///< Scene-local track whose playhead drives it.
            uint16_t t0     = 0;  ///< Position the span opens at.
            uint16_t t1     = 0;  ///< Position it closes at; always > t0.
            uint16_t delay  = 1;  ///< Frames each step of the turn is held for.
            uint16_t width  = 0;  ///< Entries in the band; always >= 2.
            uint16_t offset = 0;  ///< How far the band currently stands turned.
            uint8_t first   = 0;  ///< First palette entry of the band.
            uint8_t last    = 0;  ///< Last entry of the band, inclusive.

            /// Whether the band turns for one traversal of its span only.
            bool fire_once = false;

            /// Whether the playhead stood inside the span last frame.
            bool inside = false;

            /// Whether a fire_once band has had its one traversal.
            bool spent = false;
        };

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
        mtl::vector<cycle_span> s_cycles;

        /**
         * @brief The ANIM_WAIT a blocked script is sitting on, if any.
         *
         * Held as a record rather than a sentinel index because "no track of
         * that name in this scene" is a normal wait that has to be released,
         * not the absence of a wait, and the two cannot share an encoding with
         * a track index without eventually colliding with a real one.
         */
        struct wait_state
        {
            uint16_t name  = 0;      ///< Track name the script asked for.
            uint16_t track = 0;      ///< Scene-local index; only valid when found.
            uint8_t depth  = 0;      ///< Waits on the blocking counter; 0 = none.
            bool found     = false;  ///< Whether this scene has that track.
        };

        wait_state s_wait;


        /**
         * @brief Names @p mode the way the editor and the format docs do.
         */
        char const* loop_mode_name(uint8_t mode)
        {
            switch (mtl::to<data::loop_mode>(mode))
            {
                case data::loop_mode::NONE: return "None";
                case data::loop_mode::RESTART: return "Restart";
                case data::loop_mode::PING_PONG: return "PingPong";
                default: return "?";
            }
        }

        /**
         * @brief Names @p type the way the editor and the format docs do.
         */
        char const* keyframe_type_name(uint8_t type)
        {
            switch (mtl::to<data::keyframe_type>(type))
            {
                case data::keyframe_type::GFX_MOVE: return "GFX_MOVE";
                case data::keyframe_type::GFX_VISIBILITY: return "GFX_VISIBILITY";
                case data::keyframe_type::PALETTE_CYCLE_START: return "PALETTE_CYCLE_START";
                case data::keyframe_type::PALETTE_CYCLE_END: return "PALETTE_CYCLE_END";
                case data::keyframe_type::SFX_PLAY: return "SFX_PLAY";
                case data::keyframe_type::MUSIC_PLAY: return "MUSIC_PLAY";
                case data::keyframe_type::MUSIC_STOP: return "MUSIC_STOP";
                case data::keyframe_type::EXECUTE_SCRIPT: return "EXECUTE_SCRIPT";
                default: return "?";
            }
        }

        /// @brief Whether this slice acts on @p type at all.
        bool is_implemented(uint8_t type)
        {
            switch (mtl::to<data::keyframe_type>(type))
            {
                case data::keyframe_type::GFX_MOVE:
                case data::keyframe_type::GFX_VISIBILITY:
                case data::keyframe_type::PALETTE_CYCLE_START:
                case data::keyframe_type::PALETTE_CYCLE_END:
                case data::keyframe_type::SFX_PLAY:
                case data::keyframe_type::MUSIC_PLAY:
                case data::keyframe_type::MUSIC_STOP:
                case data::keyframe_type::EXECUTE_SCRIPT: return true;
                default: return false;
            }
        }

        /// @brief Whether @p key moves a graphic instance.
        bool is_move(data::keyframe const& key)
        {
            return mtl::to<data::keyframe_type>(key.type) == data::keyframe_type::GFX_MOVE;
        }

        /// @brief @p key read as a GFX_MOVE; only call it when is_move() holds.
        data::gfx_move_payload const& move_of(data::keyframe const& key)
        {
            return *mtl::force_to<data::gfx_move_payload const*>(key.payload);
        }

        /**
         * @brief The GFXI id @p key names, for the types that name one.
         *
         * GFX_MOVE and GFX_VISIBILITY both open their payload with the id, so
         * the two are read the same way where only the id is wanted.
         */
        uint16_t instance_of(data::keyframe const& key)
        {
            return *mtl::force_to<uint16_t const*>(key.payload);
        }

        /// @brief Whether @p key opens a palette cycle's span.
        bool is_cycle_start(data::keyframe const& key)
        {
            return mtl::to<data::keyframe_type>(key.type)
                == data::keyframe_type::PALETTE_CYCLE_START;
        }

        /// @brief Whether @p key closes one.
        bool is_cycle_end(data::keyframe const& key)
        {
            return mtl::to<data::keyframe_type>(key.type) == data::keyframe_type::PALETTE_CYCLE_END;
        }

        /// @brief @p key read as a PALETTE_CYCLE_START; only when is_cycle_start() holds.
        data::palette_cycle_start_payload const& cycle_start_of(data::keyframe const& key)
        {
            return *mtl::force_to<data::palette_cycle_start_payload const*>(key.payload);
        }

        /**
         * @brief The cycle id @p key names, for the two types that name one.
         *
         * Both palette cycle payloads open with the id, so a START and an END
         * are read the same way where only the id is wanted.
         */
        uint16_t cycle_id_of(data::keyframe const& key)
        {
            return *mtl::force_to<uint16_t const*>(key.payload);
        }

        /**
         * @brief The scene-local slot for global GFXI @p id.
         *
         * @return NO_INSTANCE when the id names an instance this scene does not
         *         own, which the load reports once rather than every frame.
         */
        uint16_t find_instance(uint16_t id)
        {
            if (id < s_gfxi_first) return NO_INSTANCE;
            uint16_t const local = mtl::to<uint16_t>(id - s_gfxi_first);
            return local < s_gfxi_count ? local : NO_INSTANCE;
        }

        /// @brief @p track's keyframes, in stored order.
        mtl::span<data::keyframe> track_keys(data::track const& track)
        {
            auto const all = g_pEngine->data().keyframes();
            return mtl::span<data::keyframe>(all.data() + track.keyframe_range.start,
                track.keyframe_range.count);
        }

        /// @brief Whether the fire_once key at scene-local @p index has already fired.
        bool has_fired(uint32_t index)
        {
            return (s_fired[index >> 3] & mtl::to<uint8_t>(1 << (index & 7))) != 0;
        }

        /// @brief Records that the fire_once key at scene-local @p index has fired.
        void mark_fired(uint32_t index)
        {
            s_fired[index >> 3] |= mtl::to<uint8_t>(1 << (index & 7));
        }

        /**
         * @brief Offers every fire_once key of the track at @p index again.
         *
         * A run is the unit a fire_once keyframe fires once within, and
         * scene_timeline_play() always begins a new run, so the marks from the
         * previous one are wiped rather than carried over.
         */
        void clear_fired(uint16_t index, data::track const& track)
        {
            uint32_t const base = s_key_base[index];
            for (uint16_t offset = 0; offset < track.keyframe_range.count; ++offset)
            {
                uint32_t const marker = base + offset;
                s_fired[marker >> 3] &= mtl::to<uint8_t>(0xFF ^ (1 << (marker & 7)));
            }
        }

        /**
         * @brief Whether @p track's keyframes really lie inside the KEYS chunk.
         *
         * Playing a track whose range points past the chunk would read whatever
         * follows it, so a track that fails this is never started: not at load,
         * and not by ANIM_PLAY either.
         */
        bool keys_in_range(data::track const& track)
        {
            auto const all = g_pEngine->data().keyframes();
            return mtl::to<size_t>(track.keyframe_range.start) + track.keyframe_range.count
                <= all.size();
        }

        /**
         * @brief The scene-local index of the track called @p name_id.
         *
         * @return NO_TRACK when this scene has no such track, which is an
         *         ordinary outcome: a script is not owned by a scene, so it can
         *         legitimately name a track that only exists in another one.
         */
        uint16_t find_track(uint16_t name_id)
        {
            auto const tracks = scene_timeline_tracks();
            for (uint16_t index = 0; index < s_count; ++index)
            {
                if (tracks[index].name_id == name_id) return index;
            }
            return NO_TRACK;
        }

        /**
         * @brief Lets the script blocked on ANIM_WAIT carry on.
         *
         * @param why How the wait came to be over, for the log.
         */
        void release_waiter(char const* why)
        {
            if (s_wait.depth == 0) return;

            uint16_t const name = s_wait.name;
            uint8_t const depth = s_wait.depth;
            s_wait              = wait_state{};

            auto& vm = g_pEngine->vm();

            // Unconditional, exactly as nb_fade_done() is, and for the same
            // reason: every wait recorded here was paired with an
            // enter_blocked(), so skipping the decrement leaves the counter
            // permanently one too high and wedges the *next* blocking call just
            // as surely as an extra decrement would wrap it to 255. A hotspot
            // click can cancel the block out from under a wait (it re-enters
            // execute() without checking the VM is idle), and when it does this
            // is what puts the counter back where it started.
            for (uint8_t step = 0; step < depth; ++step) vm.decrement_blocking();

            if (vm.state() != noir_state::blocked)
            {
                NE_LOG("the wait on track name %hu (%s) had already been cancelled",
                    name,
                    why);
                return;
            }
            NE_LOG("the wait on track name %hu is over (%s)", name, why);
        }

        /**
         * @brief Ends a wait that is already satisfied before any track ticks.
         *
         * Covers the ways a wait can be over without a cycle boundary to notice
         * it by: a name this scene has no track for, a track that was never
         * started or has already finished, and one stopped by ANIM_STOP.
         */
        void update_waiter()
        {
            if (s_wait.depth == 0) return;
            if (!s_wait.found) return release_waiter("no such track in this scene");
            if (!s_clocks[s_wait.track].is_running()) return release_waiter("it is not running");
        }

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
            uint32_t base             = 0;        ///< s_fired index of keys[0].
            uint16_t offset           = 0;        ///< This keyframe's index in keys.
            uint16_t track            = 0;        ///< Scene-local track index.
            bool reversing            = false;    ///< Whether the playhead heads back to 0.
        };

        /**
         * @brief The nearest GFX_MOVE for @p instance on one side of ctx.offset.
         *
         * A spent fire_once key is passed over rather than found: it no longer
         * places its instance, so it cannot bracket a movement either.
         *
         * @param forward Which side to look on, in time order.
         * @return An index into ctx.keys, or NO_KEY when that side holds none.
         */
        uint16_t neighbour_move(fire_context const& ctx, uint16_t instance, bool forward)
        {
            uint16_t step = ctx.offset;
            while (true)
            {
                if (forward)
                {
                    if (++step >= ctx.keys.size()) return NO_KEY;
                }
                else
                {
                    if (step == 0) return NO_KEY;
                    --step;
                }

                auto const& key = ctx.keys[step];
                if (!is_move(key) || move_of(key).instance_id != instance) continue;
                if ((key.flags & data::KEYFRAME_FLAG_FIRE_ONCE) && has_fired(ctx.base + step))
                {
                    continue;
                }
                return step;
            }
        }

        /// @brief Takes the instance at @p slot off whatever span it was on.
        void clear_glide(uint16_t slot) { s_glides[slot].active = false; }

        /**
         * @brief Puts the instance at @p slot on the span @p t0 to @p t1.
         *
         * The ends are given in time order however the playhead is travelling,
         * which is what lets one expression serve a reverse leg: the position is
         * read off the span rather than accumulated along it.
         */
        void set_glide(uint16_t slot,
            uint16_t track,
            data::point<uint16_t> from,
            uint16_t t0,
            data::point<uint16_t> to,
            uint16_t t1)
        {
            // Two moves of one instance at one time are rejected by the editor,
            // so this means a pak built by something else. Dividing by the span
            // would trap rather than merely misplace the instance, hence a test
            // rather than trust.
            if (t1 <= t0)
            {
                clear_glide(slot);
                return;
            }

            glide& g = s_glides[slot];
            g.from   = from;
            g.to     = to;
            g.t0     = t0;
            g.t1     = t1;
            g.track  = track;
            g.active = true;
        }

        /// @brief @p from moved @p done parts out of @p span of the way to @p to.
        uint16_t lerp(uint16_t from, uint16_t to, int32_t done, int32_t span)
        {
            int32_t const delta = mtl::to<int32_t>(to) - mtl::to<int32_t>(from);
            return mtl::to<uint16_t>(mtl::to<int32_t>(from) + (delta * done) / span);
        }

        /**
         * @brief Places every gliding instance where its track's playhead says.
         *
         * Computed from the playhead rather than stepped along, so a span costs
         * nothing to re-enter, gathers no drift however long it is, lands exactly
         * on its far end, and reads the same backwards as forwards. The divide
         * this costs is some 44 cycles against a frame's ~280,000; the search it
         * replaces was the expensive part, and the cached span removes that.
         */
        void update_glides()
        {
            for (uint16_t slot = 0; slot < s_glides.size(); ++slot)
            {
                glide const& g = s_glides[slot];
                if (!g.active) continue;

                // A halted playhead would only recompute the same position, and
                // leaving the instance untouched is what stopping should do.
                auto const& clock = s_clocks[g.track];
                if (!clock.is_running()) continue;

                int32_t const span = mtl::to<int32_t>(g.t1) - mtl::to<int32_t>(g.t0);
                int32_t const done = mtl::to<int32_t>(clock.position()) - mtl::to<int32_t>(g.t0);

                data::point<uint16_t> const pos{ lerp(g.from.y, g.to.y, done, span),
                    lerp(g.from.x, g.to.x, done, span) };
                scene_bobs_set_instance_pos(slot, pos);
            }
        }

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
        void update_cycles()
        {
            for (auto& span : s_cycles)
            {
                auto const& clock       = s_clocks[span.track];
                uint16_t const position = clock.position();

                // Half-open: the band is the START's to turn right up to the
                // END's position, and the END's position itself belongs to
                // whatever comes next.
                bool const inside = clock.is_running() && position >= span.t0 && position < span.t1;

                // A fire_once band turns for one traversal and no more, so it is
                // spent by leaving the span rather than by entering it: leaving
                // is the point at which the traversal is over.
                if (span.fire_once && span.inside && !inside) span.spent = true;
                span.inside = inside;

                // A band outside its span is left standing where the span left
                // it rather than being put back. Putting it back would be a jump
                // in the middle of the track: the offset the span ends on is
                // wherever the rotation had got to, and a ping-pong re-enters at
                // exactly that offset on its way back, so snapping to authored
                // order at the end would throw the band home and then straight
                // back out again a moment later. Holding makes a ping-pong
                // continuous everywhere: out, hold, and back the way it came.
                //
                // The one place a jump remains is a RESTART loop whose span is
                // not a whole number of revolutions, which lands back on offset
                // zero at the position the span opens at. A restart is a
                // discontinuity by definition, and authoring the span as
                // (t1 - t0) == n * width * delay removes even that one.
                uint16_t const wanted = (inside && !span.spent)
                    ? mtl::to<uint16_t>(((position - span.t0) / span.delay) % span.width)
                    : span.offset;

                if (wanted == span.offset) continue;

                // Always forwards, because a rotation is cyclic: turning the
                // rest of the way round is how the band goes back, and it is the
                // same one pass either way.
                uint16_t const steps
                    = mtl::to<uint16_t>((wanted + span.width - span.offset) % span.width);
                palette_cycle_rotate(span.first, span.last, steps);
                span.offset = wanted;
            }
        }

        /**
         * @brief Leaves every band of this scene standing as it was authored.
         *
         * A rotation is a permutation, so turning each band the rest of the way
         * round restores it exactly. Worth doing on the way out because a scene
         * without a background of its own loads no palette and would otherwise
         * inherit the last scene's bands mid-turn.
         */
        void restore_cycles()
        {
            for (auto& span : s_cycles)
            {
                if (span.offset == 0) continue;
                palette_cycle_rotate(
                    span.first, span.last, mtl::to<uint16_t>(span.width - span.offset));
                span.offset = 0;
            }
        }

        /**
         * @brief Offers the bands of the track at @p index their traversal again.
         *
         * The counterpart to clear_fired(): a run is the unit a fire_once band
         * turns once within. The offset is left alone, because it describes the
         * palette as it actually stands rather than a policy; the band picks up
         * from there when the playhead next reaches the span.
         */
        void clear_cycles(uint16_t index)
        {
            for (auto& span : s_cycles)
            {
                if (span.track != index) continue;
                span.inside = false;
                span.spent  = false;
            }
        }

        /**
         * @brief Runs @p key as a GFX_MOVE and works out what comes next.
         *
         * The instance lands exactly on the keyframe's position, and the span
         * the playhead is now entering is cached so that no later frame has to
         * search for it.
         */
        bool fire_move(data::keyframe const& key, fire_context const& ctx)
        {
            auto const& payload = move_of(key);
            uint16_t const slot = find_instance(payload.instance_id);
            if (slot == NO_INSTANCE) return false;  // Reported once at load.

            scene_bobs_set_instance_pos(slot, payload.pos);

            // A ping-pong turns round on the very frame it reaches its duration,
            // so a keyframe sitting there is crossed once, going up, and never
            // again coming down. The span it brackets on the way back has to be
            // set up here because there is no second crossing to do it.
            bool const turning =
                mtl::to<data::loop_mode>(ctx.record->loop_mode) == data::loop_mode::PING_PONG
                && key.time >= ctx.record->duration;
            bool const backward = ctx.reversing || turning;

            uint16_t const other = neighbour_move(ctx, payload.instance_id, !backward);
            if (other == NO_KEY)
            {
                clear_glide(slot);
                return true;
            }

            // Spans are described in time order, not in travel order: the later
            // of the two keys carries the flag whichever way the playhead runs,
            // which is exactly what spares a reverse leg a second rule.
            auto const& lo = backward ? ctx.keys[other] : key;
            auto const& hi = backward ? key : ctx.keys[other];

            if ((hi.flags & data::KEYFRAME_FLAG_INTERPOLATE) == 0)
            {
                clear_glide(slot);
                return true;
            }

            set_glide(slot, ctx.track, move_of(lo).pos, lo.time, move_of(hi).pos, hi.time);
            return true;
        }

        /// @brief Runs @p key as a GFX_VISIBILITY.
        bool fire_visibility(data::keyframe const& key)
        {
            auto const& payload =
                *mtl::force_to<data::gfx_visibility_payload const*>(key.payload);

            uint16_t const slot = find_instance(payload.instance_id);
            if (slot == NO_INSTANCE) return false;  // Reported once at load.

            scene_bobs_set_instance_visible(slot, payload.visible != 0);
            return true;
        }

        /// @brief Runs @p key as an SFX_PLAY.
        bool fire_sfx(data::keyframe const& key)
        {
            // Preloaded when the scene did, because a sound has to be resident
            // in Chip RAM before it is asked for. The channel the payload names
            // is deliberately not read; see scene_audio.
            scene_audio_play(mtl::force_to<data::sfx_play_payload const*>(key.payload)->sfx_hash);
            return true;
        }

        /// @brief Runs @p key as a MUSIC_PLAY.
        bool fire_music(data::keyframe const& key)
        {
            auto const& payload = *mtl::force_to<data::music_play_payload const*>(key.payload);

            // Read here rather than preloaded with the scene: a module is far
            // larger than a sound effect, and a scene may name several it never
            // reaches. The cost is a hitch on the frame this fires. Nothing
            // drifts, because the clock counts frames processed rather than
            // time passed, but it is a stall and it is meant to be one.
            mtl::system_use_guard guard;

            auto mod = g_pEngine->data().open_asset_uncached(payload.music_hash);
            if (!mod)
            {
                NE_LOG("no music asset for hash %08lx", mtl::to<ULONG>(payload.music_hash));
                return false;
            }

            musicLoad(mtl::move(mod));
            musicPlayCurrent(payload.loop);
            NE_LOG("MUSIC_PLAY %08lx, loop %hu",
                mtl::to<ULONG>(payload.music_hash),
                mtl::to<uint16_t>(payload.loop));
            return true;
        }

        /// @brief Runs @p key as an EXECUTE_SCRIPT.
        bool fire_script(data::keyframe const& key, uint16_t track_index)
        {
            auto const& payload = *mtl::force_to<data::execute_script_payload const*>(key.payload);
            auto& vm            = g_pEngine->vm();

            // execute() saves the registers of a re-entered call but not the
            // VM's state or its blocking and wait counters, so starting a script
            // over a blocked one would strand the counter and release the outer
            // script early - taking ANIM_WAIT with it. The same test means only
            // the first script keyframe of a frame runs, which is deliberate:
            // two execute() calls in one frame would run in reverse order.
            if (vm.state() != noir_state::idle)
            {
                NE_LOG("track %hu cannot run script %hu, the VM is busy",
                    track_index,
                    payload.function_id);
                return false;
            }

            vm.execute(payload.function_id);
            NE_LOG("track %hu ran script %hu", track_index, payload.function_id);
            return true;
        }

        /**
         * @brief Runs @p key, if this slice knows how to.
         *
         * @return Whether the keyframe actually fired. A keyframe that did not
         *         fire has not used up its fire_once, so a looping track will
         *         offer it again rather than losing it for the rest of the run.
         */
        bool fire(data::keyframe const& key, fire_context const& ctx)
        {
            switch (mtl::to<data::keyframe_type>(key.type))
            {
                case data::keyframe_type::GFX_MOVE: return fire_move(key, ctx);
                case data::keyframe_type::GFX_VISIBILITY: return fire_visibility(key);
                case data::keyframe_type::SFX_PLAY: return fire_sfx(key);
                case data::keyframe_type::MUSIC_PLAY: return fire_music(key);
                case data::keyframe_type::EXECUTE_SCRIPT: return fire_script(key, ctx.track);

                case data::keyframe_type::MUSIC_STOP:
                    musicStop();
                    NE_LOG("MUSIC_STOP");
                    return true;

                case data::keyframe_type::PALETTE_CYCLE_START:
                case data::keyframe_type::PALETTE_CYCLE_END:
                    // A palette cycle is a span the playhead stands inside, not
                    // an event it crosses: update_cycles() reads it off the
                    // position every frame, and these two records only say where
                    // the span begins and ends. Nothing fires, so nothing here
                    // spends a fire_once either - the span keeps that itself.
                    return false;

                default:
                    // Every other type is reported once at load and ignored
                    // here, so that an unimplemented keyframe on a looping track
                    // cannot log 50 times a second.
                    return false;
            }
        }

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
        void prime_glides(uint16_t track_index, data::track const& track)
        {
            // A cycle begins with nothing of the last one still in motion, so
            // that a movement whose keyframes are spent or gone cannot leave an
            // instance following a span the playhead has left. Only this track's
            // movements are dropped; another track's are its own business.
            for (uint16_t slot = 0; slot < s_glides.size(); ++slot)
            {
                if (s_glides[slot].active && s_glides[slot].track == track_index)
                {
                    clear_glide(slot);
                }
            }

            fire_context ctx;
            ctx.keys   = track_keys(track);
            ctx.record = &track;
            ctx.base   = s_key_base[track_index];
            ctx.track  = track_index;

            for (uint16_t offset = 0; offset < ctx.keys.size(); ++offset)
            {
                auto const& key = ctx.keys[offset];
                if (!is_move(key) || (key.flags & data::KEYFRAME_FLAG_INTERPOLATE) == 0) continue;

                // A move at position 0 is simply where the instance is at
                // position 0; the crossing on the cycle's first frame says so.
                if (key.time == 0) continue;

                if ((key.flags & data::KEYFRAME_FLAG_FIRE_ONCE) && has_fired(ctx.base + offset))
                {
                    continue;
                }

                auto const& payload = move_of(key);
                uint16_t const slot = find_instance(payload.instance_id);
                if (slot == NO_INSTANCE) continue;

                // Only the cycle's first move for an instance leads from where
                // that instance already is. Every later one is picked up as the
                // move before it is crossed.
                ctx.offset = offset;
                if (neighbour_move(ctx, payload.instance_id, false) != NO_KEY) continue;

                data::point<uint16_t> from{ 0, 0 };
                if (!scene_bobs_instance_pos(slot, from)) continue;

                set_glide(slot, track_index, from, 0, payload.pos, key.time);
            }
        }

        /**
         * @brief Collects the palette bands the track at @p index turns.
         *
         * A START and its END are paired here rather than at fire time, so that
         * a band whose records do not make a span is complained about once at
         * load instead of fifty times a second. The two are always in the same
         * track, which is why an END can name nothing but a cycle id and still
         * be unambiguous.
         */
        void collect_cycles(uint16_t track_index, data::track const& track)
        {
            auto const keys = track_keys(track);
            for (uint16_t offset = 0; offset < keys.size(); ++offset)
            {
                auto const& open = keys[offset];
                if (!is_cycle_start(open)) continue;

                auto const& payload = cycle_start_of(open);

                uint16_t close = NO_KEY;
                for (uint16_t other = 0; other < keys.size(); ++other)
                {
                    if (!is_cycle_end(keys[other])) continue;
                    if (cycle_id_of(keys[other]) != payload.cycle_id) continue;
                    if (keys[other].time <= open.time) continue;
                    close = other;
                    break;
                }

                // The editor pairs these and rejects the rest, so any of the
                // following means the pak was built by something else.
                if (close == NO_KEY)
                {
                    NE_LOG("  track %hu cycle %hu has no end after it; it will not turn",
                        track_index,
                        payload.cycle_id);
                    continue;
                }

                // A reversed or single-entry band is not a band, and turning one
                // would walk off the end of the palette rather than merely look
                // wrong, so it is refused rather than clamped.
                if (payload.last <= payload.first)
                {
                    NE_LOG("  track %hu cycle %hu spans entries %hhu..%hhu, which is not a band",
                        track_index,
                        payload.cycle_id,
                        payload.first,
                        payload.last);
                    continue;
                }

                cycle_span span;
                span.track = track_index;
                span.t0    = open.time;
                span.t1    = keys[close].time;
                span.first = payload.first;
                span.last  = payload.last;
                span.width = mtl::to<uint16_t>(payload.last - payload.first + 1);

                // Zero frames a step is no step at all, and dividing by it traps
                // rather than misbehaving, so it becomes the fastest turn there
                // is instead.
                span.delay = payload.delay == 0 ? 1 : payload.delay;
                if (payload.delay == 0)
                {
                    NE_LOG("  track %hu cycle %hu holds each step for 0 frames; using 1",
                        track_index,
                        payload.cycle_id);
                }

                span.fire_once = (open.flags & data::KEYFRAME_FLAG_FIRE_ONCE) != 0;
                s_cycles.push_back(span);
            }
        }

        /**
         * @brief Fires whatever the track at @p track_index has under its playhead.
         */
        void fire_keyframes(uint16_t track_index, data::track const& track)
        {
            if (track.keyframe_range.count == 0) return;

            fire_context ctx;
            ctx.keys   = track_keys(track);
            ctx.record = &track;
            ctx.base   = s_key_base[track_index];
            ctx.track  = track_index;

            auto const& clock = s_clocks[track_index];
            auto const run    = clock.keys_at(ctx.keys);
            if (run.count == 0) return;

            ctx.reversing = clock.is_reversing();

            for (uint16_t step = 0; step < run.count; ++step)
            {
                // A track played backwards is played backwards all the way down,
                // so keys sharing a position run in reverse on the return leg.
                ctx.offset = ctx.reversing
                    ? mtl::to<uint16_t>(run.first + run.count - 1 - step)
                    : mtl::to<uint16_t>(run.first + step);

                auto const& key       = ctx.keys[ctx.offset];
                bool const fire_once  = (key.flags & data::KEYFRAME_FLAG_FIRE_ONCE) != 0;
                uint32_t const marker = ctx.base + ctx.offset;

                if (fire_once && has_fired(marker)) continue;
                if (fire(key, ctx) && fire_once) mark_fired(marker);
            }
        }
    }  // namespace

    void scene_timeline_clear()
    {
        // Tracks cannot outlive their scene, so neither can a wait on one: the
        // record has to go before s_clocks is emptied, or it would index the
        // next scene's playheads. The release itself is belt and braces, since
        // load_scene() resets the VM straight after this and that clears the
        // blocking counter anyway.
        release_waiter("the scene ended");

        s_first      = 0;
        s_count      = 0;
        s_gfxi_first = 0;
        s_gfxi_count = 0;
        s_clocks.clear();
        s_key_base.clear();
        s_fired.clear();
        s_glides.clear();
        s_cycles.clear();
    }

    void scene_timeline_restore_palette()
    {
        restore_cycles();
    }

    mtl::span<data::track> scene_timeline_tracks()
    {
        auto const all = g_pEngine->data().tracks();
        return mtl::span<data::track>(all.data() + s_first, s_count);
    }

    track_clock const& scene_timeline_clock(uint16_t index)
    {
        return s_clocks[index];
    }

    void scene_timeline_load(data::scene const& scene)
    {
        scene_timeline_clear();

        // Kept whatever the timeline turns out to be, so that a GFXI id in a
        // keyframe payload can be mapped to a scene-local slot.
        s_gfxi_first = scene.graphic_instance_range.start;
        s_gfxi_count = scene.graphic_instance_range.count;

        if (scene.timeline_id == NO_TIMELINE) return;

        auto const timelines = g_pEngine->data().timelines();
        if (scene.timeline_id >= timelines.size())
        {
            NE_LOG("scene names timeline %hu of %lu",
                scene.timeline_id,
                mtl::to<ULONG>(timelines.size()));
            return;
        }

        auto const& time     = timelines[scene.timeline_id];
        auto const all       = g_pEngine->data().tracks();
        uint16_t const first = time.track_range.start;
        uint16_t const count = time.track_range.count;

        // An empty or out-of-bounds range simply leaves the scene inert.
        if (count == 0 || mtl::to<size_t>(first) + count > all.size())
        {
            if (count != 0)
            {
                NE_LOG("track range %hu+%hu exceeds %lu tracks",
                    first,
                    count,
                    mtl::to<ULONG>(all.size()));
            }
            return;
        }

        s_first = first;
        s_count = count;
        s_clocks.reserve(count);
        s_key_base.reserve(count);

        // One movement slot per instance, whether or not any track moves it:
        // an instance's position is its own, so two tracks moving it are settled
        // by which of them wrote last rather than by remembering both.
        s_glides.reserve(s_gfxi_count);
        for (uint16_t slot = 0; slot < s_gfxi_count; ++slot) s_glides.push_back(glide{});

        uint32_t total_keys      = 0;
        uint16_t unimplemented   = 0;  ///< Bitmask over data::keyframe_type.
        auto const all_keyframes = g_pEngine->data().keyframes();

        NE_LOG("%hu, %hu track(s)", scene.timeline_id, count);
        for (uint16_t index = 0; index < count; ++index)
        {
            auto const& track = all[mtl::to<size_t>(first) + index];
            NE_LOG("  track %hu: name %hu, keys %hu+%hu, %hu frames, %s x%hu, %s",
                index,
                track.name_id,
                track.keyframe_range.start,
                track.keyframe_range.count,
                track.duration,
                loop_mode_name(track.loop_mode),
                mtl::to<uint16_t>(track.loop_count),
                (track.flags & data::TRACK_FLAG_AUTO_START) ? "auto" : "on cue");

            // The editor rejects this, so it means the pak was built by
            // something else. track_clock plays it as if it were 1 frame long
            // rather than refusing, but the data is still wrong.
            if (track.duration == 0)
            {
                NE_LOG("  track %hu has no duration; playing it as 1 frame", index);
            }

            bool const in_range = keys_in_range(track);

            s_key_base.push_back(total_keys);
            total_keys += track.keyframe_range.count;

            track_clock clock;
            if (!in_range)
            {
                // Playing a track whose keys point outside the chunk would read
                // whatever follows it, so the track is left permanently idle.
                NE_LOG("  track %hu keys %hu+%hu exceed %lu keyframes; it will not play",
                    index,
                    track.keyframe_range.start,
                    track.keyframe_range.count,
                    mtl::to<ULONG>(all_keyframes.size()));
            }
            else
            {
                for (uint16_t offset = 0; offset < track.keyframe_range.count; ++offset)
                {
                    auto const& key = all_keyframes[mtl::to<size_t>(track.keyframe_range.start) + offset];
                    if (!is_implemented(key.type) && key.type < data::KEYFRAME_TYPE_COUNT)
                    {
                        unimplemented |= mtl::to<uint16_t>(1 << key.type);
                    }

                    // Reported here rather than at fire time for the same reason:
                    // a bad id on a looping track would say so 50 times a second.
                    bool const names_instance = is_move(key)
                        || mtl::to<data::keyframe_type>(key.type)
                            == data::keyframe_type::GFX_VISIBILITY;

                    if (names_instance && find_instance(instance_of(key)) == NO_INSTANCE)
                    {
                        NE_LOG("  track %hu key %hu names instance %hu, which is not in this scene",
                            index,
                            offset,
                            instance_of(key));
                    }
                }

                if (track.flags & data::TRACK_FLAG_AUTO_START) clock.start();
                collect_cycles(index, track);
            }
            s_clocks.push_back(clock);
        }

        // Reported once here rather than at fire time, where an unimplemented
        // keyframe on a looping track would log 50 times a second.
        for (uint8_t type = 0; type < data::KEYFRAME_TYPE_COUNT; ++type)
        {
            if (unimplemented & (1 << type))
            {
                NE_LOG("  %s keyframes are not implemented yet and will be skipped",
                    keyframe_type_name(type));
            }
        }

        // The editor rejects a name repeated inside one scene, so this means the
        // pak was built by something else. ANIM_PLAY takes the first match,
        // which is at least deterministic, but the later one is unreachable.
        for (uint16_t a = 0; a < count; ++a)
        {
            for (uint16_t b = mtl::to<uint16_t>(a + 1); b < count; ++b)
            {
                if (all[mtl::to<size_t>(first) + a].name_id != all[mtl::to<size_t>(first) + b].name_id)
                {
                    continue;
                }

                NE_LOG("  tracks %hu and %hu share name %hu; only the first can be played",
                    a,
                    b,
                    all[mtl::to<size_t>(first) + a].name_id);
                break;
            }
        }

        s_fired.reserve((total_keys + 7) / 8);
        for (uint32_t byte = 0; byte < (total_keys + 7) / 8; ++byte) s_fired.push_back(0);

        // After s_fired exists, because priming has to see which fire_once keys
        // are spent, and after scene_bobs_load(), because a movement with no
        // earlier one to lead it starts from where its instance already stands.
        for (uint16_t index = 0; index < count; ++index)
        {
            if (s_clocks[index].is_running()) prime_glides(index, all[mtl::to<size_t>(first) + index]);
        }
    }

    void scene_timeline_update()
    {
        // Ahead of the early-out, because a scene with no tracks at all can
        // still have a script blocked on a track it does not have, and leaving
        // that one blocked would strand it for the rest of the game.
        update_waiter();

        if (s_count == 0) return;

        // Ahead of the tracks, so that a movement is read off the playhead the
        // frame it is standing on rather than the one it is about to move to.
        // A crossing writes the exact position afterwards, and the two agree at
        // a span's ends, so nothing is drawn twice differently.
        update_glides();

        // Read off the playhead alongside the movements and for the same
        // reason: a band belongs to the position the playhead stands on this
        // frame, not the one it is about to move to.
        update_cycles();

        auto const tracks = scene_timeline_tracks();
        for (uint16_t index = 0; index < s_count; ++index)
        {
            auto& clock = s_clocks[index];
            if (!clock.is_running()) continue;

            // Fire before ticking, so a run's very first frame plays whatever
            // sits at position 0 rather than stepping straight over it.
            fire_keyframes(index, tracks[index]);

            switch (clock.tick(tracks[index]))
            {
                case tick_result::LOOPED:
                    // Every cycle leads into its first movement the way the
                    // first cycle did, so a looping track glides into it rather
                    // than gliding once and teleporting ever after.
                    prime_glides(index, tracks[index]);

                    // A wait ends at the first cycle boundary after it began, so
                    // that waiting on a track that loops for ever costs one
                    // cycle instead of the whole scene.
                    if (s_wait.found && s_wait.track == index) release_waiter("it came round");

                    // A short looping track turns over dozens of times a second,
                    // which would bury everything else in the log, so only the
                    // first few of each track's cycles are reported.
                    if (clock.cycles_done() <= LOGGED_CYCLES)
                    {
                        NE_LOG("track %hu completed cycle %hu", index, clock.cycles_done());
                    }
                    else if (clock.cycles_done() == LOGGED_CYCLES + 1)
                    {
                        NE_LOG("track %hu is looping; further cycles are not logged", index);
                    }
                    break;

                case tick_result::FINISHED:
                    if (s_wait.found && s_wait.track == index) release_waiter("it finished");
                    NE_LOG("track %hu finished after %hu cycle(s)", index, clock.cycles_done());
                    break;

                case tick_result::IDLE:
                case tick_result::RUNNING:
                    break;
            }
        }

        // Once every band has been turned, so that a frame which moved several
        // of them still rebuilds the hardware palette only the once.
        palette_cycle_commit();
    }

    void scene_timeline_play(uint16_t name_id)
    {
        uint16_t const index = find_track(name_id);
        if (index == NO_TRACK)
        {
            NE_LOG("ANIM_PLAY names track %hu, which this scene does not have", name_id);
            return;
        }

        // A play is a new run, and a run that is already under way cannot be
        // begun again without deciding what happens to everything the first one
        // has already done. A script that means to start over says so: stop,
        // then play.
        if (s_clocks[index].is_running())
        {
            NE_LOG("ANIM_PLAY ignored; track %hu (name %hu) is already running",
                index,
                name_id);
            return;
        }

        auto const& track = scene_timeline_tracks()[index];
        if (!keys_in_range(track))
        {
            NE_LOG("ANIM_PLAY cannot start track %hu; its keyframes are out of range",
                index);
            return;
        }

        clear_fired(index, track);
        clear_cycles(index);
        s_clocks[index].start();
        prime_glides(index, track);
        NE_LOG("ANIM_PLAY started track %hu (name %hu)", index, name_id);
    }

    void scene_timeline_stop(uint16_t name_id)
    {
        uint16_t const index = find_track(name_id);
        if (index == NO_TRACK)
        {
            NE_LOG("ANIM_STOP names track %hu, which this scene does not have", name_id);
            return;
        }

        // Stopping a track that is not running is the ordinary way to make sure
        // of a track's state before playing it, so it passes quietly.
        if (!s_clocks[index].is_running()) return;

        // Anything waiting on this track is released by the next
        // scene_timeline_update(), which finds it no longer running.
        s_clocks[index].stop();
        NE_LOG("ANIM_STOP halted track %hu (name %hu) at position %hu",
            index,
            name_id,
            s_clocks[index].position());
    }

    void scene_timeline_wait(uint16_t name_id)
    {
        uint16_t const index = find_track(name_id);
        if (index == NO_TRACK)
        {
            NE_LOG("ANIM_WAIT names track %hu, which this scene does not have", name_id);
        }

        // Two waits can only stack if a hotspot click re-entered the VM over an
        // already blocked script. They share the one blocking counter and the
        // one state, so the newer wait takes over and both end together.
        if (s_wait.depth != 0)
        {
            NE_LOG("ANIM_WAIT on track name %hu displaced a wait on track name %hu",
                name_id,
                s_wait.name);
        }

        // The opcode handler blocks the VM only after this returns, so the
        // counter cannot be balanced here even when the wait is already
        // satisfied; scene_timeline_update() picks it up later in the same
        // frame. Every recorded wait is therefore owed exactly one decrement.
        wait_state next;
        next.name  = name_id;
        next.found = (index != NO_TRACK);
        next.track = next.found ? index : uint16_t(0);

        // Saturating rather than wrapping, though by 255 nested waits the VM's
        // own uint8_t counter has wrapped too and nothing is recoverable.
        next.depth = (s_wait.depth == UINT8_MAX) ? s_wait.depth
                                                 : mtl::to<uint8_t>(s_wait.depth + 1);
        s_wait     = next;
    }
}  // namespace NEONengine
