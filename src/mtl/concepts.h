#ifndef __CONCEPTS_H__INCLUDED__
#define __CONCEPTS_H__INCLUDED__

namespace mtl
{
    template<typename A, typename B> inline constexpr bool is_same_v = false;
    template<typename A> inline constexpr bool is_same_v<A, A> = true;
    template<typename A, typename B> concept same_as = is_same_v<A, B>;
}

#endif // __CONCEPTS_H__INCLUDED__