#include "scene_timeline.h"

#include "types.h"

#include "neonengine.h"

#include <ace/utils/extview.h>
#include <ace/utils/palette.h>

#include <mtl/utility.h>
#include <mtl/vector.h>

#include "core/game_data.h"
#include "core/logger.h"
#include "core/music.h"
#include "core/scene_bobs.h"
#include "core/screen.h"
#include "core/track_clock.h"

NE_LOG_DOMAIN(Timeline);

namespace NEONengine
{
    namespace
    {
        /// No track of that name in this scene.
        constexpr u16 NO_TRACK = 0xFFFF;

        /// Cycle boundaries logged per track before this falls silent.
        constexpr u16 LOGGED_CYCLES = 3;

        /**
         * @brief Names @p mode the way the editor and the format docs do.
         */
        char const* loop_mode_name(u8 mode)
        {
            switch (to<data::loop_mode>(mode))
            {
                case data::loop_mode::NONE: return "None";
                case data::loop_mode::RESTART: return "Restart";
                case data::loop_mode::PING_PONG: return "PingPong";
                default: return "?";
            }
        }

        /// @brief Whether @p key moves a graphic instance.
        bool is_move(data::keyframe const& key)
        {
            return to<data::keyframe_type>(key.type) == data::keyframe_type::GFX_MOVE;
        }

        /// @brief @p key read as a GFX_MOVE; only call it when is_move() holds.
        data::gfx_move_payload const& move_of(data::keyframe const& key)
        {
            return *force_to<data::gfx_move_payload const*>(key.payload);
        }

        /**
         * @brief The GFXI id @p key names, for the types that name one.
         *
         * GFX_MOVE and GFX_VISIBILITY both open their payload with the id, so
         * the two are read the same way where only the id is wanted.
         */
        u16 instance_of(data::keyframe const& key)
        {
            return *force_to<u16 const*>(key.payload);
        }

        /// @brief Whether @p key opens a palette cycle's span.
        bool is_cycle_start(data::keyframe const& key)
        {
            return to<data::keyframe_type>(key.type)
                == data::keyframe_type::PALETTE_CYCLE_START;
        }

        /// @brief Whether @p key closes one.
        bool is_cycle_end(data::keyframe const& key)
        {
            return to<data::keyframe_type>(key.type) == data::keyframe_type::PALETTE_CYCLE_END;
        }

        /// @brief @p key read as a PALETTE_CYCLE_START; only when is_cycle_start() holds.
        data::palette_cycle_start_payload const& cycle_start_of(data::keyframe const& key)
        {
            return *force_to<data::palette_cycle_start_payload const*>(key.payload);
        }

        /**
         * @brief The cycle id @p key names, for the two types that name one.
         *
         * Both palette cycle payloads open with the id, so a START and an END
         * are read the same way where only the id is wanted.
         */
        u16 cycle_id_of(data::keyframe const& key)
        {
            return *force_to<u16 const*>(key.payload);
        }

        /// @brief @p from moved @p done parts out of @p span of the way to @p dest.
        u16 lerp(u16 from, u16 dest, i32 done, i32 span)
        {
            i32 const delta = to<i32>(dest) - to<i32>(from);
            return to<u16>(to<i32>(from) + (delta * done) / span);
        }

        /**
         * @brief Reverses the reference palette entries [@p from, @p to].
         *
         * The building block of the rotation: turning a band right by k is the
         * whole band reversed, then each of its two pieces reversed back. That
         * is one pass over the band and a bit, whatever k is, and needs no
         * scratch buffer - which matters, because a band may be the entire
         * 256-entry palette and the stack is not the place for a copy of it.
         */
        void reverse(ULONG* entries, u16 first, u16 last)
        {
            while (first < last)
            {
                ULONG const held = entries[first];
                entries[first]   = entries[last];
                entries[last]    = held;
                ++first;
                --last;
            }
        }
    }  // namespace

    scene_timeline::~scene_timeline()
    {
        unload();
    }

    u16 scene_timeline::find_instance(u16 id)
    {
        if (id < _gfxi_first) return NO_INSTANCE;
        u16 const local = to<u16>(id - _gfxi_first);
        return local < _gfxi_count ? local : NO_INSTANCE;
    }

    mtl::span<data::keyframe> scene_timeline::track_keys(data::track const& track)
    {
        auto const all = _data.keyframes();
        return mtl::span<data::keyframe>(all.data() + track.keyframe_range.start,
            track.keyframe_range.count);
    }

    bool scene_timeline::has_fired(u32 index)
    {
        return (_fired[index >> 3] & to<u8>(1 << (index & 7))) != 0;
    }

    void scene_timeline::mark_fired(u32 index)
    {
        _fired[index >> 3] |= to<u8>(1 << (index & 7));
    }

    void scene_timeline::clear_fired(u16 index, data::track const& track)
    {
        u32 const base = _key_base[index];
        for (u16 offset = 0; offset < track.keyframe_range.count; ++offset)
        {
            u32 const marker = base + offset;
            _fired[marker >> 3] &= to<u8>(0xFF ^ (1 << (marker & 7)));
        }
    }

    bool scene_timeline::keys_in_range(data::track const& track)
    {
        auto const all = _data.keyframes();
        return to<size_t>(track.keyframe_range.start) + track.keyframe_range.count
            <= all.size();
    }

    u16 scene_timeline::find_track(u16 name_id)
    {
        auto const all = tracks();
        for (u16 index = 0; index < _count; ++index)
        {
            if (all[index].name_id == name_id) return index;
        }
        return NO_TRACK;
    }

    u16 scene_timeline::neighbour_move(fire_context const& ctx, u16 instance, bool forward)
    {
        u16 step = ctx.offset;
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

    void scene_timeline::clear_glide(u16 slot) { _glides[slot].active = false; }

    void scene_timeline::set_glide(u16 slot,
        u16 track,
        data::point<u16> from,
        u16 t0,
        data::point<u16> dest,
        u16 t1)
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

        glide& g = _glides[slot];
        g.from   = from;
        g.to     = dest;
        g.t0     = t0;
        g.t1     = t1;
        g.track  = track;
        g.active = true;
    }

    void scene_timeline::update_glides()
    {
        for (u16 slot = 0; slot < _glides.size(); ++slot)
        {
            glide const& g = _glides[slot];
            if (!g.active) continue;

            // A halted playhead would only recompute the same position, and
            // leaving the instance untouched is what stopping should do.
            auto const& clock = _clocks[g.track];
            if (!clock.is_running()) continue;

            i32 const span = to<i32>(g.t1) - to<i32>(g.t0);
            i32 const done = to<i32>(clock.position()) - to<i32>(g.t0);

            data::point<u16> const pos{ lerp(g.from.y, g.to.y, done, span),
                lerp(g.from.x, g.to.x, done, span) };
            _bobs.set_instance_pos(slot, pos);
        }
    }

    void scene_timeline::update_cycles()
    {
        for (auto& span : _cycles)
        {
            auto const& clock       = _clocks[span.track];
            u16 const position = clock.position();

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
            u16 const wanted = (inside && !span.spent)
                ? to<u16>(((position - span.t0) / span.delay) % span.width)
                : span.offset;

            if (wanted == span.offset) continue;

            // Always forwards, because a rotation is cyclic: turning the
            // rest of the way round is how the band goes back, and it is the
            // same one pass either way.
            u16 const steps
                = to<u16>((wanted + span.width - span.offset) % span.width);
            rotate_palette_band(span.first, span.last, steps);
            span.offset = wanted;
        }
    }

    void scene_timeline::restore_cycles()
    {
        for (auto& span : _cycles)
        {
            if (span.offset == 0) continue;
            rotate_palette_band(
                span.first, span.last, to<u16>(span.width - span.offset));
            span.offset = 0;
        }
    }

    void scene_timeline::clear_cycles(u16 index)
    {
        for (auto& span : _cycles)
        {
            if (span.track != index) continue;
            span.inside = false;
            span.spent  = false;
        }
    }

    void scene_timeline::rotate_palette_band(u8 first, u8 last, u16 steps)
    {
        // Not a band. The editor rejects it, so it means the pak was built by
        // something else; rotating a reversed range would walk off the end of
        // the palette, which is worth refusing rather than reporting.
        if (last <= first) return;

        u16 const width = to<u16>(last - first + 1);

        steps %= width;
        if (steps == 0) return;

        auto* entries = force_to<ULONG*>(screenGetPalette(g_mainScreen));

        reverse(entries, first, last);
        reverse(entries, first, to<u16>(first + steps - 1));
        reverse(entries, to<u16>(first + steps), last);

        if (_palette_dirty_first == NO_ENTRY || first < _palette_dirty_first)
        {
            _palette_dirty_first = first;
        }
        if (_palette_dirty_last < last) _palette_dirty_last = last;
    }

    void scene_timeline::commit_palette()
    {
        if (_palette_dirty_first == NO_ENTRY) return;

        u16 const first = _palette_dirty_first;
        u16 const last  = _palette_dirty_last;
        _palette_dirty_first = NO_ENTRY;
        _palette_dirty_last  = 0;

        // A fade rebuilds the display from the reference for the whole palette
        // this frame anyway, and it runs after this does, so it will carry the
        // rotation across on its own. Doing it here as well would rebuild the
        // hardware palette a second time for nothing.
        if (screenFadeOwnsPalette(g_mainScreen)) return;

        screenPublishPalette(g_mainScreen, first, to<u16>(last - first + 1));
    }

    bool scene_timeline::fire_move(data::keyframe const& key, fire_context const& ctx)
    {
        auto const& payload = move_of(key);
        u16 const slot = find_instance(payload.instance_id);
        if (slot == NO_INSTANCE) return false;  // Reported once at load.

        _bobs.set_instance_pos(slot, payload.pos);

        // A ping-pong turns round on the very frame it reaches its duration,
        // so a keyframe sitting there is crossed once, going up, and never
        // again coming down. The span it brackets on the way back has to be
        // set up here because there is no second crossing to do it.
        bool const turning =
            to<data::loop_mode>(ctx.record->loop_mode) == data::loop_mode::PING_PONG
            && key.time >= ctx.record->duration;
        bool const backward = ctx.reversing || turning;

        u16 const other = neighbour_move(ctx, payload.instance_id, !backward);
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

    bool scene_timeline::fire_visibility(data::keyframe const& key)
    {
        auto const& payload =
            *force_to<data::gfx_visibility_payload const*>(key.payload);

        u16 const slot = find_instance(payload.instance_id);
        if (slot == NO_INSTANCE) return false;  // Reported once at load.

        _bobs.set_instance_visible(slot, payload.visible != 0);
        return true;
    }

    bool scene_timeline::fire_sfx(data::keyframe const& key)
    {
        // Preloaded when the scene did, because a sound has to be resident
        // in Chip RAM before it is asked for. The channel the payload names
        // is deliberately not read; see scene_sound_effects.
        _sound_effects.play(force_to<data::sfx_play_payload const*>(key.payload)->sfx_hash);
        return true;
    }

    bool scene_timeline::fire_music(data::keyframe const& key)
    {
        auto const& payload = *force_to<data::music_play_payload const*>(key.payload);

        // Read here rather than preloaded with the scene: a module is far
        // larger than a sound effect, and a scene may name several it never
        // reaches. The cost is a hitch on the frame this fires. Nothing
        // drifts, because the clock counts frames processed rather than
        // time passed, but it is a stall and it is meant to be one.
        mtl::system_use_guard guard;

        auto mod = _data.open_asset_uncached(payload.music_hash);
        if (!mod)
        {
            NE_LOG("no music asset for hash %08lx", to<ULONG>(payload.music_hash));
            return false;
        }

        musicLoad(mtl::move(mod));
        musicPlayCurrent(payload.loop);
        NE_LOG("MUSIC_PLAY %08lx, loop %hu",
            to<ULONG>(payload.music_hash),
            to<u16>(payload.loop));
        return true;
    }

    bool scene_timeline::fire_script(data::keyframe const& key, u16 track_index)
    {
        auto const& payload = *force_to<data::execute_script_payload const*>(key.payload);
        auto& vm            = g_pEngine->vm();

        // Sessions run side by side, so several keyframes in one frame each
        // get one, in the order they fired. The one refusal left is a script
        // that is already running: it keeps its place in the run list rather
        // than restarting, and the keyframe counts as not fired.
        if (!vm.execute(payload.function_id))
        {
            NE_LOG("track %hu could not run script %hu",
                track_index,
                payload.function_id);
            return false;
        }

        NE_LOG("track %hu ran script %hu", track_index, payload.function_id);
        return true;
    }

    bool scene_timeline::fire(data::keyframe const& key, fire_context const& ctx)
    {
        switch (to<data::keyframe_type>(key.type))
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

    void scene_timeline::prime_glides(u16 track_index, data::track const& track)
    {
        // A cycle begins with nothing of the last one still in motion, so
        // that a movement whose keyframes are spent or gone cannot leave an
        // instance following a span the playhead has left. Only this track's
        // movements are dropped; another track's are its own business.
        for (u16 slot = 0; slot < _glides.size(); ++slot)
        {
            if (_glides[slot].active && _glides[slot].track == track_index)
            {
                clear_glide(slot);
            }
        }

        fire_context ctx;
        ctx.keys   = track_keys(track);
        ctx.record = &track;
        ctx.base   = _key_base[track_index];
        ctx.track  = track_index;

        for (u16 offset = 0; offset < ctx.keys.size(); ++offset)
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
            u16 const slot = find_instance(payload.instance_id);
            if (slot == NO_INSTANCE) continue;

            // Only the cycle's first move for an instance leads from where
            // that instance already is. Every later one is picked up as the
            // move before it is crossed.
            ctx.offset = offset;
            if (neighbour_move(ctx, payload.instance_id, false) != NO_KEY) continue;

            data::point<u16> from{ 0, 0 };
            if (!_bobs.instance_pos(slot, from)) continue;

            set_glide(slot, track_index, from, 0, payload.pos, key.time);
        }
    }

    void scene_timeline::collect_cycles(u16 track_index, data::track const& track)
    {
        auto const keys = track_keys(track);
        for (u16 offset = 0; offset < keys.size(); ++offset)
        {
            auto const& open = keys[offset];
            if (!is_cycle_start(open)) continue;

            auto const& payload = cycle_start_of(open);

            u16 close = NO_KEY;
            for (u16 other = 0; other < keys.size(); ++other)
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
            span.width = to<u16>(payload.last - payload.first + 1);

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
            _cycles.push_back(span);
        }
    }

    void scene_timeline::fire_keyframes(u16 track_index, data::track const& track)
    {
        if (track.keyframe_range.count == 0) return;

        fire_context ctx;
        ctx.keys   = track_keys(track);
        ctx.record = &track;
        ctx.base   = _key_base[track_index];
        ctx.track  = track_index;

        auto const& clock = _clocks[track_index];
        auto const run    = clock.keys_at(ctx.keys);
        if (run.count == 0) return;

        ctx.reversing = clock.is_reversing();

        for (u16 step = 0; step < run.count; ++step)
        {
            // A track played backwards is played backwards all the way down,
            // so keys sharing a position run in reverse on the return leg.
            ctx.offset = ctx.reversing
                ? to<u16>(run.first + run.count - 1 - step)
                : to<u16>(run.first + step);

            auto const& key       = ctx.keys[ctx.offset];
            bool const fire_once  = (key.flags & data::KEYFRAME_FLAG_FIRE_ONCE) != 0;
            u32 const marker = ctx.base + ctx.offset;

            if (fire_once && has_fired(marker)) continue;
            if (fire(key, ctx) && fire_once) mark_fired(marker);
        }
    }

    void scene_timeline::unload()
    {
        _first      = 0;
        _count      = 0;
        _gfxi_first = 0;
        _gfxi_count = 0;
        _clocks.clear();
        _key_base.clear();
        _fired.clear();
        _glides.clear();
        _cycles.clear();
    }

    void scene_timeline::restore_palette()
    {
        restore_cycles();
    }

    mtl::span<data::track> scene_timeline::tracks()
    {
        auto const all = _data.tracks();
        return mtl::span<data::track>(all.data() + _first, _count);
    }

    void scene_timeline::load(data::scene const& scene)
    {
        unload();

        // Kept whatever the timeline turns out to be, so that a GFXI id in a
        // keyframe payload can be mapped to a scene-local slot.
        _gfxi_first = scene.graphic_instance_range.start;
        _gfxi_count = scene.graphic_instance_range.count;

        if (scene.timeline_id == data::NO_ID) return;

        auto const timelines = _data.timelines();
        if (scene.timeline_id >= timelines.size())
        {
            NE_LOG("scene names timeline %hu of %lu",
                scene.timeline_id,
                to<ULONG>(timelines.size()));
            return;
        }

        auto const& time = timelines[scene.timeline_id];
        auto const all   = _data.tracks();

        // An empty or out-of-bounds range simply leaves the scene inert.
        auto const scene_tracks = time.track_range.of(all);
        if (scene_tracks.is_empty())
        {
            if (time.track_range.count != 0)
            {
                NE_LOG("track range %hu+%hu exceeds %lu tracks",
                    time.track_range.start,
                    time.track_range.count,
                    to<ULONG>(all.size()));
            }
            return;
        }

        _first = time.track_range.start;
        _count = to<u16>(scene_tracks.size());
        _clocks.reserve(scene_tracks.size());
        _key_base.reserve(scene_tracks.size());

        // One movement slot per instance, whether or not any track moves it:
        // an instance's position is its own, so two tracks moving it are settled
        // by which of them wrote last rather than by remembering both.
        _glides.reserve(_gfxi_count);
        for (u16 slot = 0; slot < _gfxi_count; ++slot) _glides.push_back(glide{});

        u32 total_keys      = 0;
        u32 unknown_keys    = 0;  ///< Keyframes whose type this build cannot name.

        // Only ever read back through NE_LOG, which a build without logging
        // expands to nothing.
        [[maybe_unused]] u8 first_unknown = 0;  ///< The first such type seen.
        auto const all_keyframes = _data.keyframes();

        NE_LOG("%hu, %lu track(s)", scene.timeline_id, to<ULONG>(scene_tracks.size()));
        for (u16 index = 0; index < scene_tracks.size(); ++index)
        {
            auto const& track = scene_tracks[index];
            NE_LOG("  track %hu: name %hu, keys %hu+%hu, %hu frames, %s x%hu, %s",
                index,
                track.name_id,
                track.keyframe_range.start,
                track.keyframe_range.count,
                track.duration,
                loop_mode_name(track.loop_mode),
                to<u16>(track.loop_count),
                (track.flags & data::TRACK_FLAG_AUTO_START) ? "auto" : "on cue");

            // The editor rejects this, so it means the pak was built by
            // something else. track_clock plays it as if it were 1 frame long
            // rather than refusing, but the data is still wrong.
            if (track.duration == 0)
            {
                NE_LOG("  track %hu has no duration; playing it as 1 frame", index);
            }

            bool const in_range = keys_in_range(track);

            _key_base.push_back(total_keys);
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
                    to<ULONG>(all_keyframes.size()));
            }
            else
            {
                for (u16 offset = 0; offset < track.keyframe_range.count; ++offset)
                {
                    auto const& key = all_keyframes[to<size_t>(track.keyframe_range.start) + offset];

                    // A type this build cannot name at all means the pak was
                    // built by a newer editor. Counted here and reported once
                    // below, because a looping track would say so 50 times a
                    // second at fire time.
                    if (key.type >= data::KEYFRAME_TYPE_COUNT)
                    {
                        if (unknown_keys == 0) first_unknown = key.type;
                        ++unknown_keys;
                    }

                    // Reported here rather than at fire time for the same reason:
                    // a bad id on a looping track would say so 50 times a second.
                    bool const names_instance = is_move(key)
                        || to<data::keyframe_type>(key.type)
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
            _clocks.push_back(clock);
        }

        // A pak this far out of step is wrong as a whole, so one line says so
        // rather than one per type: the first type this build cannot name is
        // enough to identify which editor built it.
        if (unknown_keys > 0)
        {
            NE_LOG("  %lu keyframe(s) have types this build does not know, first %hu; "
                   "they will be skipped",
                to<ULONG>(unknown_keys),
                to<u16>(first_unknown));
        }

        // The editor rejects a name repeated inside one scene, so this means the
        // pak was built by something else. ANIM_PLAY takes the first match,
        // which is at least deterministic, but the later one is unreachable.
        for (u16 a = 0; a < scene_tracks.size(); ++a)
        {
            for (u16 b = to<u16>(a + 1); b < scene_tracks.size(); ++b)
            {
                if (scene_tracks[a].name_id != scene_tracks[b].name_id)
                {
                    continue;
                }

                NE_LOG("  tracks %hu and %hu share name %hu; only the first can be played",
                    a,
                    b,
                    scene_tracks[a].name_id);
                break;
            }
        }

        _fired.reserve((total_keys + 7) / 8);
        for (u32 byte = 0; byte < (total_keys + 7) / 8; ++byte) _fired.push_back(0);

        // After _fired exists, because priming has to see which fire_once keys
        // are spent, and after scene_bobs::load(), because a movement with no
        // earlier one to lead it starts from where its instance already stands.
        for (u16 index = 0; index < scene_tracks.size(); ++index)
        {
            if (_clocks[index].is_running()) prime_glides(index, scene_tracks[index]);
        }
    }

    void scene_timeline::update()
    {
        if (_count == 0) return;

        // Ahead of the tracks, so that a movement is read off the playhead the
        // frame it is standing on rather than the one it is about to move to.
        // A crossing writes the exact position afterwards, and the two agree at
        // a span's ends, so nothing is drawn twice differently.
        update_glides();

        // Read off the playhead alongside the movements and for the same
        // reason: a band belongs to the position the playhead stands on this
        // frame, not the one it is about to move to.
        update_cycles();

        auto const scene_tracks = tracks();
        for (u16 index = 0; index < _count; ++index)
        {
            auto& clock = _clocks[index];
            if (!clock.is_running()) continue;

            // Fire before ticking, so a run's very first frame plays whatever
            // sits at position 0 rather than stepping straight over it.
            fire_keyframes(index, scene_tracks[index]);

            switch (clock.tick(scene_tracks[index]))
            {
                case tick_result::LOOPED:
                    // Every cycle leads into its first movement the way the
                    // first cycle did, so a looping track glides into it rather
                    // than gliding once and teleporting ever after.
                    prime_glides(index, scene_tracks[index]);

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
                    NE_LOG("track %hu finished after %hu cycle(s)", index, clock.cycles_done());
                    break;

                case tick_result::IDLE:
                case tick_result::RUNNING:
                    break;
            }
        }

        // Once every band has been turned, so that a frame which moved several
        // of them still rebuilds the hardware palette only the once.
        commit_palette();
    }

    void scene_timeline::play(u16 name_id)
    {
        u16 const index = find_track(name_id);
        if (index == NO_TRACK)
        {
            NE_LOG("ANIM_PLAY names track %hu, which this scene does not have", name_id);
            return;
        }

        // A play is a new run, and a run that is already under way cannot be
        // begun again without deciding what happens to everything the first one
        // has already done. A script that means to start over says so: stop,
        // then play.
        if (_clocks[index].is_running())
        {
            NE_LOG("ANIM_PLAY ignored; track %hu (name %hu) is already running",
                index,
                name_id);
            return;
        }

        auto const& track = tracks()[index];
        if (!keys_in_range(track))
        {
            NE_LOG("ANIM_PLAY cannot start track %hu; its keyframes are out of range",
                index);
            return;
        }

        clear_fired(index, track);
        clear_cycles(index);
        _clocks[index].start();
        prime_glides(index, track);
        NE_LOG("ANIM_PLAY started track %hu (name %hu)", index, name_id);
    }

    void scene_timeline::stop(u16 name_id)
    {
        u16 const index = find_track(name_id);
        if (index == NO_TRACK)
        {
            NE_LOG("ANIM_STOP names track %hu, which this scene does not have", name_id);
            return;
        }

        // Stopping a track that is not running is the ordinary way to make sure
        // of a track's state before playing it, so it passes quietly.
        if (!_clocks[index].is_running()) return;

        _clocks[index].stop();
        NE_LOG("ANIM_STOP halted track %hu (name %hu) at position %hu",
            index,
            name_id,
            _clocks[index].position());
    }
}  // namespace NEONengine
