/**
 * @file span.h
 * @brief Lightweight non-owning view over a contiguous sequence of elements.
 *
 * A span stores only a pointer and a count. It never allocates, frees or
 * throws. The view is read-only.
 */

#ifndef __MTL__SPAN__INCLUDED__
#define __MTL__SPAN__INCLUDED__

#include <stdint.h>

namespace mtl
{
    /**
     * @brief Non-owning, read-only view over a contiguous run of @c T.
     *
     * Holds a pointer into memory owned elsewhere plus an element count. All
     * accessors are @c constexpr and @c noexcept. Element access is unchecked:
     * the caller is expected to have validated the bounds once when the view
     * was created.
     *
     * @tparam T Element type.
     *
     * @example
     * @code
     * for (int const& value : numbers) { ... }
     * int const& first = numbers[0];
     * @endcode
     */
    template<class T>
    class span
    {
        public:  ///////////////////////////////////////////////////////////////////////////////////
        using value_type = T;
        using size_type  = uint32_t;  // Matches the chunk header count; native word on the 68020.
        using pointer    = T const*;
        using reference  = T const&;
        using iterator   = T const*;

        /**
         * @brief Construct an empty view.
         */
        constexpr span() noexcept = default;

        /**
         * @brief Construct from a pointer and an element count.
         *
         * @param data  Pointer to the first element (owned elsewhere).
         * @param count Number of elements the view exposes.
         */
        constexpr span(pointer data, size_type count) noexcept : _data(data), _count(count) {}

        /**
         * @brief Number of elements in the view.
         */
        [[nodiscard]] constexpr size_type size() const noexcept { return _count; }

        /**
         * @brief True when the view exposes no elements.
         */
        [[nodiscard]] constexpr bool is_empty() const noexcept { return _count == 0; }

        /**
         * @brief Pointer to the first element (null for a default-constructed view).
         */
        [[nodiscard]] constexpr pointer data() const noexcept { return _data; }

        /**
         * @brief Unchecked element access.
         *
         * @param index Zero-based index; must be < size().
         *
         * @warning No bounds checking is performed.
         */
        constexpr reference operator[](size_type index) const noexcept { return _data[index]; }

        /**
         * @brief First element. Undefined when the view is empty.
         */
        constexpr reference front() const noexcept { return _data[0]; }

        /**
         * @brief Last element. Undefined when the view is empty.
         */
        constexpr reference back() const noexcept { return _data[_count - 1]; }

        /**
         * @brief Iterator to the first element.
         */
        constexpr iterator begin() const noexcept { return _data; }

        /**
         * @brief Iterator one past the last element.
         */
        constexpr iterator end() const noexcept { return _data + _count; }

        private:  //////////////////////////////////////////////////////////////////////////////////
        pointer _data{ nullptr };  ///< Pointer to the first element, owned elsewhere.
        size_type _count{ 0 };     ///< Number of elements exposed by the view.
    };
}  // namespace mtl

#endif  //__MTL__SPAN__INCLUDED__
