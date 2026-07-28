/**
 * @file asset_cache.h
 * @brief A Fast RAM cache of decompressed pak assets, keyed by pak-path hash.
 *
 * Reading an asset out of the pak is expensive twice over: a compressed entry
 * has to be unpacked a byte at a time, and every entry pays a fixed cost for
 * the open itself that dwarfs the read for anything small. Both are paid again
 * every time a scene is entered, even when the previous scene had just finished
 * with the very same asset.
 *
 * This holds the decompressed bytes so the second visit pays neither. It stores
 * raw bytes rather than built objects, which keeps one implementation serving
 * bitmaps, masks, palettes and sound effects, and keeps the whole cache in Fast
 * RAM where there is room for it. Bytes are handed back through a memory-backed
 * tFile, so every caller loads from the cache exactly as it loaded from the pak.
 *
 * The cache is an optimization and never a guarantee. Anything that must be
 * resident to be correct - the scene's sound effects, for one - has to keep its
 * own resident copy, because any entry here can be evicted at any load.
 */

#ifndef __ASSET_CACHE_H__INCLUDED__
#define __ASSET_CACHE_H__INCLUDED__

#include <stdint.h>

#include <ace++/file.h>
#include <ace++/pak_file.h>

namespace NEONengine
{
    /**
     * @brief How much Fast RAM the cache may hold, in bytes.
     *
     * Sized for the 4 MB A1200 the engine targets. This belongs in the project
     * eventually, clamped to what the machine actually has free, but a constant
     * is enough to measure the idea and costs the editor nothing meanwhile.
     */
    constexpr uint32_t ASSET_CACHE_BUDGET = 2u * 1024u * 1024u;

    /**
     * @brief The largest single asset the cache will hold, in bytes.
     *
     * One asset is not allowed to crowd out every other, so nothing above a
     * quarter of the budget is ever stored. This is what keeps a module out
     * without needing to know anything about asset types: a MOD runs to
     * hundreds of kilobytes and nothing else comes close.
     */
    constexpr uint32_t ASSET_CACHE_MAX_ENTRY = ASSET_CACHE_BUDGET / 4u;

    /**
     * @brief Empties the cache and releases every byte it holds.
     */
    void asset_cache_clear();

    /**
     * @brief Starts a new load, so entries touched from here are the newest.
     *
     * Eviction works off which load last wanted an entry, and every asset a
     * scene needs is asked for exactly once while it loads. So one counter per
     * load is a complete record of use, and it costs a single stamp per entry
     * rather than a list that has to be reordered on every access.
     *
     * It also fixes the one case eviction must never get wrong. Entries stamped
     * with the current load are refused to the evictor, so a scene whose assets
     * do not all fit can never throw away the ones it has already been given.
     *
     * Assets read before the first load - the UI font and palette at boot - keep
     * generation zero and are therefore the first to go. That is the right
     * answer for them: they are built into objects that live for the whole run
     * and are never read a second time, so holding their bytes earns nothing.
     */
    void asset_cache_begin_load();

    /**
     * @brief Opens the asset @p hash names, from the cache when it is held.
     *
     * On a miss the asset is read from @p pak, stored when it fits, and handed
     * back. An asset too large to store, or one that cannot be stored because
     * room could not be made, is passed straight through from the pak, so a
     * scene bigger than the whole cache still loads correctly.
     *
     * A file served from the cache borrows the entry's bytes and must be closed
     * before the next asset_cache_begin_load(), because the load after this one
     * may evict the entry underneath it. Every caller loads and closes within a
     * single statement, which satisfies this without having to think about it.
     *
     * @param pak  The open game pak to read on a miss.
     * @param hash The asset's adler32 pak-path hash.
     * @return An owning file handle, or an invalid handle when there is no such
     *         asset.
     */
    ace::file_ptr asset_cache_open(ace::pakfile_ptr& pak, uint32_t hash);

    /**
     * @brief Writes what the cache is holding to the log.
     *
     * Entries are grouped by what the game data says each one is. Nothing is
     * recorded per entry to make this possible: the .neon is already in Fast
     * RAM and already says which hash is a background, a mask, a sound and so
     * on, so the grouping is worked out here and cannot drift from the data it
     * describes. It costs a scan per entry, which is why it happens on request
     * rather than on every load.
     */
    void asset_cache_log();
}  // namespace NEONengine

#endif  // __ASSET_CACHE_H__INCLUDED__
