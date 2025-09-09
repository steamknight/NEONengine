/**
 * @file string_table.h
 * @brief String table for localized resource lookup.
 *
 * Provides a container for loading and accessing localized strings from a file or file descriptor.
 * Uses bstr_view for efficient string access and mtl::expected for error handling.
 */

#ifndef __STRING_TABLE__INCLUDED__
#define __STRING_TABLE__INCLUDED__

#include <stdint.h>

#include <ace/utils/file.h>

#include <mtl/expected.h>
#include <mtl/memory.h>
#include <mtl/vector.h>

#include "utils/bstr_view.h"

namespace NEONengine
{
    /**
     * @class string_table
     * @brief Table of localized strings loaded from file.
     *
     * Usage:
     * @code
     * auto result = string_table::create_from_file("lang/en.noir");
     * if (result) { auto str = result.value()->get_string(42); }
     * @endcode
     */
    class string_table;
    using string_table_ptr = mtl::unique_ptr<string_table>;

    class string_table
    {
        public
            :  ////////////////////////////////////////////////////////////////////////////////////
        /**
         * @brief Error codes for string_table operations.
         */
        enum class error_code
        {
            MISSING_HEADER,
            VERSION_NOT_SUPPORTED,
            UNSUPPORTED_LANGUAGE,
            MISSING_STRING_HEADER,
        };

        /**
         * @brief Supported language IDs.
         */
        enum class supported_languages
        {
            EN,
            IT,
            DE,
            LAST_LANGUAGE
        };

        using result = mtl::expected<string_table_ptr, string_table::error_code>;

        public:  ///////////////////////////////////////////////////////////////////////////////////
        /**
         * @brief Default constructor.
         */
        string_table() = default;

        ~string_table();

        NO_COPY(string_table)
        USE_DEFAULT_MOVE(string_table)

        /**
         * @brief Get a string by its ID.
         * @param id String identifier.
         * @return bstr_view of the string, or empty if not found.
         */
        bstr_view const get_string(uint32_t id) const;

        /**
         * @brief Create a string_table from a file path.
         * @param szFilePath Path to the file.
         * @return result (success: string_table_ptr, error: error_code)
         */
        static result create_from_file(char const* szFilePath);

        /**
         * @brief Create a string_table from a file descriptor.
         * @param pFile Pointer to tFile.
         * @return result (success: string_table_ptr, error: error_code)
         */
        static result create_from_fd(tFile* pFile);

        private:  //////////////////////////////////////////////////////////////////////////////////
        /**
         * @brief Offsets to each string in the data block.
         */
        mtl::vector<void*> _offsets;
        /**
         * @brief Raw data block containing all strings.
         */
        uint8_t* _pData{ nullptr };
    };
}  // namespace NEONengine

#endif  // __STRING_TABLE__INCLUDED__