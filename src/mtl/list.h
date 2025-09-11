/**
 * @file list.h
 * @brief Minimal singly-linked forward list container implementation
 *
 * This file provides a constexpr-compatible, minimal forward list container
 * similar to std::forward_list but without standard library dependencies.
 * Uses custom memory allocation with configurable memory types (MemF).
 */

#ifndef __MTL_LIST__INCLUDED__
#define __MTL_LIST__INCLUDED__

#include <stddef.h>

#include <mtl/memory.h>
#include <mtl/utility.h>

namespace mtl
{
    /**
     * @brief Minimal singly-linked forward list container
     *
     * A minimal, non-throwing, singly-linked list container. All allocations use
     * the specified MemF memory type. No standard library dependencies.
     *
     * @tparam T The type of elements stored in the list
     * @tparam MemFlags The memory allocation flags (MemF enum)
     *
     * @example
     * @code
     * mtl::forward_list<int, mtl::MemF::Fast> list;
     * list.push_front(42);
     * for (auto& value : list) {
     *     // Process value
     * }
     * @endcode
     */
    template<class T, MemF MemFlags = MemF::Fast>
    class forward_list
    {
        private
            :  ///////////////////////////////////////////////////////////////////////////////////
        struct node
        {
            T value;
            node* next;
        };
        node* _head;
        node* _tail;
        size_t _size;  ///< Number of elements (tracked for O(1) size())

        public:  ///////////////////////////////////////////////////////////////////////////////////
        using value_type      = T;
        using pointer         = value_type*;
        using const_pointer   = value_type const*;
        using reference       = value_type&;
        using const_reference = value_type const&;
        using size_type       = size_t;

        public:  ///////////////////////////////////////////////////////////////////////////////////
        /**
         * @brief Default constructor
         *
         * Creates an empty list.
         */
        constexpr forward_list() noexcept : _head(nullptr), _tail(nullptr), _size(0) {}

        /**
         * @brief Destructor
         *
         * Destroys all nodes and deallocates memory.
         */
        ~forward_list() noexcept { clear(); }

        // Copy operations deleted (unique ownership of node chain)
        forward_list(forward_list const&)            = delete;
        forward_list& operator=(forward_list const&) = delete;

        // Move transfers ownership safely
        forward_list(forward_list&& other) noexcept
            : _head(other._head)
            , _tail(other._tail)
            , _size(other._size)
        {
            other._head = other._tail = nullptr;
            other._size               = 0;
        }

        forward_list& operator=(forward_list&& other) noexcept
        {
            if (this != &other)
            {
                clear();
                _head       = other._head;
                _tail       = other._tail;
                _size       = other._size;
                other._head = other._tail = nullptr;
                other._size               = 0;
            }
            return *this;
        }

        /*
         *     /\
         *    /  \   ___ ___ ___  ___ ___
         *   / /\ \ / __/ __/ _ \/ __/ __|
         *  / ____ \ (_| (_|  __/\__ \__ \
         * /_/    \_\___\___\___||___/___/
         *
         */

        /**
         * @brief Get a reference to the first element
         * @return Reference to the first element
         *
         * @note Logs and crashes if the list is empty
         */
        reference front() noexcept
        {
            if (!_head) LOG_CRASH("forward_list::front() called on empty list");

            return _head->value;
        }

        /**
         * @brief Get a const reference to the first element
         * @return const_reference to the first element
         *
         * @note Logs and crashes if the list is empty
         */
        const_reference front() const noexcept
        {
            if (!_head) LOG_CRASH("forward_list::front() called on empty list");

            return _head->value;
        }

        /**
         * @brief Get a reference to the last element
         * @return Reference to the last element
         *
         * @note Logs and crashes if the list is empty
         */
        reference back() noexcept
        {
            if (!_tail) LOG_CRASH("forward_list::back() called on empty list");

            return _tail->value;
        }

        /**
         * @brief Get a const reference to the last element
         * @return const_reference to the last element
         *
         * @note Logs and crashes if the list is empty
         */
        const_reference back() const noexcept
        {
            if (!_tail) LOG_CRASH("forward_list::back() called on empty list");

            return _tail->value;
        }

        /**
         * @brief Get a reference to the element at the specified index
         *
         * @param index The index of the element to access
         * @return T& A reference to the element at the specified index
         */
        reference operator[](size_t index)
        {
            node* current = _head;
            while (current && index > 0)
            {
                current = current->next;
                --index;
            }

            if (!current)
            {
                LOG_CRASH("forward_list::operator[] index out of bounds");
            }

            return current->value;
        }

        /**
         * @brief Get a const reference to the element at the specified index
         *
         * @param index The index of the element to access
         * @return T const& A const reference to the element at the specified index
         */
        const_reference operator[](size_t index) const
        {
            node* current = _head;
            while (current && index > 0)
            {
                current = current->next;
                --index;
            }

            if (!current)
            {
                LOG_CRASH("forward_list::operator[] index out of bounds");
            }

            return current->value;
        }

        /*
         *  _____                       _ _
         * / ____|                     (_) |
         * | |     __ _ _ __   __ _  ___ _| |_ _   _
         * | |    / _` | '_ \ / _` |/ __| | __| | | |
         * | |___| (_| | |_) | (_| | (__| | |_| |_| |
         * \_____\__,_| .__/ \__,_|\___|_|\__|\__, |
         *             | |                      __/ |
         *             |_|                     |___/
         */

        /**
         * @brief Check if the list is empty
         * @return true if list is empty, false otherwise
         */
        constexpr bool is_empty() const noexcept { return _head == nullptr; }

        /**
         * @brief Get the number of elements in the list
         * @return Number of elements
         * @note O(1) complexity
         */
        constexpr size_t size() const noexcept { return _size; }

        /**
         * @brief Remove all elements from the list
         */
        void clear() noexcept
        {
            node* current = _head;

            while (current)
            {
                node* next = current->next;
                _destroy_node(current);
                current = next;
            }

            _head = nullptr;
            _tail = nullptr;
            _size = 0;
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
         * @brief Iterator for forward_list
         */
        class const_iterator;
        class iterator
        {
            public:
            node* current;

            explicit iterator(node* n) : current(n) {}
            T& operator*() const { return current->value; }
            T* operator->() const { return &current->value; }
            operator const_iterator() const { return const_iterator(current); }
            iterator& operator++()
            {
                current = current->next;
                return *this;
            }
            iterator operator++(int)
            {
                iterator tmp = *this;
                ++(*this);
                return tmp;
            }
            bool operator==(iterator const& other) const { return current == other.current; }
            bool operator!=(iterator const& other) const { return current != other.current; }
        };

        /**
         * @brief Const iterator for forward_list
         */
        class const_iterator
        {
            public:
            node const* current;

            explicit const_iterator(node const* n) : current(n) {}
            T const& operator*() const { return current->value; }
            T const* operator->() const { return &current->value; }
            const_iterator& operator++()
            {
                current = current->next;
                return *this;
            }
            const_iterator operator++(int)
            {
                const_iterator tmp = *this;
                ++(*this);
                return tmp;
            }
            bool operator==(const_iterator const& other) const { return current == other.current; }
            bool operator!=(const_iterator const& other) const { return current != other.current; }
        };

        /**
         * @brief Get iterator to the beginning of the list
         * @return Iterator to the first element
         */
        iterator begin() noexcept { return iterator(_head); }

        /**
         * @brief Get const iterator to the beginning of the list
         * @return Const iterator to the first element
         */
        const_iterator begin() const noexcept { return const_iterator(_head); }

        /**
         * @brief Get const iterator to the beginning of the list
         * @return Const iterator to the first element
         */
        const_iterator cbegin() const noexcept { return const_iterator(_head); }

        /**
         * @brief Get iterator to the end of the list
         * @return Iterator to one past the last element
         */
        iterator end() noexcept { return iterator(nullptr); }

        /**
         * @brief Get const iterator to the end of the list
         * @return Const iterator to one past the last element
         */
        const_iterator end() const noexcept { return const_iterator(nullptr); }

        /**
         * @brief Get const iterator to the end of the list
         * @return Const iterator to one past the last element
         */
        const_iterator cend() const noexcept { return const_iterator(nullptr); }

        /*
         *  __  __           _ _  __ _
         * |  \/  |         | (_)/ _(_)
         * | \  / | ___   __| |_| |_ _  ___ _ __ ___
         * | |\/| |/ _ \ / _` | |  _| |/ _ \ '__/ __|
         * | |  | | (_) | (_| | | | | |  __/ |  \__ \
         * |_|  |_|\___/ \__,_|_|_| |_|\___|_|  |___/
         *
         */

        /**
         * @brief Add an element to the front of the list
         * @param value Element to add
         * @note O(1) complexity
         */
        void push_front(const_reference value)
        {
            node* new_node = _create_node(value);
            if (!new_node)
            {
                LOG_CRASH("forward_list::push_front memory allocation failed");
                return;
            }

            _create_head(new_node);
        }

        /**
         * @brief Add an element to the front of the list (move version)
         * @param value Element to move
         *
         * @note O(1) complexity
         */
        void push_front(T&& value)
        {
            node* new_node = _create_node(mtl::move(value));
            if (!new_node)
            {
                LOG_CRASH("forward_list::push_front memory allocation failed");
                return;
            }

            _create_head(new_node);
        }

        /**
         * @brief Add an element to the back of the list
         * @param value Element to add
         *
         * @note O(1) complexity
         */
        void push_back(T const& value)
        {
            node* new_node = _create_node(value);
            if (!new_node)
            {
                LOG_CRASH("forward_list::push_back memory allocation failed");
                return;
            }

            _create_tail(new_node);
        }

        /**
         * @brief Add an element to the back of the list (move version)
         * @param value Element to move
         *
         * @note O(1) complexity
         */
        void push_back(T&& value)
        {
            node* new_node = _create_node(mtl::move(value));

            if (!new_node)
            {
                LOG_CRASH("forward_list::push_back memory allocation failed");
                return;
            }

            _create_tail(new_node);
        }

        /**
         * @brief Remove the first element from the list.
         * Does nothing if the list is empty.
         *
         * @note O(1) complexity
         */
        void pop_front()
        {
            if (!_head) return;

            node* old_head = _head;
            _head          = _head->next;

            if (!_head) _tail = nullptr;

            _destroy_node(old_head);

            --_size;
        }

        /**
         * @brief Remove the last element from the list.
         * Does nothing if the list is empty.
         *
         * @note O(n) complexity
         */
        void pop_back()
        {
            if (!_head) return;

            if (_head == _tail)
            {
                _destroy_node(_head);
                _head = _tail = nullptr;
                _size         = 0;
                return;
            }

            node* prev = _head;

            while (prev->next != _tail)
            {
                prev = prev->next;
            }

            _destroy_node(_tail);

            _tail       = prev;
            _tail->next = nullptr;
            --_size;
        }

        /**
         * @brief Swap contents with another forward_list
         * @param other The other list to swap with
         */
        void swap(forward_list& other) noexcept
        {
            mtl::swap(_head, other._head);
            mtl::swap(_tail, other._tail);
            mtl::swap(_size, other._size);
        }

        /**
         * @brief Insert a value after the given iterator
         *
         * @param pos Iterator after which to insert
         * @param value Value to insert
         * @return Iterator to the inserted node
         *
         * @note O(1) complexity
         */
        iterator insert_after(iterator pos, T const& value)
        {
            if (pos == end()) return end();

            node* new_node = _create_node(value);
            if (!new_node) return end();

            return _insert_after(pos, new_node);
        }

        /**
         * @brief Insert a value after the given iterator (move version)
         * @param pos Iterator after which to insert
         * @param value Value to insert
         * @return Iterator to the inserted node
         * @note O(1) complexity
         */
        iterator insert_after(iterator pos, T&& value)
        {
            if (pos == end()) return end();

            node* new_node = _create_node(mtl::move(value));
            if (!new_node) return end();

            return _insert_after(pos, new_node);
        }

        /**
         * @brief Erase the node after the given iterator
         *
         * @param pos Iterator before the node to erase
         * @return Iterator to the node after the erased node
         *
         * @note O(1) complexity
         */
        iterator erase_after(iterator pos)
        {
            if (pos == end() || !pos.current->next) return end();

            node* to_erase    = pos.current->next;
            pos.current->next = to_erase->next;

            if (_tail == to_erase) _tail = pos.current;

            _destroy_node(to_erase);
            --_size;

            return iterator(pos.current->next);
        }

        /**
         * @brief Emplace (construct) an element at the front (O(1))
         *
         * @tparam Args Constructor argument types
         * @param args Constructor arguments
         *
         * @return Pointer to constructed element or nullptr on allocation failure
         */
        template<typename... Args>
        T* emplace_front(Args&&... args)
        {
            node* new_node = _create_node_emplace(mtl::forward<Args>(args)...);

            if (!new_node)
            {
                LOG_CRASH("forward_list::emplace_front memory allocation failed");
                return nullptr;
            }

            _create_head(new_node);
            return &new_node->value;
        }
        /**
         * @brief Emplace (construct) an element at the back (O(1))
         *
         * @tparam Args Constructor argument types
         * @param args Constructor arguments
         *
         * @return Pointer to constructed element or nullptr on allocation failure
         */
        template<typename... Args>
        T* emplace_back(Args&&... args)
        {
            node* new_node = _create_node_emplace(mtl::forward<Args>(args)...);

            if (!new_node)
            {
                LOG_CRASH("forward_list::emplace_back memory allocation failed");
                return nullptr;
            }

            _create_tail(new_node);
            return &new_node->value;
        }

        /**
         * @brief Emplace (construct) element after position (O(1))
         *
         * @tparam Args Constructor argument types
         * @param pos Iterator after which to insert
         * @param args Constructor arguments
         *
         * @return Pointer to constructed element or nullptr on allocation failure
         */
        template<typename... Args>
        T* emplace_after(iterator pos, Args&&... args)
        {
            if (pos == end()) return nullptr;

            node* new_node = _create_node_emplace(mtl::forward<Args>(args)...);

            if (!new_node)
            {
                LOG_CRASH("forward_list::emplace_after memory allocation failed");
                return nullptr;
            }

            new_node->next    = pos.current->next;
            pos.current->next = new_node;

            if (_tail == pos.current) _tail = new_node;

            ++_size;

            return &new_node->value;
        }

        private:  //////////////////////////////////////////////////////////////////////////////////
        node* _create_node(T const& value)
        {
            node* n = new (MemFlags) node{ value, nullptr };
            return n;
        }

        node* _create_node(T&& value)
        {
            node* n = new (MemFlags) node{ mtl::move(value), nullptr };
            return n;
        }

        void _create_head(node* head)
        {
            if (!head) return;

            head->next = _head;
            _head      = head;

            if (!_tail) _tail = head;

            ++_size;
        }

        void _create_tail(node* tail)
        {
            if (!tail) return;

            tail->next = nullptr;

            if (_tail) _tail->next = tail;

            _tail = tail;

            if (!_head) _head = tail;

            ++_size;
        }

        iterator _insert_after(iterator pos, node* new_node)
        {
            if (!new_node) return end();

            new_node->next    = pos.current->next;
            pos.current->next = new_node;

            if (_tail == pos.current) _tail = new_node;

            ++_size;

            return iterator(new_node);
        }

        template<typename... Args>
        node* _create_node_emplace(Args&&... args)
        {
            return new (MemFlags) node{ T(mtl::forward<Args>(args)...), nullptr };
        }

        void _destroy_node(node* n) noexcept
        {
            n->~node();
            ::operator delete(n, MemFlags);
        }
    };
}  // namespace mtl

#endif  // __MTL_LIST__INCLUDED__
