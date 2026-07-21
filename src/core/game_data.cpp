#include "game_data.h"

#include "neonengine.h"

#include <ace/managers/log.h>
#include <ace/utils/file.h>

#include <ace++/pak_file.h>

#include <mtl/memory.h>
#include <mtl/utility.h>

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

        auto neon = ace::pakFileGetFile(pak, NEON_FILE_NAME);
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

        NE_LOG("game_data: version %hu, build %lu, %lu bytes, %lu chunks",
            mtl::to<UWORD>(header.version),
            mtl::to<ULONG>(header.build_number),
            mtl::to<ULONG>(header.total_size),
            mtl::to<ULONG>(header.chunk_count));

        auto gd = game_data_ptr(new (mtl::MemF::Fast) game_data(mtl::move(pak), mtl::move(image)));

        auto const* conf_hdr = gd->find_chunk(data::fourcc_of(data::known_chunk::config));
        auto const* uist_hdr = gd->find_chunk(data::fourcc_of(data::known_chunk::ui_state));
        if (!conf_hdr || !uist_hdr)
        {
            return mtl::make_error<game_data_ptr, error>(error::REQUIRED_CHUNK_MISSING);
        }
        if (conf_hdr->body_size < sizeof(data::config) || uist_hdr->body_size < sizeof(data::ui_state))
        {
            return mtl::make_error<game_data_ptr, error>(error::CHUNK_TOO_SMALL);
        }

        gd->_config   = mtl::force_to<data::config const*>(conf_hdr + 1);
        gd->_ui_state = mtl::force_to<data::ui_state const*>(uist_hdr + 1);

        NE_LOG("game_data: CONF %hux%hu fade=%hu; UIST palette=%08lx font=%08lx",
            mtl::to<UWORD>(gd->_config->width),
            mtl::to<UWORD>(gd->_config->height),
            mtl::to<UWORD>(gd->_config->default_fade_frames),
            mtl::to<ULONG>(gd->_ui_state->ui_palette_hash),
            mtl::to<ULONG>(gd->_ui_state->font_hash));

        return gd;
    }

    data::chunk_header const* game_data::find_chunk(uint32_t fourcc) const noexcept
    {
        constexpr auto chunk_header_bytes = mtl::to<uint32_t>(sizeof(data::chunk_header));

        auto const& hdr = header();
        auto const total = hdr.total_size;
        uint32_t offset  = mtl::to<uint32_t>(sizeof(data::header));

        for (uint32_t remaining = hdr.chunk_count; remaining != 0; --remaining)
        {
            if (offset + chunk_header_bytes > total)
            {
                break;
            }

            auto const* chunk = mtl::force_to<data::chunk_header const*>(_buffer.data() + offset);
            auto const next   = offset + chunk_header_bytes + chunk->body_size;
            if (next <= offset || next > total)
            {
                break;
            }

            if (chunk->fourcc == fourcc)
            {
                return chunk;
            }

            offset = next;
        }

        return nullptr;
    }
}  // namespace NEONengine
