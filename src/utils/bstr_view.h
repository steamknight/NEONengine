/**
 * @file bstr_view.h
 * @brief Lightweight non-owning view over a custom length-prefixed bstr.
 *
 * bstr memory layout (little endian assumed for length field):
 *   [uint32 length][N bytes characters]['\0']
 * Where length is the number of character bytes (excluding the null terminator).
 *
 * This class does NOT allocate or free memory. It simply observes an existing
 * bstr or a plain C string. It's analogous to std::string_view but tailored to
 * the project constraints (no <string_view>, no <cstring>, no exceptions).
 */

#ifndef __BSTR_VIEW__INCLUDED__
#define __BSTR_VIEW__INCLUDED__

#include <stddef.h> // size_t

// If project has a central assert / trap macro, you can include it here.
// For now we stay minimal and avoid dependencies.

class bstr_view {
public:
    using value_type = char;
    using pointer = const value_type*;
    using size_type = size_t;

private:
    // Pointer to first character (never null; points to static empty string if empty)
    pointer _data;
    // Stored length in characters (not including null terminator)
    size_type _size;

    // Helper to compute length of a C string (no std::strlen)
    static constexpr size_type cstr_len(const char* s) noexcept {
        if (!s) return 0; // treat null as empty
        const char* p = s;
        while (*p) { ++p; }
        return static_cast<size_type>(p - s);
    }

    // Internal empty string storage
    static constexpr char empty_char = '\0';

public:
    // ------------------------------------------------- ctors

    // Default: empty view
    constexpr bstr_view() noexcept
        : _data(&empty_char), _size(0) {}

    // Construct directly from a string literal / char array.
    // N includes the terminating null for standard string literals.
    template <size_type N>
    constexpr bstr_view(const char (&lit)[N]) noexcept
        : _data(lit), _size(N ? (N - 1) : 0) {}

    // Construct from C string (const char*) – computes length at runtime/constexpr
    constexpr bstr_view(const char* s) noexcept
        : _data(s ? s : &empty_char), _size(cstr_len(s)) {}

    // Construct from mutable C string (char*)
    constexpr bstr_view(char* s) noexcept
        : _data(s ? s : &empty_char), _size(cstr_len(s)) {}

    // Construct directly from pointer + explicit size (does not check null terminator)
    constexpr bstr_view(const char* s, size_type len) noexcept
        : _data((s && len) ? s : (s ? s : &empty_char)), _size(len) {}

    // Factory: create view from beginning of a bstr block (points at the 4-byte length header)
    // NOTE: 'header' must point to at least 5 bytes (4 length + 1 null). No validation done.
    static constexpr bstr_view from_bstr(const void* header) noexcept {
        if (!header) {
            return bstr_view();
        }
        const unsigned char* raw = static_cast<const unsigned char*>(header);
        // Read 32-bit length (assuming native endianness matches stored format)
        // Layout: [0..3] length, [4..4+len-1] chars, [4+len] '\0'
        unsigned long len =
            (static_cast<unsigned long>(raw[0])      ) |
            (static_cast<unsigned long>(raw[1]) << 8 ) |
            (static_cast<unsigned long>(raw[2]) << 16) |
            (static_cast<unsigned long>(raw[3]) << 24);
        const char* strData = reinterpret_cast<const char*>(raw + 4);
        return bstr_view(strData, static_cast<size_type>(len));
    }

    // ------------------------------------------------- observers

    constexpr size_type size() const noexcept { return _size; }
    constexpr size_type length() const noexcept { return _size; }
    constexpr bool empty() const noexcept { return _size == 0; }

    // Pointer to characters (never null). Guaranteed null-terminated if source was a valid bstr or C string.
    constexpr const char* data() const noexcept { return _data; }
    constexpr const char* c_str() const noexcept { return _data; }

    // Provide mutable pointer only if the source was non-const (cannot reliably track that without extra state).
    // For zero-overhead we always expose a non-const pointer via cast; user must ensure they are allowed to modify.
    constexpr char* data() noexcept { return const_cast<char*>(_data); }
    constexpr char* c_str() noexcept { return const_cast<char*>(_data); }

    // Indexing (no bounds check)
    constexpr const char& operator[](size_type i) const noexcept { return _data[i]; }
    constexpr char& operator[](size_type i) noexcept { return const_cast<char*>(_data)[i]; }

    // Iterators (raw pointers)
    constexpr const char* begin() const noexcept { return _data; }
    constexpr const char* end() const noexcept { return _data + _size; }
    constexpr const char* cbegin() const noexcept { return _data; }
    constexpr const char* cend() const noexcept { return _data + _size; }

    // Conversions
    constexpr operator const char*() const noexcept { return _data; }
    constexpr operator char*() noexcept { return const_cast<char*>(_data); }

    // Comparison (lexicographical, minimal implementation)
    constexpr int compare(bstr_view other) const noexcept {
        const size_type n = (_size < other._size) ? _size : other._size;
        for (size_type i = 0; i < n; ++i) {
            unsigned char a = static_cast<unsigned char>(_data[i]);
            unsigned char b = static_cast<unsigned char>(other._data[i]);
            if (a < b) return -1;
            if (a > b) return 1;
        }
        if (_size < other._size) return -1;
        if (_size > other._size) return 1;
        return 0;
    }

    constexpr bool operator==(bstr_view other) const noexcept { return compare(other) == 0; }
    constexpr bool operator!=(bstr_view other) const noexcept { return compare(other) != 0; }
    constexpr bool operator<(bstr_view other)  const noexcept { return compare(other) < 0; }
};

#endif // __BSTR_VIEW__INCLUDED__

