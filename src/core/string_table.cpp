#include "string_table.h"

#include "types.h"

#include "neonengine.h"

#include <ace/utils/disk_file.h>

#include <ace++/log.h>

#include <mtl/expected.h>
#include <mtl/utility.h>

#include "core/logger.h"
#include "utils/adler32.h"

NE_LOG_DOMAIN(Strings);

namespace NEONengine
{
    using namespace mtl;

    namespace
    {
        constexpr u32 NOIR_MAGIC        = 0x4E4F4952;  ///< 'NOIR'.
        constexpr u16 SUPPORTED_VERSION = 2;
        constexpr u32 LANGUAGE_PATH_SIZE = 32;  ///< Fits "data/lang/xx.noir".
        constexpr u32 MAX_STRING_COUNT   = 65535;
        constexpr u32 MAX_TOTAL_SIZE     = 0xFFFFFFFCu;

        /**
         * @brief The 36-byte `.noir` header, overlaid on the file's first bytes.
         *
         * Every field is big-endian, which is the CPU's own order, so the
         * struct can be read straight off disk.
         */
        struct noir_header
        {
            u32 magic;         ///< 0:  'NOIR'.
            u16 version;       ///< 4:  Format version.
            u16 build_period;  ///< 6:  UTC build period, YYMM.
            u16 header_size;   ///< 8:  Size of this header, 36.
            u16 language_id;   ///< 10: Language id.
            u32 format_flags;  ///< 12: Format flags, zero.
            u32 total_size;    ///< 16: Total file size.
            u32 string_count;  ///< 20: Number of string records.
            u32 payload_size;  ///< 24: Payload byte size.
            u32 build_number;  ///< 28: Monotonic project build number.
            u32 checksum;      ///< 32: Adler32 of bytes 36..EOF.
        };

        static_assert(sizeof(noir_header) == 36, "noir_header must be exactly 36 bytes");
        static_assert(__builtin_offsetof(noir_header, header_size) == 8, "header_size must be at offset 8");
        static_assert(__builtin_offsetof(noir_header, language_id) == 10, "language_id must be at offset 10");
        static_assert(__builtin_offsetof(noir_header, total_size) == 16, "total_size must be at offset 16");
        static_assert(__builtin_offsetof(noir_header, string_count) == 20, "string_count must be at offset 20");
        static_assert(__builtin_offsetof(noir_header, payload_size) == 24, "payload_size must be at offset 24");
        static_assert(__builtin_offsetof(noir_header, checksum) == 32, "checksum must be at offset 32");

        /**
         * @brief Zero bytes following a record of @p length.
         *
         * Always one to four, never zero. Padding to the next 4-byte boundary
         * alone would leave a length divisible by four with no trailing byte,
         * so a full four bytes are added in that case instead. That keeps every
         * length prefix 4-byte aligned *and* guarantees at least one zero byte
         * after the text, which is what lets bstr_view::data() be handed
         * straight to a C API.
         */
        constexpr u32 padding_for(u32 length) noexcept
        {
            return 4 - (length & 3);
        }

        /**
         * @brief Write @p lang's `.noir` path into @p path.
         */
        void make_language_path(language lang, char (&path)[LANGUAGE_PATH_SIZE])
        {
            snprintf(path, LANGUAGE_PATH_SIZE, "data/lang/%s.noir", language_code(lang));
        }

        /**
         * @brief Read and validate a `.noir` header from @p pFile.
         *
         * Checks everything that must hold before the payload is trusted: the
         * magic, a supported version, the fixed header size, a known language,
         * and a payload size consistent with the total file size.
         */
        mtl::expected<noir_header, string_table::error_code> read_header(tFile* pFile)
        {
            using error_code = string_table::error_code;

            noir_header header;
            if (fileRead(pFile, &header, sizeof(noir_header)) != sizeof(noir_header))
            {
                NE_LOG("could not read a %lu byte header",
                    to<ULONG>(sizeof(noir_header)));
                return mtl::unexpected(error_code::MISSING_HEADER);
            }

            if (header.magic != NOIR_MAGIC)
            {
                NE_LOG("expected magic 'NOIR', got '%c%c%c%c'",
                    to<char>((header.magic >> 24) & 0xFF),
                    to<char>((header.magic >> 16) & 0xFF),
                    to<char>((header.magic >> 8) & 0xFF),
                    to<char>((header.magic >> 0) & 0xFF));
                return mtl::unexpected(error_code::MISSING_HEADER);
            }

            if (header.version != SUPPORTED_VERSION)
            {
                NE_LOG("unsupported version %hu (expected %hu)",
                    header.version, SUPPORTED_VERSION);
                return mtl::unexpected(error_code::VERSION_NOT_SUPPORTED);
            }

            if (header.header_size != sizeof(noir_header))
            {
                NE_LOG("header size %hu, expected %lu",
                    header.header_size, to<ULONG>(sizeof(noir_header)));
                return mtl::unexpected(error_code::MALFORMED_HEADER);
            }

            if (header.language_id >= to<u16>(language::COUNT))
            {
                NE_LOG("unsupported language id %hu", header.language_id);
                return mtl::unexpected(error_code::UNSUPPORTED_LANGUAGE);
            }

            if (header.format_flags != 0)
            {
                NE_LOG("unknown format flags %08lx",
                    to<ULONG>(header.format_flags));
                return mtl::unexpected(error_code::MALFORMED_HEADER);
            }

            if (header.string_count > MAX_STRING_COUNT)
            {
                NE_LOG("%lu strings exceeds the %lu the format allows",
                    to<ULONG>(header.string_count), to<ULONG>(MAX_STRING_COUNT));
                return mtl::unexpected(error_code::MALFORMED_HEADER);
            }

            // The payload is the whole file past the header, so a mismatch means
            // the sizes were not written together and nothing else can be trusted.
            if (header.total_size < sizeof(noir_header)
                || header.total_size > MAX_TOTAL_SIZE
                || (header.total_size & 3) != 0
                || header.payload_size != header.total_size - sizeof(noir_header))
            {
                NE_LOG("payload %lu does not match total %lu minus header",
                    to<ULONG>(header.payload_size), to<ULONG>(header.total_size));
                return mtl::unexpected(error_code::MALFORMED_HEADER);
            }

            // Trust the declared sizes only once the file is really that big;
            // otherwise a tiny corrupt file could drive a huge allocation.
            auto const actual_size = fileGetSize(pFile);
            if (actual_size < 0 || to<u32>(actual_size) != header.total_size)
            {
                NE_LOG("file is %ld bytes, header declares %lu",
                    actual_size, to<ULONG>(header.total_size));
                return mtl::unexpected(error_code::MALFORMED_HEADER);
            }

            return header;
        }
    }  // namespace

    char const* language_code(language lang) noexcept
    {
        switch (lang)
        {
            case language::IT: return "it";
            case language::DE: return "de";
            case language::EN:  // fallthrough
            default: return "en";
        }
    }

    bstr_view const string_table::get_string(u32 id) const
    {
        if (id >= _offsets.size()) return bstr_view();

        return bstr_view::from_bstr(_offsets[id]);
    }

    string_table::result string_table::create(language lang)
    {
        char path[LANGUAGE_PATH_SIZE];
        make_language_path(lang, path);

        auto result = create_from_file(path);
        if (!result) return result;

        // The file is named for a language, so a header that disagrees means the
        // wrong table would silently stand in for the requested one.
        if (result.value()->language_id() != lang)
        {
            NE_LOG("'%s' holds language %hu, not the requested %hu",
                path,
                to<UWORD>(result.value()->language_id()),
                to<UWORD>(lang));
            return mtl::unexpected(error_code::LANGUAGE_MISMATCH);
        }

        return result;
    }

    string_table::result string_table::create_from_file(char const* szFilePath)
    {
        return create_from_fd(ace::file_ptr(diskFileOpen(szFilePath, DISK_FILE_MODE_READ, 1)));
    }

    string_table::result string_table::create_from_fd(ace::file_ptr&& pFile)
    {
        ACE_LOG_BLOCK("NEONengine::string_table::create_from_fd");

        if (!pFile)
        {
            return mtl::unexpected(error_code::FILE_NOT_FOUND);
        }

        auto header = read_header(pFile.get());
        if (!header)
        {
            return mtl::unexpected(header.error());
        }

        auto pTable    = string_table_ptr(new (MemF::Fast) string_table());
        auto const& hd = header.value();

        pTable->_language     = to<language>(hd.language_id);
        pTable->_build_period = hd.build_period;
        pTable->_build_number = hd.build_number;

        pTable->_payload.resize(hd.payload_size);
        if (hd.payload_size != 0
            && fileRead(pFile.get(), pTable->_payload.data(), hd.payload_size) != hd.payload_size)
        {
            NE_LOG("payload is shorter than its declared %lu bytes",
                to<ULONG>(hd.payload_size));
            return mtl::unexpected(error_code::MALFORMED_PAYLOAD);
        }

        auto const checksum = adler32(pTable->_payload.data(), hd.payload_size);
        if (checksum != hd.checksum)
        {
            NE_LOG("checksum %08lx does not match the header's %08lx",
                to<ULONG>(checksum), to<ULONG>(hd.checksum));
            return mtl::unexpected(error_code::CHECKSUM_MISMATCH);
        }

        // Index every record up front so lookups are a single array read. The
        // contract is that exactly string_count records fill the payload
        // exactly, so anything else is a malformed file rather than a table to
        // be salvaged.
        pTable->_offsets.reserve(hd.string_count);

        u32 offset = 0;
        for (u32 index = 0; index < hd.string_count; ++index)
        {
            // offset never passes payload_size, so this cannot underflow.
            u32 const remaining = hd.payload_size - offset;
            if (remaining < sizeof(u32))
            {
                NE_LOG("record %lu has no room for its length prefix",
                    to<ULONG>(index));
                return mtl::unexpected(error_code::MALFORMED_PAYLOAD);
            }

            auto* pRecord     = pTable->_payload.data() + offset;
            auto const length = to<u32>(bstr_view::from_bstr(pRecord).length());

            // Each test subtracts from the bytes that are left rather than
            // adding the record up, so a corrupt length near the 32-bit maximum
            // cannot wrap a sum and understate how much room the record needs.
            u32 const after_prefix = remaining - sizeof(u32);
            u32 const padding      = padding_for(length);
            if (length > after_prefix || padding > after_prefix - length)
            {
                NE_LOG("record %lu of %lu bytes runs past the payload",
                    to<ULONG>(index), to<ULONG>(length));
                return mtl::unexpected(error_code::MALFORMED_PAYLOAD);
            }

            pTable->_offsets.push_back(pRecord);
            offset += sizeof(u32) + length + padding;
        }

        if (offset != hd.payload_size)
        {
            NE_LOG("%lu records consumed %lu of %lu payload bytes",
                to<ULONG>(hd.string_count),
                to<ULONG>(offset),
                to<ULONG>(hd.payload_size));
            return mtl::unexpected(error_code::MALFORMED_PAYLOAD);
        }

        pTable->_string_count = to<u16>(pTable->_offsets.size());

        NE_LOG("loaded '%s', %hu strings",
            language_code(pTable->_language), pTable->_string_count);

        return mtl::move(pTable);
    }

    u32 string_table::peek_language_name(language lang, char* pBuffer, u32 capacity)
    {
        ACE_LOG_BLOCK("NEONengine::string_table::peek_language_name");

        if (!pBuffer || capacity == 0) return 0;
        pBuffer[0] = '\0';

        char path[LANGUAGE_PATH_SIZE];
        make_language_path(lang, path);

        auto pFile = ace::file_ptr(diskFileOpen(path, DISK_FILE_MODE_READ, 1));
        if (!pFile) return 0;

        auto header = read_header(pFile.get());
        if (!header) return 0;

        auto const& hd = header.value();
        if (hd.string_count == 0 || to<language>(hd.language_id) != lang) return 0;

        // The first record starts immediately after the header, which is exactly
        // where reading the header left the cursor.
        u32 length = 0;
        if (fileRead(pFile.get(), &length, sizeof(length)) != sizeof(length)) return 0;
        if (length == 0 || hd.payload_size < sizeof(u32)
            || length > hd.payload_size - sizeof(u32))
        {
            return 0;
        }

        auto const copied = (length < capacity - 1) ? length : capacity - 1;
        if (copied != 0 && fileRead(pFile.get(), pBuffer, copied) != copied) return 0;
        pBuffer[copied] = '\0';

        return copied;
    }
}  // namespace NEONengine
