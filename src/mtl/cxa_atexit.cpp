/**
 * Minimal __cxa_atexit / __cxa_finalize implementation.
 *
 * This enables use of static (global) objects with non-trivial destructors without
 * pulling in the full libstdc++ runtime. GCC expects the symbols __cxa_atexit,
 * __cxa_finalize and __dso_handle to exist when generating code that registers
 * global/static destructors.
 */

#include "cstdint.h"
#include <ace/managers/log.h>
#include <stddef.h>

// Provide minimal guard variable implementation for function-scope statics.
extern "C" {
    int __cxa_guard_acquire(unsigned long long* guard) {
        if ((*guard) & 1ULL) return 0; // already initialized
        // Set in-progress bit (bit 0 = initialized per Itanium spec; but we simplify)
        return 1; // proceed with initialization
    }
    void __cxa_guard_release(unsigned long long* guard) {
        *guard |= 1ULL; // mark as initialized
    }
    void __cxa_guard_abort(unsigned long long* /*guard*/) {
        // No action needed for this minimal implementation
    }
}

extern "C" {
    // Required handle used by the compiler for shared object grouping (single image here)
    void* __dso_handle = &__dso_handle;

    struct __cxa_atexit_entry {
        void (*dtor)(void*);
        void* obj;
        void* dso_handle;
    };

    // Tune this as needed; counts ALL global objects with non-trivial destructors.
#ifndef MTL_CXA_ATEXIT_MAX
#define MTL_CXA_ATEXIT_MAX 64
#endif

    static __cxa_atexit_entry g_atexit_entries[MTL_CXA_ATEXIT_MAX];
    static unsigned int g_atexit_count = 0;

    int __cxa_atexit(void (*dtor)(void*), void* obj, void* dso_handle) noexcept {
        if (g_atexit_count >= MTL_CXA_ATEXIT_MAX) {
            logWrite("__cxa_atexit overflow (%lu >= %u) - destructor ignored", (ULONG)g_atexit_count, (unsigned)MTL_CXA_ATEXIT_MAX);
            return -1; // Indicate failure (ignored by most runtimes)
        }
        g_atexit_entries[g_atexit_count].dtor = dtor;
        g_atexit_entries[g_atexit_count].obj = obj;
        g_atexit_entries[g_atexit_count].dso_handle = dso_handle;
        ++g_atexit_count;
        return 0;
    }

    void __cxa_finalize(void* dso_handle) noexcept {
        if (g_atexit_count == 0) return;
        // Walk backwards so we destruct in reverse order of construction
        for (int i = (int)g_atexit_count - 1; i >= 0; --i) {
            auto &entry = g_atexit_entries[i];
            if (!entry.dtor) continue; // Already called
            if (dso_handle && entry.dso_handle != dso_handle) continue; // Different module
            void (*dtor)(void*) = entry.dtor;
            void* obj = entry.obj;
            entry.dtor = nullptr; // Prevent double invoke
            dtor(obj);
        }
        if (!dso_handle) {
            g_atexit_count = 0; // All dtors done
        }
    }
}
