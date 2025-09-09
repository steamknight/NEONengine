/**
 * @file bstr_view.h
 * @brief Lightweight non‑owning string view for the engine's raw or
 * length‑prefixed bstr blocks.
 *
 * A bstr (in this project) is laid out in memory as:
 * @code
 * [ uint32_t length ][ char data... ][ '\0' ]
 * @endcode
 * Where @c length is the number of character bytes (not counting the trailing
 * null).
 *
 * This type never allocates, frees or throws; it is a pure façade over existing
 * memory (similar in spirit to @c std::string_view). It can be constructed
 * from:
 *  - A raw C string (null terminated)
 *  - A string literal
 *  - A pair (pointer, length)
 *  - A bstr block starting at its 4‑byte length header (@ref
 * bstr_view::from_bstr)
 *
 * Limitations:
 *  - No bounds checking on operator[] (mirrors typical unchecked access
 * patterns).
 *  - Assumes the string length is of the proper endienness.
 *
 * Unlike the @c std::string_view, this class considers the string IMMUTABLE
 * since it can take string literals.
 */

#ifndef __BSTR_VIEW__INCLUDED__
#define __BSTR_VIEW__INCLUDED__

#include <stddef.h>

#include <mini_std/stdint.h>
#include <mini_std/string.h>

#include <mtl/utility.h>

namespace NEONengine
{
    /**
     * @brief Non‑owning view over contiguous character data.
     *
     * All accessors are @c constexpr where possible enabling compile‑time usage
     * for string literals. The stored pointer always references a valid buffer
     * (falls back to a static @c '\0' for empty views) so @ref data() never
     * returns null.
     *
     * The string is considered immutable.
     */
    class bstr_view
    {
        public:
        using value_type = char const;
        using pointer    = value_type*;
        using reference  = value_type&;
        using size_type  = size_t;

        public:  ///////////////////////////////////////////////////////////////////////////////////
        /*
         *   _____                _                   _
         *  / ____|              | |                 | |
         * | |     ___  _ __  ___| |_ _ __ _   _  ___| |_ ___  _ __ ___
         * | |    / _ \| '_ \/ __| __| '__| | | |/ __| __/ _ \| '__/ __|
         * | |___| (_) | | | \__ \ |_| |  | |_| | (__| || (_) | |  \__ \
         *  \_____\___/|_| |_|___/\__|_|   \__,_|\___|\__\___/|_|  |___/
         */

        /**
         * @brief Construct an empty view.
         */
        constexpr bstr_view() noexcept : _data(&empty_char), _length(0) {}

        /**
         * @brief Construct from a string literal / char array.
         *
         * @tparam N Array extent (includes trailing null for literals).
         * Stores length as N-1 when N > 0.
         */
        template<size_type N>
        constexpr bstr_view(char const (&lit)[N]) noexcept : _data(lit)
                                                           , _length(N ? (N - 1) : 0)
        {}

        /**
         * @brief Construct from null terminated C string (length scanned once).
         *
         * @param s Pointer (may be nullptr, treated as empty).
         */
        constexpr bstr_view(char const* s) noexcept : _data(s ? s : &empty_char), _length(strlen(s))
        {}

        /**
         * @brief Construct from pointer + explicit length (no null validation).
         *
         * @param s Data pointer (may be nullptr => becomes empty view unless
         * len>0 which is ignored).
         * @param len Number of characters to expose.
         */
        constexpr bstr_view(pointer s, size_type len) noexcept
            : _data((s && len) ? s : (s ? s : &empty_char))
            , _length(len)
        {}

        /**
         * @brief Create from pointer to start of bstr length header.
         *
         * @param header Pointer to first byte of 32‑bit length field.
         * @warning No validation of buffer size or terminator. Ensure the
         * memory is valid.
         */
        static constexpr bstr_view from_bstr(void const* header) noexcept
        {
            if (!header) { return bstr_view(); }

            uint8_t const* raw = mtl::to<uint8_t const*>(header);

            // Read 32-bit length (assuming native endianness matches stored
            // format) Layout: [0..3] length, [4..4+len-1] chars, [4+len] '\0'
            uint32_t len = (mtl::to<uint32_t>(raw[0]) << 24)    //
                           | (mtl::to<uint32_t>(raw[1]) << 16)  //
                           | (mtl::to<uint32_t>(raw[2]) << 8)   //
                           | (mtl::to<uint32_t>(raw[3]) << 0);
            pointer strData = reinterpret_cast<pointer>(raw + 4);

            return bstr_view(strData, mtl::to<size_type>(len));
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
         * @brief Length of the string
         *
         * @return Number of characters (not including trailing null).
         */
        constexpr size_type length() const noexcept { return _length; }

        /**
         * @brief Checks if the view is empty.
         *
         * @return True if the view has zero length.
         */
        constexpr bool is_empty() const noexcept { return _length == 0; }

        /**
         * @brief Pointer to characters (never null).
         * Guaranteed null-terminated if source was a valid bstr or C string.
         *
         * @return Pointer to first character (never nullptr).
         */
        constexpr pointer data() const noexcept { return _data; }

        /**
         * @brief Unchecked element access.
         *
         * @param i Index < size()
         */
        constexpr char const& operator[](size_type i) const noexcept { return _data[i]; }

        /*
         * Constant iterators for beginning and end.
         */
        constexpr pointer begin() const noexcept { return _data; }
        constexpr pointer end() const noexcept { return _data + _length; }

        // Conversions
        constexpr operator pointer() const noexcept { return _data; }

        /*
         *   _____                                 _
         *  / ____|                               (_)
         * | |     ___  _ __ ___  _ __   __ _ _ __ _ ___  ___  _ __
         * | |    / _ \| '_ ` _ \| '_ \ / _` | '__| / __|/ _ \| '_ \
         * | |___| (_) | | | | | | |_) | (_| | |  | \__ \ (_) | | | |
         *  \_____\___/|_| |_| |_| .__/ \__,_|_|  |_|___/\___/|_| |_|
         *                       | |
         *                       |_|
         */

        /**
         * @brief Lexicographical comparison.
         * @return < 0 if *this < other, 0 if equal, > 0 if *this > other.
         */
        constexpr int compare(bstr_view other) const noexcept
        {
            size_type const n = (_length < other._length) ? _length : other._length;
            for (size_type i = 0; i < n; ++i)
            {
                auto a = mtl::to<uint8_t>(_data[i]);
                auto b = mtl::to<uint8_t>(other._data[i]);
                if (a < b) return -1;
                if (a > b) return 1;
            }
            if (_length < other._length) return -1;
            if (_length > other._length) return 1;
            return 0;
        }

        /**
         * @brief Equality comparison
         */
        constexpr bool operator==(bstr_view other) const noexcept { return compare(other) == 0; }

        /**
         * @brief Inequality comparison
         */
        constexpr bool operator!=(bstr_view other) const noexcept { return compare(other) != 0; }

        /**
         * @brief Strict weak ordering for associative containers / sorting
         */
        constexpr bool operator<(bstr_view other) const noexcept { return compare(other) < 0; }

        private:  //////////////////////////////////////////////////////////////////////////////////
        // Pointer to first character (never null; points to static empty string
        // if empty)
        pointer _data{ &empty_char };

        // Stored length in characters (not including null terminator)
        size_type _length{ 0 };

        // Internal empty string storage
        static constexpr char empty_char = '\0';
    };
}  // namespace NEONengine

#endif  // __BSTR_VIEW__INCLUDED__
