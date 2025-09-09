#ifndef __MTL__UTILITY__INCLUDED__
#define __MTL__UTILITY__INCLUDED__

#include <ace/managers/log.h>
#include <ace/managers/system.h>

namespace mtl
{
    template<typename T>
    struct remove_reference
    {
        using type = T;
    };

    template<typename T>
    struct remove_reference<T&>
    {
        using type = T;
    };

    template<typename T>
    struct remove_reference<T&&>
    {
        using type = T;
    };

    template<typename T>
    using remove_reference_t = typename remove_reference<T>::type;

    template<typename T>
    constexpr T&& forward(remove_reference_t<T>& t) noexcept
    {
        return static_cast<T&&>(t);
    }

    template<typename T>
    constexpr T&& forward(remove_reference_t<T>&& t) noexcept
    {
        return static_cast<T&&>(t);
    }

    template<typename T>
    constexpr remove_reference_t<T>&& move(T&& t) noexcept
    {
        return static_cast<remove_reference_t<T>&&>(t);
    }

#define LOG_CRASH(error)                  \
    logWrite("FATAL ERROR: %s", (error)); \
    *(volatile int*)0 = 0;

#define TRAP(error)                                                         \
    if (__builtin_is_constant_evaluated()) { static_assert(false, error); } \
    else { LOG_CRASH(error) }

    // Convenience functions to make casting easier to type and read
    template<typename T, typename U>
    constexpr auto to(U value) -> T
    {
        return static_cast<T>(value);
    }

    template<typename T, typename U>
    auto force_to(U value) -> T
    {
        return reinterpret_cast<T>(value);
    }

    // Convenience macros for class definitions

#define USE_DEFAULT_COPY(class_name)                    \
    class_name(const class_name&)            = default; \
    class_name& operator=(const class_name&) = default;

#define USE_DEFAULT_MOVE(class_name)                        \
    class_name(class_name&&) noexcept            = default; \
    class_name& operator=(class_name&&) noexcept = default;

#define USE_DEFAULT_COPY_V(class_name)                          \
    class_name(const class_name&)                    = default; \
    virtual class_name& operator=(const class_name&) = default;

#define USE_DEFAULT_MOVE_V(class_name)                              \
    class_name(class_name&&) noexcept                    = default; \
    virtual class_name& operator=(class_name&&) noexcept = default;

#define NO_COPY(class_name)                            \
    class_name(const class_name&)            = delete; \
    class_name& operator=(const class_name&) = delete;

#define NO_MOVE(class_name)                                \
    class_name(class_name&&) noexcept            = delete; \
    class_name& operator=(class_name&&) noexcept = delete;

    /**
     * @brief RAII guard for system usage
     *
     * @example
     * @code
     * // Turn on system for only a portion of a function
     * ...
     * {
     *     mtl::system_use_guard guard;
     *     someFunctionThatRequiresTheOS();
     * }
     * ...
     * @endcode
     */
    class system_use_guard
    {
        public:
        system_use_guard() noexcept { systemUse(); }

        ~system_use_guard() noexcept { systemUnuse(); }

        NO_COPY(system_use_guard)
        NO_MOVE(system_use_guard)
    };

    /**
     * @brief Rounds a value up to the next multiple of Size.
     *
     * @param value
     * @return constexpr size_t
     */
    template<size_t Size>
    constexpr size_t round_up(size_t value) noexcept
    {
        return (value + (Size - 1)) & ~(Size - 1);
    }

    template<typename T>
    constexpr void swap(T& a, T& b)
    {
        T tmp = to<T&&>(a);
        a     = to<T&&>(b);
        b     = to<T&&>(tmp);
    }
}  // namespace mtl

#endif  //__MTL__UTILITY__INCLUDED__