/**
 * @file asset_cache.h
 * @brief A Fast RAM cache of decompressed pak assets, keyed by pak-path hash.
 *
 * A pak read costs a fixed open plus, for a compressed entry, a byte-at-a-time
 * unpack. Holding the decompressed bytes means a later scene pays neither.
 *
 * Entries are raw bytes, not built objects, so one implementation serves
 * bitmaps, masks, palettes and sound effects. Bytes come back through a
 * memory-backed tFile, so callers load from the cache exactly as from the pak.
 *
 * Any entry can be evicted at any load, so anything that must stay resident
 * keeps its own copy.
 */

#ifndef __ASSET_CACHE_H__INCLUDED__
#define __ASSET_CACHE_H__INCLUDED__

#include "types.h"

#include <stdint.h>

#include <ace++/file.h>
#include <ace++/pak_file.h>

namespace NEONengine
{
    /**
     * @brief How much Fast RAM the cache may hold, in bytes.
     *
     * Sized for the 4 MB A1200 the engine targets.
     */
    constexpr u32 ASSET_CACHE_BUDGET = 2u * 1024u * 1024u;

    /**
     * @brief The largest single asset the cache will hold, in bytes.
     *
     * One asset is not allowed to crowd out every other, so nothing above a
     * quarter of the budget is ever stored.
     */
    constexpr u32 ASSET_CACHE_MAX_ENTRY = ASSET_CACHE_BUDGET / 4u;

    /**
     * @brief Empties the cache and releases every byte it holds.
     */
    void asset_cache_clear();

    /**
     * @brief Starts a new load, so entries touched from here are the newest.
     *
     * Eviction picks the oldest load, not the oldest entry. A scene asks for
     * each asset exactly once while loading, so one counter per load records
     * use completely.
     *
     * Entries stamped with the current load are never evicted, so a scene whose
     * assets do not all fit cannot discard what it has already been given.
     *
     * Boot assets keep generation zero and go first. They are built into
     * objects that live for the whole run and are never read again.
     */
    void asset_cache_begin_load();

    /**
     * @brief Opens the asset @p hash names, from the cache when it is held.
     *
     * On a miss the asset is read from @p pak and stored when it fits. An asset
     * too large, or one that cannot be stored, passes straight through, so a
     * scene bigger than the whole cache still loads.
     *
     * A cached file borrows the entry's bytes and must be closed before the
     * next asset_cache_begin_load(), which may evict the entry underneath it.
     *
     * @param pak  The open game pak to read on a miss.
     * @param hash The asset's adler32 pak-path hash.
     * @return An owning file handle, or an invalid handle when there is no such
     *         asset.
     */
    ace::file_ptr asset_cache_open(ace::pakfile_ptr& pak, u32 hash);

    /**
     * @brief Writes what the cache is holding to the log.
     *
     * Entries are grouped by kind, read from the .neon on demand rather than
     * recorded per entry, so the grouping cannot drift. It scans once per
     * entry, which is why it runs on request.
     */
    void asset_cache_log();
}  // namespace NEONengine

#endif  // __ASSET_CACHE_H__INCLUDED__
