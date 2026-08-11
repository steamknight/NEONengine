/**
 * @file bitset.h
 * @brief Runtime-sized bitset container
 *
 * A fixed-capacity bitset whose size is set at construction time, not compile
 * time. Bits are packed into words of type T (e.g. u8, u16, u32, u64).
 * The capacity cannot change after construction.
 *
 * @tparam T        The word type used to store bits. Must be an unsigned
 *                  integer type. Defaults to u8.
 * @tparam MemFlags Memory allocation flags passed to the allocator.
 *
 * @example
 * @code
 * mtl::bitset<u32> flags(256); // 256 bits packed into 8 u32 words
 * flags.set(7);
 * flags.set(200);
 * if (flags.test(7)) { ... }
 * flags.flip();
 * @endcode
 */

#ifndef __MTL__BITSET__INCLUDED__
#define __MTL__BITSET__INCLUDED__

#include <stddef.h>

#include "cstdint.h"
#include "memory.h"
#include "utility.h"

namespace mtl
{
    template<class T = u8, MemF MemFlags = MemF::Fast>
    class bitset
    {
        public:  ///////////////////////////////////////////////////////////////////////////////////
        /*
         *   _____                _                   _
         *  / ____|              | |                 | |
         * | |     ___  _ __  ___| |_ _ __ _   _  ___| |_ ___  _ __ ___
         * | |    / _ \| '_ \/ __| __| '__| | | |/ __| __/ _ \| '__/ __|
         * | |___| (_) | | | \__ \ |_| |  | |_| | (__| || (_) | |  \__ \
         *  \_____\___/|_| |_|___/\__|_|   \__,_|\___|\__\___/|_|  |___/
         *
         */

        /**
         * @brief Construct an empty bitset holding no bits.
         */
        bitset() noexcept
            : _bit_count(0), _word_count(0), _words(nullptr)
        {
        }

        explicit bitset(usize bit_count)
            : _bit_count(bit_count)
            , _word_count((bit_count + _bits_per_word - 1) / _bits_per_word)
            , _words(nullptr)
        {
            if (_word_count > 0) { _words = new(MemFlags | MemF::Clear) T[_word_count]; }
        }

        ~bitset()
        {
            delete[] _words;
        }

        /**
         * @brief Copy constructor.
         */
        bitset(bitset const& other)
            : _bit_count(other._bit_count)
            , _word_count(other._word_count)
            , _words(nullptr)
        {
            if (_word_count == 0) return;
            _words = new(MemFlags) T[_word_count];
            for (usize i = 0; i < _word_count; ++i) { _words[i] = other._words[i]; }
        }

        /**
         * @brief Copy assignment.
         */
        bitset& operator=(bitset const& other)
        {
            if (this != &other)
            {
                delete[] _words;
                _bit_count  = other._bit_count;
                _word_count = other._word_count;
                _words      = nullptr;
                if (_word_count > 0)
                {
                    _words = new(MemFlags) T[_word_count];
                    for (usize i = 0; i < _word_count; ++i) { _words[i] = other._words[i]; }
                }
            }
            return *this;
        }

        /**
         * @brief Move constructor.
         */
        bitset(bitset&& other) noexcept
            : _bit_count(other._bit_count)
            , _word_count(other._word_count)
            , _words(other._words)
        {
            other._words      = nullptr;
            other._bit_count  = 0;
            other._word_count = 0;
        }

        /**
         * @brief Move assignment.
         */
        bitset& operator=(bitset&& other) noexcept
        {
            if (this != &other)
            {
                delete[] _words;
                _bit_count        = other._bit_count;
                _word_count       = other._word_count;
                _words            = other._words;
                other._words      = nullptr;
                other._bit_count  = 0;
                other._word_count = 0;
            }
            return *this;
        }

        /*
         *     /\
         *    /  \   ___ ___ ___  ___ ___  ___  _ __ ___
         *   / /\ \ / __/ __/ _ \/ __/ __|/ _ \| '__/ __|
         *  / ____ \ (_| (_|  __/\__ \__ \ (_) | |  \__ \
         * /_/    \_\___\___\___||___/___/\___/|_|  |___/
         *
         */

        /**
         * @brief Read a single bit.
         *
         * @param pos  Zero-based bit index. Must be < size().
         * @return true if the bit is set, false if clear.
         *
         * @warning No bounds check. Use test() for a checked read.
         */
        bool operator[](usize pos) const noexcept
        {
            return (_words[pos / _bits_per_word] >> (pos % _bits_per_word)) & T(1);
        }

        /**
         * @brief Read a single bit with bounds checking.
         *
         * A bit the bitset was not sized for is never set, so an index past the
         * end reads as false rather than killing the machine. std::bitset throws
         * here, which is not on offer in a build without exceptions.
         *
         * @param pos  Zero-based bit index.
         * @return true if the bit is set, false if clear or out of range.
         */
        bool test(usize pos) const noexcept
        {
            if (pos >= _bit_count) return false;
            return (*this)[pos];
        }

        /*
         *   ____
         *  / __ \
         * | |  | |_   _  ___ _ __ _   _
         * | |  | | | | |/ _ \ '__| | | |
         * | |__| | |_| |  __/ |  | |_| |
         *  \___\_\\__,_|\___|_|   \__, |
         *                          __/ |
         *                         |___/
         */

        /**
         * @brief True if at least one bit is set.
         */
        bool any() const noexcept
        {
            for (usize i = 0; i < _word_count; ++i)
            {
                if (_words[i]) return true;
            }
            return false;
        }

        /**
         * @brief True if all bits are set.
         */
        bool all() const noexcept
        {
            // Full words must be all-ones.
            usize const full_words = _bit_count / _bits_per_word;
            T const     all_ones   = ~T(0);
            for (usize i = 0; i < full_words; ++i)
            {
                if (_words[i] != all_ones) return false;
            }
            // Check the last partial word, if any.
            T const mask = _tail_mask();
            if (mask != 0 && (_words[_word_count - 1] & mask) != mask) return false;
            return true;
        }

        /**
         * @brief True if no bits are set.
         */
        bool none() const noexcept { return !any(); }

        /**
         * @brief Number of bits set to true.
         */
        usize count() const noexcept
        {
            usize n = 0;
            for (usize i = 0; i < _word_count; ++i)
            {
                // Brian Kernighan's method.
                T w = _words[i];
                while (w) { ++n; w &= w - T(1); }
            }
            return n;
        }

        /**
         * @brief Total number of bits the bitset holds.
         */
        usize size() const noexcept { return _bit_count; }

        /*
         *  __  __       _        _   _
         * |  \/  |     | |      | | (_)
         * | \  / |_   _| |_ __ _| |_ _  ___  _ __
         * | |\/| | | | | __/ _` | __| |/ _ \| '_ \
         * | |  | | |_| | || (_| | |_| | (_) | | | |
         * |_|  |_|\__,_|\__\__,_|\__|_|\___/|_| |_|
         *
         */

        /**
         * @brief Set all bits to true.
         */
        bitset& set() noexcept
        {
            T const all_ones = ~T(0);
            for (usize i = 0; i < _word_count; ++i) { _words[i] = all_ones; }
            _trim();
            return *this;
        }

        /**
         * @brief Set a single bit to true or false. An index past the end does nothing.
         */
        bitset& set(usize pos, bool value = true) noexcept
        {
            if (pos >= _bit_count) return *this;

            usize const bit = pos % _bits_per_word;
            T const mask = T(1) << bit;
            T& word = _words[pos / _bits_per_word];

            word = (word & ~mask) | (T(value) << bit);
            return *this;
        }

        /**
         * @brief Clear all bits to false.
         */
        bitset& reset() noexcept
        {
            for (usize i = 0; i < _word_count; ++i) { _words[i] = T(0); }
            return *this;
        }

        /**
         * @brief Clear a single bit to false. An index past the end does nothing.
         *
         * @param pos  Zero-based bit index.
         */
        bitset& reset(usize pos) noexcept
        {
            return set(pos, false);
        }

        /**
         * @brief Flip all bits.
         */
        bitset& flip() noexcept
        {
            for (usize i = 0; i < _word_count; ++i) { _words[i] = ~_words[i]; }
            _trim();
            return *this;
        }

        /**
         * @brief Flip a single bit. An index past the end does nothing.
         *
         * @param pos  Zero-based bit index.
         */
        bitset& flip(usize pos) noexcept
        {
            if (pos >= _bit_count) return *this;
            _words[pos / _bits_per_word] ^= T(1) << (pos % _bits_per_word);
            return *this;
        }

        /*
         *   ____                       _
         *  / __ \                     | |
         * | |  | |_ __   ___ _ __ __ _| |_ ___  _ __ ___
         * | |  | | '_ \ / _ \ '__/ _` | __/ _ \| '__/ __|
         * | |__| | |_) |  __/ | | (_| | || (_) | |  \__ \
         *  \____/| .__/ \___|_|  \__,_|\__\___/|_|  |___/
         *        | |
         *        |_|
         */

        bitset& operator&=(bitset const& rhs) noexcept
        {
            usize const shared = _shared_words(rhs);
            for (usize i = 0; i < shared; ++i) { _words[i] &= rhs._words[i]; }
            // Bits this holds but rhs does not are ANDed against nothing.
            for (usize i = shared; i < _word_count; ++i) { _words[i] = T(0); }
            return *this;
        }

        bitset& operator|=(bitset const& rhs) noexcept
        {
            usize const shared = _shared_words(rhs);
            for (usize i = 0; i < shared; ++i) { _words[i] |= rhs._words[i]; }
            _trim();
            return *this;
        }

        bitset& operator^=(bitset const& rhs) noexcept
        {
            usize const shared = _shared_words(rhs);
            for (usize i = 0; i < shared; ++i) { _words[i] ^= rhs._words[i]; }
            _trim();
            return *this;
        }

        bitset operator~() const
        {
            bitset result(*this);
            result.flip();
            return result;
        }

        /**
         * @brief Shift all bits left (towards higher indices) by @p shift positions.
         */
        bitset& operator<<=(usize shift) noexcept
        {
            if (shift >= _bit_count) { reset(); return *this; }
            if (shift == 0) return *this;

            usize const word_shift = shift / _bits_per_word;
            usize const bit_shift  = shift % _bits_per_word;

            if (bit_shift == 0)
            {
                for (usize i = _word_count; i-- > 0;)
                {
                    _words[i] = (i >= word_shift) ? _words[i - word_shift] : T(0);
                }
            }
            else
            {
                for (usize i = _word_count; i-- > 0;)
                {
                    T lo = (i >= word_shift)     ? (_words[i - word_shift] << bit_shift)                            : T(0);
                    T hi = (i >= word_shift + 1) ? (_words[i - word_shift - 1] >> (_bits_per_word - bit_shift))     : T(0);
                    _words[i] = lo | hi;
                }
            }

            _trim();
            return *this;
        }

        /**
         * @brief Shift all bits right (towards lower indices) by @p shift positions.
         */
        bitset& operator>>=(usize shift) noexcept
        {
            if (shift >= _bit_count) { reset(); return *this; }
            if (shift == 0) return *this;

            usize const word_shift = shift / _bits_per_word;
            usize const bit_shift  = shift % _bits_per_word;

            if (bit_shift == 0)
            {
                for (usize i = 0; i < _word_count; ++i)
                {
                    _words[i] = (i + word_shift < _word_count) ? _words[i + word_shift] : T(0);
                }
            }
            else
            {
                for (usize i = 0; i < _word_count; ++i)
                {
                    T hi = (i + word_shift < _word_count)     ? (_words[i + word_shift] >> bit_shift)                        : T(0);
                    T lo = (i + word_shift + 1 < _word_count) ? (_words[i + word_shift + 1] << (_bits_per_word - bit_shift)) : T(0);
                    _words[i] = hi | lo;
                }
            }

            return *this;
        }

        bitset operator<<(usize shift) const { bitset r(*this); r <<= shift; return r; }
        bitset operator>>(usize shift) const { bitset r(*this); r >>= shift; return r; }

        friend bitset operator&(bitset lhs, bitset const& rhs) noexcept { lhs &= rhs; return lhs; }
        friend bitset operator|(bitset lhs, bitset const& rhs) noexcept { lhs |= rhs; return lhs; }
        friend bitset operator^(bitset lhs, bitset const& rhs) noexcept { lhs ^= rhs; return lhs; }

        private:  //////////////////////////////////////////////////////////////////////////////////
        static constexpr usize _bits_per_word = sizeof(T) * 8;

        usize _bit_count;   ///< Total bits, as given to the constructor.
        usize _word_count;  ///< Number of T words in _words.
        T*    _words;       ///< Packed word storage, owned by this object.

        /**
         * @brief Words both bitsets have, so a mismatched pair cannot run off
         *        the shorter buffer.
         */
        usize _shared_words(bitset const& other) const noexcept
        {
            return _word_count < other._word_count ? _word_count : other._word_count;
        }

        /**
         * @brief Mask covering the valid bits in the last word.
         *
         * Returns 0 when _bit_count is an exact multiple of _bits_per_word,
         * meaning there is no partial word to worry about.
         */
        T _tail_mask() const noexcept
        {
            usize const tail_bits = _bit_count % _bits_per_word;
            return (tail_bits == 0) ? T(0) : T((T(1) << tail_bits) - T(1));
        }

        /**
         * @brief Zero the stray bits beyond _bit_count in the last word.
         *
         * Must be called after any operation that may set bits past the
         * logical end of the bitset (set(), flip(), <<=).
         */
        void _trim() noexcept
        {
            T const mask = _tail_mask();
            if (mask != 0) { _words[_word_count - 1] &= mask; }
        }
    };

}  // namespace mtl

#endif  // __MTL__BITSET__INCLUDED__
