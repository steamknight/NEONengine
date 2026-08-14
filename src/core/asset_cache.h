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

#include <mtl/memory.h>
#include <mtl/vector.h>

namespace NEONengine
{
    class game_data;

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

    class asset_cache;
    using asset_cache_ptr = mtl::unique_ptr<asset_cache>;

    class asset_cache
    {
        public:  //////////////////////////////////////////////////////////////////////////////////
        /**
         * @brief Caches assets read from @p pak, which must outlive the cache.
         */
        explicit asset_cache(ace::pakfile_ptr& pak) noexcept : _pak(pak) {}

        /** @brief Releases every byte held. Hands memory back to the OS. */
        ~asset_cache() { clear(); }

        asset_cache(asset_cache const&)            = delete;
        asset_cache& operator=(asset_cache const&) = delete;

        /**
         * @brief Empties the cache and releases every byte it holds.
         */
        void clear();

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
        void begin_load() noexcept { ++_generation; }

        /**
         * @brief Opens the asset @p hash names, from the cache when it is held.
         *
         * On a miss the asset is read from the pak and stored when it fits. An
         * asset too large, or one that cannot be stored, passes straight through,
         * so a scene bigger than the whole cache still loads.
         *
         * A cached file borrows the entry's bytes and must be closed before the
         * next begin_load(), which may evict the entry underneath it.
         *
         * @param hash The asset's adler32 pak-path hash.
         * @return An owning file handle, or an invalid handle when there is no such
         *         asset.
         */
        ace::file_ptr open(u32 hash);

        /**
         * @brief Writes what the cache is holding to the log.
         *
         * Entries are grouped by kind, read from @p data on demand rather than
         * recorded per entry, so the grouping cannot drift. It scans the game data
         * once per entry, which is why it runs on request.
         */
        void log(game_data const& data) const;

        private:  /////////////////////////////////////////////////////////////////////////////////
        /// One held asset: its bytes, and which load last wanted them.
        struct entry
        {
            u32 hash       = 0;
            u32 generation = 0;
            u8* bytes      = nullptr;  ///< Fast RAM, owned by this entry.
            u32 size       = 0;
        };

        /// @brief The held entry @p hash names, or nullptr.
        entry* find(u32 hash);

        /// @brief Releases the entry at @p index and its bytes.
        void drop(size_t index);

        /**
         * @brief Frees entries until @p size bytes fit, oldest load first.
         *
         * Entries wanted by the load in progress are never candidates. They have
         * already been handed out, and dropping one would have this load evict
         * its own work and read it again. When only those are left, this gives
         * up rather than touching them.
         *
         * @return True when @p size now fits.
         */
        bool make_room(u32 size);

        ace::pakfile_ptr&  _pak;              ///< The pak read on a miss. Not owned.
        mtl::vector<entry> _entries;
        u32                _used{ 0 };        ///< Bytes held, always the sum of the entry sizes.
        u32                _generation{ 0 };  ///< Which load is in progress.
        u32                _hits{ 0 };
        u32                _misses{ 0 };
    };
}  // namespace NEONengine

#endif  // __ASSET_CACHE_H__INCLUDED__
