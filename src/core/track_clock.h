/**
 * @file track_clock.h
 * @brief The playhead of a single timeline track.
 *
 * Deliberately knows nothing but its own track record: no engine, no game data,
 * no ACE. A scene's tracks each own one of these and are ticked once per PAL
 * frame, which is what makes their clocks independent - a track started later
 * begins at its own position zero rather than joining one already running.
 *
 * The playhead occupies exactly one position per frame. A track's duration is
 * the *last position*, not a count of frames, because a keyframe is allowed to
 * sit exactly on it; the editor validates keyframe times as 0 <= time <=
 * duration. So a cycle covers these positions:
 *
 *   NONE, RESTART   0, 1, ... D           (D + 1 frames)
 *   PING_PONG       0, 1, ... D ... 1     (2 * D frames)
 *
 * In both modes the tick that completes a cycle lands the playhead back on 0,
 * which is the next cycle's first frame. Nothing is duplicated at the seam, and
 * a ping-pong cycle is literally 0 -> duration -> 0.
 */

#ifndef __TRACK_CLOCK_H__INCLUDED__
#define __TRACK_CLOCK_H__INCLUDED__

#include <stdint.h>

#include <mtl/utility.h>

#include "game_data_format.h"

namespace NEONengine
{
    /**
     * @brief What one tick of a track_clock did, beyond moving the playhead.
     */
    enum class tick_result : uint8_t
    {
        IDLE,       ///< The track was not running; nothing moved.
        RUNNING,    ///< The playhead advanced within the current cycle.
        LOOPED,     ///< A cycle completed and the next one began at position 0.
        FINISHED,   ///< The last cycle completed and the track stopped.
    };

    /**
     * @brief The playhead of one track: where it is and how much is left to play.
     *
     * Reset by start(), advanced by tick(), and halted by stop(). Every call
     * takes the track record so the clock can never be advanced against
     * different parameters than it was started with.
     */
    class track_clock
    {
        public:  ///////////////////////////////////////////////////////////////////////////////////

        /**
         * @brief Puts the playhead on position 0 and starts running forwards.
         *
         * Always restarts: there is no resume, so a track told to play from a
         * position part-way through a previous run still begins at its own 0.
         */
        void start()
        {
            m_position   = 0;
            m_cycles     = 0;
            m_running    = true;
            m_reverse    = false;
        }

        /**
         * @brief Halts the playhead where it stands.
         *
         * A stopped clock's position is meaningless - start() is the only way
         * back to running, and it rewinds - so it is left alone rather than
         * being reset, and tick() will not move it.
         */
        void stop() { m_running = false; }

        /// @brief Whether the playhead is advancing.
        bool is_running() const { return m_running; }

        /// @brief The position the playhead occupies this frame.
        uint16_t position() const { return m_position; }

        /// @brief Cycles completed since start(), saturating rather than wrapping.
        uint16_t cycles_done() const { return m_cycles; }

        /// @brief Whether a ping-pong playhead is on its way back to the start.
        bool is_reversing() const { return m_reverse; }

        /**
         * @brief Moves the playhead on by one frame.
         *
         * @param track The record this clock was started against. A duration of
         *              zero is nonsensical and is treated as 1, so that a track
         *              from a malformed pak cannot send the playhead below zero.
         *
         * @return What the tick did, so the caller can act on a cycle boundary.
         */
        tick_result tick(data::track const& track)
        {
            if (!m_running) return tick_result::IDLE;

            auto const mode    = mtl::to<data::loop_mode>(track.loop_mode);
            bool const wrapped = (mode == data::loop_mode::PING_PONG)
                ? advance_ping_pong(track.duration)
                : advance_forward(track.duration);

            if (!wrapped) return tick_result::RUNNING;

            if (m_cycles != UINT16_MAX) ++m_cycles;

            // A track that does not loop gets exactly the one cycle. Otherwise a
            // loop count of zero runs for as long as the scene lasts.
            bool const last = (mode == data::loop_mode::NONE)
                || (track.loop_count != 0 && m_cycles >= track.loop_count);

            if (!last) return tick_result::LOOPED;

            m_running = false;
            return tick_result::FINISHED;
        }

        private:  //////////////////////////////////////////////////////////////////////////////////

        /**
         * @brief Steps 0, 1, ... duration and back to 0.
         * @return Whether the playhead landed on 0, completing a cycle.
         */
        bool advance_forward(uint16_t duration)
        {
            if (m_position >= effective_duration(duration))
            {
                m_position = 0;
                return true;
            }
            ++m_position;
            return false;
        }

        /**
         * @brief Steps 0, 1, ... duration ... 1 and back to 0.
         *
         * With a duration of 1 the turn at the far end lands straight back on 0,
         * so the reversal and the cycle boundary are the same tick.
         *
         * @return Whether the playhead landed on 0, completing a cycle.
         */
        bool advance_ping_pong(uint16_t duration)
        {
            if (!m_reverse)
            {
                if (m_position < effective_duration(duration))
                {
                    ++m_position;
                    return false;
                }
                m_reverse  = true;
                m_position = effective_duration(duration) - 1;
            }
            else
            {
                --m_position;
            }

            if (m_position != 0) return false;

            m_reverse = false;
            return true;
        }

        /**
         * @brief @p duration, or 1 if the record carries a nonsensical zero.
         *
         * The editor rejects a zero duration, but the clock must not depend on
         * that: reversing away from position 0 would take the playhead below
         * zero and wrap it to the top of the range.
         */
        static uint16_t effective_duration(uint16_t duration) { return duration == 0 ? 1 : duration; }

        uint16_t m_position = 0;      ///< The position occupied this frame.
        uint16_t m_cycles   = 0;      ///< Cycles completed since start().
        bool m_running      = false;  ///< Whether tick() moves the playhead.
        bool m_reverse      = false;  ///< Whether a ping-pong is heading back to 0.
    };
}  // namespace NEONengine

#endif  // __TRACK_CLOCK_H__INCLUDED__
