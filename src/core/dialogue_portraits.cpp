#include "dialogue_portraits.h"

#include "types.h"

#include "neonengine.h"

#include <ace/managers/blit.h>
#include <ace/utils/bitmap.h>

#include <string.h>

#include <ace++/bitmap.h>
#include <ace++/palette.h>

#include <mtl/utility.h>
#include <mtl/vector.h>

#include "core/logger.h"
#include "core/screen.h"

NE_LOG_DOMAIN(Dialogue);

namespace NEONengine
{
    namespace
    {
        /// A portrait's rows are this many bytes wide, per bitplane.
        constexpr u16 PORTRAIT_BYTE_WIDTH = PORTRAIT_WIDTH / 8;

        /// Planes a 32-color portrait is authored at, holding local indices 0..31.
        constexpr UBYTE PORTRAIT_PLANES = 5;

        static_assert(
            (1u << PORTRAIT_PLANES) == PORTRAIT_PALETTE_COLORS, "the band is 2^planes colors");

        /// One loaded portrait: its artwork, its mask, and the colors it needs.
        struct portrait
        {
            u16 bitmap_id{ data::NO_ID };
            ace::bitmap_ptr image{ nullptr };
            ace::bitmap_ptr mask{ nullptr };
            ULONG colors[PORTRAIT_PALETTE_COLORS]{};
        };

        /// Every portrait the running conversation can show, in no order.
        mtl::vector<portrait> s_portraits;

        /// The BMPS id whose colors are in the band now, or NO_ID.
        u16 s_band_holder = data::NO_ID;

        /// What the band held before the conversation took it.
        ULONG s_band_held[PORTRAIT_PALETTE_COLORS]{};
        bool s_band_taken = false;

        /** @brief The loaded portrait for BMPS @p bitmap_id, or null. */
        portrait* find(u16 bitmap_id)
        {
            if (bitmap_id == data::NO_ID) return nullptr;

            for (size_t i = 0; i < s_portraits.size(); ++i)
            {
                if (s_portraits[i].bitmap_id == bitmap_id) return &s_portraits[i];
            }
            return nullptr;
        }

        /** @brief The BMPS id @p speaker_id's portrait comes from, or NO_ID. */
        u16 portrait_of(u16 speaker_id)
        {
            if (speaker_id == data::NO_ID) return data::NO_ID;

            auto const speakers = g_pEngine->data().speakers();
            if (speaker_id >= speakers.size()) return data::NO_ID;

            return speakers[speaker_id].portrait_bitmap_id;
        }

        /**
         * @brief Whether @p bitmap is at least a portrait's size.
         *
         * The blit is always the fixed size, so anything smaller would be read
         * past the end of what was allocated for it.
         */
        bool is_portrait_sized(tBitMap const* bitmap)
        {
            return bitmap->Rows >= PORTRAIT_HEIGHT
                && bitmapGetByteWidth(bitmap) >= PORTRAIT_BYTE_WIDTH;
        }

        /**
         * @brief Rebuilds @p source at @p depth planes with the band's base baked in.
         *
         * Portraits are authored at 32 colors and carry local indices 0..31,
         * while the screen is deeper than that and the band they are drawn from
         * starts at PORTRAIT_PALETTE_FIRST. The planes the artwork has are
         * copied across as they are, and each plane above them is filled or left
         * clear according to the matching bit of the base index - so local index
         * n comes out as base + n, which is where its colors were put. The base
         * is a multiple of the band's size, which is what makes that an
         * or rather than an addition.
         *
         * Doing it once here rather than per blit is what lets the portrait be
         * drawn with a plain cookie-cut, exactly as a bob's frames are.
         *
         * @param source A portrait-sized bitmap, in any memory.
         * @param base   The palette index the artwork's index 0 becomes.
         * @return A Chip RAM bitmap the blitter can read, or null.
         */
        ace::bitmap_ptr bake_to_band(tBitMap const* source, UBYTE depth, u16 base)
        {
            ace::bitmap_ptr baked
                = ace::bitmapCreate(PORTRAIT_WIDTH, PORTRAIT_HEIGHT, depth, BMF_INTERLEAVED | BMF_CLEAR);
            if (!baked) return baked;

            // Planes[p] steps a row by BytesPerRow whether the bitmap is
            // interleaved or not, so one addressing scheme serves both.
            for (u16 row = 0; row < PORTRAIT_HEIGHT; ++row)
            {
                ULONG const from = to<ULONG>(row) * source->BytesPerRow;
                ULONG const into = to<ULONG>(row) * baked->BytesPerRow;

                for (UBYTE plane = 0; plane < depth; ++plane)
                {
                    UBYTE* out = baked->Planes[plane] + into;

                    if (plane < source->Depth)
                    {
                        memcpy(out, source->Planes[plane] + from, PORTRAIT_BYTE_WIDTH);
                    }
                    else if ((base >> plane) & 1)
                    {
                        // Set for the whole rectangle, not only where the mask
                        // is opaque: the cookie-cut drops the rest anyway.
                        memset(out, 0xFF, PORTRAIT_BYTE_WIDTH);
                    }
                }
            }

            return baked;
        }

        /**
         * @brief Rebuilds @p source's mask bits into every one of @p depth planes.
         *
         * The blitter reads a mask with the artwork's own stride, so a mask has
         * to carry its bits in as many planes as the artwork it cuts - and the
         * artwork has just been rebuilt deeper than the mask was exported at.
         */
        ace::bitmap_ptr bake_mask(tBitMap const* source, UBYTE depth)
        {
            ace::bitmap_ptr baked
                = ace::bitmapCreate(PORTRAIT_WIDTH, PORTRAIT_HEIGHT, depth, BMF_INTERLEAVED);
            if (!baked) return baked;

            for (u16 row = 0; row < PORTRAIT_HEIGHT; ++row)
            {
                UBYTE const* from = source->Planes[0] + to<ULONG>(row) * source->BytesPerRow;
                ULONG const into  = to<ULONG>(row) * baked->BytesPerRow;

                for (UBYTE plane = 0; plane < depth; ++plane)
                {
                    memcpy(baked->Planes[plane] + into, from, PORTRAIT_BYTE_WIDTH);
                }
            }

            return baked;
        }

        /**
         * @brief Reads BMPS @p bitmap_id's artwork, mask and colors into @p out.
         *
         * Reads the pak: the caller holds the system-use guard. Returns false
         * with @p out unusable when anything is missing or the wrong shape, so
         * the speaker ends up with no portrait rather than a bad blit.
         */
        bool load_portrait(u16 bitmap_id, portrait& out)
        {
            game_data& gd      = g_pEngine->data();
            auto const bitmaps = gd.bitmaps();
            if (bitmap_id >= bitmaps.size())
            {
                NE_LOG("portrait names bitmap %hu, which is not in the pak", bitmap_id);
                return false;
            }

            data::bitmap_ref const& ref = bitmaps[bitmap_id];

            // Fast RAM: only the CPU reads these, and what the blitter will read
            // is built from them below.
            ace::bitmap_ptr source = ace::bitmapCreateFromFd(gd.open_asset(ref.bitmap_hash), 1);
            if (!source)
            {
                NE_LOG("portrait bitmap %hu could not be read", bitmap_id);
                return false;
            }

            if (!is_portrait_sized(source.get()))
            {
                NE_LOG("portrait bitmap %hu is smaller than %hux%hu",
                    bitmap_id,
                    PORTRAIT_WIDTH,
                    PORTRAIT_HEIGHT);
                return false;
            }

            // More planes than the band has colors for cannot be told where to
            // land: index 32 and up would run past the end of the band.
            if (source->Depth == 0 || source->Depth > PORTRAIT_PLANES)
            {
                NE_LOG("portrait bitmap %hu has %hhu planes, band holds %hhu",
                    bitmap_id,
                    source->Depth,
                    PORTRAIT_PLANES);
                return false;
            }

            ace::bitmap_ptr source_mask{ nullptr };
            if (ref.mask_hash != data::NO_ASSET)
            {
                source_mask = ace::bitmapCreateFromFd(gd.open_asset(ref.mask_hash), 1);
                if (!source_mask)
                {
                    NE_LOG("portrait bitmap %hu has a mask that could not be read", bitmap_id);
                    return false;
                }

                // The mask is read at the artwork's size, so a smaller one would
                // be read past its end the same way a smaller portrait would.
                if (!is_portrait_sized(source_mask.get()))
                {
                    NE_LOG("portrait bitmap %hu has a mask smaller than %hux%hu",
                        bitmap_id,
                        PORTRAIT_WIDTH,
                        PORTRAIT_HEIGHT);
                    return false;
                }
            }

            UBYTE const depth = screenGetBackBuffer(g_mainScreen)->Depth;

            ace::bitmap_ptr image = bake_to_band(source.get(), depth, PORTRAIT_PALETTE_FIRST);
            if (!image)
            {
                NE_LOG("portrait bitmap %hu would not fit in Chip RAM", bitmap_id);
                return false;
            }

            ace::bitmap_ptr mask{ nullptr };
            if (source_mask)
            {
                mask = bake_mask(source_mask.get(), depth);
                if (!mask)
                {
                    NE_LOG("portrait bitmap %hu's mask would not fit in Chip RAM", bitmap_id);
                    return false;
                }
            }

            ace::paletteLoadFromFd(gd.open_asset(ref.palette_hash),
                force_to<UWORD*>(out.colors),
                PORTRAIT_PALETTE_COLORS);

            out.bitmap_id = bitmap_id;
            out.image     = mtl::move(image);
            out.mask      = mtl::move(mask);
            return true;
        }

        /** @brief Copies @p colors into the band and puts them on screen. */
        void publish_band(ULONG const* colors)
        {
            auto* reference = force_to<ULONG*>(screenGetPalette(g_mainScreen));
            for (u16 i = 0; i < PORTRAIT_PALETTE_COLORS; ++i)
            {
                reference[PORTRAIT_PALETTE_FIRST + i] = colors[i];
            }

            screenPublishPalette(g_mainScreen, PORTRAIT_PALETTE_FIRST, PORTRAIT_PALETTE_COLORS);
        }
    }  // namespace

    void dialogue_portraits_load(data::range page_range)
    {
        dialogue_portraits_unload();

        auto const pages = g_pEngine->data().pages();
        if (page_range.start >= pages.size()) return;

        u16 const end = to<u16>(page_range.start + page_range.count);

        // One guard around every read, not one per portrait: the pause belongs
        // to the box appearing, and taking the system back and forth between
        // portraits would only make it longer.
        mtl::system_use_guard guard;

        for (u16 index = page_range.start; index < end && index < pages.size(); ++index)
        {
            u16 const bitmap_id = portrait_of(pages[index].speaker_id);
            if (bitmap_id == data::NO_ID) continue;

            // Two pages sharing a speaker, or two speakers drawn from the same
            // artwork, cost one copy of it. Chip RAM is what a talkative scene
            // runs out of first.
            if (find(bitmap_id)) continue;

            portrait loaded{};
            if (!load_portrait(bitmap_id, loaded)) continue;

            s_portraits.push_back(mtl::move(loaded));
        }

        if (s_portraits.empty()) return;

        // Taken now rather than at the first draw: the band has to go back the
        // way it was even if no page turns out to have a portrait to draw.
        auto const* reference = force_to<ULONG const*>(screenGetPalette(g_mainScreen));
        for (u16 i = 0; i < PORTRAIT_PALETTE_COLORS; ++i)
        {
            s_band_held[i] = reference[PORTRAIT_PALETTE_FIRST + i];
        }
        s_band_taken = true;

        NE_LOG("loaded %hu portrait(s) for the conversation", to<u16>(s_portraits.size()));
    }

    void dialogue_portraits_unload()
    {
        if (s_band_taken)
        {
            publish_band(s_band_held);
            s_band_taken = false;
        }

        s_band_holder = data::NO_ID;
        s_portraits.clear();
    }

    bool dialogue_portraits_has(u16 speaker_id)
    {
        return find(portrait_of(speaker_id)) != nullptr;
    }

    void dialogue_portraits_draw(u16 speaker_id, u16 x, u16 y)
    {
        portrait* shown = find(portrait_of(speaker_id));
        if (!shown) return;

        if (s_band_holder != shown->bitmap_id)
        {
            publish_band(shown->colors);
            s_band_holder = shown->bitmap_id;
        }

        if (shown->mask)
        {
            screenBlitMask(g_mainScreen,
                shown->image.get(),
                0,
                0,
                to<WORD>(x),
                to<WORD>(y),
                to<WORD>(PORTRAIT_WIDTH),
                to<WORD>(PORTRAIT_HEIGHT),
                shown->mask->Planes[0]);
            return;
        }

        // Not MINTERM_COPY: the portrait's left edge is not on a 16-pixel
        // boundary, and that minterm would blank the columns either side of it.
        screenBlitCopy(g_mainScreen,
            shown->image.get(),
            0,
            0,
            to<WORD>(x),
            to<WORD>(y),
            to<WORD>(PORTRAIT_WIDTH),
            to<WORD>(PORTRAIT_HEIGHT),
            MINTERM_COOKIE);
    }
}  // namespace NEONengine
