/**
 * @file memory.cpp
 * @brief Global / flagged allocation utilities and operator new/delete overrides.
 *
 * Implements sized allocation tracking used by all dynamic memory in the
 * engine. A 4-byte size header (rounded up to max alignment) is stored in
 * front of the returned user pointer so deallocation does not need the
 * original allocation size. (This mimics the behaviour of sized delete in
 * modern C++ but in a portable, freestanding way.)
 *
 * Optional overwrite detection can be enabled with the debug canary macro
 * (MTL_DEBUG_CANARY) which places a 32-bit sentinel immediately after the
 * user block. A mismatch on free logs a corruption warning.
 *
 * Design goals:
 *  - No dependency on the standard library (freestanding / nostdlib build)
 *  - Symmetric operator new/delete paths (always go through tracking layer)
 *  - Alignment preserved for any type up to alignof(max_align_t)
 *  - Simple to instrument or extend (e.g. add leak list, guard patterns)
 *
 * Limitations / Notes:
 *  - Thread safety is not provided (single–threaded target).
 *  - Large allocations (> 4 GiB) are not supported (size stored in 32 bits).
 *  - Canary only detects simple buffer overruns (not underruns or use‑after‑free).
 */
#include "memory.h"

#include <ace/managers/log.h>

#include "utility.h"

extern "C"
{
#include <stddef.h>
}

/**
 * @def MTL_DEBUG_CANARY
 * @brief Enables post-block 32-bit canary for rudimentary overwrite detection.
 *
 * When defined, each allocation appends a 32-bit sentinel (0xDEADBEEF) after
 * the user data. On free the sentinel is validated and a log message is emitted
 * if it has been altered.
 */
#define MTL_DEBUG_CANARY

using namespace mtl;

/**
 * @section AllocationLayout Allocation Layout
 * Memory returned to the caller has a hidden header in front of it in order to
 * store the original requested size. This enables deallocation without the
 * caller supplying the size (mirrors typical malloc implementation techniques).
 *
 * Layout (addresses increase left → right):
 * @code
 * [ 4-byte size ][ optional padding .. ][ user data ........ ][ canary ]
 * ^ raw pointer returned by memAlloc()      ^ returned to user  ^ (debug only)
 * @endcode
 */
static constexpr size_t RAW_SIZE_FIELD_SIZE = sizeof(uint32_t);
static constexpr size_t ALIGNMENT           = alignof(max_align_t);
static constexpr size_t HEADER_SIZE
    = (RAW_SIZE_FIELD_SIZE + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1);  // rounded up

#ifdef MTL_DEBUG_CANARY
static constexpr uint32_t CANARY_VALUE = 0xDEADBEEF;
#endif

/**
 * @brief Allocate a block with size tracking (and optional canary).
 *
 * @param size      Number of user bytes requested.
 * @param memFlags  Amiga memory flags (FAST / CHIP etc.).
 * @param errorMsg  Message to log upon allocation failure.
 * @return Pointer to usable memory (or nullptr on failure).
 */
static void* allocWithSizeTracking(size_t size, ULONG memFlags, char const* errorMsg)
{
    size_t totalSize = HEADER_SIZE + size;
#ifdef MTL_DEBUG_CANARY
    totalSize += sizeof(uint32_t);  // space for trailing canary
#endif

    uint8_t* raw = static_cast<uint8_t*>(memAlloc(totalSize, memFlags));
    if (!raw)
    {
        logWrite("%s", errorMsg);
        return nullptr;
    }

    // Write size at beginning (first 4 bytes). Padding bytes (if any) left unspecified.
    *reinterpret_cast<uint32_t*>(raw) = static_cast<uint32_t>(size);

    uint8_t* userPtr = raw + HEADER_SIZE;

#ifdef MTL_DEBUG_CANARY
    // Place canary immediately after user block
    *reinterpret_cast<uint32_t*>(userPtr + size) = CANARY_VALUE;
#endif

    return userPtr;
}

/**
 * @brief Free a block previously allocated by allocWithSizeTracking.
 *
 * Validates the optional canary (if enabled) and logs if corruption is
 * detected. The original size is recovered from the header then passed to
 * memFree.
 *
 * @param ptr User pointer returned from operator new / allocWithSizeTracking.
 */
static void deallocWithSizeTracking(void* ptr)
{
    if (!ptr) return;
    uint8_t* userPtr    = static_cast<uint8_t*>(ptr);
    uint8_t* raw        = userPtr - HEADER_SIZE;
    uint32_t storedSize = *reinterpret_cast<uint32_t*>(raw);

#ifdef MTL_DEBUG_CANARY
    uint32_t* canary = reinterpret_cast<uint32_t*>(userPtr + storedSize);
    if (*canary != CANARY_VALUE)
    {
        logWrite("Memory corruption detected (canary mismatch) for block %p size %lu",
                 ptr,
                 static_cast<unsigned long>(storedSize));
    }
#endif

    size_t totalSize = HEADER_SIZE + storedSize;
#ifdef MTL_DEBUG_CANARY
    totalSize += sizeof(uint32_t);
#endif
    memFree(raw, totalSize);
}

/**
 * @brief Global operator new override (FAST/CHIP decided at compile config).
 * @param size Number of bytes requested.
 */
void* operator new(decltype(sizeof(int)) size) noexcept
{
    return allocWithSizeTracking(size, GLOBAL_MEM_FLAGS, "Global new failed: out of memory");
}

/**
 * @brief Global operator delete override (size recovered from header).
 */
void operator delete(void* ptr) noexcept
{
    deallocWithSizeTracking(ptr);
}

/**
 * @brief Global array new override.
 */
void* operator new[](decltype(sizeof(int)) size) noexcept
{
    return allocWithSizeTracking(size, GLOBAL_MEM_FLAGS, "Global new[] failed: out of memory");
}

/**
 * @brief Global array delete override.
 */
void operator delete[](void* ptr) noexcept
{
    deallocWithSizeTracking(ptr);
}

/**
 * @name Sized Delete (C++14+)
 * Sized variants ignore the provided size parameter since the stored size
 * header is authoritative.
 * @{ */
void operator delete(void* ptr, decltype(sizeof(int)) /*size*/) noexcept
{
    operator delete(ptr);
}
void operator delete[](void* ptr, decltype(sizeof(int)) /*size*/) noexcept
{
    operator delete[](ptr);
}
/** @} */

/**
 * @brief Placement new with explicit memory flags (FAST/CHIP).
 * @param size     Number of bytes requested.
 * @param memFlags Desired memory pool.
 */
void* operator new(decltype(sizeof(int)) size, mtl::MemF memFlags) noexcept
{
    return allocWithSizeTracking(
        size, static_cast<ULONG>(memFlags), "Placement new failed: out of memory");
}

/**
 * @brief Placement array new with explicit memory flags.
 */
void* operator new[](decltype(sizeof(int)) size, mtl::MemF memFlags) noexcept
{
    return allocWithSizeTracking(
        size, static_cast<ULONG>(memFlags), "Placement new[] failed: out of memory");
}

/**
 * @brief Placement delete (only called if constructor throws – kept for completeness).
 */
void operator delete(void* ptr, mtl::MemF /*memFlags*/) noexcept
{
    operator delete(ptr);
}
void operator delete[](void* ptr, mtl::MemF /*memFlags*/) noexcept
{
    operator delete[](ptr);
}
