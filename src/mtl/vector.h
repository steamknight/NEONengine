/**
 * @file vector.h
 * @brief Dynamic array container implementation
 * 
 * This file provides a constexpr-compatible dynamic array container
 * similar to std::vector but without standard library dependencies.
 * Uses custom memory allocation with configurable memory types.
 */

#ifndef __MTL_VECTOR__INCLUDED__
#define __MTL_VECTOR__INCLUDED__

#include <stddef.h>
#include "cstdint.h"
#include "memory.h"
#include "utility.h"

#ifdef AMIGA
#include <proto/exec.h>
#include <clib/exec_protos.h>
#endif

namespace mtl
{
    /**
     * @brief Dynamic array container with custom memory allocation
     * 
     * A dynamically resizable array container that provides safe array access
     * with automatic memory management. Uses custom allocators with specified
     * memory flags for Amiga-specific memory types (Fast/Chip/etc).
     * 
     * @tparam T The type of elements stored in the vector
     * @tparam MemFlags The memory allocation flags (MemF enum)
     * 
     * @example
     * @code
     * mtl::vector<int, mtl::MemF::Fast> numbers;
     * numbers.push_back(1);
     * numbers.push_back(2);
     * numbers[0] = 10;
     * for (auto& num : numbers) {
     *     // Process each number
     * }
     * @endcode
     * 
     * @see mtl::MemF
     */
    template<class T, MemF MemFlags = MemF::Fast>
    class vector
    {
    public:
        /**
         * @brief Default constructor
         * 
         * Creates an empty vector with no allocated storage.
         */
        constexpr vector() noexcept 
            : _data(nullptr), _size(0), _capacity(0) 
        {
        }

        /**
         * @brief Construct with initial capacity
         * 
         * @param initial_capacity Initial capacity to reserve
         */
        explicit vector(size_t initial_capacity)
            : _data(nullptr), _size(0), _capacity(0)
        {
            if (initial_capacity > 0) {
                reserve(initial_capacity);
            }
        }

        /**
         * @brief Construct with size and default value
         * 
         * @param count Number of elements to create
         * @param value Value to initialize elements with
         */
        vector(size_t count, const T& value)
            : vector()
        {
            assign(count, value);
        }

        /**
         * @brief Construct from C-style array
         * 
         * @tparam N Size of the array
         * @param initializer C-style array to initialize from
         */
        template<size_t N>
        vector(const T (&initializer)[N])
            : vector()
        {
            assign(N, initializer);
        }

        /**
         * @brief Copy constructor
         * 
         * @param other Vector to copy from
         */
        vector(const vector& other)
            : vector()
        {
            if (other._size > 0) {
                reserve(other._size);
                for (size_t i = 0; i < other._size; ++i) {
                    push_back(other._data[i]);
                }
            }
        }

        /**
         * @brief Move constructor
         * 
         * @param other Vector to move from
         */
        vector(vector&& other) noexcept
            : _data(other._data), _size(other._size), _capacity(other._capacity)
        {
            other._data = nullptr;
            other._size = 0;
            other._capacity = 0;
        }

        /**
         * @brief Destructor
         * 
         * Destroys all elements and deallocates memory.
         */
        ~vector() noexcept
        {
            clear();
            deallocate(_data, _capacity);
        }

        /**
         * @brief Copy assignment operator
         * 
         * @param other Vector to copy from
         * @return Reference to this vector
         */
        vector& operator=(const vector& other)
        {
            if (this != &other) {
                clear();
                if (other._size > 0) {
                    reserve(other._size);
                    for (size_t i = 0; i < other._size; ++i) {
                        push_back(other._data[i]);
                    }
                }
            }
            return *this;
        }

        /**
         * @brief Move assignment operator
         * 
         * @param other Vector to move from
         * @return Reference to this vector
         */
        vector& operator=(vector&& other) noexcept
        {
            if (this != &other) {
                clear();
                deallocate(_data, _capacity);
                
                _data = other._data;
                _size = other._size;
                _capacity = other._capacity;
                
                other._data = nullptr;
                other._size = 0;
                other._capacity = 0;
            }
            return *this;
        }

        /**
         * @brief Assign values to vector
         * 
         * @param count Number of elements
         * @param value Value to assign to all elements
         */
        void assign(size_t count, const T& value)
        {
            clear();
            if (count > 0) {
                reserve(count);
                for (size_t i = 0; i < count; ++i) {
                    push_back(value);
                }
            }
        }

        /**
         * @brief Assign values from C-style array
         * 
         * @tparam N Size of the array
         * @param array C-style array to copy from
         */
        template<size_t N>
        void assign(size_t count, const T (&array)[N])
        {
            clear();
            size_t elements_to_copy = (count < N) ? count : N;
            if (elements_to_copy > 0) {
                reserve(elements_to_copy);
                for (size_t i = 0; i < elements_to_copy; ++i) {
                    push_back(array[i]);
                }
            }
        }

        // Element access
        /**
         * @brief Access element by index (unchecked)
         * 
         * @param index Zero-based index of the element
         * @return Reference to the element at the specified index
         * 
         * @warning No bounds checking is performed. Use at() for safe access.
         */
        constexpr T& operator[](size_t index) noexcept
        {
            return _data[index];
        }

        /**
         * @brief Access element by index (unchecked, const version)
         * 
         * @param index Zero-based index of the element
         * @return Const reference to the element at the specified index
         * 
         * @warning No bounds checking is performed. Use at() for safe access.
         */
        constexpr T const& operator[](size_t index) const noexcept
        {
            return _data[index];
        }

        /**
         * @brief Access element with bounds checking
         * 
         * @param index Zero-based index of the element
         * @return Reference to the element at the specified index
         */
        constexpr T& at(size_t index)
        {
            if (index >= _size) {
                LOG_CRASH("Vector index out of bounds");
                return _data[0]; // Return something to avoid warnings
            }
            return _data[index];
        }

        /**
         * @brief Access element with bounds checking (const version)
         * 
         * @param index Zero-based index of the element
         * @return Const reference to the element at the specified index
         */
        constexpr T const& at(size_t index) const
        {
            if (index >= _size) {
                LOG_CRASH("Vector index out of bounds");
                return _data[0]; // Return something to avoid warnings
            }
            return _data[index];
        }

        /**
         * @brief Access first element
         * 
         * @return Reference to the first element
         * 
         * @warning Undefined behavior if vector is empty
         */
        constexpr T& front() noexcept
        {
            return _data[0];
        }

        /**
         * @brief Access first element (const version)
         * 
         * @return Const reference to the first element
         * 
         * @warning Undefined behavior if vector is empty
         */
        constexpr T const& front() const noexcept
        {
            return _data[0];
        }

        /**
         * @brief Access last element
         * 
         * @return Reference to the last element
         * 
         * @warning Undefined behavior if vector is empty
         */
        constexpr T& back() noexcept
        {
            return _data[_size - 1];
        }

        /**
         * @brief Access last element (const version)
         * 
         * @return Const reference to the last element
         * 
         * @warning Undefined behavior if vector is empty
         */
        constexpr T const& back() const noexcept
        {
            return _data[_size - 1];
        }

        /**
         * @brief Get pointer to underlying data
         * 
         * @return Pointer to the underlying data
         */
        constexpr T* data() noexcept
        {
            return _data;
        }

        /**
         * @brief Get pointer to underlying data (const version)
         * 
         * @return Const pointer to the underlying data
         */
        constexpr const T* data() const noexcept
        {
            return _data;
        }

        // Iterators
        /**
         * @brief Get iterator to the beginning
         * 
         * @return Iterator to the first element
         */
        constexpr T* begin() noexcept
        {
            return _data;
        }

        /**
         * @brief Get const iterator to the beginning
         * 
         * @return Const iterator to the first element
         */
        constexpr T const* begin() const noexcept
        {
            return _data;
        }

        /**
         * @brief Get const iterator to the beginning
         * 
         * @return Const iterator to the first element
         */
        constexpr T const* cbegin() const noexcept
        {
            return _data;
        }

        /**
         * @brief Get iterator to the end
         * 
         * @return Iterator to one past the last element
         */
        constexpr T* end() noexcept
        {
            return _data + _size;
        }

        /**
         * @brief Get const iterator to the end
         * 
         * @return Const iterator to one past the last element
         */
        constexpr T const* end() const noexcept
        {
            return _data + _size;
        }

        /**
         * @brief Get const iterator to the end
         * 
         * @return Const iterator to one past the last element
         */
        constexpr T const* cend() const noexcept
        {
            return _data + _size;
        }

        // Capacity
        /**
         * @brief Check if vector is empty
         * 
         * @return true if vector has no elements, false otherwise
         */
        constexpr bool empty() const noexcept
        {
            return _size == 0;
        }

        /**
         * @brief Get the number of elements
         * 
         * @return The number of elements in the vector
         */
        constexpr size_t size() const noexcept
        {
            return _size;
        }

        /**
         * @brief Get the maximum possible number of elements
         * 
         * @return The maximum number of elements the vector can hold based on available memory
         */
        constexpr size_t max_size() const noexcept
        {
            if (__builtin_is_constant_evaluated()) {
                // During constant evaluation, return theoretical maximum
                return static_cast<size_t>(-1) / sizeof(T);
            } else {
                // At runtime, return actual available memory divided by element size
                ULONG available_bytes = AvailMem(static_cast<ULONG>(MemFlags));
                return available_bytes / sizeof(T);
            }
        }

        /**
         * @brief Reserve storage for at least the specified number of elements
         * 
         * @param new_capacity Minimum capacity to reserve
         */
        void reserve(size_t new_capacity)
        {
            if (new_capacity > _capacity) {
                reallocate(new_capacity);
            }
        }

        /**
         * @brief Get the current capacity
         * 
         * @return The number of elements that can be stored without reallocation
         */
        constexpr size_t capacity() const noexcept
        {
            return _capacity;
        }

        /**
         * @brief Reduce capacity to fit the current size
         */
        constexpr void shrink_to_fit()
        {
            if (_capacity > _size) {
                if (_size == 0) {
                    deallocate(_data, _capacity);
                    _data = nullptr;
                    _capacity = 0;
                } else {
                    reallocate(_size);
                }
            }
        }

        // Modifiers
        /**
         * @brief Remove all elements
         */
        void clear() noexcept
        {
            for (size_t i = 0; i < _size; ++i) {
                destroy_at(&_data[i]);
            }
            _size = 0;
        }

        /**
         * @brief Add element to the end
         * 
         * @param value Element to add
         */
        void push_back(const T& value)
        {
            if (_size >= _capacity) {
                size_t new_capacity = _capacity == 0 ? DEFAULT_CAPACITY : _capacity * GROWTH_FACTOR;
                reallocate(new_capacity);
            }
            construct_at(&_data[_size], value);
            ++_size;
        }

        /**
         * @brief Add element to the end (move version)
         * 
         * @param value Element to move
         */
        void push_back(T&& value)
        {
            if (_size >= _capacity) {
                size_t new_capacity = _capacity == 0 ? DEFAULT_CAPACITY : _capacity * GROWTH_FACTOR;
                reallocate(new_capacity);
            }
            construct_at(&_data[_size], move(value));
            ++_size;
        }

        /**
         * @brief Construct element in-place at the end
         * 
         * @tparam Args Types of arguments to forward
         * @param args Arguments to forward to constructor
         * @return Reference to the newly constructed element
         */
        template<typename... Args>
        T & emplace_back(Args&&... args)
        {
            if (_size >= _capacity) {
                size_t new_capacity = _capacity == 0 ? DEFAULT_CAPACITY : _capacity * GROWTH_FACTOR;
                reallocate(new_capacity);
            }
            construct_at(&_data[_size], forward<Args>(args)...);
            ++_size;
            return _data[_size - 1];
        }

        /**
         * @brief Remove the last element
         * 
         * @warning Undefined behavior if vector is empty
         */
        void pop_back()
        {
            if (_size > 0) {
                --_size;
                destroy_at(&_data[_size]);
            }
        }

        /**
         * @brief Resize the vector to contain count elements
         * 
         * @param count New size of the vector
         */
        void resize(size_t count)
        {
            if (count > _size) {
                if (count > _capacity) {
                    reallocate(count);
                }
                for (size_t i = _size; i < count; ++i) {
                    construct_at(&_data[i]);
                }
            } else if (count < _size) {
                for (size_t i = count; i < _size; ++i) {
                    destroy_at(&_data[i]);
                }
            }
            _size = count;
        }

        /**
         * @brief Resize the vector to contain count elements
         * 
         * @param count New size of the vector
         * @param value Value to initialize new elements with
         */
        void resize(size_t count, const T& value)
        {
            if (count > _size) {
                if (count > _capacity) {
                    reallocate(count);
                }
                for (size_t i = _size; i < count; ++i) {
                    construct_at(&_data[i], value);
                }
            } else if (count < _size) {
                for (size_t i = count; i < _size; ++i) {
                    destroy_at(&_data[i]);
                }
            }
            _size = count;
        }

        /**
         * @brief Swap contents with another vector
         * 
         * @param other The other vector to swap with
         */
        void swap(vector& other) noexcept
        {
            T* temp_data = _data;
            size_t temp_size = _size;
            size_t temp_capacity = _capacity;

            _data = other._data;
            _size = other._size;
            _capacity = other._capacity;

            other._data = temp_data;
            other._size = temp_size;
            other._capacity = temp_capacity;
        }

        // Comparison operators
        /**
         * @brief Compare two vectors for equality
         * 
         * @param other The vector to compare with
         * @return true if all elements are equal, false otherwise
         */
        constexpr bool operator==(const vector& other) const
        {
            if (_size != other._size) {
                return false;
            }
            for (size_t i = 0; i < _size; ++i) {
                if (_data[i] != other._data[i]) {
                    return false;
                }
            }
            return true;
        }

        /**
         * @brief Compare two vectors for inequality
         * 
         * @param other The vector to compare with
         * @return true if vectors are not equal, false otherwise
         */
        constexpr bool operator!=(const vector& other) const
        {
            return !(*this == other);
        }

    private:
        T* _data;           ///< Pointer to the allocated data
        size_t _size;       ///< Current number of elements
        size_t _capacity;   ///< Current allocated capacity

        /**
         * @brief Initial capacity for new vectors
         */
        static constexpr size_t DEFAULT_CAPACITY = 4;

        /**
         * @brief Growth factor for capacity expansion (multiplier)
         */
        static constexpr size_t GROWTH_FACTOR = 2;

        /**
         * @brief Allocate memory for n elements
         * 
         * @param n Number of elements to allocate for
         * @return Pointer to allocated memory, or nullptr on failure
         */
        T* allocate(size_t n) noexcept
        {
            if (n == 0) return nullptr;
            // Always route through our custom operator new that handles
            // size tracking. This keeps allocation/deallocation symmetric
            // and avoids mismatches with memFree sizing.
            return static_cast<T*>(::operator new(n * sizeof(T), MemFlags));
        }

        /**
         * @brief Deallocate memory
         * 
         * @param ptr Pointer to memory to deallocate
         * @param n Number of elements (may be unused depending on allocator)
         */
        void deallocate(T* ptr, size_t /*n*/) noexcept
        {
            if (ptr) {
                ::operator delete(ptr); // Our global delete uses stored size
            }
        }

        /**
         * @brief Construct elements in-place using placement new
         * 
         * @param ptr Pointer to location to construct (memory already allocated)
         * @param args Arguments to forward to constructor
         */
        template<typename... Args>
        void construct_at(T* ptr, Args&&... args) noexcept
        {
            new(ptr) T(forward<Args>(args)...);
        }

        /**
         * @brief Destroy elements by calling destructor
         * 
         * @param ptr Pointer to element to destroy
         */
        void destroy_at(T* ptr) noexcept
        {
            ptr->~T();
        }

        /**
         * @brief Reallocate storage to new capacity
         * 
         * @param new_capacity The new capacity to allocate
         */
        void reallocate(size_t new_capacity)
        {
            if (new_capacity == 0) {
                clear();
                shrink_to_fit();
                return;
            }

            T* new_data = allocate(new_capacity);
            if (!new_data) {
                LOG_CRASH("Vector reallocation failed: out of memory");
                return;
            }

            // Move/copy existing elements
            for (size_t i = 0; i < _size; ++i) {
                if constexpr (requires { move(_data[i]); }) {
                    construct_at(&new_data[i], move(_data[i]));
                } else {
                    construct_at(&new_data[i], _data[i]);
                }
                destroy_at(&_data[i]);
            }

            deallocate(_data, _capacity);
            _data = new_data;
            _capacity = new_capacity;
        }
    };
}

#endif // __MTL_VECTOR__INCLUDED__
