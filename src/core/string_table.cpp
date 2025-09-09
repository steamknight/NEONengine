#include "string_table.h"

#include "neonengine.h"

#include <ace/utils/disk_file.h>

#include <ace++/log.h>

#include <mtl/utility.h>

namespace NEONengine
{
    using namespace mtl;

    constexpr uint32_t make_magic(char const name[4])
    {
        return to<uint32_t>(to<uint32_t>(name[0]) << 24    //
                            | to<uint32_t>(name[1]) << 16  //
                            | to<uint32_t>(name[2]) << 8   //
                            | to<uint32_t>(name[3]) << 0   //
        );
    }

    struct noir_header
    {
        uint32_t magic;
        uint16_t version;
        uint16_t languageId;
    };

    struct string_chunk_header
    {
        uint32_t chunkName;
        uint32_t stringCount;
        size_t dataSize;
    };

    constexpr char MAGIC[4]        = { 'N', 'O', 'I', 'R' };
    constexpr char STRING_CHUNK[4] = { 'S', 'T', 'R', 'G' };

    constexpr uint16_t SUPPORTED_VERSION = 2;

    string_table::~string_table()
    {
        delete[] _pData;
    }

    bstr_view const string_table::get_string(uint32_t id) const
    {
        if (id >= _offsets.size()) return bstr_view();

        return bstr_view::from_bstr(_offsets[id]);
    }

    string_table::result string_table::create_from_file(char const* szFilePath)
    {
        return create_from_fd(diskFileOpen(szFilePath, DISK_FILE_MODE_READ, 1));
    }

    string_table::result string_table::create_from_fd(tFile* pFile)
    {
        ACE_LOG_BLOCK("NEONengine::string_table::create_from_fd");

        auto pTable = string_table_ptr(new (MemF::Fast) string_table());

        noir_header header;
        fileRead(pFile, &header, sizeof(noir_header));

        if (header.magic != make_magic(MAGIC))
        {
            NE_LOG("Wrong Magic number. Expecte 'NOIR', got '%c%c%c%c'",
                   to<char>((header.magic >> 24) & 0xFF),
                   to<char>((header.magic >> 16) & 0xFF),
                   to<char>((header.magic >> 8) & 0xFF),
                   to<char>((header.magic >> 0) & 0xFF));

            return mtl::make_error<string_table_ptr, error_code>(error_code::MISSING_HEADER);
        }

        if (header.version > SUPPORTED_VERSION)
        {
            NE_LOG("Unsupported version %u (expected %u)", header.version, SUPPORTED_VERSION);

            return mtl::make_error<string_table_ptr, error_code>(error_code::VERSION_NOT_SUPPORTED);
        }

        if (header.languageId >= to<uint32_t>(supported_languages::LAST_LANGUAGE))
        {
            NE_LOG("Unsupported language id %u", header.languageId);

            return mtl::make_error<string_table_ptr, error_code>(error_code::UNSUPPORTED_LANGUAGE);
        }

        string_chunk_header string_header;
        fileRead(pFile, &string_header, sizeof(string_chunk_header));
        if (string_header.chunkName != make_magic(STRING_CHUNK))
        {
            NE_LOG("Unexpected chunk '%c%c%c%c'. Expected 'STRG'",
                   to<char>((string_header.chunkName >> 24) & 0xFF),
                   to<char>((string_header.chunkName >> 16) & 0xFF),
                   to<char>((string_header.chunkName >> 8) & 0xFF),
                   to<char>((string_header.chunkName >> 0) & 0xFF));

            return mtl::make_error<string_table_ptr, string_table::error_code>(
                string_table::error_code::MISSING_STRING_HEADER);
        }

        pTable->_offsets.reserve(string_header.stringCount);
        pTable->_pData = new (mtl::MemF::Fast) uint8_t[string_header.dataSize];
        fileRead(pFile, pTable->_pData, string_header.dataSize);

        auto pCurrent = mtl::force_to<char*>(pTable->_pData);

        for (size_t index = 0; index < string_header.stringCount; ++index)
        {
            pTable->_offsets.push_back(pCurrent);
            auto string = bstr_view::from_bstr(mtl::force_to<void*>(pCurrent));
            pCurrent += (sizeof(uint32_t) + string.length());
        }

        return mtl::make_success<string_table_ptr, error_code>(mtl::move(pTable));
    }

}  // namespace NEONengine