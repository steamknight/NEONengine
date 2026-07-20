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

#include <stdint.h>

#include <mtl/expected.h>
#include <mtl/memory.h>
#include <mtl/utility.h>
#include <mtl/vector.h>

#include "game_data_format.h"

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
    constexpr uint16_t NEON_SUPPORTED_VERSION = 2;

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
            PAK_NOT_FOUND,        ///< The pak file is missing or could not be opened.
            NEON_NOT_IN_PAK,      ///< The pak has no NEON_FILE_NAME entry.
            TOO_SMALL,            ///< The entry is smaller than a header.
            READ_FAILED,          ///< Fewer bytes were read than expected.
            BAD_MAGIC,            ///< The file does not start with 'NEON'.
            UNSUPPORTED_VERSION,  ///< version != NEON_SUPPORTED_VERSION.
            BAD_HEADER,           ///< header_size or total_size is inconsistent.
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
         * @brief The validated 64-byte file header.
         */
        neon_header const& header() const noexcept
        {
            return *mtl::force_to<neon_header const*>(_buffer.data());
        }

        private:  //////////////////////////////////////////////////////////////////////////////////
        explicit game_data(mtl::vector<uint8_t>&& image) noexcept : _buffer(mtl::move(image)) {}

        mtl::vector<uint8_t> _buffer;  ///< The whole .neon image, in Fast RAM (self-freeing).
    };
}  // namespace NEONengine

#endif  // __GAME_DATA_H__INCLUDED__
