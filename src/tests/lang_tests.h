#ifndef __LANG_TESTS_H__INCLUDED__
#define __LANG_TESTS_H__INCLUDED__

#ifdef ACE_TEST_RUNNER

#include <ace/managers/memory.h>
#include <string.h>

#include "utils/bstr.h"
#include "core/lang.h"

#include "test_macros.h"

namespace NEONengine::tests
{
    TEST_IMPL(test_lang_load_invalid_file)
    {
        LanguageCode code = langLoad("nonexistent.noir");
        TEST_ASSERT(code == LC_ERROR, "Loading a nonexistent file should return LC_ERROR");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_lang_load_valid_file)
    {
        LanguageCode code = langLoad("data/lang/test.noir");
        TEST_ASSERT(code == LC_EN, "Loading English loc file should return LC_EN");
        langDestroy();
        TEST_SUCCESS;
    }

    TEST_IMPL(test_lang_get_string_by_id)
    {
        LanguageCode code = langLoad("data/lang/test.noir");
        TEST_ASSERT(code == LC_EN, "Loading English loc file should return LC_EN");

        Bstring str = langGetStringById(0);
        TEST_ASSERT(str != NULL, "String with ID 0 should not be NULL");
        TEST_ASSERT(strcmp(bstrGetData(str), "Hello World!") == 0, "String with ID 0 does not match expected value");

        langDestroy();
        TEST_SUCCESS;
    }

    TEST_IMPL(test_lang_get_long_string_by_id)
    {
        LanguageCode code = langLoad("data/lang/test.noir");
        TEST_ASSERT(code == LC_EN, "Loading English loc file should return LC_EN");

        Bstring str = langGetStringById(0);
        TEST_ASSERT(str != NULL, "String with ID 0 should not be NULL");
        TEST_ASSERT(strcmp(bstrGetData(str), "What do we do with a drunken sailor, what do we do with a drunken sailor, what do we do with a drunken sailor early in the morning?") == 0, "String with ID 0 does not match expected value");

        langDestroy();
        TEST_SUCCESS;
    }

    TEST_SUITE_BEGIN(lang)
    TEST(test_lang_load_invalid_file)
    TEST(test_lang_load_valid_file)
    TEST(test_lang_get_string_by_id)
    TEST_SUITE_END
}

#endif // #ifdef ACE_TEST_RUNNER

#endif // __ARRAY_TESTS_H__INCLUDED__
