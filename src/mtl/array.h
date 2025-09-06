/**
 * @file array.h
 * @brief Fixed-size array container implementation
 * 
 * This file provides a constexpr-compatible fixed-size array container
 * similar to std::array but without standard library dependencies.
 *  
 */

#ifndef __MTL_ARRAY__INCLUDED__
#define __MTL_ARRAY__INCLUDED__

#include <stddef.h>
#include <stdint.h>
#include "cstdint.h"
#include "utility.h"

namespace mtl
{
    /**
     * @brief Fixed-size array container
     * 
     * A compile-time fixed-size array container that provides safe array access
     * with bounds checking and various utility functions. All operations are
     * constexpr-compatible where possible.
     * 
     * @tparam T The type of elements stored in the array
     * @tparam Size The number of elements in the array
     * 
     * @example
     * @code
     * mtl::array<int, 5> numbers = {1, 2, 3, 4, 5};
     * numbers[0] = 10;
     * for (auto& num : numbers) {
     *     // Process each number
     * }
     * @endcode
     */
    template<class T, size_t Size>
    class array
    {
        public:
            /*
             *  _____                 _                   _                 
             * / ____|               | |                 | |                
             * | |     ___  _ __  ___| |_ _ __ _   _  ___| |_ ___  _ __ ___ 
             * | |    / _ \| '_ \/ __| __| '__| | | |/ __| __/ _ \| '__/ __|
             * | |___| (_) | | | \__ \ |_| |  | |_| | (__| || (_) | |  \__ \
             *  \_____\___/|_| |_|___/\__|_|   \__,_|\___|\__\___/|_|  |___/
             *                                                             
             */

            /**
             * @brief Default constructor
             * 
             * Creates an array with uninitialized elements.
             * For primitive types, the values are undefined.
             */
            constexpr array() = default;
            
            /**
             * @brief Initialize from initializer list
             * 
             * @param initializer List of values to initialize the array with
             * 
             * @note The size of the initializer must match the template Size parameter
             */
            constexpr array(T const (&initializer)[Size])
            {
                for (size_t index = 0; index < Size; ++index)
                {
                    _data[index] = initializer[index];
                }
            }

            /*
             *     /\                                        
             *    /  \   ___ ___ ___  ___ ___  ___  _ __ ___ 
             *   / /\ \ / __/ __/ _ \/ __/ __|/ _ \| '__/ __|
             *  / ____ \ (_| (_|  __/\__ \__ \ (_) | |  \__ \
             * /_/    \_\___\___\___||___/___/\___/|_|  |___/
             *                                               
            */

            /**
             * @brief Access element by index (unchecked)
             * 
             * @param index Zero-based index of the element
             * @return Reference to the element at the specified index
             * 
             * @warning No bounds checking is performed. Use at() for safe access.
             */
            constexpr T& operator[](size_t index) { return _data[index]; }
            
            /**
             * @brief Access element by index (unchecked, const version)
             * 
             * @param index Zero-based index of the element
             * @return Const reference to the element at the specified index
             * 
             * @warning No bounds checking is performed. Use at() for safe access.
             */
            constexpr T const& operator[](size_t index) const { return _data[index]; }

            /**
             * @brief Access element with bounds checking
             * 
             * @param index Zero-based index of the element
             * @return Reference to the element at the specified index
             * 
             * @throws Calls TRAP() if index is out of bounds
             */
            constexpr T& at(size_t index)
            {
                if (index < Size)
                {
                    return _data[index];
                }
                else
                {
                    TRAP("Index out of bounds.");
                    return _data[0]; // To satisfy compiler, will never reach here
                }
            }
            
            /**
             * @brief Access element with bounds checking (const version)
             * 
             * @param index Zero-based index of the element
             * @return Const reference to the element at the specified index
             * 
             * @throws Calls TRAP() if index is out of bounds
             */
            constexpr T const& at(size_t index) const
            {
                if (index < Size)
                {
                    return _data[index];
                }
                else
                {
                    TRAP("Index out of bounds.");
                    return _data[0]; // To satisfy compiler, will never reach here
                }
            }

            /*
             *  _____ _                 _                 
             * |_   _| |               | |                
             *   | | | |_ ___ _ __ __ _| |_ ___  _ __ ___ 
             *   | | | __/ _ \ '__/ _` | __/ _ \| '__/ __|
             *  _| |_| ||  __/ | | (_| | || (_) | |  \__ \
             * |_____|\__\___|_|  \__,_|\__\___/|_|  |___/
             *                                            
             */

            /**
             * @brief Get iterator to the beginning
             * 
             * @return Pointer to the first element
             */
            constexpr T* begin() { return _data; }
            
            /**
             * @brief Get const iterator to the beginning
             * 
             * @return Const pointer to the first element
             */
            constexpr T const* begin() const { return _data; }

            /**
             * @brief Get iterator to the end
             * 
             * @return Pointer to one past the last element
             */
            constexpr T* end() { return _data + Size; }
            
            /**
             * @brief Get const iterator to the end
             * 
             * @return Const pointer to one past the last element
             */
            constexpr T const* end() const { return _data + Size; }

            /**
             * @brief Get the number of elements
             * 
             * @return The size of the array (template parameter Size)
             */
            constexpr size_t size() const noexcept { return Size; }

            /*
             *  _    _ _   _ _ _ _         
             * | |  | | | (_) (_) |        
             * | |  | | |_ _| |_| |_ _   _ 
             * | |  | | __| | | | __| | | |
             * | |__| | |_| | | | |_| |_| |
             *  \____/ \__|_|_|_|\__|\__, |
             *                        __/ |
             *                       |___/ 
            */

            /**
             * @brief Fill all elements with the specified value
             * 
             * @param value The value to assign to all elements
             * 
             * @example
             * @code
             * mtl::array<int, 10> arr;
             * arr.fill(42); // All elements are now 42
             * @endcode
             */
            constexpr void fill(T const& value) noexcept
            {
                for (size_t index = 0; index < Size; ++index)
                {
                    _data[index] = value;
                }
            }

            /**
             * @brief Swap contents with another array
             * 
             * @param other The other array to swap with
             * 
             * @note Both arrays must have the same type and size
             */
            constexpr void swap(array& other) noexcept
            {
                for (size_t index = 0; index < Size; ++index)
                {
                    T temp = _data[index];
                    _data[index] = other[index];
                    other[index] = temp;
                }
            }

            /**
             * @brief Reverse the order of elements in the array
             * 
             * @example
             * @code
             * mtl::array<int, 5> arr = {1, 2, 3, 4, 5};
             * arr.reverse(); // arr is now {5, 4, 3, 2, 1}
             * @endcode
             */
            constexpr void reverse() noexcept
            {
                for (size_t index = 0; index < Size / 2; ++index)
                {
                    T temp = _data[index];
                    _data[index] = _data[Size - 1 - index];
                    _data[Size - 1 - index] = temp;
                }
            }

            /**
             * @brief Compare two arrays for equality
             * 
             * @param other The array to compare with
             * @return true if all elements are equal, false otherwise
             * 
             * @note Uses operator== for element comparison
             */
            constexpr bool operator==(array const& other) const
            {
                for (size_t index = 0; index < Size; ++index)
                {
                    if (_data[index] != other[index])
                        return false;
                }

                return true;
            }
            
        private:
            T _data[Size]; ///< Internal storage for array elements
    };
}

#endif // __MTL_ARRAY__INCLUDED__