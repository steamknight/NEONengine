/**
 * @file types.h
 * @brief Pulls mtl into the engine namespaces.
 *
 * Engine code writes u16, not uint16_t, and to<>, not mtl::to<>. mtl defines
 * the aliases; this makes them reachable without qualification. mtl itself
 * still spells the underlying types out, since it is what defines them.
 */

#ifndef __TYPES_H__INCLUDED__
#define __TYPES_H__INCLUDED__

#include <mtl/cstdint.h>
#include <mtl/utility.h>

namespace NEONengine
{
    using namespace mtl;
}

namespace ace
{
    using namespace mtl;
}

#endif  // __TYPES_H__INCLUDED__
