#include "game_data.h"

#include "neonengine.h"

#include <ace/utils/file.h>

#include <ace++/pak_file.h>

#include <mtl/memory.h>
#include <mtl/utility.h>

#include "core/logger.h"

NE_LOG_DOMAIN(Data);

namespace NEONengine
{
    game_data::result game_data::load()
    {
        mtl::system_use_guard guard;

        auto pak = ace::pakFileOpen(PAK_FILE_PATH, 1 /* uninterrupted bulk read */);
        if (!pak)
        {
            return mtl::make_error<game_data_ptr, error>(error::PAK_NOT_FOUND);
        }

        auto neon = ace::pakFileGetFileByPath(pak, NEON_FILE_NAME);
        if (!neon)
        {
            return mtl::make_error<game_data_ptr, error>(error::NEON_NOT_IN_PAK);
        }

        LONG const signed_size = fileGetSize(neon.get());
        if (signed_size < mtl::to<LONG>(sizeof(data::header)))
        {
            return mtl::make_error<game_data_ptr, error>(error::TOO_SMALL);
        }
        auto const size = mtl::to<uint32_t>(signed_size);

        mtl::vector<uint8_t> image;
        image.resize(size);
        if (fileRead(neon.get(), image.data(), size) != size)
        {
            return mtl::make_error<game_data_ptr, error>(error::READ_FAILED);
        }

        auto const& header = *mtl::force_to<data::header const*>(image.data());
        if (header.magic != data::NEON_MAGIC)
        {
            return mtl::make_error<game_data_ptr, error>(error::BAD_MAGIC);
        }
        if (header.version != NEON_SUPPORTED_VERSION)
        {
            return mtl::make_error<game_data_ptr, error>(error::UNSUPPORTED_VERSION);
        }
        if (header.header_size != sizeof(data::header) || header.total_size != size)
        {
            return mtl::make_error<game_data_ptr, error>(error::BAD_HEADER);
        }

        NE_LOG("version %hu, build %lu, %lu bytes, %lu chunks",
            mtl::to<UWORD>(header.version),
            mtl::to<ULONG>(header.build_number),
            mtl::to<ULONG>(header.total_size),
            mtl::to<ULONG>(header.chunk_count));

        auto gd = game_data_ptr(new (mtl::MemF::Fast) game_data(mtl::move(pak), mtl::move(image)));

        error index_error;
        if (!index_chunks(gd->_buffer.data(), size, gd->_chunks, index_error))
        {
            return mtl::make_error<game_data_ptr, error>(index_error);
        }

        auto const conf_body = gd->_chunks[mtl::to<uint32_t>(data::known_chunk::config)]->body_size;
        auto const uist_body = gd->_chunks[mtl::to<uint32_t>(data::known_chunk::ui_state)]->body_size;
        auto const invc_body = gd->_chunks[mtl::to<uint32_t>(data::known_chunk::inventory_config)]->body_size;
        auto const flgs_body = gd->_chunks[mtl::to<uint32_t>(data::known_chunk::flags)]->body_size;
        if (conf_body < sizeof(data::config) || uist_body < sizeof(data::ui_state)
            || invc_body < sizeof(data::inventory_config) || flgs_body < sizeof(uint16_t))
        {
            return mtl::make_error<game_data_ptr, error>(error::CHUNK_TOO_SMALL);
        }

        NE_LOG("CONF %hux%hu fade=%hu; UIST palette=%08lx font=%08lx",
            mtl::to<UWORD>(gd->config().width),
            mtl::to<UWORD>(gd->config().height),
            mtl::to<UWORD>(gd->config().default_fade_frames),
            mtl::to<ULONG>(gd->ui_state().ui_palette_hash),
            mtl::to<ULONG>(gd->ui_state().font_hash));

        return gd;
    }

    bool game_data::index_chunks(uint8_t const* image, uint32_t size,
        data::chunk_header const* (&out)[data::KNOWN_CHUNK_COUNT], error& out_error) noexcept
    {
        for (auto& slot : out)
        {
            slot = nullptr;
        }

        constexpr auto header_bytes       = mtl::to<uint32_t>(sizeof(data::header));
        constexpr auto chunk_header_bytes = mtl::to<uint32_t>(sizeof(data::chunk_header));

        auto const& hdr = *mtl::force_to<data::header const*>(image);
        if (hdr.chunk_count != data::KNOWN_CHUNK_COUNT)
        {
            out_error = error::REQUIRED_CHUNK_MISSING;
            return false;
        }

        uint32_t offset = header_bytes;
        for (uint32_t remaining = hdr.chunk_count; remaining != 0; --remaining)
        {
            if (offset + chunk_header_bytes > size)
            {
                out_error = error::CHUNK_OUT_OF_BOUNDS;
                return false;
            }

            auto const* chunk = mtl::force_to<data::chunk_header const*>(image + offset);
            auto const next   = offset + chunk_header_bytes + chunk->body_size;
            if (next <= offset || next > size)
            {
                out_error = error::CHUNK_OUT_OF_BOUNDS;
                return false;
            }

            auto const index = data::known_chunk_index(chunk->fourcc);
            if (index >= data::KNOWN_CHUNK_COUNT)
            {
                out_error = error::UNKNOWN_CHUNK;
                return false;
            }
            if (out[index] != nullptr)
            {
                out_error = error::DUPLICATE_CHUNK;
                return false;
            }

            auto const stride = data::KNOWN_CHUNK_ELEMENT_SIZES[index];
            if (stride != 0)
            {
                auto const count = chunk->element_count;
                if (chunk->element_size != stride || count > chunk->body_size / stride
                    || chunk->body_size != mtl::to<uint32_t>(mtl::round_up<4>(count * stride)))
                {
                    out_error = error::BAD_CHUNK_STRIDE;
                    return false;
                }
            }
            else if (chunk->element_size != 0 || chunk->element_count != 0)
            {
                out_error = error::BAD_CHUNK_STRIDE;
                return false;
            }

            out[index] = chunk;
            offset     = next;
        }

        if (offset != hdr.total_size)
        {
            out_error = error::CHUNK_OUT_OF_BOUNDS;
            return false;
        }

        for (auto const* slot : out)
        {
            if (slot == nullptr)
            {
                out_error = error::REQUIRED_CHUNK_MISSING;
                return false;
            }
        }

        return true;
    }
}  // namespace NEONengine
