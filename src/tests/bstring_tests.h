#ifndef __BSTRING_TESTS_H__INCLUDED__
#define __BSTRING_TESTS_H__INCLUDED__

#ifdef ACE_TEST_RUNNER

#include <ace/managers/memory.h>
#include <string.h>
#include "utils/bstr.h"
#include "test_macros.h"

TEST_IMPL(test_bstr_create_basic)
{
    const char *text = "Hello, World!";
    Bstring bstr = bstrCreate(text, MEMF_FAST);
    TEST_ASSERT(bstr, "Failed to allocate Bstring");
    TEST_ASSERT(bstrLength(bstr) == strlen(text), "Wrong string length");
    TEST_ASSERT(strcmp(bstrGetData(bstr), text) == 0, "String contents do not match");
    bstrDestroy(&bstr);
    TEST_ASSERT(bstr == NULL, "Bstring not deallocated");
    TEST_SUCCESS;
}

TEST_IMPL(test_bstr_copy)
{
    const char *text = "Copy me!";
    Bstring src = bstrCreate(text, MEMF_FAST);
    Bstring dst = bstrCreate("12345678", MEMF_FAST);
    bstrCopy(src, dst);
    TEST_ASSERT(bstrCompare(src, dst) == 0, "Copied string does not match");
    bstrDestroy(&src);
    bstrDestroy(&dst);
    TEST_SUCCESS;
}

TEST_IMPL(test_bstr_concat)
{
    Bstring lhs = bstrCreate("Hello, ", MEMF_FAST);
    Bstring rhs = bstrCreate("World!", MEMF_FAST);
    Bstring result = bstrConcat(lhs, rhs, MEMF_FAST);
    TEST_ASSERT(strcmp(bstrGetData(result), "Hello, World!") == 0, "Concatenated string is incorrect");
    bstrDestroy(&lhs);
    bstrDestroy(&rhs);
    bstrDestroy(&result);
    TEST_SUCCESS;
}

TEST_IMPL(test_bstr_compare)
{
    Bstring a = bstrCreate("abc", MEMF_FAST);
    Bstring b = bstrCreate("abc", MEMF_FAST);
    Bstring c = bstrCreate("abd", MEMF_FAST);
    TEST_ASSERT(bstrCompare(a, b) == 0, "Equal strings not equal");
    TEST_ASSERT(bstrCompare(a, c) < 0, "Comparison failed (a < c)");
    TEST_ASSERT(bstrCompare(c, a) > 0, "Comparison failed (c > a)");
    bstrDestroy(&a);
    bstrDestroy(&b);
    bstrDestroy(&c);
    TEST_SUCCESS;
}

TEST_IMPL(test_bstr_destroy_null)
{
    Bstring bstr = NULL;
    bstrDestroy(&bstr); // Should not crash
    TEST_ASSERT(bstr == NULL, "Destroying NULL Bstring did not leave pointer NULL");
    TEST_SUCCESS;
}

// Edge case: Empty string
TEST_IMPL(test_bstr_empty_string)
{
    Bstring bstr = bstrCreate("", MEMF_FAST);
    TEST_ASSERT(bstr, "Failed to allocate empty Bstring");
    TEST_ASSERT(bstrLength(bstr) == 0, "Empty Bstring length should be 0");
    TEST_ASSERT(strcmp(bstrGetData(bstr), "") == 0, "Empty Bstring buffer should be empty string");
    bstrDestroy(&bstr);
    TEST_SUCCESS;
}

// Edge case: Self copy
TEST_IMPL(test_bstr_self_copy)
{
    Bstring bstr = bstrCreate("self", MEMF_FAST);
    bstrCopy(bstr, bstr);
    TEST_ASSERT(strcmp(bstrGetData(bstr), "self") == 0, "Self copy should not change string");
    bstrDestroy(&bstr);
    TEST_SUCCESS;
}

// Edge case: Concat with empty string
TEST_IMPL(test_bstr_concat_with_empty)
{
    Bstring empty = bstrCreate("", MEMF_FAST);
    Bstring nonempty = bstrCreate("abc", MEMF_FAST);
    Bstring res1 = bstrConcat(empty, nonempty, MEMF_FAST);
    Bstring res2 = bstrConcat(nonempty, empty, MEMF_FAST);
    TEST_ASSERT(strcmp(bstrGetData(res1), "abc") == 0, "Empty + nonempty concat failed");
    TEST_ASSERT(strcmp(bstrGetData(res2), "abc") == 0, "Nonempty + empty concat failed");
    bstrDestroy(&empty);
    bstrDestroy(&nonempty);
    bstrDestroy(&res1);
    bstrDestroy(&res2);
    TEST_SUCCESS;
}

// Edge case: Destroy already destroyed string
TEST_IMPL(test_bstr_destroy_twice)
{
    Bstring bstr = bstrCreate("twice", MEMF_FAST);
    bstrDestroy(&bstr);
    bstrDestroy(&bstr); // Should not crash
    TEST_ASSERT(bstr == NULL, "Destroying twice should leave pointer NULL");
    TEST_SUCCESS;
}

// Edge case: Very long string
TEST_IMPL(test_bstr_very_long_string)
{
    char longstr[2049];
    for (int i = 0; i < 2048; ++i)
        longstr[i] = 'A';
    longstr[2048] = '\0';
    Bstring bstr = bstrCreate(longstr, MEMF_FAST);
    TEST_ASSERT(bstr, "Failed to allocate long Bstring");
    TEST_ASSERT(bstrLength(bstr) == 2048, "Long Bstring length incorrect");
    TEST_ASSERT(strcmp(bstrGetData(bstr), longstr) == 0, "Long Bstring contents incorrect");
    bstrDestroy(&bstr);
    TEST_SUCCESS;
}

// Edge case: CopyN with count = 0
TEST_IMPL(test_bstr_copy_n_zero)
{
    Bstring src = bstrCreate("abcdef", MEMF_FAST);
    Bstring dst = bstrCreate("123456", MEMF_FAST);
    ULONG originalLen = bstrLength(dst);
    bstrCopyN(src, dst, 0);
    TEST_ASSERT(bstrLength(dst) == originalLen, "CopyN zero should not change destination length");
    bstrDestroy(&src);
    bstrDestroy(&dst);
    TEST_SUCCESS;
}

// Edge case: Compare with NULL (should assert or handle gracefully)
// This test is commented out because it will likely assert and stop the test runner.
/*
TEST_IMPL(test_bstr_compare_with_null)
{
    Bstring a = bstrCreate("abc", MEMF_FAST);
    int cmp = bstrCompare(a, NULL);
    // Should not reach here if assert fires
    TEST_ASSERT(cmp != 0, "Compare with NULL should not be equal");
    bstrDestroy(&a);
    TEST_SUCCESS;
}
*/

TEST_SUITE_BEGIN(bstring)
TEST(test_bstr_create_basic)
TEST(test_bstr_copy)
TEST(test_bstr_concat)
TEST(test_bstr_compare)
TEST(test_bstr_destroy_null)
TEST(test_bstr_empty_string)
TEST(test_bstr_self_copy)
TEST(test_bstr_concat_with_empty)
TEST(test_bstr_destroy_twice)
TEST(test_bstr_very_long_string)
TEST(test_bstr_copy_n_zero)
// TEST(test_bstr_compare_with_null) // Uncomment if you want to test assert behavior
TEST_SUITE_END

#endif // #ifdef ACE_TEST_RUNNER

#endif // __BSTRING_TESTS_H__INCLUDED__
