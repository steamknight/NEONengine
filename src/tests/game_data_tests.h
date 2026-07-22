#ifndef __GAME_DATA_TESTS_H__INCLUDED__
#define __GAME_DATA_TESTS_H__INCLUDED__

#ifdef ACE_TEST_RUNNER

#include "core/game_data.h"

#include "test_macros.h"

namespace NEONengine
{
    namespace
    {
        using error = game_data::error;

        /// A minimal image: the header plus one empty chunk per known chunk.
        constexpr uint32_t kEmptyImageSize = mtl::to<uint32_t>(sizeof(data::header))
            + data::KNOWN_CHUNK_COUNT * mtl::to<uint32_t>(sizeof(data::chunk_header));

        /// Byte offset of the chunk at table position @p index.
        constexpr uint32_t chunk_offset(uint32_t index)
        {
            return mtl::to<uint32_t>(sizeof(data::header))
                + index * mtl::to<uint32_t>(sizeof(data::chunk_header));
        }

        /// The chunk header at table position @p index within @p buf.
        data::chunk_header& chunk_at(uint8_t* buf, data::known_chunk kind)
        {
            return *mtl::force_to<data::chunk_header*>(buf + chunk_offset(mtl::to<uint32_t>(kind)));
        }

        /// Overlays a valid minimal image on @p buf: a header plus one empty chunk
        /// per known chunk in catalog order, each carrying its expected stride.
        void build_empty_image(uint8_t* buf)
        {
            for (uint32_t i = 0; i < kEmptyImageSize; ++i)
            {
                buf[i] = 0;
            }

            auto& header       = *mtl::force_to<data::header*>(buf);
            header.magic       = data::NEON_MAGIC;
            header.version     = NEON_SUPPORTED_VERSION;
            header.header_size = sizeof(data::header);
            header.total_size  = kEmptyImageSize;
            header.chunk_count = data::KNOWN_CHUNK_COUNT;

            uint32_t offset = mtl::to<uint32_t>(sizeof(data::header));
            for (uint32_t i = 0; i < data::KNOWN_CHUNK_COUNT; ++i)
            {
                auto& chunk         = *mtl::force_to<data::chunk_header*>(buf + offset);
                chunk.fourcc        = data::KNOWN_CHUNK_FOURCCS[i];
                chunk.body_size     = 0;
                chunk.element_count = 0;
                chunk.element_size  = data::KNOWN_CHUNK_ELEMENT_SIZES[i];
                offset += mtl::to<uint32_t>(sizeof(data::chunk_header));
            }
        }

        /// Runs index_chunks and checks it rejected the image with @p expected.
        char const* expect_reject(uint8_t const* image, uint32_t size, error expected)
        {
            data::chunk_header const* out[data::KNOWN_CHUNK_COUNT];
            error err = error::PAK_NOT_FOUND;
            if (game_data::index_chunks(image, size, out, err))
            {
                return "expected rejection but the image was accepted";
            }
            if (err != expected)
            {
                return "image rejected with an unexpected error";
            }
            return nullptr;
        }
    }  // namespace

    TEST_IMPL(test_index_accepts_minimal_image)
    {
        alignas(4) uint8_t buf[kEmptyImageSize];
        build_empty_image(buf);

        data::chunk_header const* out[data::KNOWN_CHUNK_COUNT];
        error err = error::PAK_NOT_FOUND;
        TEST_ASSERT(game_data::index_chunks(buf, sizeof(buf), out, err), "minimal image rejected");
        for (uint32_t i = 0; i < data::KNOWN_CHUNK_COUNT; ++i)
        {
            TEST_ASSERT(out[i] != nullptr, "a known chunk was not indexed");
            TEST_ASSERT(out[i]->fourcc == data::KNOWN_CHUNK_FOURCCS[i], "chunk indexed into the wrong slot");
        }
        TEST_SUCCESS;
    }

    TEST_IMPL(test_index_reads_collection_count)
    {
        constexpr auto scene_bytes = mtl::to<uint32_t>(sizeof(data::scene));
        alignas(4) uint8_t buf[kEmptyImageSize + scene_bytes];
        build_empty_image(buf);

        // SCEN is the last chunk, so its payload extends the image without
        // shifting any other chunk.
        auto const size  = kEmptyImageSize + scene_bytes;
        auto& header     = *mtl::force_to<data::header*>(buf);
        header.total_size = size;
        auto& scenes      = chunk_at(buf, data::known_chunk::scenes);
        scenes.element_count = 1;
        scenes.body_size     = scene_bytes;

        data::chunk_header const* out[data::KNOWN_CHUNK_COUNT];
        error err = error::PAK_NOT_FOUND;
        TEST_ASSERT(game_data::index_chunks(buf, size, out, err), "image with a populated collection rejected");
        TEST_ASSERT(out[mtl::to<uint32_t>(data::known_chunk::scenes)]->element_count == 1,
            "collection element count not preserved");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_index_rejects_wrong_chunk_count)
    {
        alignas(4) uint8_t buf[kEmptyImageSize];
        build_empty_image(buf);
        mtl::force_to<data::header*>(buf)->chunk_count = data::KNOWN_CHUNK_COUNT - 1;
        return expect_reject(buf, sizeof(buf), error::REQUIRED_CHUNK_MISSING);
    }

    TEST_IMPL(test_index_rejects_unknown_chunk)
    {
        alignas(4) uint8_t buf[kEmptyImageSize];
        build_empty_image(buf);
        chunk_at(buf, data::known_chunk::config).fourcc = data::make_fourcc("ZZZZ");
        return expect_reject(buf, sizeof(buf), error::UNKNOWN_CHUNK);
    }

    TEST_IMPL(test_index_rejects_duplicate_chunk)
    {
        alignas(4) uint8_t buf[kEmptyImageSize];
        build_empty_image(buf);
        chunk_at(buf, data::known_chunk::ui_state).fourcc = data::fourcc_of(data::known_chunk::config);
        return expect_reject(buf, sizeof(buf), error::DUPLICATE_CHUNK);
    }

    TEST_IMPL(test_index_rejects_bad_element_size)
    {
        alignas(4) uint8_t buf[kEmptyImageSize];
        build_empty_image(buf);
        chunk_at(buf, data::known_chunk::hotspots).element_size = sizeof(data::hotspot) - 1;
        return expect_reject(buf, sizeof(buf), error::BAD_CHUNK_STRIDE);
    }

    TEST_IMPL(test_index_rejects_bad_body_size)
    {
        alignas(4) uint8_t buf[kEmptyImageSize];
        build_empty_image(buf);
        // Correct stride and empty count, but a nonzero body: inconsistent.
        chunk_at(buf, data::known_chunk::hotspots).body_size = sizeof(data::hotspot);
        return expect_reject(buf, sizeof(buf), error::BAD_CHUNK_STRIDE);
    }

    TEST_IMPL(test_index_rejects_data_chunk_with_elements)
    {
        alignas(4) uint8_t buf[kEmptyImageSize];
        build_empty_image(buf);
        chunk_at(buf, data::known_chunk::config).element_size = sizeof(data::config);
        return expect_reject(buf, sizeof(buf), error::BAD_CHUNK_STRIDE);
    }

    TEST_IMPL(test_index_rejects_out_of_bounds_chunk)
    {
        alignas(4) uint8_t buf[kEmptyImageSize];
        build_empty_image(buf);
        chunk_at(buf, data::known_chunk::config).body_size = kEmptyImageSize;
        return expect_reject(buf, sizeof(buf), error::CHUNK_OUT_OF_BOUNDS);
    }

    TEST_IMPL(test_index_rejects_total_size_mismatch)
    {
        alignas(4) uint8_t buf[kEmptyImageSize];
        build_empty_image(buf);
        mtl::force_to<data::header*>(buf)->total_size = kEmptyImageSize - 4;
        return expect_reject(buf, sizeof(buf), error::CHUNK_OUT_OF_BOUNDS);
    }

    TEST_IMPL(test_index_measures_mid_table_data_bodies)
    {
        // SCRP (bytecode) and FLGS (flags) are Data chunks that carry bodies in
        // the middle of the table in a real file. Verify index_chunks advances
        // past them and that their bytes land where scripts()/initial_flags() read.
        constexpr uint32_t scrp_body = 8;  // eight bytecode bytes
        constexpr uint32_t flgs_body = 4;  // u16 count + two packed bytes
        constexpr uint32_t size      = kEmptyImageSize + scrp_body + flgs_body;
        alignas(4) uint8_t buf[size];
        for (uint32_t i = 0; i < size; ++i)
        {
            buf[i] = 0;
        }

        auto& header       = *mtl::force_to<data::header*>(buf);
        header.magic       = data::NEON_MAGIC;
        header.version     = NEON_SUPPORTED_VERSION;
        header.header_size = sizeof(data::header);
        header.total_size  = size;
        header.chunk_count = data::KNOWN_CHUNK_COUNT;

        uint32_t offset = mtl::to<uint32_t>(sizeof(data::header));
        for (uint32_t i = 0; i < data::KNOWN_CHUNK_COUNT; ++i)
        {
            auto& chunk         = *mtl::force_to<data::chunk_header*>(buf + offset);
            chunk.fourcc        = data::KNOWN_CHUNK_FOURCCS[i];
            chunk.element_size  = data::KNOWN_CHUNK_ELEMENT_SIZES[i];
            chunk.element_count = 0;

            auto* payload = buf + offset + sizeof(data::chunk_header);
            uint32_t body = 0;
            if (i == mtl::to<uint32_t>(data::known_chunk::bytecode))
            {
                body = scrp_body;
                for (uint32_t b = 0; b < scrp_body; ++b)
                {
                    payload[b] = mtl::to<uint8_t>(0xA0 + b);
                }
            }
            else if (i == mtl::to<uint32_t>(data::known_chunk::flags))
            {
                body                                = flgs_body;
                *mtl::force_to<uint16_t*>(payload)  = 12;    // twelve flags
                payload[sizeof(uint16_t) + 0]       = 0x05;  // packed initial bits
                payload[sizeof(uint16_t) + 1]       = 0x00;
            }
            chunk.body_size = body;
            offset += mtl::to<uint32_t>(sizeof(data::chunk_header)) + body;
        }

        data::chunk_header const* out[data::KNOWN_CHUNK_COUNT];
        error err = error::PAK_NOT_FOUND;
        TEST_ASSERT(game_data::index_chunks(buf, size, out, err), "image with mid-table data bodies rejected");

        auto const* scrp = out[mtl::to<uint32_t>(data::known_chunk::bytecode)];
        auto const* flgs = out[mtl::to<uint32_t>(data::known_chunk::flags)];
        TEST_ASSERT(scrp->body_size == scrp_body, "SCRP body size not measured");
        TEST_ASSERT(flgs->body_size == flgs_body, "FLGS body size not measured");

        auto const* scrp_bytes = mtl::force_to<uint8_t const*>(scrp + 1);
        TEST_ASSERT(scrp_bytes[0] == 0xA0 && scrp_bytes[scrp_body - 1] == mtl::to<uint8_t>(0xA0 + scrp_body - 1),
            "SCRP bytecode not readable at its body");

        auto const* flgs_bytes = mtl::force_to<uint8_t const*>(flgs + 1);
        TEST_ASSERT(*mtl::force_to<uint16_t const*>(flgs_bytes) == 12, "FLGS count not readable");
        TEST_ASSERT(flgs_bytes[sizeof(uint16_t)] == 0x05, "FLGS initial bits not readable");
        TEST_SUCCESS;
    }

    TEST_SUITE_BEGIN(game_data)
        TEST(test_index_accepts_minimal_image)
        TEST(test_index_reads_collection_count)
        TEST(test_index_rejects_wrong_chunk_count)
        TEST(test_index_rejects_unknown_chunk)
        TEST(test_index_rejects_duplicate_chunk)
        TEST(test_index_rejects_bad_element_size)
        TEST(test_index_rejects_bad_body_size)
        TEST(test_index_rejects_data_chunk_with_elements)
        TEST(test_index_rejects_out_of_bounds_chunk)
        TEST(test_index_rejects_total_size_mismatch)
        TEST(test_index_measures_mid_table_data_bodies)
    TEST_SUITE_END
}  // namespace NEONengine

#endif  // ACE_TEST_RUNNER

#endif  // __GAME_DATA_TESTS_H__INCLUDED__
