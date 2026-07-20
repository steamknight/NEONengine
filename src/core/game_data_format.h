/**
 * @file game_data_format.h
 * @brief On-disk byte layout of the version 2 .neon container.
 *
 * These structs mirror the .neon binary contract exactly so the loader can
 * overlay them directly onto the loaded buffer: the file and the 68020 are both
 * big-endian, and every field sits on its natural alignment, so fields are read
 * in place with no byte swapping and no unaligned access.
 *
 * They are pure data - no behaviour, no padding beyond what the contract already
 * specifies. The authoritative contract lives in the editor repo, in
 * docs/game_data_format.md and docs/game_data_record_layouts.md.
 */

#ifndef __GAME_DATA_FORMAT_H__INCLUDED__
#define __GAME_DATA_FORMAT_H__INCLUDED__

#include <stdint.h>

#include <mtl/utility.h>

namespace NEONengine
{
    /**
     * @brief Builds a big-endian FourCC from a four-character code.
     *
     * @param code A four-character string literal, e.g. "NEON".
     */
    constexpr uint32_t make_fourcc(char const (&code)[5])
    {
        return (mtl::to<uint32_t>(mtl::to<uint8_t>(code[0])) << 24)
             | (mtl::to<uint32_t>(mtl::to<uint8_t>(code[1])) << 16)
             | (mtl::to<uint32_t>(mtl::to<uint8_t>(code[2])) << 8)
             | mtl::to<uint32_t>(mtl::to<uint8_t>(code[3]));
    }

    /**
     * @brief The .neon file magic, 'NEON'
     */
    constexpr uint32_t NEON_MAGIC = make_fourcc("NEON");
    static_assert(NEON_MAGIC == 0x4E454F4Eu, "NEON_MAGIC must be the big-endian 'NEON' FourCC");

    /**
     * @brief The fixed 64-byte header at the start of every .neon file.
     */
    struct neon_header
    {
        uint32_t magic;         ///< 0:  'NEON' as a big-endian FourCC (== NEON_MAGIC).
        uint16_t version;       ///< 4:  Container version; must equal NEON_SUPPORTED_VERSION.
        uint16_t build_period;  ///< 6:  UTC build period, decimal YYMM.
        uint16_t header_size;   ///< 8:  Size of this header; must equal sizeof(neon_header).
        uint16_t reserved0;     ///< 10: Reserved, zero.
        uint32_t format_flags;  ///< 12: Reserved, zero.
        uint32_t total_size;    ///< 16: Total .neon size in bytes.
        uint32_t checksum;      ///< 20: Adler32 of bytes 64..EOF.
        uint32_t chunk_count;   ///< 24: Number of chunks in the file.
        uint32_t build_number;  ///< 28: Monotonic project build number.
        uint8_t reserved1[32];  ///< 32: Reserved, zero.
    };

    static_assert(sizeof(neon_header) == 64, "neon_header must be exactly 64 bytes");
    static_assert(__builtin_offsetof(neon_header, total_size) == 16, "total_size must be at offset 16");
    static_assert(__builtin_offsetof(neon_header, checksum) == 20, "checksum must be at offset 20");
    static_assert(__builtin_offsetof(neon_header, chunk_count) == 24, "chunk_count must be at offset 24");
}  // namespace NEONengine

#endif  // __GAME_DATA_FORMAT_H__INCLUDED__
