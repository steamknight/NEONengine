/**
 * @file memory_file.h
 * @brief A tFile that reads from a range of memory instead of a disk or a pak.
 *
 * ACE's loaders all take a tFile*, and tFile is a callback table plus a data
 * pointer rather than a concrete disk handle. That makes a memory-backed file
 * possible, and with one every ACE loader can be pointed at bytes already in
 * RAM without changing the loader or its caller. The asset cache uses this to
 * serve a cached asset through the same call the pak would have served.
 */

#ifndef __ACEPP__MEMORY_FILE_H___INCLUDED__
#define __ACEPP__MEMORY_FILE_H___INCLUDED__

#include <ace/utils/file.h>

#include <stdint.h>

namespace ace
{
    /**
     * @brief Opens @p pData as a read-only file of @p ulSize bytes.
     *
     * The bytes are borrowed, not owned: closing the returned file frees the
     * handle and leaves @p pData alone. The caller must therefore keep @p pData
     * alive for as long as the file is open.
     *
     * Writing is refused. Seeking and reading are clamped to @p ulSize, which
     * matches how a pak subfile behaves at its own end.
     *
     * @param pData  The bytes to read from. Must outlive the returned file.
     * @param ulSize How many bytes are readable at @p pData.
     * @return An open file, or nullptr if the handle could not be allocated.
     */
    tFile* memoryFileOpen(uint8_t const* pData, ULONG ulSize);
}  // namespace ace

#endif  // __ACEPP__MEMORY_FILE_H___INCLUDED__
