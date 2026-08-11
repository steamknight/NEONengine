#include "dialogue_portraits.h"

#include "types.h"

#include "neonengine.h"

#include <ace/managers/blit.h>
#include <ace/utils/bitmap.h>

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

            UBYTE const depth = screenGetBackBuffer(g_mainScreen)->Depth;

            // Chip RAM: the blitter reads these exactly as the exporter wrote
            // them, with the band's base already in the pixels.
            ace::bitmap_ptr image = ace::bitmapCreateFromFd(gd.open_asset(ref.bitmap_hash), 0);
            if (!image)
            {
                NE_LOG("portrait bitmap %hu could not be read", bitmap_id);
                return false;
            }

            if (!is_portrait_sized(image.get()))
            {
                NE_LOG("portrait bitmap %hu is smaller than %hux%hu",
                    bitmap_id,
                    PORTRAIT_WIDTH,
                    PORTRAIT_HEIGHT);
                return false;
            }

            // A shallower portrait would leave the planes above it holding the
            // previous scene's pixels, and a deeper one would be blitted into
            // planes the screen does not have.
            if (image->Depth != depth)
            {
                NE_LOG("portrait bitmap %hu has %hhu planes, screen has %hhu",
                    bitmap_id,
                    image->Depth,
                    depth);
                return false;
            }

            ace::bitmap_ptr mask{ nullptr };
            if (ref.mask_hash != data::NO_ASSET)
            {
                mask = ace::bitmapCreateFromFd(gd.open_asset(ref.mask_hash), 0);
                if (!mask)
                {
                    NE_LOG("portrait bitmap %hu has a mask that could not be read", bitmap_id);
                    return false;
                }

                // The mask is read at the artwork's size, so a smaller one would
                // be read past its end the same way a smaller portrait would.
                if (!is_portrait_sized(mask.get()))
                {
                    NE_LOG("portrait bitmap %hu has a mask smaller than %hux%hu",
                        bitmap_id,
                        PORTRAIT_WIDTH,
                        PORTRAIT_HEIGHT);
                    return false;
                }

                // The blitter walks the mask with the artwork's own stride, so a
                // mask that is not laid out identically - same width, depth and
                // interleaving, which BytesPerRow together with Depth covers -
                // would be read out of step with it and past its end.
                if (mask->BytesPerRow != image->BytesPerRow || mask->Depth != image->Depth)
                {
                    NE_LOG("portrait bitmap %hu's mask is laid out %hu:%hhu, artwork %hu:%hhu",
                        bitmap_id,
                        mask->BytesPerRow,
                        mask->Depth,
                        image->BytesPerRow,
                        image->Depth);
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

        auto const pages = page_range.of(g_pEngine->data().pages());

        // One guard around every read, not one per portrait: the pause belongs
        // to the box appearing, and taking the system back and forth between
        // portraits would only make it longer.
        mtl::system_use_guard guard;

        for (auto const& page : pages)
        {
            u16 const bitmap_id = portrait_of(page.speaker_id);
            if (bitmap_id == data::NO_ID) continue;

            // Don't load the same portrait twice
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

    void dialogue_portraits_release_band()
    {
        // Nothing to hand back when no portrait ever took the band, and nothing
        // to do when the scene's own colors are already the live ones.
        if (!s_band_taken || s_band_holder == data::NO_ID) return;

        publish_band(s_band_held);
        s_band_holder = data::NO_ID;
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
