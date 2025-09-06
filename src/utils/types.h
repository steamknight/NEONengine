#ifndef __TYPES_H__INCLUDED__
#define __TYPES_H__INCLUDED__

namespace NEONengine
{
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
}

#endif