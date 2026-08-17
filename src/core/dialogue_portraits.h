/**
 * @file dialogue_portraits.h
 * @brief The speaker portraits one conversation can put on screen.
 *
 * A portrait is read from the pak, which blocks, so every distinct one a
 * dialogue's pages name is loaded when the conversation starts rather than as
 * each speaker takes their turn: the set is knowable from the pages, and a
 * pause as the box appears reads as normal where one mid-conversation reads as
 * a stall.
 *
 * Portrait colors live in their own palette band, the way a bob's colors live
 * in its slot: showing a portrait means putting its colors in the band and
 * drawing artwork whose indices point there. The exporter writes a portrait at
 * the screen's depth with the band's base already folded into the pixels, as it
 * does for bob strips, so the engine reads one straight into Chip RAM and the
 * draw is a plain cookie-cut. The band belongs to the scene's reverse bob slots
 * the rest of the time, so what was there is put back when the conversation
 * ends.
 *
 * One conversation at a time, like the state that owns it.
 */

#ifndef __DIALOGUE_PORTRAITS_H__INCLUDED__
#define __DIALOGUE_PORTRAITS_H__INCLUDED__

#include "types.h"

#include <ace++/bitmap.h>

#include <mtl/vector.h>

#include "core/asset_cache.h"
#include "core/game_data.h"
#include "core/game_data_format.h"

namespace NEONengine
{
    /// Every portrait is authored at this size; nothing scales on the Amiga.
    constexpr u16 PORTRAIT_WIDTH  = 96;
    constexpr u16 PORTRAIT_HEIGHT = 96;

    /**
     * @brief The palette band a portrait's colors are overlaid into.
     *
     * Indices 32..63, which are the scene's reverse bob slots 2..5 the rest of
     * the time. The portrait and the inventory both take this band, which is
     * why the two are never on screen together.
     */
    constexpr u16 PORTRAIT_PALETTE_FIRST  = 32;
    constexpr u16 PORTRAIT_PALETTE_COLORS = 32;

    /**
     * @brief The portraits one conversation can put on screen.
     *
     * The state that draws the box builds one of these as the conversation
     * opens and drops it as the conversation closes, which is when the palette
     * band goes back. That state erases the frame and the portrait in its own
     * destructor body, so the pixels under the band are the scene's again
     * before this object is destroyed.
     */
    class dialogue_portraits
    {
        public:  //////////////////////////////////////////////////////////////////////////////////
        /** @brief Reads portraits from @p data through @p cache; both must outlive this. */
        dialogue_portraits(game_data& data, asset_cache& cache) noexcept
            : _data(data), _cache(cache)
        {
        }

        dialogue_portraits(dialogue_portraits const&)            = delete;
        dialogue_portraits& operator=(dialogue_portraits const&) = delete;

        /**
         * @brief Releases every portrait and hands the palette band back.
         *
         * Hands the band back only when the conversation took it, so a
         * conversation whose speakers had no artwork leaves the scene's colors
         * untouched.
         */
        ~dialogue_portraits();

        /**
         * @brief Loads every distinct portrait the pages in @p page_range name.
         *
         * Reads the pak and takes system use itself, so it blocks; call it as
         * the conversation starts and not again. A portrait that cannot be read
         * is logged and left out, and its speaker then simply has none.
         *
         * @param page_range The running dialogue's PAGS range.
         */
        void load(data::range page_range);

        /**
         * @brief Whether @p speaker_id has a portrait that can be drawn.
         *
         * False for a page with no speaker, a speaker authored without a
         * portrait, and a speaker whose portrait failed to load - the three
         * cases where the frame takes the whole width instead.
         */
        bool has(u16 speaker_id) const;

        /**
         * @brief Puts @p speaker_id on screen at @p x, @p y, through their mask.
         *
         * Puts that portrait's colors in the band first, so only one portrait's
         * colors are ever live - which is what the band being 32 entries for a
         * whole conversation means.
         *
         * A speaker with no portrait draws nothing and hands the band back
         * instead: the last speaker's colors would otherwise stay live over a
         * column the scene has just had back. So call this whenever the speaker
         * changes, not only when the new one has artwork.
         *
         * Takes scene-space coordinates, as Screen::blit_copy does.
         */
        void show(u16 speaker_id, u16 x, u16 y);

        private:  /////////////////////////////////////////////////////////////////////////////////
        /// One loaded portrait: its artwork, its mask, and the colors it needs.
        struct portrait
        {
            u16             bitmap_id{ data::NO_ID };
            ace::bitmap_ptr image{ nullptr };
            ace::bitmap_ptr mask{ nullptr };
            ULONG           colors[PORTRAIT_PALETTE_COLORS]{};
        };

        /** @brief The loaded portrait for BMPS @p bitmap_id, or null. */
        portrait const* find(u16 bitmap_id) const;

        /** @brief The BMPS id @p speaker_id's portrait comes from, or NO_ID. */
        u16 portrait_of(u16 speaker_id) const;

        /**
         * @brief Reads BMPS @p bitmap_id's artwork, mask and colors into @p out.
         *
         * Reads the pak: the caller holds the system-use guard. Returns false
         * with @p out unusable when anything is missing or the wrong shape, so
         * the speaker ends up with no portrait rather than a bad blit.
         */
        bool load_portrait(u16 bitmap_id, portrait& out);

        /** @brief Copies @p colors into the band and puts them on screen. */
        void publish_band(ULONG const* colors);

        /**
         * @brief Hands the palette band back, keeping the portraits loaded.
         *
         * The next speaker who has a portrait simply takes the band again.
         * Safe when the band was never taken, and when it has already gone
         * back.
         */
        void release_band();

        game_data&   _data;
        asset_cache& _cache;

        /// Every portrait the running conversation can show, in no order.
        mtl::vector<portrait> _portraits;

        /// The BMPS id whose colors are in the band now, or NO_ID.
        u16 _band_holder{ data::NO_ID };

        /// What the band held before the conversation took it.
        ULONG _band_held[PORTRAIT_PALETTE_COLORS]{};
        bool  _band_taken{ false };
    };
}  // namespace NEONengine

#endif  // __DIALOGUE_PORTRAITS_H__INCLUDED__
