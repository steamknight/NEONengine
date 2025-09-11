#ifndef __MTL_LIST_TESTS_H__INCLUDED__
#define __MTL_LIST_TESTS_H__INCLUDED__

#ifdef ACE_TEST_RUNNER

#include <mtl/list.h>

#include "test_macros.h"

namespace NEONengine
{

    // Basic construction and destruction tests
    TEST_IMPL(forward_list_default_construct)
    {
        mtl::forward_list<int> list;
        TEST_ASSERT(list.is_empty(), "default list not empty");
        TEST_ASSERT(list.size() == 0, "default list size not zero");
        // front()/back() undefined on empty list; don't call here.
        TEST_SUCCESS;
    }

    // Push front tests
    TEST_IMPL(forward_list_push_front)
    {
        mtl::forward_list<int> list;

        list.push_front(42);
        TEST_ASSERT(!list.is_empty(), "list empty after push_front");
        TEST_ASSERT(list.size() == 1, "size not 1 after single push_front");
        TEST_ASSERT(list.front() == 42, "front value wrong after push_front");
        TEST_ASSERT(list.back() == 42, "back value wrong after single push_front");

        list.push_front(10);
        TEST_ASSERT(list.size() == 2, "size not 2 after second push_front");
        TEST_ASSERT(list.front() == 10, "front not newest after second push_front");
        TEST_ASSERT(list.back() == 42, "back changed after second push_front");

        TEST_SUCCESS;
    }

    // Push back tests
    TEST_IMPL(forward_list_push_back)
    {
        mtl::forward_list<int> list;

        list.push_back(100);
        TEST_ASSERT(!list.is_empty(), "list empty after push_back");
        TEST_ASSERT(list.size() == 1, "size not 1 after single push_back");
        TEST_ASSERT(list.front() == 100, "front value wrong after push_back");
        TEST_ASSERT(list.back() == 100, "back value wrong after single push_back");

        list.push_back(200);
        TEST_ASSERT(list.size() == 2, "size not 2 after second push_back");
        TEST_ASSERT(list.front() == 100, "front changed after second push_back");
        TEST_ASSERT(list.back() == 200, "back not newest after second push_back");

        TEST_SUCCESS;
    }

    // Mixed push tests
    TEST_IMPL(forward_list_mixed_push)
    {
        mtl::forward_list<int> list;

        list.push_back(1);    // [1]
        list.push_front(0);   // [0, 1]
        list.push_back(2);    // [0, 1, 2]
        list.push_front(-1);  // [-1, 0, 1, 2]

        TEST_ASSERT(list.size() == 4, "size wrong after mixed pushes");
        TEST_ASSERT(list.front() == -1, "front wrong after mixed pushes");
        TEST_ASSERT(list.back() == 2, "back wrong after mixed pushes");

        TEST_SUCCESS;
    }

    // Pop front tests
    TEST_IMPL(forward_list_pop_front)
    {
        mtl::forward_list<int> list;

        // Pop from empty list should be safe
        list.pop_front();
        TEST_ASSERT(list.is_empty(), "pop_front on empty changed state");

        list.push_front(10);
        list.push_front(20);
        list.push_front(30);  // [30, 20, 10]

        list.pop_front();  // [20, 10]
        TEST_ASSERT(list.size() == 2, "size wrong after pop_front");
        TEST_ASSERT(list.front() == 20, "front wrong after pop_front");
        TEST_ASSERT(list.back() == 10, "back wrong after pop_front");

        list.pop_front();  // [10]
        TEST_ASSERT(list.size() == 1, "size wrong after second pop_front");
        TEST_ASSERT(list.front() == 10, "front wrong after second pop_front");
        TEST_ASSERT(list.back() == 10, "back wrong when single element");

        list.pop_front();  // []
        TEST_ASSERT(list.is_empty(), "list not empty after popping last element");
        TEST_ASSERT(list.size() == 0, "size not zero after popping last element");
        // Don't call front()/back() on empty list.

        TEST_SUCCESS;
    }

    // Pop back tests
    TEST_IMPL(forward_list_pop_back)
    {
        mtl::forward_list<int> list;

        // Pop from empty list should be safe
        list.pop_back();
        TEST_ASSERT(list.is_empty(), "pop_back on empty changed state");

        list.push_back(10);
        list.push_back(20);
        list.push_back(30);  // [10, 20, 30]

        list.pop_back();  // [10, 20]
        TEST_ASSERT(list.size() == 2, "size wrong after pop_back");
        TEST_ASSERT(list.front() == 10, "front wrong after pop_back");
        TEST_ASSERT(list.back() == 20, "back wrong after pop_back");

        list.pop_back();  // [10]
        TEST_ASSERT(list.size() == 1, "size wrong after second pop_back");
        TEST_ASSERT(list.front() == 10, "front wrong after second pop_back");
        TEST_ASSERT(list.back() == 10, "back wrong when single element after pop_back");

        list.pop_back();  // []
        TEST_ASSERT(list.is_empty(), "list not empty after pop_back to empty");
        TEST_ASSERT(list.size() == 0, "size not zero after pop_back to empty");

        TEST_SUCCESS;
    }

    // Clear tests
    TEST_IMPL(forward_list_clear)
    {
        mtl::forward_list<int> list;

        // Clear empty list
        list.clear();
        TEST_ASSERT(list.is_empty(), "clear on empty list changed state");

        // Add elements and clear
        list.push_back(1);
        list.push_back(2);
        list.push_back(3);
        TEST_ASSERT(list.size() == 3, "setup for clear test failed");

        list.clear();
        TEST_ASSERT(list.is_empty(), "list not empty after clear");
        TEST_ASSERT(list.size() == 0, "size not zero after clear");
        // Don't call front()/back() on empty list here.

        TEST_SUCCESS;
    }

    // Iterator tests
    TEST_IMPL(forward_list_iterators)
    {
        mtl::forward_list<int> list;

        // Empty list iteration
        size_t count = 0;
        for (auto it = list.begin(); it != list.end(); ++it)
        {
            ++count;
        }
        TEST_ASSERT(count == 0, "empty list iteration count wrong");

        // Add elements
        list.push_back(10);
        list.push_back(20);
        list.push_back(30);

        // Forward iteration
        count          = 0;
        int expected[] = { 10, 20, 30 };
        auto it        = list.begin();
        for (size_t i = 0; i < 3 && it != list.end(); ++i, ++it)
        {
            TEST_ASSERT(*it == expected[i], "iterator value mismatch");
            ++count;
        }
        TEST_ASSERT(count == 3, "iteration count mismatch");
        TEST_ASSERT(it == list.end(), "iterator not at end after full traversal");

        TEST_SUCCESS;
    }

    // Const iterator tests
    TEST_IMPL(forward_list_const_iterators)
    {
        mtl::forward_list<int> list;
        list.push_back(5);
        list.push_back(15);
        list.push_back(25);

        auto const& const_list = list;

        // Test const begin/end
        size_t count = 0;
        for (auto it = const_list.begin(); it != const_list.end(); ++it)
        {
            ++count;
        }
        TEST_ASSERT(count == 3, "const iterator count wrong");

        // Test cbegin/cend
        count = 0;
        for (auto it = const_list.cbegin(); it != const_list.cend(); ++it)
        {
            ++count;
        }
        TEST_ASSERT(count == 3, "cbegin/cend iterator count wrong");

        TEST_SUCCESS;
    }

    // Range-based for loop test
    TEST_IMPL(forward_list_range_for)
    {
        mtl::forward_list<int> list;
        list.push_back(7);
        list.push_back(14);
        list.push_back(21);

        size_t count = 0;
        int sum      = 0;
        for (auto const& value : list)
        {
            sum += value;
            ++count;
        }

        TEST_ASSERT(count == 3, "range-for count wrong");
        TEST_ASSERT(sum == 42, "range-for sum wrong");

        TEST_SUCCESS;
    }

    // Insert after tests
    TEST_IMPL(forward_list_insert_after)
    {
        mtl::forward_list<int> list;

        // Insert after end() should return end()
        auto it = list.insert_after(list.end(), 100);
        TEST_ASSERT(it == list.end(), "insert_after end() didn't return end()");
        TEST_ASSERT(list.is_empty(), "insert_after end() modified empty list");

        // Build: [1, 2, 3]
        list.push_back(1);
        list.push_back(3);

        // Insert 2 between 1 and 3
        auto pos = list.begin();  // points to 1
        it       = list.insert_after(pos, 2);
        TEST_ASSERT(list.size() == 3, "size wrong after insert_after");
        TEST_ASSERT(*it == 2, "insert_after return iterator wrong");

        // Verify order: [1, 2, 3]
        auto check_it = list.begin();
        TEST_ASSERT(*check_it == 1, "first element wrong after insert_after");
        ++check_it;
        TEST_ASSERT(*check_it == 2, "inserted element wrong");
        ++check_it;
        TEST_ASSERT(*check_it == 3, "last element wrong after insert_after");

        TEST_SUCCESS;
    }

    // Insert after with move
    TEST_IMPL(forward_list_insert_after_move)
    {
        mtl::forward_list<int> list;
        list.push_back(10);

        int value = 20;
        auto it   = list.insert_after(list.begin(), static_cast<int&&>(value));
        TEST_ASSERT(list.size() == 2, "size wrong after move insert_after");
        TEST_ASSERT(*it == 20, "move insert_after value wrong");

        TEST_SUCCESS;
    }

    // Erase after tests
    TEST_IMPL(forward_list_erase_after)
    {
        mtl::forward_list<int> list;

        // Erase after end() should return end()
        auto it = list.erase_after(list.end());
        TEST_ASSERT(it == list.end(), "erase_after end() didn't return end()");

        // Build: [1, 2, 3, 4]
        list.push_back(1);
        list.push_back(2);
        list.push_back(3);
        list.push_back(4);

        // Erase 2 (after first element)
        auto pos = list.begin();           // points to 1
        it       = list.erase_after(pos);  // erases 2, returns iterator to 3
        TEST_ASSERT(list.size() == 3, "size wrong after erase_after");
        TEST_ASSERT(*it == 3, "erase_after return iterator wrong");

        // Verify order: [1, 3, 4]
        auto check_it = list.begin();
        TEST_ASSERT(*check_it == 1, "first element wrong after erase_after");
        ++check_it;
        TEST_ASSERT(*check_it == 3, "second element wrong after erase_after");
        ++check_it;
        TEST_ASSERT(*check_it == 4, "third element wrong after erase_after");

        TEST_SUCCESS;
    }

    // Swap tests
    TEST_IMPL(forward_list_swap)
    {
        mtl::forward_list<int> list1;
        mtl::forward_list<int> list2;

        // Swap empty lists
        list1.swap(list2);
        TEST_ASSERT(list1.is_empty() && list2.is_empty(), "swap empty lists failed");

        // Setup lists: list1=[1,2], list2=[10,20,30]
        list1.push_back(1);
        list1.push_back(2);
        list2.push_back(10);
        list2.push_back(20);
        list2.push_back(30);

        list1.swap(list2);

        // Check list1 now has [10,20,30]
        TEST_ASSERT(list1.size() == 3, "list1 size wrong after swap");
        TEST_ASSERT(list1.front() == 10, "list1 front wrong after swap");
        TEST_ASSERT(list1.back() == 30, "list1 back wrong after swap");

        // Check list2 now has [1,2]
        TEST_ASSERT(list2.size() == 2, "list2 size wrong after swap");
        TEST_ASSERT(list2.front() == 1, "list2 front wrong after swap");
        TEST_ASSERT(list2.back() == 2, "list2 back wrong after swap");

        TEST_SUCCESS;
    }

    // Emplace tests
    TEST_IMPL(forward_list_emplace)
    {
        mtl::forward_list<int> list;

        // Emplace front
        int* ptr = list.emplace_front(42);
        TEST_ASSERT(ptr != nullptr, "emplace_front returned null");
        TEST_ASSERT(*ptr == 42, "emplace_front value wrong");
        TEST_ASSERT(list.size() == 1, "size wrong after emplace_front");
        TEST_ASSERT(list.front() == 42, "front wrong after emplace_front");

        // Emplace back
        ptr = list.emplace_back(84);
        TEST_ASSERT(ptr != nullptr, "emplace_back returned null");
        TEST_ASSERT(*ptr == 84, "emplace_back value wrong");
        TEST_ASSERT(list.size() == 2, "size wrong after emplace_back");
        TEST_ASSERT(list.back() == 84, "back wrong after emplace_back");

        // Emplace after
        auto it = list.begin();  // points to 42
        ptr     = list.emplace_after(it, 63);
        TEST_ASSERT(ptr != nullptr, "emplace_after returned null");
        TEST_ASSERT(*ptr == 63, "emplace_after value wrong");
        TEST_ASSERT(list.size() == 3, "size wrong after emplace_after");

        // Check order: [42, 63, 84]
        auto check_it = list.begin();
        TEST_ASSERT(*check_it == 42, "first element wrong after emplace_after");
        ++check_it;
        TEST_ASSERT(*check_it == 63, "emplaced element wrong");
        ++check_it;
        TEST_ASSERT(*check_it == 84, "last element wrong after emplace_after");

        TEST_SUCCESS;
    }

    // Move semantics tests
    TEST_IMPL(forward_list_move_semantics)
    {
        mtl::forward_list<int> list1;
        list1.push_back(1);
        list1.push_back(2);
        list1.push_back(3);

        // Move constructor
        mtl::forward_list<int> list2(static_cast<mtl::forward_list<int>&&>(list1));
        TEST_ASSERT(list1.is_empty(), "moved-from list not empty");
        TEST_ASSERT(list1.size() == 0, "moved-from list size not zero");
        TEST_ASSERT(list2.size() == 3, "moved-to list size wrong");
        TEST_ASSERT(list2.front() == 1, "moved-to list content wrong");

        // Move assignment
        mtl::forward_list<int> list3;
        list3.push_back(99);
        list3 = static_cast<mtl::forward_list<int>&&>(list2);
        TEST_ASSERT(list2.is_empty(), "move-assigned-from list not empty");
        TEST_ASSERT(list3.size() == 3, "move-assigned-to list size wrong");
        TEST_ASSERT(list3.front() == 1, "move-assigned-to list content wrong");

        TEST_SUCCESS;
    }

    // Memory type tests (different MemF flags)
    TEST_IMPL(forward_list_memory_types)
    {
        mtl::forward_list<int, mtl::MemF::Fast> fast_list;
        mtl::forward_list<int, mtl::MemF::Chip> chip_list;

        fast_list.push_back(100);
        chip_list.push_back(200);

        TEST_ASSERT(fast_list.size() == 1, "fast memory list size wrong");
        TEST_ASSERT(chip_list.size() == 1, "chip memory list size wrong");
        TEST_ASSERT(fast_list.front() == 100, "fast memory list value wrong");
        TEST_ASSERT(chip_list.front() == 200, "chip memory list value wrong");

        TEST_SUCCESS;
    }

    TEST_SUITE_BEGIN(mtl_list)
    TEST(forward_list_default_construct)
    TEST(forward_list_push_front)
    TEST(forward_list_push_back)
    TEST(forward_list_mixed_push)
    TEST(forward_list_pop_front)
    TEST(forward_list_pop_back)
    TEST(forward_list_clear)
    TEST(forward_list_iterators)
    TEST(forward_list_const_iterators)
    TEST(forward_list_range_for)
    TEST(forward_list_insert_after)
    TEST(forward_list_insert_after_move)
    TEST(forward_list_erase_after)
    TEST(forward_list_swap)
    TEST(forward_list_emplace)
    TEST(forward_list_move_semantics)
    TEST(forward_list_memory_types)
    TEST_SUITE_END

}  // namespace NEONengine

#endif  // ACE_TEST_RUNNER

#endif  // __MTL_LIST_TESTS_H__INCLUDED__
