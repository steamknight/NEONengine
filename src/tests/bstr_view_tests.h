#ifndef __BSTR_VIEW_TESTS_H__INCLUDED__
#define __BSTR_VIEW_TESTS_H__INCLUDED__

#ifdef ACE_TEST_RUNNER

#include "utils/bstr_view.h"
#include "test_macros.h"
#include <mini_std/string.h>

namespace NEONengine {

TEST_IMPL(test_bstr_view_default_construct)
{
    bstr_view v;
    TEST_ASSERT(v.size() == 0, "default size not zero");
    TEST_ASSERT(v.empty(), "default not empty");
    TEST_ASSERT(v.data()[0] == '\0', "default data not empty string");
    TEST_SUCCESS;
}

TEST_IMPL(test_bstr_view_from_literal)
{
    bstr_view v("Hello");
    TEST_ASSERT(v.size() == 5, "literal size wrong");
    TEST_ASSERT(!v.empty(), "literal seen as empty");
    TEST_ASSERT(v[0] == 'H', "first char mismatch");
    TEST_ASSERT(v[4] == 'o', "last char mismatch");
    const char* c = v; // implicit conversion
    TEST_ASSERT(c[1] == 'e', "conversion mismatch");
    TEST_SUCCESS;
}

TEST_IMPL(test_bstr_view_from_cstring_mutable)
{
    char buf[6] = {'H','e','l','l','o','\0'};
    bstr_view v(buf);
    TEST_ASSERT(v.size() == 5, "mutable cstring size wrong");
    char* m = (char*)v; // non-const conversion
    m[0] = 'h';
    TEST_ASSERT(v[0] == 'h', "write through view failed");
    TEST_SUCCESS;
}

TEST_IMPL(test_bstr_view_from_ptr_and_len)
{
    const char text[] = {'A','B','C','D','E','F'}; // no null required
    bstr_view v(text, 3); // only first 3 chars
    TEST_ASSERT(v.size() == 3, "ptr+len size wrong");
    TEST_ASSERT(v[0] == 'A' && v[2] == 'C', "ptr+len chars wrong");
    TEST_SUCCESS;
}

TEST_IMPL(test_bstr_view_compare)
{
    bstr_view a("abc");
    bstr_view b("abc");
    bstr_view c("abd");
    TEST_ASSERT(a == b, "equality failed");
    TEST_ASSERT(a.compare(c) < 0, "lex compare a < c failed");
    TEST_ASSERT(c.compare(a) > 0, "lex compare c > a failed");
    TEST_SUCCESS;
}

TEST_IMPL(test_bstr_view_from_bstr_header)
{
    // Build a fake bstr for "Hi" (length=2)
    unsigned char raw[4 + 2 + 1];
    raw[0] = 2; raw[1] = 0; raw[2] = 0; raw[3] = 0; // length 2 LE
    raw[4] = 'H'; raw[5] = 'i'; raw[6] = '\0';
    bstr_view v = bstr_view::from_bstr(raw);
    TEST_ASSERT(v.size() == 2, "bstr header size wrong");
    TEST_ASSERT(v[0] == 'H' && v[1] == 'i', "bstr header contents wrong");

    // Empty bstr (length=0)
    unsigned char emptyRaw[4 + 1];
    emptyRaw[0] = 0; emptyRaw[1] = 0; emptyRaw[2] = 0; emptyRaw[3] = 0; emptyRaw[4] = '\0';
    bstr_view e = bstr_view::from_bstr(emptyRaw);
    TEST_ASSERT(e.size() == 0 && e.empty(), "empty bstr not recognized");
    TEST_SUCCESS;
}

TEST_IMPL(test_bstr_view_iteration)
{
    bstr_view v("xyz");
    size_t count = 0;
    for (const char* p = v.begin(); p != v.end(); ++p) {
        ++count;
    }
    TEST_ASSERT(count == v.size(), "iterator traversal count mismatch");
    TEST_SUCCESS;
}

TEST_SUITE_BEGIN(bstr_view)
    TEST(test_bstr_view_default_construct)
    TEST(test_bstr_view_from_literal)
    TEST(test_bstr_view_from_cstring_mutable)
    TEST(test_bstr_view_from_ptr_and_len)
    TEST(test_bstr_view_compare)
    TEST(test_bstr_view_from_bstr_header)
    TEST(test_bstr_view_iteration)
TEST_SUITE_END

} // namespace NEONengine::tests

#endif // ACE_TEST_RUNNER

#endif // __BSTR_VIEW_TESTS_H__INCLUDED__
