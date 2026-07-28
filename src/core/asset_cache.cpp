#include "asset_cache.h"

#include "types.h"

#include "neonengine.h"

#include <ace/managers/memory.h>
#include <ace/utils/file.h>

#include <mtl/utility.h>
#include <mtl/vector.h>

#include <ace++/memory_file.h>

#include "core/game_data.h"
#include "core/game_data_format.h"
#include "core/logger.h"

NE_LOG_DOMAIN(Cache);

namespace NEONengine
{
    namespace
    {
        /// One held asset: its bytes, and which load last wanted them.
        struct cache_entry
        {
            u32 hash       = 0;
            u32 generation = 0;
            u8* bytes      = nullptr;  ///< Fast RAM, owned by this entry.
            u32 size       = 0;
        };

        mtl::vector<cache_entry> s_entries;
        u32 s_used       = 0;  ///< Bytes held, always the sum of the entry sizes.
        u32 s_generation = 0;  ///< Which load is in progress.
        u32 s_hits       = 0;
        u32 s_misses     = 0;

        /// @brief The held entry @p hash names, or nullptr.
        cache_entry* find_entry(u32 hash)
        {
            for (auto& entry : s_entries)
            {
                if (entry.hash == hash) return &entry;
            }
            return nullptr;
        }

        /// @brief Releases the entry at @p index and its bytes.
        void drop_entry(size_t index)
        {
            cache_entry& entry = s_entries[index];
            memFree(entry.bytes, entry.size);
            s_used -= entry.size;

            // Order carries no meaning here - eviction reads the stamp, not the
            // position - so the last entry fills the gap and no block moves.
            s_entries[index] = s_entries[s_entries.size() - 1];
            s_entries.pop_back();
        }

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
        bool make_room(u32 size)
        {
            while (s_used + size > ASSET_CACHE_BUDGET)
            {
                size_t oldest    = s_entries.size();
                u32 bestGen = 0;

                for (size_t i = 0; i < s_entries.size(); ++i)
                {
                    if (s_entries[i].generation == s_generation) continue;
                    if (oldest == s_entries.size() || s_entries[i].generation < bestGen)
                    {
                        oldest  = i;
                        bestGen = s_entries[i].generation;
                    }
                }

                if (oldest == s_entries.size()) return false;
                drop_entry(oldest);
            }

            return true;
        }

        /**
         * @brief Reads all of @p file into a new Fast RAM block of @p size bytes.
         *
         * Refuses to hold anything that did not land in Fast RAM. The cache
         * exists to spend Fast RAM, which the machine has spare, on saving pak
         * reads. Chip RAM is the scarce half and the half a scene can actually
         * run out of, so a megabyte of cached bytes sitting there would cost far
         * more than the reads it saves.
         *
         * That check has to be made after the fact rather than asked for. ACE's
         * memAllocFast() asks for MEMF_ANY, which exec is free to answer with
         * Chip, and even a MEMF_FAST request falls back to Chip inside
         * _memAllocRls() when Fast is exhausted.
         *
         * @return The block, or nullptr when it could not be allocated in Fast
         *         RAM or the file was shorter than it said it was.
         */
        u8* read_whole(tFile* file, u32 size)
        {
            auto* bytes = to<u8*>(memAlloc(size, MEMF_FAST));
            if (!bytes) return nullptr;

            if (memType(bytes) != MEMF_FAST)
            {
                memFree(bytes, size);
                return nullptr;
            }

            if (fileRead(file, bytes, size) != size)
            {
                memFree(bytes, size);
                return nullptr;
            }

            return bytes;
        }

        /// What the game data says a cached asset is. Reporting only.
        enum class asset_kind : u8
        {
            background,
            bob_frames,
            bob_mask,
            palette,
            sfx,
            portrait,
            ui,
            music,
            other,
            COUNT
        };

        char const* kind_name(asset_kind kind)
        {
            switch (kind)
            {
                case asset_kind::background: return "BGs     ";
                case asset_kind::bob_frames: return "BOB gfx ";
                case asset_kind::bob_mask:   return "BOB mask";
                case asset_kind::palette:    return "Palettes";
                case asset_kind::sfx:        return "SFX     ";
                case asset_kind::portrait:   return "Portrait";
                case asset_kind::ui:         return "UI      ";
                case asset_kind::music:      return "Music   ";
                default:                     return "Other   ";
            }
        }

        /**
         * @brief What role @p bitmap_id plays, for a hash already known to be a bitmap.
         *
         * A bitmap is a background or a bob only because some other record
         * points at it, so the role has to be looked up rather than read off the
         * bitmap itself.
         */
        asset_kind classify_bitmap(u16 bitmap_id)
        {
            game_data& gd = g_pEngine->data();

            auto const& ui = gd.ui_state();
            if (bitmap_id == ui.nine_patch_bitmap_id || bitmap_id == ui.cursor_bitmap_id)
            {
                return asset_kind::ui;
            }
            if (bitmap_id == gd.inventory_config().sheet_bitmap_id) return asset_kind::ui;

            for (auto const& scene : gd.scenes())
            {
                if (scene.background_bitmap_id == bitmap_id) return asset_kind::background;
            }

            for (auto const& gfx : gd.graphics())
            {
                if (gfx.source_bitmap_id == bitmap_id) return asset_kind::bob_frames;
            }

            for (auto const& speaker : gd.speakers())
            {
                if (speaker.portrait_bitmap_id == bitmap_id) return asset_kind::portrait;
            }

            return asset_kind::other;
        }

        /// @brief Whether any hotspot or keyframe plays the sound @p hash names.
        bool is_sfx(u32 hash)
        {
            game_data& gd = g_pEngine->data();

            for (auto const& hotspot : gd.hotspots())
            {
                if (hotspot.click_sfx_hash == hash) return true;
            }

            for (auto const& key : gd.keyframes())
            {
                if (to<data::keyframe_type>(key.type) != data::keyframe_type::SFX_PLAY)
                {
                    continue;
                }
                if (force_to<data::sfx_play_payload const*>(key.payload)->sfx_hash == hash)
                {
                    return true;
                }
            }

            return false;
        }

        /**
         * @brief What the game data says the asset @p hash names is.
         *
         * First match wins. One hash is one asset, so the categories cannot
         * genuinely overlap, and anything nothing refers to falls to `other`
         * where it stands out as either a stale entry or a gap here.
         */
        asset_kind classify(u32 hash)
        {
            game_data& gd = g_pEngine->data();

            auto const& ui = gd.ui_state();
            if (hash == ui.ui_palette_hash || hash == ui.font_hash) return asset_kind::ui;

            auto const bitmaps = gd.bitmaps();
            for (size_t i = 0; i < bitmaps.size(); ++i)
            {
                auto const& ref = bitmaps[i];
                if (ref.palette_hash == hash) return asset_kind::palette;
                if (ref.mask_hash != data::NO_ASSET && ref.mask_hash == hash)
                {
                    return asset_kind::bob_mask;
                }
                if (ref.bitmap_hash == hash) return classify_bitmap(to<u16>(i));
            }

            if (is_sfx(hash)) return asset_kind::sfx;

            // Music is never cached, so this only fires if that ever stops being
            // true, which is exactly when it wants saying out loud.
            for (auto const& scene : gd.scenes())
            {
                if (scene.music_hash == hash) return asset_kind::music;
            }

            return asset_kind::other;
        }
    }  // namespace

    void asset_cache_clear()
    {
        while (!s_entries.empty())
        {
            drop_entry(s_entries.size() - 1);
        }
    }

    void asset_cache_begin_load()
    {
        ++s_generation;
    }

    ace::file_ptr asset_cache_open(ace::pakfile_ptr& pak, u32 hash)
    {
        if (cache_entry* entry = find_entry(hash))
        {
            // A handle is a few bytes, so this only fails when memory is gone
            // entirely. Falling back to the pak keeps the asset loading, which
            // is the one promise the cache makes to every caller.
            if (tFile* served = ace::memoryFileOpen(entry->bytes, entry->size))
            {
                entry->generation = s_generation;
                ++s_hits;
                return ace::file_ptr(served);
            }

            return ace::pakFileGetFileByHash(pak, hash);
        }

        ++s_misses;

        ace::file_ptr file = ace::pakFileGetFileByHash(pak, hash);
        if (!file) return file;

        // A compressed entry reports what it unpacks to, not what it occupies,
        // so this is the size to hold and the size to read.
        LONG const reported = fileGetSize(file.get());
        if (reported <= 0 || to<u32>(reported) > ASSET_CACHE_MAX_ENTRY)
        {
            return file;
        }

        u32 const size = to<u32>(reported);
        if (!make_room(size)) return file;

        u8* bytes = read_whole(file.get(), size);
        if (!bytes)
        {
            // Part of the file has been consumed, so the handle cannot be given
            // out as it stands. Open it again and let this one go uncached.
            return ace::pakFileGetFileByHash(pak, hash);
        }

        s_entries.push_back(cache_entry{ hash, s_generation, bytes, size });
        s_used += size;

        // Held either way: the entry is complete and the next load can use it,
        // even if this one has to read the asset from the pak a second time.
        if (tFile* served = ace::memoryFileOpen(bytes, size)) return ace::file_ptr(served);
        return ace::pakFileGetFileByHash(pak, hash);
    }

    void asset_cache_log()
    {
        u32 counts[to<size_t>(asset_kind::COUNT)]{};
        u32 bytes[to<size_t>(asset_kind::COUNT)]{};

        for (auto const& entry : s_entries)
        {
            size_t const kind = to<size_t>(classify(entry.hash));
            ++counts[kind];
            bytes[kind] += entry.size;
        }

        NE_LOG("%lu entries, %lu of %lu KB, gen %lu, %lu hits / %lu misses",
            to<ULONG>(s_entries.size()),
            to<ULONG>(s_used >> 10),
            to<ULONG>(ASSET_CACHE_BUDGET >> 10),
            to<ULONG>(s_generation),
            to<ULONG>(s_hits),
            to<ULONG>(s_misses));

        for (size_t kind = 0; kind < to<size_t>(asset_kind::COUNT); ++kind)
        {
            if (!counts[kind]) continue;

            NE_LOG("  %s %3lu at %5lu KB",
                kind_name(to<asset_kind>(kind)),
                to<ULONG>(counts[kind]),
                to<ULONG>(bytes[kind] >> 10));
        }
    }
}  // namespace NEONengine
