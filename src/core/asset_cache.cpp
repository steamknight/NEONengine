#include "asset_cache.h"

#include "types.h"

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
        asset_kind classify_bitmap(game_data const& gd, u16 bitmap_id)
        {
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
        bool is_sfx(game_data const& gd, u32 hash)
        {
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
        asset_kind classify(game_data const& gd, u32 hash)
        {
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
                if (ref.bitmap_hash == hash) return classify_bitmap(gd, to<u16>(i));
            }

            if (is_sfx(gd, hash)) return asset_kind::sfx;

            // Music is never cached, so this only fires if that ever stops being
            // true, which is exactly when it wants saying out loud.
            for (auto const& scene : gd.scenes())
            {
                if (scene.music_hash == hash) return asset_kind::music;
            }

            return asset_kind::other;
        }
    }  // namespace

    asset_cache::entry* asset_cache::find(u32 hash)
    {
        for (auto& held : _entries)
        {
            if (held.hash == hash) return &held;
        }
        return nullptr;
    }

    void asset_cache::drop(size_t index)
    {
        entry& held = _entries[index];
        memFree(held.bytes, held.size);
        _used -= held.size;

        // Order carries no meaning here - eviction reads the stamp, not the
        // position - so the last entry fills the gap and no block moves.
        _entries[index] = _entries[_entries.size() - 1];
        _entries.pop_back();
    }

    bool asset_cache::make_room(u32 size)
    {
        while (_used + size > ASSET_CACHE_BUDGET)
        {
            size_t oldest = _entries.size();
            u32 bestGen   = 0;

            for (size_t i = 0; i < _entries.size(); ++i)
            {
                if (_entries[i].generation == _generation) continue;
                if (oldest == _entries.size() || _entries[i].generation < bestGen)
                {
                    oldest  = i;
                    bestGen = _entries[i].generation;
                }
            }

            if (oldest == _entries.size()) return false;
            drop(oldest);
        }

        return true;
    }

    void asset_cache::clear()
    {
        while (!_entries.empty())
        {
            drop(_entries.size() - 1);
        }
    }

    ace::file_ptr asset_cache::open(u32 hash)
    {
        if (entry* held = find(hash))
        {
            // A handle is a few bytes, so this only fails when memory is gone
            // entirely. Falling back to the pak keeps the asset loading, which
            // is the one promise the cache makes to every caller.
            if (tFile* served = ace::memoryFileOpen(held->bytes, held->size))
            {
                held->generation = _generation;
                ++_hits;
                return ace::file_ptr(served);
            }

            return ace::pakFileGetFileByHash(_pak, hash);
        }

        ++_misses;

        ace::file_ptr file = ace::pakFileGetFileByHash(_pak, hash);
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
            return ace::pakFileGetFileByHash(_pak, hash);
        }

        _entries.push_back(entry{ hash, _generation, bytes, size });
        _used += size;

        // Held either way: the entry is complete and the next load can use it,
        // even if this one has to read the asset from the pak a second time.
        if (tFile* served = ace::memoryFileOpen(bytes, size)) return ace::file_ptr(served);
        return ace::pakFileGetFileByHash(_pak, hash);
    }

    void asset_cache::log(game_data const& data) const
    {
        u32 counts[to<size_t>(asset_kind::COUNT)]{};
        u32 bytes[to<size_t>(asset_kind::COUNT)]{};

        for (auto const& held : _entries)
        {
            size_t const kind = to<size_t>(classify(data, held.hash));
            ++counts[kind];
            bytes[kind] += held.size;
        }

        NE_LOG("%lu entries, %lu of %lu KB, gen %lu, %lu hits / %lu misses",
            to<ULONG>(_entries.size()),
            to<ULONG>(_used >> 10),
            to<ULONG>(ASSET_CACHE_BUDGET >> 10),
            to<ULONG>(_generation),
            to<ULONG>(_hits),
            to<ULONG>(_misses));

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
