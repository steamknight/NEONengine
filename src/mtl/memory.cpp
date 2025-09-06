#include <ace/managers/log.h>

#include "memory.h"
#include "utility.h"

extern "C" {
#include <stddef.h>
}

#define MTL_DEBUG_CANARY

using namespace mtl;

// We keep a size header in front of each allocation. To avoid breaking alignment
// for types with stricter requirements we round the header size up to max_align_t.
// Layout: [ size(uint32_t) | padding ... | user data ... ]
static constexpr size_t RAW_SIZE_FIELD_SIZE = sizeof(uint32_t);
static constexpr size_t ALIGNMENT = alignof(max_align_t);
static constexpr size_t HEADER_SIZE = (RAW_SIZE_FIELD_SIZE + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1); // rounded up

// (Optional) enable simple overrun canary after the user block for debug builds
#ifdef MTL_DEBUG_CANARY
static constexpr uint32_t CANARY_VALUE = 0xDEADBEEF;
#endif

static void* allocWithSizeTracking(size_t size, ULONG memFlags, const char* errorMsg) {
    // Guard against impossible large allocations (truncate to 32-bit stored size)
    if (size > 0xFFFFFFFFu) {
        logWrite("Allocation too large (%lu bytes)", static_cast<unsigned long>(size));
        return nullptr;
    }

    size_t totalSize = HEADER_SIZE + size;
#ifdef MTL_DEBUG_CANARY
    totalSize += sizeof(uint32_t); // space for trailing canary
#endif

    uint8_t* raw = static_cast<uint8_t*>(memAlloc(totalSize, memFlags));
    if (!raw) {
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

static void deallocWithSizeTracking(void* ptr) {
    if (!ptr) return;
    uint8_t* userPtr = static_cast<uint8_t*>(ptr);
    uint8_t* raw = userPtr - HEADER_SIZE;
    uint32_t storedSize = *reinterpret_cast<uint32_t*>(raw);

#ifdef MTL_DEBUG_CANARY
    uint32_t* canary = reinterpret_cast<uint32_t*>(userPtr + storedSize);
    if (*canary != CANARY_VALUE) {
        logWrite("Memory corruption detected (canary mismatch) for block %p size %lu", ptr, static_cast<unsigned long>(storedSize));
    }
#endif

    size_t totalSize = HEADER_SIZE + storedSize;
#ifdef MTL_DEBUG_CANARY
    totalSize += sizeof(uint32_t);
#endif
    memFree(raw, totalSize);
}

void* operator new(decltype(sizeof(int)) size) noexcept {
    return allocWithSizeTracking(size, GLOBAL_MEM_FLAGS, "Global new failed: out of memory");
}

void operator delete(void* ptr) noexcept {
    deallocWithSizeTracking(ptr);
}

void* operator new[](decltype(sizeof(int)) size) noexcept {
    return allocWithSizeTracking(size, GLOBAL_MEM_FLAGS, "Global new[] failed: out of memory");
}

void operator delete[](void* ptr) noexcept {
    deallocWithSizeTracking(ptr);
}

// Sized delete operators (C++14 and later)
void operator delete(void* ptr, decltype(sizeof(int)) size) noexcept {
    // For sized delete, we can ignore the passed size and use our stored size
    operator delete(ptr);
}

void operator delete[](void* ptr, decltype(sizeof(int)) size) noexcept {
    // For sized delete, we can ignore the passed size and use our stored size
    operator delete[](ptr);
}

// Placement new operators that take MemF for memory type selection
void* operator new(decltype(sizeof(int)) size, mtl::MemF memFlags) noexcept {
    return allocWithSizeTracking(size, static_cast<ULONG>(memFlags), "Placement new failed: out of memory");
}

void* operator new[](decltype(sizeof(int)) size, mtl::MemF memFlags) noexcept {
    return allocWithSizeTracking(size, static_cast<ULONG>(memFlags), "Placement new[] failed: out of memory");
}

// Placement delete operators (called only if constructor throws)
void operator delete(void* ptr, mtl::MemF memFlags) noexcept {
    // Just delegate to regular delete since we store the size
    operator delete(ptr);
}

void operator delete[](void* ptr, mtl::MemF memFlags) noexcept {
    // Just delegate to regular delete since we store the size
    operator delete[](ptr);
}
