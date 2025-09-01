#include "array.h"

#include <ace/macros.h>
#include <ace/managers/memory.h>
#include <string.h>

#include "utils/assert.h"

struct Array
{
    ULONG ulLength;
    ULONG ulElementSize;
    ULONG ulFlags;
    UBYTE pBuffer[];
};

Array arrayCreate(ULONG ulLength, ULONG ulElementSize, ULONG ulFlags)
{
    ULONG ulTotalSize = sizeof(struct Array) + (ulElementSize * ulLength);
    Array array = memAlloc(ulTotalSize, ulFlags);

    assert(array, "Allocating memory for array failed!");

    array->ulLength = ulLength;
    array->ulElementSize = ulElementSize;
    array->ulFlags = ulFlags;

    return array;
}

void arrayDestroy(Array* pArray)
{
    assert(pArray && *pArray, "Trying to free a null array pointer.");

    ULONG ulTotalSize = sizeof(struct Array) + ((*pArray)->ulElementSize * (*pArray)->ulLength);
    memFree(*pArray, ulTotalSize);
    *pArray = NULL;
}

ULONG arrayLength(Array array)
{
    assert(array, "Trying to get the length of a NULL array.");
    return array->ulLength;
}

ULONG arrayElementSize(Array array)
{
    assert(array, "Trying to get the element size of a NULL array");
    return array->ulElementSize;
}

void* arrayGet(Array array, ULONG ulIndex)
{
    assert(array, "Trying to get an element from a NULL array");
    assert(ulIndex < array->ulLength, "Index out of bounds");

    return (void*)(array->pBuffer + (array->ulElementSize * ulIndex));
}

void* arrayPut(Array array, ULONG ulIndex, void* pElement)
{
    assert(array, "Trying to put an element into a NULL array");
    assert(ulIndex < array->ulLength, "Index out of bounds");
    assert(pElement, "Inserting invalid element");

    memcpy(array->pBuffer + (array->ulElementSize * ulIndex), pElement, array->ulElementSize);

    return arrayGet(array, ulIndex);
}

void arrayResize(Array* pArray, ULONG ulNewLength)
{
    assert(pArray && *pArray, "Trying to resize a NULL array");

    // If the new size is zero, free the array
    if (ulNewLength == 0)
    {
        arrayDestroy(pArray);
        return;
    }

    Array newArray = arrayCreate(ulNewLength, (*pArray)->ulElementSize, (*pArray)->ulFlags);
    ULONG ulMaxElements = MIN((*pArray)->ulLength, ulNewLength);
    ULONG ulTotalSize = (*pArray)->ulElementSize * ulMaxElements;
    memcpy(newArray->pBuffer, (*pArray)->pBuffer, ulTotalSize);

    arrayDestroy(pArray);
    *pArray = newArray;
}

void arrayAutoResize(Array* pArray)
{
    assert(pArray && *pArray, "Trying to auto-resize a NULL array");

    ULONG ulNewLength = (*pArray)->ulLength ? (*pArray)->ulLength * 2 : 1;
    arrayResize(pArray, ulNewLength);
}

void arrayCopy(Array source, Array destination, ULONG ulStartIndex, ULONG ulCount)
{
    assert(source, "Source array is NULL");
    assert(source->ulLength >= (ulStartIndex + ulCount), "Attempting to copy more elements than there are in the source array.");
    assert(destination, "Destination array is NULL");
    assert(destination->ulLength >= ulCount, "Destination array is too small");
    assert(source->ulElementSize == destination->ulElementSize, "Arrays are incompatible");

    void* pSource = (UBYTE*)source->pBuffer + (source->ulElementSize * ulStartIndex);
    ULONG ulSize = ulCount * source->ulElementSize;

    memcpy(destination->pBuffer, pSource, ulSize);
}
