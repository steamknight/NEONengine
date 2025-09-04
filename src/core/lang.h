#ifndef __LANG_H__INCLUDED__
#define __LANG_H__INCLUDED__

#include <ace/types.h>

#include "utils/bstr.h"

namespace NEONengine
{
    constexpr int LOC_SUPPORTED_VERSION = 1;

    /**
     * @brief Defines the start and end indices for a word in a string.
     */
    struct NeonWord
    {
        UWORD uwStartIdx;
        UWORD uwEndIdx;
    };

    /**
     * @brief All the words that form a string. This should not be altered.
     * ulSize is the number of words in the string.
     * pWords is an array of words,
     */
    typedef struct _NeonWordList
    {
        ULONG ulSize;
        NeonWord pWords[];
    } NeonWordList;

    /**
     * @brief An id defining which language is defined in the language pack. If
     * there's a error reading the language pack, LC_ERROR is returned instead.
     */
    typedef enum _LanguageCode
    {
        LC_ERROR = 0,
        LC_EN = 1,
        LC_IT = 2,
        LC_DE = 3,
    } LanguageCode;

    /**
     * @brief Loads a language pack.
     * 
     * @param szFilePath The file path to the language pack.
     * @return LanguageCode The language in the pack, or LC_ERROR if there's a
     * problem
     * 
     * @see langDestroy()
     * @see LanaguageCode
     */
    LanguageCode langLoad(const char *szFilePath);

    /**
     * @brief Unloads the language pack from memory.
     * 
     * @see langLoad()
     */
    void langDestroy();

    /**
     * @brief Get a string by id.
     * 
     * @param uwStringId Id if the string to retrieve.
     * @return const NeonString* The requested string.
     */
    Bstring langGetStringById(UWORD uwStringId);

    /**
     * @brief Get all the words in a string specified by id.
     * 
     * @param uwStringId Id if the string to retrieve.
     * @return const NeonWordList* The requested words.
     */
    const NeonWordList *langGetStringWordsById(UWORD uwStringId);
}

#endif // __LANG_H__INCLUDED__
