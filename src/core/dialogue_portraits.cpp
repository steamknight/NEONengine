#include "dialogue_portraits.h"

#include "types.h"

#include <ace/managers/blit.h>
#include <ace/utils/bitmap.h>

#include <ace++/palette.h>

#include <mtl/utility.h>

#include "core/logger.h"
#include "core/screen.h"
#include "utils/assert.h"

NE_LOG_DOMAIN(Dialogue);

namespace NEONengine
{
    namespace
    {
        /// A portrait's rows are this many bytes wide, per bitplane.
        constexpr u16 PORTRAIT_BYTE_WIDTH = PORTRAIT_WIDTH / 8;

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
    }  // namespace

    dialogue_portraits::~dialogue_portraits()
    {
        if (_band_taken) publish_band(_band_held);
    }

    dialogue_portraits::portrait const* dialogue_portraits::find(u16 bitmap_id) const
    {
        if (bitmap_id == data::NO_ID) return nullptr;

        for (size_t i = 0; i < _portraits.size(); ++i)
        {
            if (_portraits[i].bitmap_id == bitmap_id) return &_portraits[i];
        }
        return nullptr;
    }

    u16 dialogue_portraits::portrait_of(u16 speaker_id) const
    {
        if (speaker_id == data::NO_ID) return data::NO_ID;

        auto const speakers = _data.speakers();
        if (speaker_id >= speakers.size()) return data::NO_ID;

        return speakers[speaker_id].portrait_bitmap_id;
    }

    bool dialogue_portraits::load_portrait(u16 bitmap_id, portrait& out)
    {
        auto const bitmaps = _data.bitmaps();
        if (bitmap_id >= bitmaps.size())
        {
            NE_LOG("portrait names bitmap %hu, which is not in the pak", bitmap_id);
            return false;
        }

        data::bitmap_ref const& ref = bitmaps[bitmap_id];

        UBYTE const depth = g_mainScreen->back_buffer()->Depth;

        // Chip RAM: the blitter reads these exactly as the exporter wrote
        // them, with the band's base already in the pixels.
        ace::bitmap_ptr image = ace::bitmapCreateFromFd(_cache.open(ref.bitmap_hash), 0);
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
            mask = ace::bitmapCreateFromFd(_cache.open(ref.mask_hash), 0);
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

        ace::paletteLoadFromFd(_cache.open(ref.palette_hash),
            force_to<UWORD*>(out.colors),
            PORTRAIT_PALETTE_COLORS);

        out.bitmap_id = bitmap_id;
        out.image     = mtl::move(image);
        out.mask      = mtl::move(mask);
        return true;
    }

    void dialogue_portraits::publish_band(ULONG const* colors)
    {
        auto* reference = force_to<ULONG*>(g_mainScreen->palette());
        for (u16 i = 0; i < PORTRAIT_PALETTE_COLORS; ++i)
        {
            reference[PORTRAIT_PALETTE_FIRST + i] = colors[i];
        }

        g_mainScreen->publish_palette(PORTRAIT_PALETTE_FIRST, PORTRAIT_PALETTE_COLORS);
    }

    void dialogue_portraits::load(data::range page_range)
    {
        assert(_portraits.empty() && !_band_taken, "loaded a conversation's portraits twice");

        auto const pages = page_range.of(_data.pages());

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

            _portraits.push_back(mtl::move(loaded));
        }

        if (_portraits.empty()) return;

        // Taken now rather than at the first draw: the band has to go back the
        // way it was even if no page turns out to have a portrait to draw.
        auto const* reference = force_to<ULONG const*>(g_mainScreen->palette());
        for (u16 i = 0; i < PORTRAIT_PALETTE_COLORS; ++i)
        {
            _band_held[i] = reference[PORTRAIT_PALETTE_FIRST + i];
        }
        _band_taken = true;

        NE_LOG("loaded %hu portrait(s) for the conversation", to<u16>(_portraits.size()));
    }

    void dialogue_portraits::release_band()
    {
        // Nothing to hand back when no portrait ever took the band, and nothing
        // to do when the scene's own colors are already the live ones.
        if (!_band_taken || _band_holder == data::NO_ID) return;

        publish_band(_band_held);
        _band_holder = data::NO_ID;
    }

    bool dialogue_portraits::has(u16 speaker_id) const
    {
        return find(portrait_of(speaker_id)) != nullptr;
    }

    void dialogue_portraits::show(u16 speaker_id, u16 x, u16 y)
    {
        portrait const* shown = find(portrait_of(speaker_id));
        if (!shown)
        {
            release_band();
            return;
        }

        if (_band_holder != shown->bitmap_id)
        {
            publish_band(shown->colors);
            _band_holder = shown->bitmap_id;
        }

        if (shown->mask)
        {
            g_mainScreen->blit_copy_mask(
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
        g_mainScreen->blit_copy(
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
