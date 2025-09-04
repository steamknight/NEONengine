#ifndef __ARRAY_TESTS_H__INCLUDED__
#define __ARRAY_TESTS_H__INCLUDED__

#ifdef ACE_TEST_RUNNER

#include <ace/managers/memory.h>
#include <string.h>
#include "utils/array.h"

#include "test_macros.h"

namepace NEONengine::tests
{
    TEST_IMPL(test_array_create_basic)
    {
        ULONG ulLength = 10;
        ULONG ulElementSize = sizeof(ULONG);

        Array array = arrayCreate(ulLength, ulElementSize, MEMF_FAST);
        TEST_ASSERT(array, "Failed to allocate memory");
        TEST_ASSERT(arrayLength(array) == ulLength, "Wrong number of elements");
        TEST_ASSERT(arrayElementSize(array) == ulElementSize, "Wrong element size");
        TEST_ASSERT(memType(array) == MEMF_FAST, "Array allocated is wrong memory type");

        arrayDestroy(&array);
        TEST_ASSERT(array == NULL, "Array not deallocated");

        TEST_SUCCESS;
    }

    TEST_IMPL(test_array_create_in_chip_ram)
    {
        ULONG ulLength = 10;
        ULONG ulElementSize = sizeof(ULONG);

        Array array = arrayCreate(ulLength, ulElementSize, MEMF_CHIP);
        TEST_ASSERT(array, "Failed to allocate memory");
        TEST_ASSERT(memType(array) == MEMF_CHIP, "Array allocated is wrong memory type");

        arrayDestroy(&array);
        TEST_SUCCESS;
    }

    TEST_IMPL(test_array_create_zero_size)
    {
        ULONG ulLength = 0;
        ULONG ulElementSize = sizeof(ULONG);

        Array array = arrayCreate(ulLength, ulElementSize, MEMF_FAST);
        TEST_ASSERT(array, "Failed to allocate memory");
        TEST_ASSERT(arrayLength(array) == ulLength, "Wrong number of elements");

        arrayDestroy(&array);
        TEST_SUCCESS;
    }

    typedef struct _ArrayTests_TestStruct
    {
        ULONG a;
        UWORD b;
        UBYTE c;
    } ArrayTests_TestStruct;

    TEST_IMPL(test_array_create_struct_array)
    {
        ULONG ulLength = 1;
        ULONG ulElementSize = sizeof(ArrayTests_TestStruct);

        Array array = arrayCreate(ulLength, ulElementSize, MEMF_FAST);
        TEST_ASSERT(array, "Failed to allocate memory");

        ArrayTests_TestStruct atts = { 0xDEADBEEF, 0xC0DE, 0xEF };
        arrayPut(array, 0, &atts);

        ArrayTests_TestStruct* pAtts = arrayGet(array, 0);
        TEST_ASSERT(pAtts->a == 0xDEADBEEF, "ULONG value is wrong");
        TEST_ASSERT(pAtts->b == 0xC0DE, "UWORD value is wrong");
        TEST_ASSERT(pAtts->c == 0xEF, "UBYTE value is wrong");

        arrayDestroy(&array);
        TEST_SUCCESS;
    }

    TEST_IMPL(test_array_consecutive_struct_entries)
    {
        ULONG ulLength = 3;
        ULONG ulElementSize = sizeof(ArrayTests_TestStruct);

        Array array = arrayCreate(ulLength, ulElementSize, MEMF_FAST);
        TEST_ASSERT(array, "Failed to allocate memory");

        ArrayTests_TestStruct atts1 = { 0xFEEDAC1D, 0xC1A0, 0xAB };
        ArrayTests_TestStruct atts2 = { 0xDEADBEEF, 0xC0DE, 0xEF };
        arrayPut(array, 1, &atts1);
        arrayPut(array, 2, &atts2);

        ArrayTests_TestStruct* pAtts = arrayGet(array, 2);
        TEST_ASSERT(pAtts->a == 0xDEADBEEF, "ULONG value is wrong");
        TEST_ASSERT(pAtts->b == 0xC0DE, "UWORD value is wrong");
        TEST_ASSERT(pAtts->c == 0xEF, "UBYTE value is wrong");

        arrayDestroy(&array);
        TEST_SUCCESS;
    }

    TEST_IMPL(test_array_put_get_every_element)
    {
        ULONG ulLength = 0x1000;
        ULONG ulElementSize = sizeof(UWORD);

        Array array = arrayCreate(ulLength, ulElementSize, MEMF_FAST);
        TEST_ASSERT(array, "Failed to allocate memory");

        for (UWORD i = 0; i < ulLength; ++i)
        {
            arrayPut(array, i, &i);
        }

        for (UWORD i = 0; i < ulLength; ++i)
        {
            UWORD* pVal = arrayGet(array, i);
            TEST_ASSERT(*pVal == i, "Array element is the wrong value");
        }

        arrayDestroy(&array);
        TEST_SUCCESS;
    }

    TEST_IMPL(test_array_resize_larger)
    {
        ULONG ulLength = 10;
        ULONG ulNewLength = 20;
        ULONG ulElementSize = sizeof(UWORD);

        Array array = arrayCreate(ulLength, ulElementSize, MEMF_FAST);
        TEST_ASSERT(array, "Failed to allocate memory");

        arrayResize(&array, ulNewLength);
        TEST_ASSERT(arrayLength(array) == ulNewLength, "Resized array is the wrong size");

        UWORD ulVal = 0xC1A0;
        arrayPut(array, ulNewLength - 1, &ulVal);
        UWORD* pVal = arrayGet(array, ulNewLength - 1);
        TEST_ASSERT(*pVal == ulVal, "Array element is the wrong value");

        arrayDestroy(&array);
        TEST_SUCCESS;
    }

    TEST_IMPL(test_array_resize_smaller)
    {
        ULONG ulLength = 10;
        ULONG ulNewLength = 5;
        ULONG ulElementSize = sizeof(UWORD);

        Array array = arrayCreate(ulLength, ulElementSize, MEMF_FAST);
        TEST_ASSERT(array, "Failed to allocate memory");

        for (UWORD i = 0; i < ulLength; ++i)
        {
            arrayPut(array, i, &i);
        }

        arrayResize(&array, ulNewLength);
        UWORD uwVal = 0xABCD;
        arrayPut(array, ulNewLength - 1, &uwVal);

        for (UWORD i = 0; i < ulNewLength; ++i)
        {
            UWORD* pVal = arrayGet(array, i);
            TEST_ASSERT(*pVal == ((i == ulNewLength - 1) ? uwVal : i), "Array element is the wrong value");
        }

        arrayDestroy(&array);
        TEST_SUCCESS;
    }

    TEST_IMPL(test_array_copy)
    {
        ULONG ulLength = 10;
        ULONG ulHalfLength = ulLength >> 1;
        ULONG ulElementSize = sizeof(ULONG);

        Array array1 = arrayCreate(ulLength, ulElementSize, MEMF_FAST);
        TEST_ASSERT(array1, "Failed to allocate memory");

        for (ULONG i = 0; i < ulLength; i++)
        {
            arrayPut(array1, i, &i);
        }

        Array array2 = arrayCreate(ulHalfLength, ulElementSize, MEMF_FAST);
        arrayCopy(array1, array2, ulHalfLength, ulHalfLength);

        for (ULONG i = 0; i < ulHalfLength; i++)
        {
            ULONG* pVal = arrayGet(array2, i);
            TEST_ASSERT(*pVal == i + ulHalfLength, "Array element is the wrong value");
        }

        arrayDestroy(&array1);
        arrayDestroy(&array2);
        TEST_SUCCESS;
    }

    TEST_IMPL(test_array_strings)
    {
        ULONG ulLength = 3;
        ULONG ulElementSize = sizeof(char*);

        Array array = arrayCreate(ulLength, ulElementSize, MEMF_FAST);
        TEST_ASSERT(array, "Failed to allocate memory");

        char* szFirst = "Hello World";
        char* szSecond = "Test";
        char* szThird = "I sure hope this works...";

        arrayPut(array, 0, &szFirst);
        arrayPut(array, 1, &szSecond);
        arrayPut(array, 2, &szThird);

        char** pszText = arrayGet(array, 1);
        TEST_ASSERT(strcmp(*pszText, szSecond) == 0, "Strings don't match");

        arrayDestroy(&array);
        TEST_SUCCESS;
    }

    TEST_SUITE_BEGIN(array)
        TEST(test_array_create_basic)
        TEST(test_array_create_in_chip_ram)
        TEST(test_array_create_zero_size)
        TEST(test_array_create_struct_array)
        TEST(test_array_consecutive_struct_entries)
        TEST(test_array_put_get_every_element)
        TEST(test_array_resize_larger)
        TEST(test_array_resize_smaller)
        TEST(test_array_copy)
        TEST(test_array_strings)
    TEST_SUITE_END

}
#endif // #ifdef ACE_TEST_RUNNER

#endif // __ARRAY_TESTS_H__INCLUDED__