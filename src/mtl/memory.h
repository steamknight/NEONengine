#ifndef __MTL__MEMORY__INCLUDED__
#define __MTL__MEMORY__INCLUDED__

#include <stddef.h>
#include <stdint.h>

#include <ace/managers/memory.h>

// Placement new declaration (since we don't use std::new)
inline void* operator new(decltype(sizeof(int)), void* ptr) noexcept
{
    return ptr;
}

namespace mtl
{
/**
 * The global new and delete use Fast memory by default.
 * Define USE_CHIP_MEMORY to make all global new/delete use Chip memory.
 */
#ifdef USE_CHIP_MEMORY
#define GLOBAL_MEM_FLAGS MEMF_CHIP
#else
#define GLOBAL_MEM_FLAGS MEMF_FAST
#endif

    /**
     * Memory allocation flags, maps directly to MEMF_*
     */
    enum class MemF : int32_t
    {
        Any      = MEMF_ANY,  // Any type of memory will do
        Public   = MEMF_PUBLIC,
        Chip     = MEMF_CHIP,
        Fast     = MEMF_FAST,
        Local    = MEMF_LOCAL,     // Memory that does not go away at RESET
        Dma24Bit = MEMF_24BITDMA,  // DMAable memory within 24 bits of addres
        Kick     = MEMF_KICK,      // Memory that can be used for KickTags

        Clear   = MEMF_CLEAR,    // AllocMem: NULL out area before return
        Largest = MEMF_LARGEST,  // AvailMem: return the largest chunk size
        Reverse = MEMF_REVERSE,  // AllocMem: allocate from the top down
        Total   = MEMF_TOTAL,    // AvailMem: return total size of memory

        NoExpunge = MEMF_NO_EXPUNGE,  // AllocMem: Do not cause expunge on failure
    };

    /**
     * Bitwise OR operator for MemF to allow combining flags.
     *
     * @param lhs Left hand side of the operation
     * @param rhs Right hand side of the operation
     * @return MemF The combined flags
     */
    constexpr MemF operator|(MemF lhs, MemF rhs)
    {
        return static_cast<MemF>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
    }

    /**
     * Manages the lifetime of a raw pointer. Can have a function or lambda as
     * a custom deleter for that type allowing easy deletion of pointers created
     * by C functions.
     *
     * @example
     * using bitmap_ptr = unique_ptr<tBitMap, bitmapDestroy>;
     * auto pBitmap = bitmap_ptr(bitmapCreate(...));
     */
    template<class T, auto Deleter = [](T*) {}>
    class unique_ptr
    {
        public:
        /**
         * @brief Constructor. Takes ownership of the pointer.
         *
         * @param pointer The pointer to manage
         */
        explicit unique_ptr(T* pointer) noexcept : _pointer(pointer) {}

        unique_ptr(nullptr_t) noexcept : _pointer(nullptr) {}

        /*
         * Copy constructor and assignment are removed as, like the
         * Highlander, there can be only one.
         */
        unique_ptr(unique_ptr const&)            = delete;
        unique_ptr& operator=(unique_ptr const&) = delete;

        /**
         * @brief Moves ownership of the pointer from one unique_ptr to another.
         *
         * @param other
         */
        unique_ptr(unique_ptr&& other) noexcept : _pointer(other._pointer)
        {
            other._pointer = nullptr;
        }

        /**
         * @brief Moves ownership of the pointer from one unique_ptr to another.
         */
        unique_ptr& operator=(unique_ptr&& other) noexcept
        {
            reset(other.release());
            return *this;
        }

        /**
         * @brief Destroy the unique ptr object. Calls the Deleter on the
         * pointer.
         */
        ~unique_ptr() noexcept
        {
            if (_pointer) { Deleter(_pointer); }
        }

        /**
         * @brief Replaces the currently managed pointer with another, calling
         * the deleter on the old one.
         *
         * @param new_pointer The new pointer to manage
         */
        void reset(T* new_pointer)
        {
            if (_pointer != new_pointer)
            {
                if (_pointer) { Deleter(_pointer); }
                _pointer = new_pointer;
            }
        }

        /**
         * @brief Releases ownership of the managed pointer without deleting it.
         * Caller becomes responsible for deleting it.
         *
         * @return T* The released pointer.
         */
        T* release() noexcept
        {
            T* old   = _pointer;
            _pointer = nullptr;
            return old;
        }

        /**
         * @brief Gives access to the owned pointer so that the unique_ptr
         * can be passed into function expecting the underlying type poitner.
         *
         * The unique_ptr is still responsible for destroying the pointer.
         *
         * @return T* The owned pointer
         */
        T* get() { return _pointer; }

        /**
         * @brief Convenience operator to easily verify if the pointer is valid.
         *
         * @return true Pointer is valid.
         * @return false Pointer is nullptr.
         */
        explicit operator bool() const noexcept { return _pointer != nullptr; }

        /**
         * @brief Dereference operators to access the underlying pointer.
         */
        T* operator->() const noexcept { return _pointer; }

        /**
         * @brief Dereference operator to access the underlying pointer.
         */
        T& operator*() const noexcept { return *_pointer; }

        private:
        T* _pointer;
    };

    template<typename T, typename... Args>
    unique_ptr<T> make_unique(Args&&... args)
    {
        return unique_ptr<T>(new T(forward<Args>(args)...));
    }
}  // namespace mtl

// Placement new operators that take MemF for memory type selection
void* operator new(decltype(sizeof(int)) size, mtl::MemF memFlags) noexcept;
void* operator new[](decltype(sizeof(int)) size, mtl::MemF memFlags) noexcept;

// Note: Placement delete operators are only called if the constructor throws
// Since we're using noexcept, these are mainly for completeness
void operator delete(void* ptr, mtl::MemF memFlags) noexcept;
void operator delete[](void* ptr, mtl::MemF memFlags) noexcept;

#endif  //__MTL__MEMORY__INCLUDED__