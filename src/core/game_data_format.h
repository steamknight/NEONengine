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

#include "spatial.h"

namespace NEONengine::data
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
     * @brief The id an optional record reference carries when it names nothing.
     */
    constexpr uint16_t NO_ID = 0xFFFF;

    /**
     * @brief The pak-path hash an optional asset reference carries when absent.
     */
    constexpr uint32_t NO_ASSET = 0xFFFFFFFFu;

    /**
     * @brief The fixed 64-byte header at the start of every .neon file.
     */
    struct header
    {
        uint32_t magic;         ///< 0:  'NEON' as a big-endian FourCC (== NEON_MAGIC).
        uint16_t version;       ///< 4:  Container version; must equal NEON_SUPPORTED_VERSION.
        uint16_t build_period;  ///< 6:  UTC build period, decimal YYMM.
        uint16_t header_size;   ///< 8:  Size of this header; must equal sizeof(header).
        uint16_t reserved0;     ///< 10: Reserved, zero.
        uint32_t format_flags;  ///< 12: Reserved, zero.
        uint32_t total_size;    ///< 16: Total .neon size in bytes.
        uint32_t checksum;      ///< 20: Adler32 of bytes 64..EOF.
        uint32_t chunk_count;   ///< 24: Number of chunks in the file.
        uint32_t build_number;  ///< 28: Monotonic project build number.
        uint8_t reserved1[32];  ///< 32: Reserved, zero.
    };

    static_assert(sizeof(header) == 64, "header must be exactly 64 bytes");
    static_assert(__builtin_offsetof(header, total_size) == 16, "total_size must be at offset 16");
    static_assert(__builtin_offsetof(header, checksum) == 20, "checksum must be at offset 20");
    static_assert(__builtin_offsetof(header, chunk_count) == 24, "chunk_count must be at offset 24");

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
     * @brief A contiguous window into a collection, as an element start and count.
     *
     * Records use this to address the child elements they own in another chunk.
     */
    struct range
    {
        uint16_t start;  ///< 0: Index of the first element.
        uint16_t count;  ///< 2: Number of elements.

        friend constexpr bool operator==(range const& lhs, range const& rhs) noexcept
        {
            return lhs.start == rhs.start && lhs.count == rhs.count;
        }

        friend constexpr bool operator!=(range const& lhs, range const& rhs) noexcept { return !(lhs == rhs); }
    };

    static_assert(sizeof(range) == 4, "range must be exactly 4 bytes");
    static_assert(__builtin_offsetof(range, count) == 2, "range count must be at offset 2");
    static_assert(__is_standard_layout(range), "range must be standard-layout to overlay the file buffer");
    static_assert(__is_trivially_copyable(range), "range must be trivially copyable to overlay the file buffer");

    /**
     * @brief 'CONF' payload: project display geometry and the default fade.
     *
     * A Data chunk, 8 bytes.
     */
    struct config
    {
        uint16_t width;                ///< 0: Scene/display width.
        uint16_t height;               ///< 2: Scene/display height.
        uint16_t default_fade_frames;  ///< 4: Default fade duration in PAL frames.
        uint16_t boot_scene_id;        ///< 6: SCEN id the engine boots into (0 by default).
    };

    static_assert(sizeof(config) == 8, "config must be exactly 8 bytes");
    static_assert(__builtin_offsetof(config, boot_scene_id) == 6, "boot_scene_id must be at offset 6");

    /**
     * @brief 'UIST' payload: shared UI palette/font and nine-patch/cursor config.
     *
     * A Data chunk, 32 bytes. The palette and font are pak-path hashes; the
     * nine-patch and cursor-sheet bitmap ids are 0xFFFF when absent.
     */
    struct ui_state
    {
        uint32_t ui_palette_hash;       ///< 0:  UI palette pak-path hash (0xFFFFFFFF invalid).
        uint32_t font_hash;             ///< 4:  Font pak-path hash (0xFFFFFFFF invalid).
        uint16_t nine_patch_bitmap_id;  ///< 8:  Nine-patch BMPS id, or 0xFFFF.
        uint16_t cursor_bitmap_id;      ///< 10: Cursor-sheet BMPS id, or 0xFFFF.
        uint16_t nine_patch_left;       ///< 12: Nine-patch left border.
        uint16_t nine_patch_top;        ///< 14: Nine-patch top border.
        uint16_t nine_patch_right;      ///< 16: Nine-patch right border.
        uint16_t nine_patch_bottom;     ///< 18: Nine-patch bottom border.
        uint16_t cursor_frame_width;    ///< 20: Cursor frame width.
        uint16_t cursor_frame_height;   ///< 22: Cursor frame height.
        range cursor_range;             ///< 24: CURS range.
        uint32_t reserved0;             ///< 28: Reserved, zero.
    };

    static_assert(sizeof(ui_state) == 32, "ui_state must be exactly 32 bytes");
    static_assert(__builtin_offsetof(ui_state, font_hash) == 4, "font_hash must be at offset 4");
    static_assert(__builtin_offsetof(ui_state, cursor_frame_width) == 20, "cursor_frame_width must be at offset 20");
    static_assert(__builtin_offsetof(ui_state, cursor_range) == 24, "cursor_range must be at offset 24");
    static_assert(__builtin_offsetof(ui_state, reserved0) == 28, "reserved0 must be at offset 28");

    /**
     * @brief 'INVC' payload: the shared inventory sheet and its item range.
     *
     * A Data chunk, 12 bytes. The disabled form uses a sheet id of 0xFFFF, zero
     * frame dimensions, and an empty item range.
     */
    struct inventory_config
    {
        uint16_t sheet_bitmap_id;   ///< 0:  Inventory-sheet BMPS id, or 0xFFFF when disabled.
        uint16_t frame_width;       ///< 2:  Item frame width.
        uint16_t frame_height;      ///< 4:  Item frame height.
        range item_range;           ///< 6:  INVT range.
        uint16_t reserved0;         ///< 10: Reserved, zero.
    };

    static_assert(sizeof(inventory_config) == 12, "inventory_config must be exactly 12 bytes");
    static_assert(__builtin_offsetof(inventory_config, item_range) == 6, "item_range must be at offset 6");

    /**
     * @brief 'CURS' element: one fixed cursor role's frame and click hotspot.
     *
     * A Collection element, 8 bytes.
     */
    struct cursor
    {
        uint16_t frame;           ///< 0: Frame index in the cursor sheet.
        point<uint16_t> hotspot;  ///< 2: Click hotspot within the frame.
        uint16_t reserved0;       ///< 6: Reserved, zero.
    };

    static_assert(sizeof(cursor) == 8, "cursor must be exactly 8 bytes");
    static_assert(__builtin_offsetof(cursor, hotspot) == 2, "hotspot must be at offset 2");
    static_assert(__builtin_offsetof(cursor, reserved0) == 6, "reserved0 must be at offset 6");

    /**
     * @brief 'BMPS' element: a bitmap id resolved to its pak-path hashes.
     *
     * A Collection element, 16 bytes.
     */
    struct bitmap_ref
    {
        uint32_t bitmap_hash;   ///< 0:  Bitmap pak-path hash.
        uint32_t mask_hash;     ///< 4:  Mask bitmap pak-path hash, or 0xFFFFFFFF.
        uint32_t palette_hash;  ///< 8:  Palette pak-path hash.
        uint32_t reserved0;     ///< 12: Reserved, zero.
    };

    static_assert(sizeof(bitmap_ref) == 16, "bitmap_ref must be exactly 16 bytes");
    static_assert(__builtin_offsetof(bitmap_ref, mask_hash) == 4, "mask_hash must be at offset 4");
    static_assert(__builtin_offsetof(bitmap_ref, palette_hash) == 8, "palette_hash must be at offset 8");

    /**
     * @brief 'INVT' element: one inventory item's frame and localized strings.
     *
     * A Collection element, 8 bytes.
     */
    struct inventory_item
    {
        uint16_t frame;                  ///< 0: Inventory-sheet frame index.
        uint16_t name_string_id;         ///< 2: Name string id.
        uint16_t description_string_id;  ///< 4: Description string id.
        uint16_t reserved0;              ///< 6: Reserved, zero.
    };

    static_assert(sizeof(inventory_item) == 8, "inventory_item must be exactly 8 bytes");

    /**
     * @brief 'SPKR' element: a dialogue speaker's name and optional portrait.
     *
     * A Collection element, 4 bytes.
     */
    struct speaker
    {
        uint16_t name_string_id;     ///< 0: Display-name string id.
        uint16_t portrait_bitmap_id; ///< 2: Portrait BMPS id, or 0xFFFF.
    };

    static_assert(sizeof(speaker) == 4, "speaker must be exactly 4 bytes");

    /**
     * @brief 'HOTS' element: a scene interaction region and its behavior.
     *
     * A Collection element, 24 bytes.
     *
     * @p graphic_id is optional. When present it names a 'GFXS' graphic whose
     * frames are drawn to show the hotspot's current state, by convention frame
     * 0 normal, 1 hovered, 2 pressed, and 3 disabled.
     *
     * @p click_sfx_hash is optional, and carries NO_ASSET when the hotspot
     * makes no sound, the same way BMPS.mask_hash does.
     */
    struct hotspot
    {
        rect bounds;               ///< 0:  Interaction region.
        uint16_t z_order;          ///< 8:  Hit-test priority.
        uint8_t cursor;            ///< 10: Cursor role 0-4.
        uint8_t flags;             ///< 11: bit0 = initially enabled.
        uint16_t hover_string_id;  ///< 12: Hover string id, or 0xFFFF.
        uint16_t click_function_id;///< 14: Click FUNC id, or 0xFFFF.
        uint16_t goto_scene_id;    ///< 16: Goto SCEN id, or 0xFFFF.
        uint16_t graphic_id;       ///< 18: State-graphic GFXS id, or 0xFFFF.
        uint32_t click_sfx_hash;   ///< 20: Click SFX pak-path hash, or 0xFFFFFFFF.
    };

    static_assert(sizeof(hotspot) == 24, "hotspot must be exactly 24 bytes");
    static_assert(__builtin_offsetof(hotspot, z_order) == 8, "z_order must be at offset 8");
    static_assert(__builtin_offsetof(hotspot, cursor) == 10, "cursor must be at offset 10");
    static_assert(__builtin_offsetof(hotspot, flags) == 11, "flags must be at offset 11");
    static_assert(__builtin_offsetof(hotspot, hover_string_id) == 12, "hover_string_id must be at offset 12");
    static_assert(__builtin_offsetof(hotspot, graphic_id) == 18, "graphic_id must be at offset 18");
    static_assert(__builtin_offsetof(hotspot, click_sfx_hash) == 20, "click_sfx_hash must be at offset 20");

    /**
     * @brief 'TXTR' element: localized bitmap-font text placed in a scene.
     *
     * A Collection element, 16 bytes.
     */
    struct text_region
    {
        rect bounds;           ///< 0:  Layout rectangle.
        uint16_t z_order;      ///< 8:  Draw order.
        uint8_t color_index;   ///< 10: UI palette color index, 0-31.
        uint8_t control_bits;  ///< 11: Alignment and full-scene layout bits.
        uint16_t string_id;    ///< 12: Text string id.
        uint16_t reserved0;    ///< 14: Reserved, zero.
    };

    static_assert(sizeof(text_region) == 16, "text_region must be exactly 16 bytes");
    static_assert(__builtin_offsetof(text_region, z_order) == 8, "z_order must be at offset 8");
    static_assert(__builtin_offsetof(text_region, color_index) == 10, "color_index must be at offset 10");
    static_assert(__builtin_offsetof(text_region, control_bits) == 11, "control_bits must be at offset 11");
    static_assert(__builtin_offsetof(text_region, string_id) == 12, "string_id must be at offset 12");

    /**
     * @brief 'GFXS' element: one graphic's vertical animation strip.
     *
     * Each graphic owns a dedicated single-column bitmap whose frames are
     * stacked vertically. Frame width equals the bitmap width and frame count
     * equals bitmapHeight / frameHeight. The build exports the strip as an
     * 8-bitplane interleaved bitmap and bakes its pixel indices into the
     * graphic's BOB palette-slot band.
     *
     * A Collection element, 12 bytes.
     */
    struct graphic
    {
        uint16_t source_bitmap_id;  ///< 0:  Source bitmap BMPS id.
        uint16_t frame_width;       ///< 2:  Frame width (equals the bitmap width).
        uint16_t frame_height;      ///< 4:  Frame height.
        uint8_t frame_count;        ///< 6:  Frame count, 1-255.
        uint8_t frame_delay;        ///< 7:  Frame delay in PAL frames; 0 when static.
        uint8_t palette_slot;       ///< 8:  BOB palette slot, 0-5.
        uint8_t reserved0;          ///< 9:  Reserved, zero.
        uint16_t reserved1;         ///< 10: Reserved, zero.
    };

    static_assert(sizeof(graphic) == 12, "graphic must be exactly 12 bytes");
    static_assert(__builtin_offsetof(graphic, frame_width) == 2, "frame_width must be at offset 2");
    static_assert(__builtin_offsetof(graphic, frame_height) == 4, "frame_height must be at offset 4");
    static_assert(__builtin_offsetof(graphic, frame_count) == 6, "frame_count must be at offset 6");
    static_assert(__builtin_offsetof(graphic, frame_delay) == 7, "frame_delay must be at offset 7");
    static_assert(__builtin_offsetof(graphic, palette_slot) == 8, "palette_slot must be at offset 8");

    /**
     * @brief 'GFXI' element: one placed instance of a GFXS graphic.
     *
     * A Collection element, 12 bytes.
     */
    struct graphic_instance
    {
        uint16_t graphic_id;       ///< 0:  Global GFXS id.
        point<uint16_t> pos;       ///< 2:  Placement position (Y, X).
        uint16_t z_order;          ///< 6:  Draw order.
        uint8_t reserved0;         ///< 8:  Reserved, zero (palette slot lives on GFXS).
        uint8_t flags;             ///< 9:  bit0 = initially visible.
        uint16_t reserved1;        ///< 10: Reserved, zero.
    };

    static_assert(sizeof(graphic_instance) == 12, "graphic_instance must be exactly 12 bytes");
    static_assert(__builtin_offsetof(graphic_instance, pos) == 2, "pos must be at offset 2");
    static_assert(__builtin_offsetof(graphic_instance, z_order) == 6, "z_order must be at offset 6");
    static_assert(__builtin_offsetof(graphic_instance, flags) == 9, "flags must be at offset 9");

    /**
     * @brief 'KEYS' element: one typed timeline event at a PAL-frame time.
     *
     * A Collection element, 12 bytes. The 8-byte payload is interpreted by
     * type; its typed overlays are added when the timeline runtime consumes it.
     */
    struct keyframe
    {
        uint16_t time;        ///< 0: Time in PAL frames.
        uint8_t type;         ///< 2: Keyframe type.
        uint8_t flags;        ///< 3: bit0 fire_once; bit1 interpolate (GFX_MOVE only).
        uint8_t payload[8];   ///< 4: Type-specific payload.
    };

    static_assert(sizeof(keyframe) == 12, "keyframe must be exactly 12 bytes");
    static_assert(__builtin_offsetof(keyframe, type) == 2, "type must be at offset 2");
    static_assert(__builtin_offsetof(keyframe, payload) == 4, "payload must be at offset 4");

    /**
     * @brief What a track does when its playhead reaches the end of a cycle.
     *
     * The enumerators are the record's own ids, so track::loop_mode converts
     * straight to one of these.
     */
    enum class loop_mode : uint8_t
    {
        NONE      = 0,  ///< Play once and stop.
        RESTART   = 1,  ///< Jump back to the start and play forwards again.
        PING_PONG = 2,  ///< Play back to the start in reverse, then forwards again.
    };

    /**
     * @brief What a keyframe does when the playhead reaches it.
     *
     * The enumerators are the record's own ids, so keyframe::type converts
     * straight to one of these.
     */
    enum class keyframe_type : uint8_t
    {
        GFX_MOVE            = 0,
        GFX_VISIBILITY      = 1,
        PALETTE_CYCLE_START = 2,
        PALETTE_CYCLE_END   = 3,
        SFX_PLAY            = 4,
        // 5 was SFX_STOP, and is not reused. A sound effect is a one-shot that
        // is over long before anything could ask it to stop, so the type earned
        // nothing; the VM never had an opcode for it either.
        MUSIC_PLAY          = 6,
        MUSIC_STOP          = 7,
        EXECUTE_SCRIPT      = 8,
    };

    /// One past the highest keyframe_type this build knows about.
    constexpr uint8_t KEYFRAME_TYPE_COUNT = 9;

    /// keyframe::flags bit 0: the keyframe fires only on its first encounter.
    constexpr uint8_t KEYFRAME_FLAG_FIRE_ONCE = 1 << 0;

    /// keyframe::flags bit 1: GFX_MOVE interpolates towards its position.
    constexpr uint8_t KEYFRAME_FLAG_INTERPOLATE = 1 << 1;

    /**
     * @brief 'KEYS' GFX_MOVE payload: where a graphic instance is to be.
     *
     * @c pos is the position the instance occupies when the playhead reaches
     * this keyframe. KEYFRAME_FLAG_INTERPOLATE says how it gets there: set, the
     * span from the previous GFX_MOVE for the same instance is travelled a
     * frame at a time; clear, the instance simply appears here.
     */
    struct gfx_move_payload
    {
        uint16_t instance_id;  ///< 0: Global GFXI id.
        point<uint16_t> pos;   ///< 2: Position (Y, X).
        uint16_t reserved;     ///< 6: Reserved, zero.
    };

    static_assert(sizeof(gfx_move_payload) == 8, "a keyframe payload is exactly 8 bytes");
    static_assert(__builtin_offsetof(gfx_move_payload, pos) == 2, "pos must be at offset 2");

    /**
     * @brief 'KEYS' GFX_VISIBILITY payload: whether a graphic instance draws.
     */
    struct gfx_visibility_payload
    {
        uint16_t instance_id;  ///< 0: Global GFXI id.
        uint8_t visible;       ///< 2: Nonzero to draw the instance.
        uint8_t reserved[5];   ///< 3: Reserved, zero.
    };

    static_assert(sizeof(gfx_visibility_payload) == 8, "a keyframe payload is exactly 8 bytes");

    /**
     * @brief 'KEYS' PALETTE_CYCLE_START payload: a rotating band of colors.
     *
     * @c first and @c last are inclusive, so the band is at least two entries
     * wide, and the colors rotate towards the higher index with @c last wrapping
     * round to @c first. @c delay is how many frames each step is held for.
     *
     * The pair is a span, not two events: the cycle owns its band for as long as
     * the playhead stands within [start time, end time), and the END record only
     * marks where that span closes. @c cycle_id ties the two records together
     * and is dense across the whole timeline rather than per track, so that an
     * END in one track cannot close a span another track owns.
     */
    struct palette_cycle_start_payload
    {
        uint16_t cycle_id;  ///< 0: Identifies the pair within the timeline.
        uint8_t first;      ///< 2: First palette entry of the band.
        uint8_t last;       ///< 3: Last palette entry of the band, inclusive.
        uint16_t delay;     ///< 4: Frames each rotation step is held for.
        uint16_t reserved;  ///< 6: Reserved, zero.
    };

    static_assert(sizeof(palette_cycle_start_payload) == 8, "a keyframe payload is exactly 8 bytes");
    static_assert(
        __builtin_offsetof(palette_cycle_start_payload, delay) == 4, "delay must be at offset 4");

    /**
     * @brief 'KEYS' PALETTE_CYCLE_END payload: where a rotating band's span ends.
     *
     * Names only the cycle it closes. Everything about the band itself lives on
     * the START record.
     */
    struct palette_cycle_end_payload
    {
        uint16_t cycle_id;    ///< 0: Identifies the pair within the timeline.
        uint8_t reserved[6];  ///< 2: Reserved, zero.
    };

    static_assert(sizeof(palette_cycle_end_payload) == 8, "a keyframe payload is exactly 8 bytes");

    /**
     * @brief 'KEYS' SFX_PLAY payload: the sound to play.
     *
     * @c channel is authored but not read. Effects are one-shots, so the
     * replayer is left to pick the least busy channel and hand it back to the
     * music when the sound is done. The field stays in the format in case
     * naming a channel ever earns its keep.
     */
    struct sfx_play_payload
    {
        uint32_t sfx_hash;    ///< 0: SFX pak-path hash.
        int8_t channel;       ///< 4: Unused; -1 picks a channel, 0 and 1 name one.
        uint8_t reserved[3];  ///< 5: Reserved, zero.
    };

    static_assert(sizeof(sfx_play_payload) == 8, "a keyframe payload is exactly 8 bytes");
    static_assert(__builtin_offsetof(sfx_play_payload, channel) == 4, "channel must be at offset 4");

    /**
     * @brief 'KEYS' MUSIC_PLAY payload: the module to play.
     */
    struct music_play_payload
    {
        uint32_t music_hash;  ///< 0: MOD pak-path hash.
        uint8_t loop;         ///< 4: Nonzero to repeat the song.
        uint8_t reserved[3];  ///< 5: Reserved, zero.
    };

    static_assert(sizeof(music_play_payload) == 8, "a keyframe payload is exactly 8 bytes");
    static_assert(__builtin_offsetof(music_play_payload, loop) == 4, "loop must be at offset 4");

    // MUSIC_STOP carries no payload: every byte of it is reserved zero.

    /**
     * @brief 'KEYS' EXECUTE_SCRIPT payload: the function to run.
     *
     * Overlaid on keyframe::payload, which sits at offset 4 of a 12-byte
     * record, so function_id is always 2-byte aligned.
     */
    struct execute_script_payload
    {
        uint16_t function_id;  ///< 0: FUNC id to execute.
        uint16_t reserved[3];  ///< 2: Reserved, zero.
    };

    static_assert(sizeof(execute_script_payload) == 8, "a keyframe payload is exactly 8 bytes");

    /**
     * @brief 'TRAK' element: one independently playable track of a scene timeline.
     *
     * A Collection element, 12 bytes. A track owns a contiguous run of keyframes
     * and its own clock, so several tracks in one timeline run concurrently and
     * independently. Duration, loop mode and loop count belong here rather than
     * to the timeline because each track loops on its own terms.
     *
     * @c duration is a playhead *position*, not a count of frames: a keyframe may
     * sit exactly on it, so it is reached and occupied like any other position.
     */
    struct track
    {
        range keyframe_range;  ///< 0:  KEYS range.
        uint16_t duration;     ///< 4:  Last playhead position, in PAL frames.
        uint8_t loop_mode;     ///< 6:  A data::loop_mode.
        uint8_t loop_count;    ///< 7:  Complete cycles; 0 = infinite.
        uint8_t flags;         ///< 8:  bit0 auto_start.
        uint8_t reserved0;     ///< 9:  Reserved, zero.
        uint16_t name_id;      ///< 10: Track name id, shared by tracks of the same name.
    };

    /// track::flags bit 0: the track starts by itself when the scene loads.
    constexpr uint8_t TRACK_FLAG_AUTO_START = 1 << 0;

    static_assert(sizeof(track) == 12, "track must be exactly 12 bytes");
    static_assert(__builtin_offsetof(track, duration) == 4, "duration must be at offset 4");
    static_assert(__builtin_offsetof(track, loop_mode) == 6, "loop_mode must be at offset 6");
    static_assert(__builtin_offsetof(track, flags) == 8, "flags must be at offset 8");
    static_assert(__builtin_offsetof(track, name_id) == 10, "name_id must be at offset 10");

    /**
     * @brief 'TIME' element: a scene timeline, grouping its tracks.
     *
     * A Collection element, 8 bytes. The timeline is only a container: every
     * playable property lives on the track.
     */
    struct timeline
    {
        range track_range;   ///< 0: TRAK range.
        uint32_t reserved0;  ///< 4: Reserved, zero.
    };

    static_assert(sizeof(timeline) == 8, "timeline must be exactly 8 bytes");
    static_assert(__builtin_offsetof(timeline, reserved0) == 4, "reserved0 must be at offset 4");

    /**
     * @brief 'CHOC' element: one selectable dialogue response.
     *
     * A Collection element, 16 bytes.
     */
    struct choice
    {
        uint16_t text_string_id;   ///< 0:  Text string id.
        uint16_t set_flag_id;      ///< 2:  Set flag id, or 0xFFFF.
        uint16_t clear_flag_id;    ///< 4:  Clear flag id, or 0xFFFF.
        uint16_t check_flag_id;    ///< 6:  Check flag id, or 0xFFFF.
        uint16_t goto_page_id;     ///< 8:  Goto global PAGS id, or 0xFFFF.
        uint16_t click_function_id;///< 10: On-click FUNC id, or 0xFFFF.
        uint16_t flags;            ///< 12: bit0 enabled; bit1 disable-after-read.
        uint16_t reserved0;        ///< 14: Reserved, zero.
    };

    static_assert(sizeof(choice) == 16, "choice must be exactly 16 bytes");
    static_assert(__builtin_offsetof(choice, goto_page_id) == 8, "goto_page_id must be at offset 8");
    static_assert(__builtin_offsetof(choice, flags) == 12, "flags must be at offset 12");

    /**
     * @brief 'PAGS' element: one dialogue page and its choice range.
     *
     * A Collection element, 20 bytes.
     */
    struct page
    {
        uint16_t speaker_id;         ///< 0:  Speaker id, or 0xFFFF.
        uint16_t text_string_id;     ///< 2:  Text string id.
        uint16_t set_flag_id;        ///< 4:  Set flag id, or 0xFFFF.
        uint16_t clear_flag_id;      ///< 6:  Clear flag id, or 0xFFFF.
        uint16_t check_flag_id;      ///< 8:  Check flag id, or 0xFFFF.
        uint16_t next_page_id;       ///< 10: Next global PAGS id, or 0xFFFF.
        uint16_t display_function_id;///< 12: On-display FUNC id, or 0xFFFF.
        uint16_t flags;              ///< 14: bit0 enabled; bit1 disable-after-read.
        range choice_range;          ///< 16: CHOC range.
    };

    static_assert(sizeof(page) == 20, "page must be exactly 20 bytes");
    static_assert(__builtin_offsetof(page, next_page_id) == 10, "next_page_id must be at offset 10");
    static_assert(__builtin_offsetof(page, choice_range) == 16, "choice_range must be at offset 16");

    /**
     * @brief 'DLGS' element: a dialogue over a range of pages.
     *
     * A Collection element, 8 bytes.
     */
    struct dialogue
    {
        range page_range;           ///< 0: PAGS range.
        uint16_t first_page_id;     ///< 4: First global PAGS id, or 0xFFFF.
        uint16_t reserved0;         ///< 6: Reserved, zero.
    };

    static_assert(sizeof(dialogue) == 8, "dialogue must be exactly 8 bytes");
    static_assert(__builtin_offsetof(dialogue, first_page_id) == 4, "first_page_id must be at offset 4");

    /**
     * @brief 'SCEN' element: one runtime scene's assets, behavior, and child ranges.
     *
     * A Collection element, 44 bytes.
     */
    struct scene
    {
        uint32_t music_hash;                    ///< 0:  MOD hash, 0xFFFFFFFF keep, 0xFFFFFFFE silence.
        uint16_t background_bitmap_id;          ///< 4:  Background BMPS id, or 0xFFFF.
        uint16_t timeline_id;                   ///< 6:  TIME id, or 0xFFFF.
        uint16_t on_enter_function_id;          ///< 8:  On-enter FUNC id, or 0xFFFF.
        uint16_t on_exit_function_id;           ///< 10: On-exit FUNC id, or 0xFFFF.
        uint16_t goto_scene_id;                 ///< 12: Goto SCEN id, or 0xFFFF.
        uint16_t flags;                         ///< 14: bit0 constrains mouse; bit1 shows UI overlay.
        point<uint16_t> offset;                 ///< 16: Screen position of the background top-left.
        uint16_t width;                         ///< 20: Width.
        uint16_t height;                        ///< 22: Height.
        range scene_description_range;          ///< 24: SDES range.
        range hotspot_range;                    ///< 28: HOTS range.
        range text_region_range;                ///< 32: TXTR range.
        range graphic_range;                    ///< 36: GFXS range.
        range graphic_instance_range;           ///< 40: GFXI range.
    };

    /// scene::flags bit 0: the mouse is held inside the scene rectangle.
    constexpr uint16_t SCENE_FLAG_CONSTRAIN_MOUSE = 1 << 0;

    /// scene::flags bit 1: the scene shows the game UI overlay and the pointer.
    constexpr uint16_t SCENE_FLAG_SHOW_UI = 1 << 1;

    static_assert(sizeof(scene) == 44, "scene must be exactly 44 bytes");
    static_assert(__builtin_offsetof(scene, background_bitmap_id) == 4, "background_bitmap_id must be at offset 4");
    static_assert(__builtin_offsetof(scene, offset) == 16, "offset must be at offset 16");
    static_assert(__builtin_offsetof(scene, width) == 20, "width must be at offset 20");
    static_assert(__builtin_offsetof(scene, scene_description_range) == 24, "scene_description_range must be at offset 24");
    static_assert(__builtin_offsetof(scene, graphic_instance_range) == 40, "graphic_instance_range must be at offset 40");

    // 'SDES', 'FUNC', and 'VALS' are scalar collections with no record struct:
    // they are exposed as spans of uint16_t (string ids), uint32_t (SCRP byte
    // offsets), and int16_t (initial values) respectively. 'SCRP' is a raw
    // bytecode byte payload and 'FLGS' is a uint16 flag count followed by packed
    // initial bits; both are variable Data chunks accessed without a record type.

    /**
     * @brief Identifies each of the 23 known chunks, in catalog order.
     *
     * Used to index the loaded chunk table. Each chunk's on-disk big-endian
     * FourCC is given by fourcc_of(); the four-character codes appear only as
     * data in KNOWN_CHUNK_FOURCCS, never as identifiers.
     */
    enum class known_chunk : uint32_t
    {
        config,              ///< 'CONF' Data: display geometry and default fade.
        ui_state,            ///< 'UIST' Data: UI palette/font, nine-patch, and cursor config.
        cursors,             ///< 'CURS' Collection: fixed cursor roles.
        bitmaps,             ///< 'BMPS' Collection: bitmap, mask, and palette hashes.
        inventory_config,    ///< 'INVC' Data: inventory sheet and item range.
        inventory_items,     ///< 'INVT' Collection: inventory items.
        speakers,            ///< 'SPKR' Collection: dialogue speakers.
        hotspots,            ///< 'HOTS' Collection: scene hotspots.
        text_regions,        ///< 'TXTR' Collection: scene text regions.
        graphics,            ///< 'GFXS' Collection: graphic frame sources.
        graphic_instances,   ///< 'GFXI' Collection: graphic instances.
        scene_descriptions,  ///< 'SDES' Collection: scene-description string ids.
        keyframes,           ///< 'KEYS' Collection: timeline keyframes.
        tracks,              ///< 'TRAK' Collection: timeline tracks.
        timelines,           ///< 'TIME' Collection: timelines.
        function_offsets,    ///< 'FUNC' Collection: SCRP function offsets.
        bytecode,            ///< 'SCRP' Data: NoirVM bytecode.
        flags,               ///< 'FLGS' Data: flag count and packed initial bits.
        initial_values,      ///< 'VALS' Collection: initial signed values.
        choices,             ///< 'CHOC' Collection: dialogue choices.
        pages,               ///< 'PAGS' Collection: dialogue pages.
        dialogues,           ///< 'DLGS' Collection: dialogues.
        scenes,              ///< 'SCEN' Collection: scenes and child ranges.
        count                ///< Number of known chunks.
    };

    /**
     * @brief The number of known chunks a valid version 2 file contains.
     */
    inline constexpr uint32_t KNOWN_CHUNK_COUNT = mtl::to<uint32_t>(known_chunk::count);

    /**
     * @brief The on-disk big-endian FourCC for each known chunk, in enum order.
     */
    inline constexpr uint32_t KNOWN_CHUNK_FOURCCS[] = {
        make_fourcc("CONF"), make_fourcc("UIST"), make_fourcc("CURS"), make_fourcc("BMPS"),
        make_fourcc("INVC"), make_fourcc("INVT"), make_fourcc("SPKR"), make_fourcc("HOTS"),
        make_fourcc("TXTR"), make_fourcc("GFXS"), make_fourcc("GFXI"), make_fourcc("SDES"),
        make_fourcc("KEYS"), make_fourcc("TRAK"), make_fourcc("TIME"), make_fourcc("FUNC"),
        make_fourcc("SCRP"), make_fourcc("FLGS"), make_fourcc("VALS"), make_fourcc("CHOC"),
        make_fourcc("PAGS"), make_fourcc("DLGS"), make_fourcc("SCEN"),
    };

    static_assert(sizeof(KNOWN_CHUNK_FOURCCS) / sizeof(uint32_t) == mtl::to<uint32_t>(known_chunk::count),
        "KNOWN_CHUNK_FOURCCS must have one entry per known_chunk");

    /**
     * @brief The on-disk FourCC for a known chunk.
     */
    constexpr uint32_t fourcc_of(known_chunk chunk) { return KNOWN_CHUNK_FOURCCS[mtl::to<uint32_t>(chunk)]; }

    /**
     * @brief The on-disk element size for each known chunk, in enum order.
     *
     * Collection chunks carry sizeof(record); the Data chunks (CONF, UIST, INVC,
     * SCRP, FLGS) carry 0, as they have no fixed element stride. The loader uses
     * this to reject any chunk whose stride disagrees with this build.
     */
    inline constexpr uint32_t KNOWN_CHUNK_ELEMENT_SIZES[] = {
        0,                         // CONF
        0,                         // UIST
        sizeof(cursor),            // CURS
        sizeof(bitmap_ref),        // BMPS
        0,                         // INVC
        sizeof(inventory_item),    // INVT
        sizeof(speaker),           // SPKR
        sizeof(hotspot),           // HOTS
        sizeof(text_region),       // TXTR
        sizeof(graphic),           // GFXS
        sizeof(graphic_instance),  // GFXI
        sizeof(uint16_t),          // SDES
        sizeof(keyframe),          // KEYS
        sizeof(track),             // TRAK
        sizeof(timeline),          // TIME
        sizeof(uint32_t),          // FUNC
        0,                         // SCRP
        0,                         // FLGS
        sizeof(int16_t),           // VALS
        sizeof(choice),            // CHOC
        sizeof(page),              // PAGS
        sizeof(dialogue),          // DLGS
        sizeof(scene),             // SCEN
    };

    static_assert(sizeof(KNOWN_CHUNK_ELEMENT_SIZES) / sizeof(uint32_t) == KNOWN_CHUNK_COUNT,
        "KNOWN_CHUNK_ELEMENT_SIZES must have one entry per known_chunk");

    /**
     * @brief Maps an on-disk FourCC to its known_chunk index.
     *
     * @return The index in [0, KNOWN_CHUNK_COUNT), or KNOWN_CHUNK_COUNT if the
     *         FourCC is not a known chunk.
     */
    constexpr uint32_t known_chunk_index(uint32_t fourcc)
    {
        for (uint32_t index = 0; index < KNOWN_CHUNK_COUNT; ++index)
        {
            if (KNOWN_CHUNK_FOURCCS[index] == fourcc)
            {
                return index;
            }
        }
        return KNOWN_CHUNK_COUNT;
    }
}  // namespace NEONengine::data

#endif  // __GAME_DATA_FORMAT_H__INCLUDED__
