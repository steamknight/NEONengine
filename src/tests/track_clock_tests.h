#ifndef __TRACK_CLOCK_TESTS_H__INCLUDED__
#define __TRACK_CLOCK_TESTS_H__INCLUDED__

#ifdef ACE_TEST_RUNNER

#include "core/track_clock.h"

#include "test_macros.h"

namespace NEONengine
{
    namespace
    {
        /// A track carrying only the fields the clock reads.
        data::track clock_track(uint16_t duration, data::loop_mode mode, uint8_t loop_count)
        {
            data::track track{};
            track.duration   = duration;
            track.loop_mode  = mtl::to<uint8_t>(mode);
            track.loop_count = loop_count;
            return track;
        }

        /**
         * @brief Checks the playhead occupies @p expected, one position per tick.
         *
         * Reads the position the clock already holds before each tick, so the
         * first entry is where the clock stands on entry rather than where the
         * first tick takes it.
         */
        char const* expect_path(
            track_clock& clock, data::track const& track, uint16_t const* expected, uint16_t count)
        {
            for (uint16_t frame = 0; frame < count; ++frame)
            {
                if (clock.position() != expected[frame]) return "playhead occupied the wrong position";
                clock.tick(track);
            }
            return nullptr;
        }

        /// Ticks @p frames times and reports the last result seen.
        tick_result run(track_clock& clock, data::track const& track, uint16_t frames)
        {
            tick_result result = tick_result::IDLE;
            for (uint16_t frame = 0; frame < frames; ++frame)
            {
                result = clock.tick(track);
            }
            return result;
        }
    }  // namespace

    // ── Starting and stopping ───────────────────────────────────────────────────

    TEST_IMPL(test_clock_starts_idle)
    {
        auto const track = clock_track(10, data::loop_mode::RESTART, 0);
        track_clock clock;

        TEST_ASSERT(!clock.is_running(), "a fresh clock should not be running");
        TEST_ASSERT(clock.tick(track) == tick_result::IDLE, "ticking a stopped clock should do nothing");
        TEST_ASSERT(clock.position() == 0, "a stopped clock's playhead should not move");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_clock_start_rewinds_to_zero)
    {
        track_clock clock;

        clock.start();
        TEST_ASSERT(clock.is_running(), "start should leave the clock running");
        TEST_ASSERT(clock.position() == 0, "start should put the playhead on position 0");
        TEST_ASSERT(clock.cycles_done() == 0, "start should report no completed cycles");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_clock_stop_freezes_the_playhead)
    {
        auto const track = clock_track(10, data::loop_mode::RESTART, 0);
        track_clock clock;

        clock.start();
        run(clock, track, 4);
        clock.stop();

        auto const stopped_at = clock.position();
        TEST_ASSERT(!clock.is_running(), "stop should leave the clock idle");
        TEST_ASSERT(clock.tick(track) == tick_result::IDLE, "a stopped clock should report itself idle");
        TEST_ASSERT(clock.position() == stopped_at, "a stopped clock's playhead should stay put");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_clock_restart_does_not_resume)
    {
        auto const track = clock_track(10, data::loop_mode::RESTART, 0);
        track_clock clock;

        clock.start();
        run(clock, track, 4);
        clock.stop();
        clock.start();

        TEST_ASSERT(clock.position() == 0, "starting a part-played track should rewind it, not resume it");
        TEST_ASSERT(clock.cycles_done() == 0, "starting again should forget the cycles already played");
        TEST_SUCCESS;
    }

    // ── Restart ─────────────────────────────────────────────────────────────────

    TEST_IMPL(test_clock_restart_walks_zero_to_duration)
    {
        // Duration is the last position, not a frame count, so a keyframe
        // authored at the end of the track is reached like any other.
        auto const track          = clock_track(2, data::loop_mode::RESTART, 0);
        uint16_t const expected[] = { 0, 1, 2, 0, 1, 2, 0 };
        track_clock clock;

        clock.start();
        return expect_path(clock, track, expected, 7);
    }

    TEST_IMPL(test_clock_restart_cycle_is_duration_plus_one_frames)
    {
        auto const track = clock_track(2, data::loop_mode::RESTART, 0);
        track_clock clock;

        clock.start();
        TEST_ASSERT(run(clock, track, 2) == tick_result::RUNNING, "a cycle should not end early");
        TEST_ASSERT(clock.tick(track) == tick_result::LOOPED, "a cycle should span duration + 1 frames");
        TEST_ASSERT(clock.position() == 0, "the tick that ends a cycle should land on position 0");
        TEST_ASSERT(clock.cycles_done() == 1, "completing a cycle should count it");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_clock_restart_with_no_count_never_finishes)
    {
        auto const track = clock_track(3, data::loop_mode::RESTART, 0);
        track_clock clock;

        clock.start();
        run(clock, track, 4 * 200);
        TEST_ASSERT(clock.is_running(), "a loop count of 0 should mean forever");
        TEST_ASSERT(clock.cycles_done() == 200, "every cycle should be counted");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_clock_restart_stops_after_its_loop_count)
    {
        auto const track = clock_track(1, data::loop_mode::RESTART, 3);
        track_clock clock;

        clock.start();
        // Two frames per cycle, so the third cycle ends on the sixth tick.
        TEST_ASSERT(run(clock, track, 5) == tick_result::RUNNING, "the track finished a cycle too early");
        TEST_ASSERT(clock.tick(track) == tick_result::FINISHED, "the track should finish on its last cycle");
        TEST_ASSERT(!clock.is_running(), "a finished track should not be running");
        TEST_ASSERT(clock.cycles_done() == 3, "a finished track should have played its whole loop count");
        TEST_ASSERT(clock.position() == 1, "a finished forward track should rest on its last position");
        TEST_ASSERT(clock.tick(track) == tick_result::IDLE, "a finished track should stay finished");
        TEST_SUCCESS;
    }

    // ── None ────────────────────────────────────────────────────────────────────

    TEST_IMPL(test_clock_none_plays_once)
    {
        auto const track = clock_track(2, data::loop_mode::NONE, 0);
        track_clock clock;

        clock.start();
        TEST_ASSERT(run(clock, track, 2) == tick_result::RUNNING, "the track should play its whole duration");
        TEST_ASSERT(clock.tick(track) == tick_result::FINISHED, "a non-looping track should stop after one cycle");
        TEST_ASSERT(clock.cycles_done() == 1, "a non-looping track plays exactly one cycle");
        TEST_ASSERT(clock.position() == 2, "a non-looping track should rest on its last position");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_clock_none_ignores_a_loop_count)
    {
        // The editor rejects this pairing, so it can only arrive from a pak
        // built by something else; playing once is still the honest reading.
        auto const track = clock_track(1, data::loop_mode::NONE, 5);
        track_clock clock;

        clock.start();
        TEST_ASSERT(run(clock, track, 2) == tick_result::FINISHED, "a non-looping track should ignore its count");
        TEST_SUCCESS;
    }

    // ── Ping-pong ───────────────────────────────────────────────────────────────

    TEST_IMPL(test_clock_ping_pong_walks_out_and_back)
    {
        // One cycle is 0 -> duration -> 0: the tick that returns to 0 both ends
        // the cycle and begins the next, so no position is played twice at the seam.
        auto const track          = clock_track(3, data::loop_mode::PING_PONG, 0);
        uint16_t const expected[] = { 0, 1, 2, 3, 2, 1, 0, 1, 2, 3, 2, 1, 0 };
        track_clock clock;

        clock.start();
        return expect_path(clock, track, expected, 13);
    }

    TEST_IMPL(test_clock_ping_pong_cycle_is_twice_the_duration)
    {
        auto const track = clock_track(3, data::loop_mode::PING_PONG, 0);
        track_clock clock;

        clock.start();
        TEST_ASSERT(run(clock, track, 5) == tick_result::RUNNING, "a cycle should not end early");
        TEST_ASSERT(clock.tick(track) == tick_result::LOOPED, "a cycle should span twice the duration");
        TEST_ASSERT(clock.position() == 0, "a ping-pong cycle should end back on position 0");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_clock_ping_pong_reports_its_leg)
    {
        auto const track = clock_track(2, data::loop_mode::PING_PONG, 0);
        track_clock clock;

        clock.start();
        TEST_ASSERT(!clock.is_reversing(), "a track should set out forwards");
        run(clock, track, 2);
        TEST_ASSERT(clock.position() == 2, "the playhead should reach the far end");
        clock.tick(track);
        TEST_ASSERT(clock.is_reversing(), "the playhead should turn round at the far end");
        clock.tick(track);
        TEST_ASSERT(!clock.is_reversing(), "returning to the start should face the playhead forwards again");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_clock_ping_pong_of_one_frame_turns_and_wraps_together)
    {
        // The turn lands straight back on 0, so the reversal and the cycle
        // boundary are the same tick.
        auto const track          = clock_track(1, data::loop_mode::PING_PONG, 0);
        uint16_t const expected[] = { 0, 1, 0, 1, 0 };
        track_clock clock;

        clock.start();
        auto const failure = expect_path(clock, track, expected, 5);
        if (failure) return failure;

        TEST_ASSERT(!clock.is_reversing(), "the playhead should be facing forwards at the seam");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_clock_ping_pong_stops_after_its_loop_count)
    {
        auto const track = clock_track(2, data::loop_mode::PING_PONG, 2);
        track_clock clock;

        clock.start();
        // Four frames per cycle, so the second cycle ends on the eighth tick.
        TEST_ASSERT(run(clock, track, 7) == tick_result::RUNNING, "the track finished a cycle too early");
        TEST_ASSERT(clock.tick(track) == tick_result::FINISHED, "the track should finish on its last cycle");
        TEST_ASSERT(clock.cycles_done() == 2, "a finished track should have played its whole loop count");
        TEST_ASSERT(clock.position() == 0, "a finished ping-pong should come to rest back at the start");
        TEST_SUCCESS;
    }

    // ── Malformed data ──────────────────────────────────────────────────────────

    TEST_IMPL(test_clock_zero_duration_restart_stays_in_range)
    {
        auto const track = clock_track(0, data::loop_mode::RESTART, 0);
        track_clock clock;

        clock.start();
        for (uint16_t frame = 0; frame < 16; ++frame)
        {
            clock.tick(track);
            TEST_ASSERT(clock.position() <= 1, "a track with no duration should be played as a single frame");
        }
        TEST_SUCCESS;
    }

    TEST_IMPL(test_clock_zero_duration_ping_pong_does_not_wrap_below_zero)
    {
        // Turning round at position 0 would otherwise step to -1 and wrap to the
        // top of the range, leaving the track running for another 65535 frames.
        auto const track = clock_track(0, data::loop_mode::PING_PONG, 0);
        track_clock clock;

        clock.start();
        for (uint16_t frame = 0; frame < 16; ++frame)
        {
            clock.tick(track);
            TEST_ASSERT(clock.position() <= 1, "a track with no duration should be played as a single frame");
        }
        TEST_SUCCESS;
    }

    TEST_IMPL(test_clock_cycle_count_saturates)
    {
        // Only reachable on an endlessly looping track, where the count exists
        // solely to decide how much to log; wrapping it would start the log up again.
        auto const track = clock_track(1, data::loop_mode::RESTART, 0);
        track_clock clock;

        clock.start();
        for (uint32_t frame = 0; frame < 2 * 65540UL; ++frame)
        {
            clock.tick(track);
        }
        TEST_ASSERT(clock.cycles_done() == UINT16_MAX, "the cycle count should stick rather than wrap");
        TEST_ASSERT(clock.is_running(), "saturating the cycle count should not stop the track");
        TEST_SUCCESS;
    }

    TEST_SUITE_BEGIN(track_clock)
        TEST(test_clock_starts_idle)
        TEST(test_clock_start_rewinds_to_zero)
        TEST(test_clock_stop_freezes_the_playhead)
        TEST(test_clock_restart_does_not_resume)

        TEST(test_clock_restart_walks_zero_to_duration)
        TEST(test_clock_restart_cycle_is_duration_plus_one_frames)
        TEST(test_clock_restart_with_no_count_never_finishes)
        TEST(test_clock_restart_stops_after_its_loop_count)

        TEST(test_clock_none_plays_once)
        TEST(test_clock_none_ignores_a_loop_count)

        TEST(test_clock_ping_pong_walks_out_and_back)
        TEST(test_clock_ping_pong_cycle_is_twice_the_duration)
        TEST(test_clock_ping_pong_reports_its_leg)
        TEST(test_clock_ping_pong_of_one_frame_turns_and_wraps_together)
        TEST(test_clock_ping_pong_stops_after_its_loop_count)

        TEST(test_clock_zero_duration_restart_stays_in_range)
        TEST(test_clock_zero_duration_ping_pong_does_not_wrap_below_zero)
        TEST(test_clock_cycle_count_saturates)
    TEST_SUITE_END
}  // namespace NEONengine

#endif  // ACE_TEST_RUNNER

#endif  // __TRACK_CLOCK_TESTS_H__INCLUDED__
