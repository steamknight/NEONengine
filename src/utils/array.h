#ifndef __ARRAY_H_INCLUDED__
#define __ARRAY_H_INCLUDED__

#include <ace/types.h>

/**
 * @brief Opaque pointer representing a dynamic array
 */
typedef struct Array *Array;

/**
 * @brief Creates a new array.
 *
 * @param ulLength The number of elements the array contains
 * @param ulElementSize The size of each element
 * @param ulFlags Memory allocation flags e.g. MEMF_FAST, MEMF_CHIP, MEMF_CLEAR
 * @return A newly created array.
 *
 * @see arrayDestroy
 */
Array arrayCreate(ULONG ulLength, ULONG ulElementSize, ULONG ulFlags);

/**
 * @brief Destroys and deallocates the array.
 * Will set the pointer to NULL when done.
 *
 * @param pArray Pointer to the array (not the array iself)
 */
void arrayDestroy(Array* pArray);

/**
 * @brief Returns the length of the array.
 *
 * @param array The array
 * @return The array's capacity.
 */
ULONG arrayLength(Array array);

/**
 * @brief Returns the size of the element type contained in the array.
 *
 * @param array The array
 * @return The element's size
 */
ULONG arrayElementSize(Array array);

/**
 * @brief Gets the value stored at an index.
 *
 * @param array The array
 * @param ulIndex The index to query.
 * @return A pointer to the element in the array.
 *
 * @see arrayPut
 */
void* arrayGet(Array array, ULONG ulIndex);

/**
 * @brief Sets a value at a specified index.
 * A copy of the element is made, so it's safe to pass values created on the stack.
 *
 * @param array The array
 * @param ulIndex The index to query
 * @param pElement Pointer to the element to set.
 * @return void* pointer to the element passed in, not the copy in the array.
 */
void* arrayPut(Array array, ULONG ulIndex, void* pElement);

/**
 * @brief Resizes the array.
 * Node that resizing the array to a zero-length, simply destroys it.
 *
 * @param array A pointer to the array to resize
 * @param ulNewLength The new length
 */
void arrayResize(Array* array, ULONG ulNewLength);

/**
 * @brief Copies a number of values from one array to another starting from some index.
 * An ulCount number of elements will be copied from the source array starting
 * at index ulStartIndex, and copied to the destination array from the zero-index.
 *
 * @param source The source array
 * @param destination The destination array
 * @param ulStartIndex The index to start copying from
 * @param ulCount How many elements to copy
 */
void arrayCopy(Array source, Array destination, ULONG ulStartIndex, ULONG ulCount);

#endif //__ARRAY_H_INCLUDED__