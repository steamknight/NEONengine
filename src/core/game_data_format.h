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

    /**
     * @brief The 16-byte header that precedes every chunk's payload.
     *
     * Chunks follow the file header sequentially, each as this header plus
     * body_size payload bytes (including 4-byte alignment padding); the next
     * chunk header begins at payload + body_size.
     */
    struct chunk_header
    {
        uint32_t fourcc;         ///< 0:  Chunk id as a big-endian FourCC.
        uint32_t body_size;      ///< 4:  Payload bytes after this header, incl. align-4 padding.
        uint32_t element_count;  ///< 8:  Logical element count; 0 for Data chunks.
        uint32_t element_size;   ///< 12: Element size, or 0 for Data chunks.
    };

    static_assert(sizeof(chunk_header) == 16, "chunk_header must be exactly 16 bytes");
    static_assert(__builtin_offsetof(chunk_header, body_size) == 4, "body_size must be at offset 4");
    static_assert(__builtin_offsetof(chunk_header, element_count) == 8, "element_count must be at offset 8");
    static_assert(__builtin_offsetof(chunk_header, element_size) == 12, "element_size must be at offset 12");

    /**
     * @brief 'CONF' chunk id.
     */
    constexpr uint32_t CONF_ID = make_fourcc("CONF");

    /**
     * @brief 'CONF' payload: project display geometry and the default fade.
     *
     * A Data chunk, 8 bytes.
     */
    struct conf_chunk
    {
        uint16_t width;                ///< 0: Scene/display width.
        uint16_t height;               ///< 2: Scene/display height.
        uint16_t default_fade_frames;  ///< 4: Default fade duration in PAL frames.
        uint16_t reserved0;            ///< 6: Reserved, zero.
    };

    static_assert(sizeof(conf_chunk) == 8, "conf_chunk must be exactly 8 bytes");

    /**
     * @brief 'UIST' chunk id.
     */
    constexpr uint32_t UIST_ID = make_fourcc("UIST");

    /**
     * @brief 'UIST' payload: shared UI palette/font and nine-patch/cursor config.
     *
     * A Data chunk, 32 bytes. The palette and font are pak-path hashes; the
     * nine-patch and cursor-sheet bitmap ids are 0xFFFF when absent.
     */
    struct uist_chunk
    {
        uint32_t ui_palette_hash;      ///< 0:  UI palette pak-path hash (0xFFFFFFFF invalid).
        uint32_t font_hash;            ///< 4:  Font pak-path hash (0xFFFFFFFF invalid).
        uint16_t nine_patch_bmps_id;   ///< 8:  Nine-patch BMPS id, or 0xFFFF.
        uint16_t cursor_bmps_id;       ///< 10: Cursor-sheet BMPS id, or 0xFFFF.
        uint16_t nine_patch_left;      ///< 12: Nine-patch left border.
        uint16_t nine_patch_top;       ///< 14: Nine-patch top border.
        uint16_t nine_patch_right;     ///< 16: Nine-patch right border.
        uint16_t nine_patch_bottom;    ///< 18: Nine-patch bottom border.
        uint16_t cursor_frame_width;   ///< 20: Cursor frame width.
        uint16_t cursor_frame_height;  ///< 22: Cursor frame height.
        uint16_t curs_range_start;     ///< 24: CURS range start.
        uint16_t curs_range_count;     ///< 26: CURS range count.
        uint32_t reserved0;            ///< 28: Reserved, zero.
    };

    static_assert(sizeof(uist_chunk) == 32, "uist_chunk must be exactly 32 bytes");
    static_assert(__builtin_offsetof(uist_chunk, font_hash) == 4, "font_hash must be at offset 4");
    static_assert(__builtin_offsetof(uist_chunk, cursor_frame_width) == 20, "cursor_frame_width must be at offset 20");
    static_assert(__builtin_offsetof(uist_chunk, reserved0) == 28, "reserved0 must be at offset 28");
}  // namespace NEONengine

#endif  // __GAME_DATA_FORMAT_H__INCLUDED__
