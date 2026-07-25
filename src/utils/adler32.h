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
    constexpr uint32_t adler32(uint8_t const* pData, size_t size) noexcept
    {
        constexpr uint32_t MODULO = 65521;

        uint32_t a = 1;
        uint32_t b = 0;
        for (size_t i = 0; i < size; ++i)
        {
            a = (a + pData[i]) % MODULO;
            b = (b + a) % MODULO;
        }

        return (b << 16) | a;
    }
}  // namespace NEONengine

#endif  // __ADLER32__INCLUDED__
