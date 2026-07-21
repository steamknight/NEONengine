/**
 * @file spatial.h
 * @brief Shared coordinate and rectangle value types for the .neon format.
 *
 * These mirror ACE's Y,X coordinate convention so a loaded value converts to its
 * ACE counterpart with no reordering, and so a run of points sorts by row first.
 * They carry the exact big-endian, naturally-aligned layout the loader overlays
 * onto the file buffer: pure data with value semantics and no padding.
 */

#ifndef __SPATIAL_H__INCLUDED__
#define __SPATIAL_H__INCLUDED__

#include <stdint.h>

#include <ace/types.h>

namespace NEONengine::data
{
    /**
     * @brief Maps a coordinate scalar type to its ACE Y,X coordinate type.
     *
     * Only the scalars the engine actually converts are specialized. Calling
     * point<T>::to_ace() for an unmapped T is a compile error until its mapping
     * is added here, which keeps the ACE coupling explicit and intentional.
     */
    template<typename T>
    struct ace_coord;

    template<>
    struct ace_coord<uint16_t>
    {
        using type = tUwCoordYX;
    };

    /**
     * @brief A Y,X coordinate. Y leads to match ACE and to sort by row first.
     *
     * @tparam T Coordinate scalar; uint16_t for every position in the format.
     */
    template<typename T>
    struct point
    {
        T y;
        T x;

        /**
         * @brief The equivalent ACE coordinate.
         *
         * A point and its ACE coord share one layout, so this reinterprets the
         * same bytes. It compiles to a single register load without the
         * aliasing hazard of a reinterpret_cast.
         */
        [[nodiscard]] auto to_ace() const noexcept
        {
            return __builtin_bit_cast(typename ace_coord<T>::type, *this);
        }

        friend constexpr point operator+(point const& lhs, point const& rhs) noexcept
        {
            return point{ static_cast<T>(lhs.y + rhs.y), static_cast<T>(lhs.x + rhs.x) };
        }

        friend constexpr point operator-(point const& lhs, point const& rhs) noexcept
        {
            return point{ static_cast<T>(lhs.y - rhs.y), static_cast<T>(lhs.x - rhs.x) };
        }

        friend constexpr bool operator==(point const& lhs, point const& rhs) noexcept
        {
            return lhs.y == rhs.y && lhs.x == rhs.x;
        }

        friend constexpr bool operator!=(point const& lhs, point const& rhs) noexcept
        {
            return !(lhs == rhs);
        }
    };

    static_assert(sizeof(point<uint16_t>) == 4, "point<uint16_t> must be exactly 4 bytes");
    static_assert(__builtin_offsetof(point<uint16_t>, x) == 2, "point x must follow y at offset 2");
    static_assert(__is_standard_layout(point<uint16_t>),
        "point must be standard-layout to overlay the file buffer");
    static_assert(__is_trivially_copyable(point<uint16_t>),
        "point must be trivially copyable to overlay the file buffer");
    static_assert(sizeof(point<uint16_t>{}.to_ace()) == sizeof(tUwCoordYX),
        "point<uint16_t>::to_ace must yield its ACE coord type");

    /**
     * @brief An axis-aligned rectangle: a Y,X origin plus its size.
     */
    struct rect
    {
        point<uint16_t> pos;
        uint16_t width;
        uint16_t height;

        /**
         * @brief The equivalent ACE rectangle.
         */
        [[nodiscard]] tUwRect to_ace() const noexcept
        {
            return tUwRect{ pos.y, pos.x, width, height };
        }

        friend constexpr bool operator==(rect const& lhs, rect const& rhs) noexcept
        {
            return lhs.pos == rhs.pos && lhs.width == rhs.width && lhs.height == rhs.height;
        }

        friend constexpr bool operator!=(rect const& lhs, rect const& rhs) noexcept
        {
            return !(lhs == rhs);
        }
    };

    static_assert(sizeof(rect) == 8, "rect must be exactly 8 bytes");
    static_assert(__builtin_offsetof(rect, width) == 4, "rect width must be at offset 4");
    static_assert(__builtin_offsetof(rect, height) == 6, "rect height must be at offset 6");
    static_assert(__is_standard_layout(rect),
        "rect must be standard-layout to overlay the file buffer");
    static_assert(__is_trivially_copyable(rect),
        "rect must be trivially copyable to overlay the file buffer");
}  // namespace NEONengine::data

#endif  // __SPATIAL_H__INCLUDED__
