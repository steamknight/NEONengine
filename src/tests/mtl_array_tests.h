#ifndef __MTL_ARRAY_TESTS_H__INCLUDED__
#define __MTL_ARRAY_TESTS_H__INCLUDED__

#ifdef ACE_TEST_RUNNER

#include <mtl/array.h>

#include "test_macros.h"

namespace NEONengine
{

    // Basic construction tests
    TEST_IMPL(test_array_default_construct)
    {
        mtl::array<int, 5> arr{};
        TEST_ASSERT(arr.size() == 5, "default array size wrong");
        // Note: default constructor leaves elements uninitialized for primitive types
        TEST_SUCCESS;
    }

    TEST_IMPL(test_array_fill_construct)
    {
        mtl::array<int, 6> arr;
        arr.fill(99);

        TEST_ASSERT(arr.size() == 6, "fill array size wrong");
        for (size_t i = 0; i < 6; ++i) { TEST_ASSERT(arr[i] == 99, "fill array element wrong"); }
        TEST_SUCCESS;
    }

    // Element access tests - avoiding at() method due to TRAP macro issues
    TEST_IMPL(test_array_element_access)
    {
        mtl::array<int, 3> arr;
        arr[0] = 100;
        arr[1] = 200;
        arr[2] = 300;

        TEST_ASSERT(arr[0] == 100, "operator[] access wrong at index 0");
        TEST_ASSERT(arr[1] == 200, "operator[] access wrong at index 1");
        TEST_ASSERT(arr[2] == 300, "operator[] access wrong at index 2");

        TEST_SUCCESS;
    }

    TEST_IMPL(test_array_element_modification)
    {
        mtl::array<int, 3> arr;
        arr.fill(0);  // Initialize all to 0

        arr[0] = 111;
        arr[1] = 222;
        arr[2] = 333;

        TEST_ASSERT(arr[0] == 111, "operator[] modification failed at index 0");
        TEST_ASSERT(arr[1] == 222, "operator[] modification failed at index 1");
        TEST_ASSERT(arr[2] == 333, "operator[] modification failed at index 2");

        TEST_SUCCESS;
    }

    TEST_IMPL(test_array_const_access)
    {
        mtl::array<int, 3> non_const_arr;
        non_const_arr[0] = 1;
        non_const_arr[1] = 2;
        non_const_arr[2] = 3;

        mtl::array<int, 3> const& arr = non_const_arr;

        TEST_ASSERT(arr[0] == 1, "const operator[] access wrong at index 0");
        TEST_ASSERT(arr[1] == 2, "const operator[] access wrong at index 1");
        TEST_ASSERT(arr[2] == 3, "const operator[] access wrong at index 2");

        TEST_SUCCESS;
    }

    // Iterator tests
    TEST_IMPL(test_array_iterators)
    {
        mtl::array<int, 4> arr;
        arr[0] = 5;
        arr[1] = 10;
        arr[2] = 15;
        arr[3] = 20;

        // Test begin/end
        TEST_ASSERT(arr.begin() == &arr[0], "begin() not pointing to first element");
        TEST_ASSERT(arr.end() == &arr[0] + 4, "end() not pointing past last element");

        // Test iteration
        int expected = 5;
        for (auto it = arr.begin(); it != arr.end(); ++it)
        {
            TEST_ASSERT(*it == expected, "iterator value wrong");
            expected += 5;
        }

        // Test const iterators
        mtl::array<int, 4> const& const_arr = arr;
        TEST_ASSERT(const_arr.begin() == &const_arr[0],
                    "const begin() not pointing to first element");
        TEST_ASSERT(const_arr.end() == &const_arr[0] + 4,
                    "const end() not pointing past last element");

        // Test range-based for loop
        expected = 5;
        for (auto const& value : arr)
        {
            TEST_ASSERT(value == expected, "range-based for loop value wrong");
            expected += 5;
        }

        TEST_SUCCESS;
    }

    // Size and capacity tests
    TEST_IMPL(test_array_size)
    {
        mtl::array<int, 10> arr1{};
        TEST_ASSERT(arr1.size() == 10, "size() wrong for array<int, 10>");

        mtl::array<double, 1> arr2;
        TEST_ASSERT(arr2.size() == 1, "size() wrong for array<double, 1>");

        mtl::array<char, 100> arr3;
        TEST_ASSERT(arr3.size() == 100, "size() wrong for array<char, 100>");

        // Size should be compile-time constant
        constexpr size_t size = arr1.size();
        TEST_ASSERT(size == 10, "size() not constexpr");

        TEST_SUCCESS;
    }

    // Fill tests
    TEST_IMPL(test_array_fill)
    {
        mtl::array<int, 5> arr{};

        arr.fill(42);
        for (size_t i = 0; i < 5; ++i)
        {
            TEST_ASSERT(arr[i] == 42, "fill() failed to set all elements");
        }

        arr.fill(-10);
        for (size_t i = 0; i < 5; ++i)
        {
            TEST_ASSERT(arr[i] == -10, "fill() failed to overwrite elements");
        }

        TEST_SUCCESS;
    }

    // Swap tests
    TEST_IMPL(test_array_swap)
    {
        mtl::array<int, 3> arr1;
        mtl::array<int, 3> arr2;

        arr1[0] = 1;
        arr1[1] = 2;
        arr1[2] = 3;
        arr2[0] = 10;
        arr2[1] = 20;
        arr2[2] = 30;

        arr1.swap(arr2);

        TEST_ASSERT(arr1[0] == 10 && arr1[1] == 20 && arr1[2] == 30,
                    "arr1 values wrong after swap");
        TEST_ASSERT(arr2[0] == 1 && arr2[1] == 2 && arr2[2] == 3, "arr2 values wrong after swap");

        // Swap back
        arr1.swap(arr2);
        TEST_ASSERT(arr1[0] == 1 && arr1[1] == 2 && arr1[2] == 3,
                    "arr1 values wrong after second swap");
        TEST_ASSERT(arr2[0] == 10 && arr2[1] == 20 && arr2[2] == 30,
                    "arr2 values wrong after second swap");

        TEST_SUCCESS;
    }

    // Reverse tests
    TEST_IMPL(test_array_reverse)
    {
        mtl::array<int, 5> arr;
        arr[0] = 1;
        arr[1] = 2;
        arr[2] = 3;
        arr[3] = 4;
        arr[4] = 5;

        arr.reverse();
        TEST_ASSERT(arr[0] == 5 && arr[1] == 4 && arr[2] == 3 && arr[3] == 2 && arr[4] == 1,
                    "reverse() failed to reverse array");

        // Reverse again should restore original
        arr.reverse();
        TEST_ASSERT(arr[0] == 1 && arr[1] == 2 && arr[2] == 3 && arr[3] == 4 && arr[4] == 5,
                    "double reverse() failed to restore original");

        TEST_SUCCESS;
    }

    TEST_IMPL(test_array_reverse_even_size)
    {
        mtl::array<int, 4> arr;
        arr[0] = 10;
        arr[1] = 20;
        arr[2] = 30;
        arr[3] = 40;

        arr.reverse();
        TEST_ASSERT(arr[0] == 40 && arr[1] == 30 && arr[2] == 20 && arr[3] == 10,
                    "reverse() failed on even-sized array");

        TEST_SUCCESS;
    }

    TEST_IMPL(test_array_reverse_single_element)
    {
        mtl::array<int, 1> arr;
        arr[0] = 99;

        arr.reverse();
        TEST_ASSERT(arr[0] == 99, "reverse() changed single element array");

        TEST_SUCCESS;
    }

    // Comparison tests
    TEST_IMPL(test_array_equality)
    {
        mtl::array<int, 3> arr1;
        mtl::array<int, 3> arr2;
        mtl::array<int, 3> arr3;

        arr1[0] = 1;
        arr1[1] = 2;
        arr1[2] = 3;
        arr2[0] = 1;
        arr2[1] = 2;
        arr2[2] = 3;
        arr3[0] = 1;
        arr3[1] = 2;
        arr3[2] = 4;

        TEST_ASSERT(arr1 == arr2, "equal arrays not equal");
        TEST_ASSERT(!(arr1 == arr3), "different arrays equal");
        TEST_ASSERT(arr1 == arr1, "array not equal to itself");

        TEST_SUCCESS;
    }

    // Edge cases and special scenarios
    TEST_IMPL(test_array_zero_initialization)
    {
        mtl::array<int, 3> arr;
        arr.fill(0);

        for (size_t i = 0; i < 3; ++i) { TEST_ASSERT(arr[i] == 0, "zero initialization failed"); }

        TEST_SUCCESS;
    }

    TEST_IMPL(test_array_different_types)
    {
        // Test with different data types
        mtl::array<char, 3> char_arr;
        char_arr.fill('A');
        for (size_t i = 0; i < 3; ++i)
        {
            TEST_ASSERT(char_arr[i] == 'A', "char array initialization failed");
        }

        mtl::array<int, 2> int_arr;
        int_arr.fill(42);
        for (size_t i = 0; i < 2; ++i)
        {
            TEST_ASSERT(int_arr[i] == 42, "int array initialization failed");
        }

        TEST_SUCCESS;
    }

    TEST_IMPL(test_array_copy_semantics)
    {
        mtl::array<int, 3> original;
        original[0] = 100;
        original[1] = 200;
        original[2] = 300;

        // Test copy constructor (if available)
        mtl::array<int, 3> copy = original;
        TEST_ASSERT(copy == original, "copy construction failed");

        // Modify copy to ensure independence
        copy[0] = 999;
        TEST_ASSERT(copy[0] == 999, "copy modification failed");
        TEST_ASSERT(original[0] == 100, "original was modified when copy was changed");

        TEST_SUCCESS;
    }

    TEST_IMPL(test_array_assignment)
    {
        mtl::array<int, 3> arr1;
        mtl::array<int, 3> arr2;

        arr1[0] = 1;
        arr1[1] = 2;
        arr1[2] = 3;
        arr2[0] = 10;
        arr2[1] = 20;
        arr2[2] = 30;

        // Test assignment operator (if available)
        arr1 = arr2;
        TEST_ASSERT(arr1 == arr2, "assignment failed");
        TEST_ASSERT(arr1[0] == 10 && arr1[1] == 20 && arr1[2] == 30, "assignment values wrong");

        TEST_SUCCESS;
    }

    // constexpr tests
    TEST_IMPL(test_array_constexpr_operations)
    {
        // Test constexpr operations where possible
        constexpr mtl::array<int, 3> arr{};
        constexpr size_t size = arr.size();
        TEST_ASSERT(size == 3, "constexpr size() failed");

        TEST_SUCCESS;
    }

    // Large array tests
    TEST_IMPL(test_array_large_size)
    {
        mtl::array<int, 500> large_arr;
        large_arr.fill(42);

        TEST_ASSERT(large_arr.size() == 500, "large array size wrong");
        TEST_ASSERT(large_arr[0] == 42, "large array first element wrong");
        TEST_ASSERT(large_arr[499] == 42, "large array last element wrong");
        TEST_ASSERT(large_arr[250] == 42, "large array middle element wrong");

        // Fill with different value
        large_arr.fill(123);
        TEST_ASSERT(large_arr[0] == 123, "large array fill failed at first");
        TEST_ASSERT(large_arr[499] == 123, "large array fill failed at last");

        TEST_SUCCESS;
    }

    // Nested array tests
    TEST_IMPL(test_array_nested)
    {
        mtl::array<mtl::array<int, 2>, 3> nested{};

        TEST_ASSERT(nested.size() == 3, "nested array outer size wrong");
        TEST_ASSERT(nested[0].size() == 2, "nested array inner size wrong");

        // Initialize nested array
        for (size_t i = 0; i < 3; ++i)
        {
            for (size_t j = 0; j < 2; ++j) { nested[i][j] = static_cast<int>(i * 10 + j); }
        }

        TEST_ASSERT(nested[0][0] == 0 && nested[0][1] == 1, "nested array [0] wrong");
        TEST_ASSERT(nested[1][0] == 10 && nested[1][1] == 11, "nested array [1] wrong");
        TEST_ASSERT(nested[2][0] == 20 && nested[2][1] == 21, "nested array [2] wrong");

        TEST_SUCCESS;
    }

    // Performance and memory layout tests
    TEST_IMPL(test_array_memory_layout)
    {
        mtl::array<int, 4> arr;
        arr.fill(0);

        // Test that elements are contiguous in memory
        int* ptr = &arr[0];
        TEST_ASSERT(&arr[1] == ptr + 1, "array elements not contiguous");
        TEST_ASSERT(&arr[2] == ptr + 2, "array elements not contiguous");
        TEST_ASSERT(&arr[3] == ptr + 3, "array elements not contiguous");

        // Test that begin() returns pointer to first element
        TEST_ASSERT(arr.begin() == ptr, "begin() not pointing to first element");

        TEST_SUCCESS;
    }

    // Test suite definition
    TEST_SUITE_BEGIN(mtl_array)
    TEST(test_array_default_construct)
    TEST(test_array_fill_construct)
    TEST(test_array_element_access)
    TEST(test_array_element_modification)
    TEST(test_array_const_access)
    TEST(test_array_iterators)
    TEST(test_array_size)
    TEST(test_array_fill)
    TEST(test_array_swap)
    TEST(test_array_reverse)
    TEST(test_array_reverse_even_size)
    TEST(test_array_reverse_single_element)
    TEST(test_array_equality)
    TEST(test_array_zero_initialization)
    TEST(test_array_different_types)
    TEST(test_array_copy_semantics)
    TEST(test_array_assignment)
    TEST(test_array_constexpr_operations)
    TEST(test_array_large_size)
    TEST(test_array_nested)
    TEST(test_array_memory_layout)
    TEST_SUITE_END

}  // namespace NEONengine

#endif  // ACE_TEST_RUNNER

#endif  // __MTL_ARRAY_TESTS_H__INCLUDED__
