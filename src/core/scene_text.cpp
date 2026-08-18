#include "scene_text.h"

#include "types.h"

#include "neonengine.h"

#include <ace/utils/font.h>

#include <ace++/font.h>

#include <mtl/span.h>
#include <mtl/utility.h>
#include <mtl/vector.h>

#include "core/logger.h"
#include "game_data.h"
#include "screen.h"
#include "string_table.h"
#include "text_render.h"

NE_LOG_DOMAIN(Text);

namespace NEONengine
{
    namespace
    {
        // text_region::control_bits. Horizontal and vertical alignment are two
        // bits each; in both fields 3 is not a valid alignment.
        constexpr u8 CTL_H_SHIFT     = 0;
        constexpr u8 CTL_V_SHIFT     = 2;
        constexpr u8 CTL_ALIGN_MASK  = 0x3;
        constexpr u8 CTL_FULL_WIDTH  = 1 << 4;
        constexpr u8 CTL_FULL_HEIGHT = 1 << 5;

        constexpr u8 ALIGN_CENTER = 1;
        constexpr u8 ALIGN_FAR    = 2;  ///< Right horizontally, bottom vertically.

        /// The box a region lays its text out in, after the full-scene bits.
        struct layout_box
        {
            u16 x;
            u16 y;
            u16 width;
            u16 height;
        };

        /**
         * @brief The horizontal justification @p controlBits asks for.
         *
         * The format orders its alignments left, center, right and text_justify
         * does not, so this maps rather than casts. An invalid value falls back
         * to left, which is also what an all-zero control byte means.
         */
        text_justify horizontal_of(u8 controlBits)
        {
            switch ((controlBits >> CTL_H_SHIFT) & CTL_ALIGN_MASK)
            {
                case ALIGN_CENTER: return text_justify::CENTER;
                case ALIGN_FAR: return text_justify::RIGHT;
                default: return text_justify::LEFT;
            }
        }

        /**
         * @brief The row @p textHeight rows of text begin on inside @p box.
         *
         * @p textHeight must already fit in the box, so the centered and bottom
         * cases cannot run above it.
         */
        u16 top_of(u8 controlBits, layout_box const& box, u16 textHeight)
        {
            switch ((controlBits >> CTL_V_SHIFT) & CTL_ALIGN_MASK)
            {
                case ALIGN_CENTER:
                    return to<u16>(box.y + ((box.height - textHeight) >> 1));
                case ALIGN_FAR: return to<u16>(box.y + box.height - textHeight);
                default: return box.y;
            }
        }

        /**
         * @brief @p region's layout box, expanded by its full-scene bits and
         *        clipped to the screen.
         *
         * The full-scene bits select the scene's own origin and size in place of
         * the region's rather than offsetting them. Clipping matters because
         * fontDrawTextBitMap() blits the text bitmap's full extent with no
         * bounds check of any kind, so an off-screen box would write past the
         * buffer. A box left with zero width or height draws nothing.
         */
        layout_box box_of(data::text_region const& region, data::scene const& scene)
        {
            bool const fullWidth  = (region.control_bits & CTL_FULL_WIDTH) != 0;
            bool const fullHeight = (region.control_bits & CTL_FULL_HEIGHT) != 0;

            layout_box box{
                fullWidth ? scene.offset.x : region.bounds.pos.x,
                fullHeight ? scene.offset.y : region.bounds.pos.y,
                fullWidth ? scene.width : region.bounds.width,
                fullHeight ? scene.height : region.bounds.height,
            };

            if (box.x >= SCREEN_WIDTH || box.y >= SCREEN_HEIGHT)
            {
                box.width  = 0;
                box.height = 0;
                return box;
            }

            u16 const roomX = to<u16>(SCREEN_WIDTH - box.x);
            u16 const roomY = to<u16>(SCREEN_HEIGHT - box.y);
            if (box.width > roomX) box.width = roomX;
            if (box.height > roomY) box.height = roomY;
            return box;
        }

        /**
         * @brief Orders @p regions' indices by ascending z_order into @p order.
         *
         * Insertion sort: a scene owns few regions, and it is stable, so regions
         * sharing a z_order stay in the order they were authored.
         */
        void build_draw_order(mtl::span<data::text_region> regions, mtl::vector<u16>& order)
        {
            u16 const count = to<u16>(regions.size());
            order.clear();
            order.reserve(count);

            for (u16 i = 0; i < count; ++i)
            {
                size_t pos = order.size();
                order.push_back(i);
                while (pos > 0 && regions[order[pos - 1]].z_order > regions[i].z_order)
                {
                    order[pos] = order[pos - 1];
                    --pos;
                }
                order[pos] = i;
            }
        }
    }  // namespace

    void scene_text_draw(data::scene const& scene)
    {
        if (scene.text_region_range.count == 0) return;

        auto const all     = g_pEngine->data().text_regions();
        auto const regions = scene.text_region_range.of(all);
        if (regions.is_empty())
        {
            NE_LOG("TXTR range %hu+%hu is outside the %lu region(s) loaded",
                scene.text_region_range.start,
                scene.text_region_range.count,
                to<ULONG>(all.size()));
            return;
        }

        auto order = mtl::vector<u16>(regions.size());
        build_draw_order(regions, order);

        text_renderer* pRenderer = g_pEngine->default_text_renderer();
        if (!pRenderer) return;

        u16 drawn = 0;
        for (u16 slot = 0; slot < regions.size(); ++slot)
        {
            u16 const index            = order[slot];
            data::text_region const& region = regions[index];

            auto const text = g_pEngine->strings().get_string(region.string_id);
            if (text.is_empty()) continue;

            layout_box const box = box_of(region, scene);
            if (box.width == 0 || box.height == 0) continue;

            // 24..31 belong to the hardware pointer on a scene that shows UI,
            // and are held out of the fade there, so text drawn in them would
            // stay lit on a blacked-out screen instead of fading with the scene.
            // Draw it anyway - losing the text would be worse than showing it
            // wrong - but say so. A scene without UI fades the band normally.
            if ((scene.flags & data::SCENE_FLAG_SHOW_UI) != 0
                && region.color_index >= CURSOR_PALETTE_FIRST
                && region.color_index < CURSOR_PALETTE_FIRST + CURSOR_PALETTE_COUNT)
            {
                NE_LOG("region %hu uses color %hhu, reserved for the pointer",
                    index,
                    region.color_index);
            }

            auto glyphs = pRenderer->create_text(text, box.width, horizontal_of(region.control_bits));
            if (!glyphs || glyphs->uwActualHeight == 0) continue;

            // create_text wraps to the renderer's own line limit, which owes the
            // box nothing. fontDrawTextBitMap() then blits uwActualHeight rows
            // without clipping, so text taller than its box has to be cut here
            // or it runs past the box and possibly past the buffer.
            if (glyphs->uwActualHeight > box.height)
            {
                NE_LOG("region %hu wants %hu rows, clamped to %hu",
                    index,
                    glyphs->uwActualHeight,
                    box.height);
                glyphs->uwActualHeight = box.height;
            }

            g_pEngine->main_screen().text_copy(
                glyphs.get(),
                box.x,
                top_of(region.control_bits, box, glyphs->uwActualHeight),
                region.color_index,
                FONT_COOKIE);
            ++drawn;
        }

        NE_LOG("%hu of %lu text region(s) drawn", drawn, to<ULONG>(regions.size()));
    }
}  // namespace NEONengine
