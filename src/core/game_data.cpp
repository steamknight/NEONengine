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
        if (signed_size < mtl::to<LONG>(sizeof(neon_header)))
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

        auto const& header = *mtl::force_to<neon_header const*>(image.data());
        if (header.magic != NEON_MAGIC)
        {
            return mtl::make_error<game_data_ptr, error>(error::BAD_MAGIC);
        }
        if (header.version != NEON_SUPPORTED_VERSION)
        {
            return mtl::make_error<game_data_ptr, error>(error::UNSUPPORTED_VERSION);
        }
        if (header.header_size != sizeof(neon_header) || header.total_size != size)
        {
            return mtl::make_error<game_data_ptr, error>(error::BAD_HEADER);
        }

        NE_LOG("game_data: version %hu, build %lu, %lu bytes, %lu chunks",
            mtl::to<UWORD>(header.version),
            mtl::to<ULONG>(header.build_number),
            mtl::to<ULONG>(header.total_size),
            mtl::to<ULONG>(header.chunk_count));

        return game_data_ptr(new (mtl::MemF::Fast) game_data(mtl::move(image)));
    }
}  // namespace NEONengine
