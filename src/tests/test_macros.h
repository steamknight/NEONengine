#ifndef __TEST_MACROS_H__INCLUDED__
#define __TEST_MACROS_H__INCLUDED__

#define TEST_IMPL(fn) char const* fn()

typedef struct _Test {
    char const* name;
    char const* (*test_fn)(void);
} Test;

#define TEST_SUITE_BEGIN(test_suite) \
    char const* test_suite ## _name = #test_suite; \
    Test test_suite ## _tests[] = {

#define TEST(test_name) \
    { .name = #test_name, .test_fn = test_name },

#define TEST_SUITE_END };

#define TEST_ASSERT(expr, message) \
    if ((!(expr))) return message; \
    else {}

#define TEST_SUCCESS return (char const*)0
#define TEST_FAIL(reason) return reason

#endif // __TEST_MACROS_H__INCLUDED__