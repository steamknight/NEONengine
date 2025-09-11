#ifndef __MTL_VECTOR_TESTS_H__INCLUDED__
#define __MTL_VECTOR_TESTS_H__INCLUDED__

#ifdef ACE_TEST_RUNNER

#include <mtl/vector.h>

#include "test_macros.h"

namespace NEONengine
{

    // Basic construction and destruction tests
    TEST_IMPL(test_vector_default_construct)
    {
        mtl::vector<int> vec;
        TEST_ASSERT(vec.empty(), "default vector not empty");
        TEST_ASSERT(vec.size() == 0, "default vector size not zero");
        TEST_ASSERT(vec.capacity() == 0, "default vector capacity not zero");
        TEST_ASSERT(vec.data() == nullptr, "default vector data not null");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_vector_capacity_construct)
    {
        mtl::vector<int> vec(5);
        TEST_ASSERT(vec.empty(), "capacity constructed vector not empty");
        TEST_ASSERT(vec.size() == 0, "capacity constructed vector size not zero");
        TEST_ASSERT(vec.capacity() >= 5, "capacity constructed vector capacity insufficient");
        TEST_ASSERT(vec.data() != nullptr, "capacity constructed vector data is null");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_vector_count_value_construct)
    {
        mtl::vector<int> vec(3, 42);
        TEST_ASSERT(!vec.empty(), "count-value constructed vector is empty");
        TEST_ASSERT(vec.size() == 3, "count-value constructed vector size wrong");
        TEST_ASSERT(vec.capacity() >= 3, "count-value constructed vector capacity insufficient");
        TEST_ASSERT(vec[0] == 42 && vec[1] == 42 && vec[2] == 42,
                    "count-value constructed vector values wrong");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_vector_array_construct)
    {
        int arr[] = { 1, 2, 3, 4 };
        mtl::vector<int> vec(arr);
        TEST_ASSERT(!vec.empty(), "array constructed vector is empty");
        TEST_ASSERT(vec.size() == 4, "array constructed vector size wrong");
        TEST_ASSERT(vec[0] == 1 && vec[1] == 2 && vec[2] == 3 && vec[3] == 4,
                    "array constructed vector values wrong");
        TEST_SUCCESS;
    }

    // Copy and move constructor tests
    TEST_IMPL(test_vector_copy_construct)
    {
        mtl::vector<int> original;
        original.push_back(10);
        original.push_back(20);
        original.push_back(30);

        mtl::vector<int> copy(original);
        TEST_ASSERT(copy.size() == 3, "copy constructed vector size wrong");
        TEST_ASSERT(copy[0] == 10 && copy[1] == 20 && copy[2] == 30,
                    "copy constructed vector values wrong");
        TEST_ASSERT(original.size() == 3, "original vector modified during copy");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_vector_move_construct)
    {
        mtl::vector<int> original;
        original.push_back(100);
        original.push_back(200);

        size_t original_capacity = original.capacity();
        int* original_data       = original.data();

        mtl::vector<int> moved(mtl::move(original));
        TEST_ASSERT(moved.size() == 2, "move constructed vector size wrong");
        TEST_ASSERT(moved[0] == 100 && moved[1] == 200, "move constructed vector values wrong");
        TEST_ASSERT(moved.capacity() == original_capacity,
                    "move constructed vector capacity wrong");
        TEST_ASSERT(moved.data() == original_data, "move constructed vector data not transferred");
        TEST_ASSERT(original.size() == 0, "original vector not emptied after move");
        TEST_ASSERT(original.data() == nullptr, "original vector data not nulled after move");
        TEST_SUCCESS;
    }

    // Assignment operator tests
    TEST_IMPL(test_vector_copy_assign)
    {
        mtl::vector<int> original;
        original.push_back(5);
        original.push_back(15);

        mtl::vector<int> target;
        target.push_back(999);  // Should be cleared

        target = original;
        TEST_ASSERT(target.size() == 2, "copy assigned vector size wrong");
        TEST_ASSERT(target[0] == 5 && target[1] == 15, "copy assigned vector values wrong");
        TEST_ASSERT(original.size() == 2, "original vector modified during copy assignment");
        TEST_SUCCESS;
    }

    TEST_IMPL(test_vector_move_assign)
    {
        mtl::vector<int> original;
        original.push_back(50);
        original.push_back(60);

        size_t original_capacity = original.capacity();
        int* original_data       = original.data();

        mtl::vector<int> target;
        target.push_back(999);  // Should be destroyed

        target = mtl::move(original);
        TEST_ASSERT(target.size() == 2, "move assigned vector size wrong");
        TEST_ASSERT(target[0] == 50 && target[1] == 60, "move assigned vector values wrong");
        TEST_ASSERT(target.capacity() == original_capacity, "move assigned vector capacity wrong");
        TEST_ASSERT(target.data() == original_data, "move assigned vector data not transferred");
        TEST_ASSERT(original.size() == 0, "original vector not emptied after move assignment");
        TEST_ASSERT(original.data() == nullptr,
                    "original vector data not nulled after move assignment");
        TEST_SUCCESS;
    }

    // Element access tests
    TEST_IMPL(test_vector_element_access)
    {
        mtl::vector<int> vec;
        vec.push_back(10);
        vec.push_back(20);
        vec.push_back(30);

        TEST_ASSERT(vec[0] == 10, "operator[] access wrong at index 0");
        TEST_ASSERT(vec[1] == 20, "operator[] access wrong at index 1");
        TEST_ASSERT(vec[2] == 30, "operator[] access wrong at index 2");

        TEST_ASSERT(vec.at(0) == 10, "at() access wrong at index 0");
        TEST_ASSERT(vec.at(1) == 20, "at() access wrong at index 1");
        TEST_ASSERT(vec.at(2) == 30, "at() access wrong at index 2");

        TEST_ASSERT(vec.front() == 10, "front() access wrong");
        TEST_ASSERT(vec.back() == 30, "back() access wrong");

        TEST_SUCCESS;
    }

    TEST_IMPL(test_vector_element_modification)
    {
        mtl::vector<int> vec;
        vec.push_back(1);
        vec.push_back(2);
        vec.push_back(3);

        vec[0] = 100;
        vec[1] = 200;
        vec[2] = 300;

        TEST_ASSERT(vec[0] == 100, "operator[] modification failed at index 0");
        TEST_ASSERT(vec[1] == 200, "operator[] modification failed at index 1");
        TEST_ASSERT(vec[2] == 300, "operator[] modification failed at index 2");

        vec.front() = 999;
        vec.back()  = 888;

        TEST_ASSERT(vec[0] == 999, "front() modification failed");
        TEST_ASSERT(vec[2] == 888, "back() modification failed");

        TEST_SUCCESS;
    }

    // Push back and capacity tests
    TEST_IMPL(test_vector_push_back)
    {
        mtl::vector<int> vec;

        vec.push_back(42);
        TEST_ASSERT(!vec.empty(), "vector empty after push_back");
        TEST_ASSERT(vec.size() == 1, "size not 1 after single push_back");
        TEST_ASSERT(vec[0] == 42, "value wrong after push_back");

        vec.push_back(84);
        TEST_ASSERT(vec.size() == 2, "size not 2 after second push_back");
        TEST_ASSERT(vec[0] == 42 && vec[1] == 84, "values wrong after second push_back");

        TEST_SUCCESS;
    }

    TEST_IMPL(test_vector_push_back_move)
    {
        mtl::vector<int> vec;

        int value = 123;
        vec.push_back(mtl::move(value));
        TEST_ASSERT(vec.size() == 1, "size wrong after move push_back");
        TEST_ASSERT(vec[0] == 123, "value wrong after move push_back");

        TEST_SUCCESS;
    }

    TEST_IMPL(test_vector_emplace_back)
    {
        mtl::vector<int> vec;

        int& ref = vec.emplace_back(456);
        TEST_ASSERT(vec.size() == 1, "size wrong after emplace_back");
        TEST_ASSERT(vec[0] == 456, "value wrong after emplace_back");
        TEST_ASSERT(&ref == &vec[0], "reference wrong after emplace_back");

        vec.emplace_back(789);
        TEST_ASSERT(vec.size() == 2, "size wrong after second emplace_back");
        TEST_ASSERT(vec[1] == 789, "value wrong after second emplace_back");

        TEST_SUCCESS;
    }

    TEST_IMPL(test_vector_pop_back)
    {
        mtl::vector<int> vec;
        vec.push_back(1);
        vec.push_back(2);
        vec.push_back(3);

        vec.pop_back();
        TEST_ASSERT(vec.size() == 2, "size wrong after pop_back");
        TEST_ASSERT(vec[0] == 1 && vec[1] == 2, "values wrong after pop_back");

        vec.pop_back();
        TEST_ASSERT(vec.size() == 1, "size wrong after second pop_back");
        TEST_ASSERT(vec[0] == 1, "value wrong after second pop_back");

        vec.pop_back();
        TEST_ASSERT(vec.size() == 0, "size wrong after final pop_back");
        TEST_ASSERT(vec.empty(), "vector not empty after final pop_back");

        // Pop from empty should be safe
        vec.pop_back();
        TEST_ASSERT(vec.empty(), "pop_back on empty changed state");

        TEST_SUCCESS;
    }

    // Capacity management tests
    TEST_IMPL(test_vector_reserve)
    {
        mtl::vector<int> vec;
        TEST_ASSERT(vec.capacity() == 0, "initial capacity not zero");

        vec.reserve(10);
        TEST_ASSERT(vec.capacity() >= 10, "capacity not increased by reserve");
        TEST_ASSERT(vec.size() == 0, "size changed by reserve");

        size_t old_capacity = vec.capacity();
        vec.reserve(5);  // Should not decrease
        TEST_ASSERT(vec.capacity() == old_capacity, "capacity decreased by smaller reserve");

        TEST_SUCCESS;
    }

    TEST_IMPL(test_vector_capacity_growth)
    {
        mtl::vector<int> vec;

        // Push enough elements to trigger reallocation
        for (int i = 0; i < 10; ++i) { vec.push_back(i); }

        TEST_ASSERT(vec.size() == 10, "size wrong after capacity growth");
        TEST_ASSERT(vec.capacity() >= 10, "capacity insufficient after growth");

        // Verify all values are correct
        for (int i = 0; i < 10; ++i)
        {
            TEST_ASSERT(vec[i] == i, "value wrong after capacity growth");
        }

        TEST_SUCCESS;
    }

    TEST_IMPL(test_vector_shrink_to_fit)
    {
        mtl::vector<int> vec;
        vec.reserve(100);
        vec.push_back(1);
        vec.push_back(2);

        TEST_ASSERT(vec.capacity() >= 100, "capacity not as expected before shrink");

        vec.shrink_to_fit();
        TEST_ASSERT(vec.capacity() >= vec.size(), "capacity less than size after shrink");
        TEST_ASSERT(vec.size() == 2, "size changed by shrink_to_fit");
        TEST_ASSERT(vec[0] == 1 && vec[1] == 2, "values changed by shrink_to_fit");

        TEST_SUCCESS;
    }

    // Clear and resize tests
    TEST_IMPL(test_vector_clear)
    {
        mtl::vector<int> vec;
        vec.push_back(1);
        vec.push_back(2);
        vec.push_back(3);

        size_t old_capacity = vec.capacity();
        vec.clear();

        TEST_ASSERT(vec.size() == 0, "size not zero after clear");
        TEST_ASSERT(vec.empty(), "vector not empty after clear");
        TEST_ASSERT(vec.capacity() == old_capacity, "capacity changed by clear");

        TEST_SUCCESS;
    }

    TEST_IMPL(test_vector_resize_default)
    {
        mtl::vector<int> vec;
        vec.push_back(10);
        vec.push_back(20);

        // Resize larger
        vec.resize(5);
        TEST_ASSERT(vec.size() == 5, "size wrong after resize larger");
        TEST_ASSERT(vec[0] == 10 && vec[1] == 20, "original values changed by resize");
        TEST_ASSERT(vec[2] == 0 && vec[3] == 0 && vec[4] == 0,
                    "new values not default-constructed");

        // Resize smaller
        vec.resize(2);
        TEST_ASSERT(vec.size() == 2, "size wrong after resize smaller");
        TEST_ASSERT(vec[0] == 10 && vec[1] == 20, "values wrong after resize smaller");

        TEST_SUCCESS;
    }

    TEST_IMPL(test_vector_resize_value)
    {
        mtl::vector<int> vec;
        vec.push_back(1);

        vec.resize(4, 99);
        TEST_ASSERT(vec.size() == 4, "size wrong after resize with value");
        TEST_ASSERT(vec[0] == 1, "original value changed by resize with value");
        TEST_ASSERT(vec[1] == 99 && vec[2] == 99 && vec[3] == 99, "new values not set correctly");

        TEST_SUCCESS;
    }

    // Assign tests
    TEST_IMPL(test_vector_assign_value)
    {
        mtl::vector<int> vec;
        vec.push_back(999);  // Should be cleared

        vec.assign(3, 42);
        TEST_ASSERT(vec.size() == 3, "size wrong after assign value");
        TEST_ASSERT(vec[0] == 42 && vec[1] == 42 && vec[2] == 42,
                    "values wrong after assign value");

        TEST_SUCCESS;
    }

    TEST_IMPL(test_vector_assign_array)
    {
        mtl::vector<int> vec;
        vec.push_back(999);  // Should be cleared

        int arr[] = { 10, 20, 30, 40 };
        vec.assign(4, arr);
        TEST_ASSERT(vec.size() == 4, "size wrong after assign array");
        TEST_ASSERT(vec[0] == 10 && vec[1] == 20 && vec[2] == 30 && vec[3] == 40,
                    "values wrong after assign array");

        // Test partial assignment
        vec.assign(2, arr);
        TEST_ASSERT(vec.size() == 2, "size wrong after partial assign array");
        TEST_ASSERT(vec[0] == 10 && vec[1] == 20, "values wrong after partial assign array");

        TEST_SUCCESS;
    }

    // Iterator tests
    TEST_IMPL(test_vector_iterators)
    {
        mtl::vector<int> vec;
        vec.push_back(5);
        vec.push_back(10);
        vec.push_back(15);

        // Test begin/end
        TEST_ASSERT(vec.begin() == vec.data(), "begin() not equal to data()");
        TEST_ASSERT(vec.end() == vec.data() + vec.size(), "end() not equal to data() + size()");

        // Test iteration
        int expected = 5;
        for (auto it = vec.begin(); it != vec.end(); ++it)
        {
            TEST_ASSERT(*it == expected, "iterator value wrong");
            expected += 5;
        }

        // Test const iterators
        mtl::vector<int> const& const_vec = vec;
        TEST_ASSERT(const_vec.begin() == const_vec.data(), "const begin() not equal to data()");
        TEST_ASSERT(const_vec.cbegin() == const_vec.data(), "cbegin() not equal to data()");
        TEST_ASSERT(const_vec.end() == const_vec.data() + const_vec.size(),
                    "const end() not equal to data() + size()");
        TEST_ASSERT(const_vec.cend() == const_vec.data() + const_vec.size(),
                    "cend() not equal to data() + size()");

        TEST_SUCCESS;
    }

    // Swap test
    TEST_IMPL(test_vector_swap)
    {
        mtl::vector<int> vec1;
        vec1.push_back(1);
        vec1.push_back(2);

        mtl::vector<int> vec2;
        vec2.push_back(10);
        vec2.push_back(20);
        vec2.push_back(30);

        size_t vec1_size     = vec1.size();
        size_t vec2_size     = vec2.size();
        size_t vec1_capacity = vec1.capacity();
        size_t vec2_capacity = vec2.capacity();

        vec1.swap(vec2);

        TEST_ASSERT(vec1.size() == vec2_size, "vec1 size wrong after swap");
        TEST_ASSERT(vec2.size() == vec1_size, "vec2 size wrong after swap");
        TEST_ASSERT(vec1.capacity() == vec2_capacity, "vec1 capacity wrong after swap");
        TEST_ASSERT(vec2.capacity() == vec1_capacity, "vec2 capacity wrong after swap");

        TEST_ASSERT(vec1[0] == 10 && vec1[1] == 20 && vec1[2] == 30,
                    "vec1 values wrong after swap");
        TEST_ASSERT(vec2[0] == 1 && vec2[1] == 2, "vec2 values wrong after swap");

        TEST_SUCCESS;
    }

    // Comparison operator tests
    TEST_IMPL(test_vector_equality)
    {
        mtl::vector<int> vec1;
        vec1.push_back(1);
        vec1.push_back(2);
        vec1.push_back(3);

        mtl::vector<int> vec2;
        vec2.push_back(1);
        vec2.push_back(2);
        vec2.push_back(3);

        mtl::vector<int> vec3;
        vec3.push_back(1);
        vec3.push_back(2);

        mtl::vector<int> vec4;
        vec4.push_back(1);
        vec4.push_back(2);
        vec4.push_back(4);

        TEST_ASSERT(vec1 == vec2, "equal vectors not equal");
        TEST_ASSERT(!(vec1 == vec3), "different size vectors equal");
        TEST_ASSERT(!(vec1 == vec4), "different content vectors equal");

        TEST_ASSERT(!(vec1 != vec2), "equal vectors not equal with !=");
        TEST_ASSERT(vec1 != vec3, "different size vectors equal with !=");
        TEST_ASSERT(vec1 != vec4, "different content vectors equal with !=");

        TEST_SUCCESS;
    }

    // Memory type tests
    TEST_IMPL(test_vector_memory_types)
    {
        mtl::vector<int, mtl::MemF::Fast> fast_vec;
        mtl::vector<int, mtl::MemF::Chip> chip_vec;

        fast_vec.push_back(42);
        chip_vec.push_back(84);

        TEST_ASSERT(fast_vec.size() == 1, "fast memory vector size wrong");
        TEST_ASSERT(chip_vec.size() == 1, "chip memory vector size wrong");
        TEST_ASSERT(fast_vec[0] == 42, "fast memory vector value wrong");
        TEST_ASSERT(chip_vec[0] == 84, "chip memory vector value wrong");

        TEST_SUCCESS;
    }

    // Edge cases and stress tests
    TEST_IMPL(test_vector_empty_operations)
    {
        mtl::vector<int> vec;

        // Operations on empty vector should be safe
        vec.clear();
        vec.pop_back();
        vec.shrink_to_fit();

        TEST_ASSERT(vec.empty(), "empty vector operations changed state");
        TEST_ASSERT(vec.size() == 0, "empty vector operations changed size");

        TEST_SUCCESS;
    }

    TEST_IMPL(test_vector_large_capacity)
    {
        mtl::vector<int> vec;

        // Reserve a larger capacity and test
        vec.reserve(1000);
        TEST_ASSERT(vec.capacity() >= 1000, "large reserve failed");
        TEST_ASSERT(vec.empty(), "large reserve changed size");

        // Add some elements
        for (int i = 0; i < 100; ++i) { vec.push_back(i); }

        TEST_ASSERT(vec.size() == 100, "size wrong after filling reserved vector");
        for (int i = 0; i < 100; ++i)
        {
            TEST_ASSERT(vec[i] == i, "value wrong in filled reserved vector");
        }

        TEST_SUCCESS;
    }

    // Test suite definition
    TEST_SUITE_BEGIN(mtl_vector)
    TEST(test_vector_default_construct)
    TEST(test_vector_capacity_construct)
    TEST(test_vector_count_value_construct)
    TEST(test_vector_array_construct)
    TEST(test_vector_copy_construct)
    TEST(test_vector_move_construct)
    TEST(test_vector_copy_assign)
    TEST(test_vector_move_assign)
    TEST(test_vector_element_access)
    TEST(test_vector_element_modification)
    TEST(test_vector_push_back)
    TEST(test_vector_push_back_move)
    TEST(test_vector_emplace_back)
    TEST(test_vector_pop_back)
    TEST(test_vector_reserve)
    TEST(test_vector_capacity_growth)
    TEST(test_vector_shrink_to_fit)
    TEST(test_vector_clear)
    TEST(test_vector_resize_default)
    TEST(test_vector_resize_value)
    TEST(test_vector_assign_value)
    TEST(test_vector_assign_array)
    TEST(test_vector_iterators)
    TEST(test_vector_swap)
    TEST(test_vector_equality)
    TEST(test_vector_memory_types)
    TEST(test_vector_empty_operations)
    TEST(test_vector_large_capacity)
    TEST_SUITE_END

}  // namespace NEONengine

#endif  // ACE_TEST_RUNNER

#endif  // __MTL_VECTOR_TESTS_H__INCLUDED__
