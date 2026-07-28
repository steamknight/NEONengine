#ifndef __MTL_EXPECTED_TESTS_H__INCLUDED__
#define __MTL_EXPECTED_TESTS_H__INCLUDED__

#include "test_macros.h"
#include <mtl/expected.h>

// Simple string comparison helper for tests
constexpr bool str_equal(const char* a, const char* b) {
    if (a == nullptr || b == nullptr) return a == b;
    while (*a && *b && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

// Test basic construction
TEST_IMPL(test_expected_value_construction)
{
    mtl::expected<int, char const*> result(42);
    TEST_ASSERT(result.has_value(), "Value constructed expected should have value");
    TEST_ASSERT(result, "Value constructed expected should be truthy");
    TEST_ASSERT(result.value() == 42, "Value should be accessible");
    TEST_SUCCESS;
}

TEST_IMPL(test_error_construction_tag)
{
    mtl::expected<int, char const*> result(mtl::expected<int, char const*>::unexpect, "error");
    TEST_ASSERT(!result.has_value(), "Error constructed expected should not have value");
    TEST_ASSERT(!result, "Error constructed expected should be falsy");
    TEST_ASSERT(str_equal(result.error(), "error"), "Error should be accessible");
    TEST_SUCCESS;
}

TEST_IMPL(test_unexpected_construction)
{
    auto error_val = mtl::unexpected("failed");
    mtl::expected<int, char const*> result(error_val);
    TEST_ASSERT(!result.has_value(), "unexpected constructed expected should not have value");
    TEST_ASSERT(!result, "unexpected constructed expected should be falsy");
    TEST_ASSERT(str_equal(result.error(), "failed"), "Error should be accessible");
    TEST_SUCCESS;
}

TEST_IMPL(test_unexpected_rvalue_construction)
{
    mtl::expected<int, char const*> result(mtl::unexpected("failed"));
    TEST_ASSERT(!result.has_value(), "unexpected rvalue constructed expected should not have value");
    TEST_ASSERT(!result, "unexpected rvalue constructed expected should be falsy");
    TEST_ASSERT(str_equal(result.error(), "failed"), "Error should be accessible");
    TEST_SUCCESS;
}

// Test copy construction
TEST_IMPL(test_copy_construction_value)
{
    mtl::expected<int, char const*> result1(42);
    mtl::expected<int, char const*> result2(result1);
    TEST_ASSERT(result2.has_value(), "Copy constructed value expected should have value");
    TEST_ASSERT(result2.value() == 42, "Copy constructed value should match");
    TEST_SUCCESS;
}

TEST_IMPL(test_copy_construction_error)
{
    mtl::expected<int, char const*> result1(mtl::unexpected("error"));
    mtl::expected<int, char const*> result2(result1);
    TEST_ASSERT(!result2.has_value(), "Copy constructed error expected should not have value");
    TEST_ASSERT(str_equal(result2.error(), "error"), "Copy constructed error should match");
    TEST_SUCCESS;
}

// Test assignment
TEST_IMPL(test_value_assignment)
{
    mtl::expected<int, char const*> result(mtl::unexpected("error"));
    result = 42;
    TEST_ASSERT(result.has_value(), "Value assignment should create value");
    TEST_ASSERT(result.value() == 42, "Assigned value should be correct");
    TEST_SUCCESS;
}

TEST_IMPL(test_unexpected_assignment)
{
    mtl::expected<int, char const*> result(42);
    result = mtl::unexpected("error");
    TEST_ASSERT(!result.has_value(), "unexpected assignment should clear value");
    TEST_ASSERT(str_equal(result.error(), "error"), "Assigned error should be correct");
    TEST_SUCCESS;
}

TEST_IMPL(test_copy_assignment_value)
{
    mtl::expected<int, char const*> result1(42);
    mtl::expected<int, char const*> result2(mtl::unexpected("error"));
    result2 = result1;
    TEST_ASSERT(result2.has_value(), "Copy assignment of value should work");
    TEST_ASSERT(result2.value() == 42, "Copy assigned value should match");
    TEST_SUCCESS;
}

TEST_IMPL(test_copy_assignment_error)
{
    mtl::expected<int, char const*> result1(mtl::unexpected("error"));
    mtl::expected<int, char const*> result2(42);
    result2 = result1;
    TEST_ASSERT(!result2.has_value(), "Copy assignment of error should work");
    TEST_ASSERT(str_equal(result2.error(), "error"), "Copy assigned error should match");
    TEST_SUCCESS;
}

// Test swap functionality
TEST_IMPL(test_swap_both_values)
{
    mtl::expected<int, char const*> result1(42);
    mtl::expected<int, char const*> result2(99);
    
    result1.swap(result2);
    
    TEST_ASSERT(result1.has_value(), "Swap should maintain value presence");
    TEST_ASSERT(result2.has_value(), "Swap should maintain value presence");
    TEST_ASSERT(result1.value() == 99, "Swap should exchange values");
    TEST_ASSERT(result2.value() == 42, "Swap should exchange values");
    TEST_SUCCESS;
}

TEST_IMPL(test_swap_both_errors)
{
    mtl::expected<int, char const*> result1(mtl::unexpected("error1"));
    mtl::expected<int, char const*> result2(mtl::unexpected("error2"));
    
    result1.swap(result2);
    
    TEST_ASSERT(!result1.has_value(), "Swap should maintain error presence");
    TEST_ASSERT(!result2.has_value(), "Swap should maintain error presence");
    TEST_ASSERT(str_equal(result1.error(), "error2"), "Swap should exchange errors");
    TEST_ASSERT(str_equal(result2.error(), "error1"), "Swap should exchange errors");
    TEST_SUCCESS;
}

TEST_IMPL(test_swap_value_error)
{
    mtl::expected<int, char const*> result1(42);
    mtl::expected<int, char const*> result2(mtl::unexpected("error"));
    
    result1.swap(result2);
    
    TEST_ASSERT(!result1.has_value(), "Swap should convert value to error");
    TEST_ASSERT(result2.has_value(), "Swap should convert error to value");
    TEST_ASSERT(str_equal(result1.error(), "error"), "Swap should transfer error");
    TEST_ASSERT(result2.value() == 42, "Swap should transfer value");
    TEST_SUCCESS;
}

// Test with custom types
TEST_IMPL(test_custom_value_type)
{
    struct Point { int x, y; };
    
    mtl::expected<Point, int> result(Point{10, 20});
    TEST_ASSERT(result.has_value(), "Custom type value expected should work");
    TEST_ASSERT(result.value().x == 10, "Custom type value access should work");
    TEST_ASSERT(result.value().y == 20, "Custom type value access should work");
    TEST_SUCCESS;
}

TEST_IMPL(test_custom_error_type)
{
    struct ErrorInfo { int code; char const* message; };
    
    mtl::expected<int, ErrorInfo> result(mtl::unexpected(ErrorInfo{404, "Not Found"}));
    TEST_ASSERT(!result.has_value(), "Custom error type should work");
    TEST_ASSERT(result.error().code == 404, "Custom error access should work");
    TEST_ASSERT(str_equal(result.error().message, "Not Found"), "Custom error access should work");
    TEST_SUCCESS;
}

// Test unexpected helper functions
TEST_IMPL(test_unexpected_helper_lvalue)
{
    char const* error_msg = "failed";
    auto unexpected_result = mtl::unexpected(error_msg);
    
    mtl::expected<int, char const*> result(unexpected_result);
    TEST_ASSERT(!result.has_value(), "unexpected helper with lvalue should work");
    TEST_ASSERT(str_equal(result.error(), "failed"), "unexpected helper should preserve error");
    TEST_SUCCESS;
}

TEST_IMPL(test_unexpected_helper_rvalue)
{
    auto unexpected_result = mtl::unexpected("failed");
    
    mtl::expected<int, char const*> result(unexpected_result);
    TEST_ASSERT(!result.has_value(), "unexpected helper with rvalue should work");
    TEST_ASSERT(str_equal(result.error(), "failed"), "unexpected helper should preserve error");
    TEST_SUCCESS;
}

// Test with enum error codes
TEST_IMPL(test_enum_error_codes)
{
    enum class Error { NONE, FILE_NOT_FOUND, PERMISSION_DENIED };
    
    mtl::expected<int, Error> success_result(42);
    mtl::expected<int, Error> error_result(mtl::unexpected(Error::FILE_NOT_FOUND));
    
    TEST_ASSERT(success_result.has_value(), "Enum error success should work");
    TEST_ASSERT(success_result.value() == 42, "Enum error success value should be correct");
    
    TEST_ASSERT(!error_result.has_value(), "Enum error failure should work");
    TEST_ASSERT(error_result.error() == Error::FILE_NOT_FOUND, "Enum error value should be correct");
    TEST_SUCCESS;
}

// Test constexpr functionality 
TEST_IMPL(test_expected_constexpr_functionality)
{
    constexpr auto make_success = [](int x) -> mtl::expected<int, char const*> { 
        return mtl::expected<int, char const*>(x); 
    };
    constexpr auto make_error = [](char const* err) -> mtl::expected<int, char const*> { 
        return mtl::expected<int, char const*>(mtl::unexpected(err)); 
    };
    
    auto success_result = make_success(42);
    auto error_result = make_error("failed");
    
    TEST_ASSERT(success_result.has_value(), "Constexpr success construction should work");
    TEST_ASSERT(success_result.value() == 42, "Constexpr success value should be correct");
    
    TEST_ASSERT(!error_result.has_value(), "Constexpr error construction should work");
    TEST_ASSERT(str_equal(error_result.error(), "failed"), "Constexpr error value should be correct");
    TEST_SUCCESS;
}

// Test boolean conversion
TEST_IMPL(test_boolean_conversion)
{
    mtl::expected<int, char const*> success_result(42);
    mtl::expected<int, char const*> error_result(mtl::unexpected("failed"));
    
    TEST_ASSERT(static_cast<bool>(success_result), "Success result should be truthy");
    TEST_ASSERT(!static_cast<bool>(error_result), "Error result should be falsy");
    
    // Test in conditional context
    bool success_condition = false;
    bool error_condition = false;
    
    if (success_result) { success_condition = true; }
    if (error_result) { } else { error_condition = true; }
    
    TEST_ASSERT(success_condition, "Success result should work in if condition");
    TEST_ASSERT(error_condition, "Error result should work in if condition");
    TEST_SUCCESS;
}

// Test self-assignment safety
TEST_IMPL(test_self_assignment)
{
    mtl::expected<int, char const*> result(42);
    result = result;  // Self-assignment
    
    TEST_ASSERT(result.has_value(), "Self-assignment should preserve value");
    TEST_ASSERT(result.value() == 42, "Self-assignment should preserve value correctly");
    TEST_SUCCESS;
}

TEST_IMPL(test_self_swap)
{
    mtl::expected<int, char const*> result(42);
    result.swap(result);  // Self-swap
    
    TEST_ASSERT(result.has_value(), "Self-swap should preserve value");
    TEST_ASSERT(result.value() == 42, "Self-swap should preserve value correctly");
    TEST_SUCCESS;
}

// Test move semantics (as much as we can without std::move)
TEST_IMPL(test_rvalue_construction)
{
    struct MoveTracker { 
        int value; 
        MoveTracker(int v) : value(v) {}
    };
    
    mtl::expected<MoveTracker, int> result(MoveTracker{42});
    TEST_ASSERT(result.has_value(), "Rvalue construction should work");
    TEST_ASSERT(result.value().value == 42, "Rvalue constructed value should be correct");
    TEST_SUCCESS;
}

TEST_SUITE_BEGIN(mtl_expected)
    TEST(test_expected_value_construction)
    TEST(test_error_construction_tag)
    TEST(test_unexpected_construction)
    TEST(test_unexpected_rvalue_construction)
    TEST(test_copy_construction_value)
    TEST(test_copy_construction_error)
    TEST(test_value_assignment)
    TEST(test_unexpected_assignment)
    TEST(test_copy_assignment_value)
    TEST(test_copy_assignment_error)
    TEST(test_swap_both_values)
    TEST(test_swap_both_errors)
    TEST(test_swap_value_error)
    TEST(test_custom_value_type)
    TEST(test_custom_error_type)
    TEST(test_unexpected_helper_lvalue)
    TEST(test_unexpected_helper_rvalue)
    TEST(test_enum_error_codes)
    TEST(test_expected_constexpr_functionality)
    TEST(test_boolean_conversion)
    TEST(test_self_assignment)
    TEST(test_self_swap)
    TEST(test_rvalue_construction)
TEST_SUITE_END

#endif // __MTL_EXPECTED_TESTS_H__INCLUDED__
