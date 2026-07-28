/**
 * @file game_data.h
 * @brief Loading and access for the single .neon game-data file.
 *
 * The engine loads exactly one .neon for the lifetime of the process. It is
 * extracted from the game pak, read whole into Fast RAM, validated, and then
 * accessed in place by overlaying the format structs onto the buffer. The image
 * is held in a container that frees itself when this object is destroyed.
 */

#ifndef __GAME_DATA_H__INCLUDED__
#define __GAME_DATA_H__INCLUDED__

#include "types.h"

#include <stdint.h>

#include <ace++/pak_file.h>

#include <mtl/expected.h>
#include <mtl/memory.h>
#include <mtl/span.h>
#include <mtl/utility.h>
#include <mtl/vector.h>

#include "game_data_format.h"

#include "core/asset_cache.h"

namespace NEONengine
{
    /**
     * @brief Path to the pak the engine loads, relative to the executable.
     */
    constexpr char PAK_FILE_PATH[] = "data/game.pak";

    /**
     * @brief Fixed name of the game-data entry inside the pak. Not customizable.
     */
    constexpr char NEON_FILE_NAME[] = "game.neon";

    /**
     * @brief The only .neon container version this engine can load.
     */
    constexpr u16 NEON_SUPPORTED_VERSION = 2;

    class game_data;
    using game_data_ptr = mtl::unique_ptr<game_data>;

    /**
     * @brief Owns the single loaded .neon image and exposes it in place.
     */
    class game_data
    {
        public:  ///////////////////////////////////////////////////////////////////////////////////
        /**
         * @brief Why game_data::load() failed.
         */
        enum class error
        {
            PAK_NOT_FOUND,           ///< The pak file is missing or could not be opened.
            NEON_NOT_IN_PAK,         ///< The pak has no NEON_FILE_NAME entry.
            TOO_SMALL,               ///< The entry is smaller than a header.
            READ_FAILED,             ///< Fewer bytes were read than expected.
            BAD_MAGIC,               ///< The file does not start with 'NEON'.
            UNSUPPORTED_VERSION,     ///< version != NEON_SUPPORTED_VERSION.
            BAD_HEADER,              ///< header_size or total_size is inconsistent.
            REQUIRED_CHUNK_MISSING,  ///< A known chunk is absent from the file.
            CHUNK_TOO_SMALL,         ///< A required Data chunk's payload is smaller than its record.
            CHUNK_OUT_OF_BOUNDS,     ///< A chunk header or payload runs past the file, or the count is wrong.
            UNKNOWN_CHUNK,           ///< A chunk's FourCC is not one of the known chunks.
            DUPLICATE_CHUNK,         ///< A known chunk appears more than once.
            BAD_CHUNK_STRIDE,        ///< A chunk's element size and count disagree with its body size.
        };

        using result = mtl::expected<game_data_ptr, error>;

        /**
         * @brief Loads and validates the one .neon from the game pak.
         *
         * @return The owning game_data on success, or an error code.
         */
        static result load();

        NO_COPY(game_data)

        /**
         * @brief Indexes and validates every chunk in a loaded .neon image.
         *
         * A single pass over the chunk table: it bounds-checks each chunk, maps
         * it to its known slot, and rejects unknown, duplicate, missing, or
         * mis-strided chunks. Pure and free of I/O - it takes the raw image and
         * fills @p out - so load() and the tests exercise one validation path.
         *
         * @param image     The loaded .neon bytes; its header must already be valid.
         * @param size      The number of bytes available at @p image.
         * @param out       Filled with each known chunk's header on success.
         * @param out_error Set to the failure reason when this returns false.
         * @return True when all known chunks are present and consistent.
         */
        static bool index_chunks(u8 const* image, u32 size,
            data::chunk_header const* (&out)[data::KNOWN_CHUNK_COUNT], error& out_error) noexcept;

        /**
         * @brief The validated 64-byte file header.
         */
        data::header const& header() const noexcept
        {
            return *force_to<data::header const*>(_buffer.data());
        }

        /**
         * @brief The validated 'CONF' payload: display geometry and default fade.
         */
        data::config const& config() const noexcept { return *chunk_ptr<data::config>(data::known_chunk::config); }

        /**
         * @brief The validated 'UIST' payload: UI palette/font and cursor config.
         */
        data::ui_state const& ui_state() const noexcept { return *chunk_ptr<data::ui_state>(data::known_chunk::ui_state); }

        /**
         * @brief The validated 'INVC' payload: the inventory sheet and item range.
         */
        data::inventory_config const& inventory_config() const noexcept
        {
            return *chunk_ptr<data::inventory_config>(data::known_chunk::inventory_config);
        }

        /** @brief The 'CURS' fixed cursor roles. */
        mtl::span<data::cursor> cursors() const noexcept { return chunk_span<data::cursor>(data::known_chunk::cursors); }

        /** @brief The 'BMPS' bitmap references. */
        mtl::span<data::bitmap_ref> bitmaps() const noexcept { return chunk_span<data::bitmap_ref>(data::known_chunk::bitmaps); }

        /** @brief The 'INVT' inventory items. */
        mtl::span<data::inventory_item> inventory_items() const noexcept
        {
            return chunk_span<data::inventory_item>(data::known_chunk::inventory_items);
        }

        /** @brief The 'SPKR' dialogue speakers. */
        mtl::span<data::speaker> speakers() const noexcept { return chunk_span<data::speaker>(data::known_chunk::speakers); }

        /** @brief The 'HOTS' scene hotspots. */
        mtl::span<data::hotspot> hotspots() const noexcept { return chunk_span<data::hotspot>(data::known_chunk::hotspots); }

        /** @brief The 'TXTR' scene text regions. */
        mtl::span<data::text_region> text_regions() const noexcept
        {
            return chunk_span<data::text_region>(data::known_chunk::text_regions);
        }

        /** @brief The 'GFXS' graphic frame sources. */
        mtl::span<data::graphic> graphics() const noexcept { return chunk_span<data::graphic>(data::known_chunk::graphics); }

        /** @brief The 'GFXI' graphic instances. */
        mtl::span<data::graphic_instance> graphic_instances() const noexcept
        {
            return chunk_span<data::graphic_instance>(data::known_chunk::graphic_instances);
        }

        /** @brief The 'SDES' scene-description string ids. */
        mtl::span<u16> scene_descriptions() const noexcept
        {
            return chunk_span<u16>(data::known_chunk::scene_descriptions);
        }

        /** @brief The 'KEYS' timeline keyframes. */
        mtl::span<data::keyframe> keyframes() const noexcept { return chunk_span<data::keyframe>(data::known_chunk::keyframes); }

        /** @brief The 'TRAK' timeline tracks. */
        mtl::span<data::track> tracks() const noexcept { return chunk_span<data::track>(data::known_chunk::tracks); }

        /** @brief The 'TIME' timelines. */
        mtl::span<data::timeline> timelines() const noexcept { return chunk_span<data::timeline>(data::known_chunk::timelines); }

        /** @brief The 'FUNC' script function offsets into 'SCRP'. */
        mtl::span<u32> function_offsets() const noexcept
        {
            return chunk_span<u32>(data::known_chunk::function_offsets);
        }

        /** @brief The 'SCRP' NoirVM bytecode blob; 'FUNC' offsets index into it. */
        mtl::span<u8> scripts() const noexcept
        {
            auto const* chunk = _chunks[to<u32>(data::known_chunk::bytecode)];
            return mtl::span<u8>(force_to<u8 const*>(chunk + 1), chunk->body_size);
        }

        /** @brief The 'FLGS' declared flag count. */
        u16 flag_count() const noexcept
        {
            return *force_to<u16 const*>(_chunks[to<u32>(data::known_chunk::flags)] + 1);
        }

        /** @brief The 'FLGS' packed initial flag bits (the body following the 2-byte count). */
        mtl::span<u8> initial_flags() const noexcept
        {
            auto const* chunk = _chunks[to<u32>(data::known_chunk::flags)];
            return mtl::span<u8>(force_to<u8 const*>(chunk + 1) + sizeof(u16),
                to<u32>(chunk->body_size - sizeof(u16)));
        }

        /** @brief The 'VALS' initial signed values. */
        mtl::span<i16> initial_values() const noexcept { return chunk_span<i16>(data::known_chunk::initial_values); }

        /** @brief The 'CHOC' dialogue choices. */
        mtl::span<data::choice> choices() const noexcept { return chunk_span<data::choice>(data::known_chunk::choices); }

        /** @brief The 'PAGS' dialogue pages. */
        mtl::span<data::page> pages() const noexcept { return chunk_span<data::page>(data::known_chunk::pages); }

        /** @brief The 'DLGS' dialogues. */
        mtl::span<data::dialogue> dialogues() const noexcept { return chunk_span<data::dialogue>(data::known_chunk::dialogues); }

        /** @brief The 'SCEN' scenes. */
        mtl::span<data::scene> scenes() const noexcept { return chunk_span<data::scene>(data::known_chunk::scenes); }

        /**
         * @brief Opens a pak asset by its content hash for reading.
         *
         * Assets are content-addressed by the adler32 hash of their pak path,
         * which is what the .neon records reference. The returned file is
         * decompressed transparently on read. Test it for validity before use.
         *
         * Served from the asset cache when it is held there, so a second scene
         * wanting the same asset pays neither the open nor the unpacking. The
         * cache is an optimization only: anything that has to stay resident to
         * behave correctly must hold its own copy.
         *
         * @param hash The asset's adler32 pak-path hash.
         * @return An owning file handle, or an invalid handle if not found.
         */
        ace::file_ptr open_asset(u32 hash) { return asset_cache_open(_pak, hash); }

        /**
         * @brief Opens a pak asset without going through the asset cache.
         *
         * For music, which is deliberately left out. A module is the largest
         * asset in the pak by a wide margin, and holding one would spend a
         * quarter of the cache on a single asset that a scene change usually
         * keeps anyway. Note that the cache's own size limit does not achieve
         * this on its own - a module is large but not large enough to trip it -
         * so the exclusion has to be stated here.
         *
         * @param hash The asset's adler32 pak-path hash.
         * @return An owning file handle, or an invalid handle if not found.
         */
        ace::file_ptr open_asset_uncached(u32 hash)
        {
            return ace::pakFileGetFileByHash(_pak, hash);
        }

        private:  //////////////////////////////////////////////////////////////////////////////////
        game_data(ace::pakfile_ptr&& pak, mtl::vector<u8>&& image) noexcept
            : _pak(mtl::move(pak)), _buffer(mtl::move(image))
        {
        }

        /**
         * @brief A Data chunk's payload, overlaid as its record type.
         */
        template<class T>
        T const* chunk_ptr(data::known_chunk kind) const noexcept
        {
            return force_to<T const*>(_chunks[to<u32>(kind)] + 1);
        }

        /**
         * @brief A Collection chunk's elements as a typed view over the buffer.
         */
        template<class T>
        mtl::span<T> chunk_span(data::known_chunk kind) const noexcept
        {
            auto const* chunk = _chunks[to<u32>(kind)];
            return mtl::span<T>(force_to<T const*>(chunk + 1), chunk->element_count);
        }

        ace::pakfile_ptr _pak;         ///< The open game pak, kept for the run (self-closing).
        mtl::vector<u8> _buffer;  ///< The whole .neon image, in Fast RAM (self-freeing).
        data::chunk_header const* _chunks[data::KNOWN_CHUNK_COUNT]{};  ///< Each known chunk's header in the buffer.
    };
}  // namespace NEONengine

#endif  // __GAME_DATA_H__INCLUDED__
