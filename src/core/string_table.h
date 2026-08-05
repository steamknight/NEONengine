/**
 * @file string_table.h
 * @brief One language's strings, loaded from its `.noir` file.
 *
 * The on-disk contract is owned by `avNEONedit/docs/game_data_format.md`; the
 * layout this reader implements is restated in string_table.cpp.
 */

#ifndef __STRING_TABLE__INCLUDED__
#define __STRING_TABLE__INCLUDED__

#include "types.h"

#include <stdint.h>

#include <ace++/file.h>

#include <mtl/expected.h>
#include <mtl/memory.h>
#include <mtl/utility.h>
#include <mtl/vector.h>

#include "utils/bstr_view.h"

namespace NEONengine
{
    /**
     * @brief The languages the version 2 format assigns a fixed id.
     *
     * The ids are part of the file contract, so an enumerator is both the
     * `.noir` language id and the language's index.
     */
    enum class language : u16
    {
        EN,
        IT,
        DE,
        COUNT
    };

    /**
     * @brief The lowercase two-letter code naming @p lang's `.noir` file.
     *
     * @param lang A language below language::COUNT.
     * @return The code, or "en" when @p lang is out of range.
     */
    char const* language_code(language lang) noexcept;

    /**
     * @class string_table
     * @brief Table of one language's strings, addressed by id.
     *
     * String 0 is by convention the language's own name written in that
     * language, so a language menu can label every choice without the engine
     * carrying any localized text of its own.
     *
     * Usage:
     * @code
     * auto result = string_table::create(language::EN);
     * if (result) { auto str = result.value()->get_string(42); }
     * @endcode
     */
    class string_table;
    using string_table_ptr = mtl::unique_ptr<string_table>;

    class string_table
    {
        public:  ///////////////////////////////////////////////////////////////////////////////////
        /**
         * @brief Error codes for string_table operations.
         */
        enum class error_code
        {
            FILE_NOT_FOUND,
            MISSING_HEADER,
            VERSION_NOT_SUPPORTED,
            UNSUPPORTED_LANGUAGE,
            LANGUAGE_MISMATCH,
            MALFORMED_HEADER,
            MALFORMED_PAYLOAD,
            CHECKSUM_MISMATCH,
        };

        using result = mtl::expected<string_table_ptr, error_code>;

        /**
         * @brief The id holding the language's own name.
         */
        static constexpr u32 LANGUAGE_NAME_ID = 0;

        /**
         * @brief The id holding the dialogue's "More..." choice.
         *
         * Reserved by the editor immediately after the language name, so the
         * engine can put the choice that rotates to the next choice set on
         * screen in the player's language without carrying any text of its own.
         */
        static constexpr u32 MORE_CHOICES_ID = 1;

        /**
         * @brief Longest language name @ref peek_language_name will report.
         */
        static constexpr u32 MAX_LANGUAGE_NAME = 32;

        public:  ///////////////////////////////////////////////////////////////////////////////////
        string_table() = default;

        NO_COPY(string_table)
        USE_DEFAULT_MOVE(string_table)

        /**
         * @brief Get a string by its id.
         *
         * @param id String identifier.
         * @return A view of the string, or an empty view when @p id is unknown.
         */
        bstr_view const get_string(u32 id) const;

        /**
         * @brief The number of strings in the table.
         */
        u16 size() const noexcept { return _string_count; }

        /**
         * @brief The language this table holds.
         */
        language language_id() const noexcept { return _language; }

        /**
         * @brief The UTC build period, decimal YYMM, that produced this file.
         *
         * Every file of one build shares this with the `.neon`, so the engine
         * uses it to reject a stale table whose string ids no longer line up.
         */
        u16 build_period() const noexcept { return _build_period; }

        /**
         * @brief The monotonic project build number that produced this file.
         *
         * @see build_period()
         */
        u32 build_number() const noexcept { return _build_number; }

        /**
         * @brief The language's own name, written in that language.
         *
         * @see LANGUAGE_NAME_ID
         */
        bstr_view const language_name() const { return get_string(LANGUAGE_NAME_ID); }

        /**
         * @brief The "More..." choice, written in this table's language.
         *
         * @see MORE_CHOICES_ID
         */
        bstr_view const more_choices() const { return get_string(MORE_CHOICES_ID); }

        /**
         * @brief Load @p lang from "data/lang/<code>.noir".
         *
         * @param lang The language to load.
         * @return result (success: string_table_ptr, error: error_code)
         */
        static result create(language lang);

        /**
         * @brief Load a table from a file path.
         *
         * @param szFilePath Path to the `.noir` file.
         * @return result (success: string_table_ptr, error: error_code)
         */
        static result create_from_file(char const* szFilePath);

        /**
         * @brief Load a table from an open file, which is closed either way.
         *
         * @param pFile The file to read.
         * @return result (success: string_table_ptr, error: error_code)
         */
        static result create_from_fd(ace::file_ptr&& pFile);

        /**
         * @brief Read just @p lang's name, without loading its whole table.
         *
         * Reads only the header and the first record so a language menu can
         * label every installed language without holding several tables in
         * memory at once.
         *
         * @param lang The language to name.
         * @param pBuffer Destination, always null-terminated on success.
         * @param capacity Size of @p pBuffer including the null terminator.
         * @return Name length in bytes, or 0 when @p lang is unavailable.
         */
        static u32 peek_language_name(language lang, char* pBuffer, u32 capacity);

        private:  //////////////////////////////////////////////////////////////////////////////////
        /**
         * @brief Pointer to each string's length prefix within @ref _payload.
         */
        mtl::vector<void*> _offsets;
        /**
         * @brief Every string record of the file, back to back.
         */
        mtl::vector<u8> _payload;
        u16 _string_count{ 0 };
        u16 _build_period{ 0 };
        u32 _build_number{ 0 };
        language _language{ language::EN };
    };
}  // namespace NEONengine

#endif  // __STRING_TABLE__INCLUDED__
