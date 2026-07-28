/**
 * @file adler32.h
 * @brief Adler-32 checksum over a byte buffer.
 *
 * The build tool stamps this checksum into the `.noir` and `.neon` headers, so
 * the runtime uses it to reject a truncated or corrupted data file before
 * interpreting its contents.
 */
#ifndef __ADLER32__INCLUDED__
#define __ADLER32__INCLUDED__

#include "types.h"

#include <stddef.h>
#include <stdint.h>

namespace NEONengine
{
    /**
     * @brief Compute the Adler-32 checksum of @p size bytes at @p pData.
     *
     * Matches the canonical definition, so an empty buffer checksums to 1.
     *
     * @param pData Buffer to checksum; may be null only when @p size is zero.
     * @param size Number of bytes to read.
     * @return The checksum, high half `b` and low half `a`.
     */
    constexpr u32 adler32(u8 const* pData, size_t size) noexcept
    {
        constexpr u32 MODULO = 65521;

        u32 a = 1;
        u32 b = 0;
        for (size_t i = 0; i < size; ++i)
        {
            a = (a + pData[i]) % MODULO;
            b = (b + a) % MODULO;
        }

        return (b << 16) | a;
    }
}  // namespace NEONengine

#endif  // __ADLER32__INCLUDED__
