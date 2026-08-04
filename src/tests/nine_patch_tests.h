#ifndef __NINE_PATCH_TESTS_H__INCLUDED__
#define __NINE_PATCH_TESTS_H__INCLUDED__

#include "types.h"

#include <ace/managers/blit.h>
#include <ace/utils/bitmap.h>

#include <ace++/bitmap.h>

#include "core/nine_patch.h"
#include "tests/test_macros.h"

namespace NEONengine
{
    namespace
    {
        constexpr u16 NP_SOURCE_SIZE = 64;  ///< The shipped frame's art is 64x64.
        constexpr u16 NP_BORDER      = 5;   ///< Its corners are 5x5, so its middle is 54x54.
        constexpr u8 NP_INK          = 1;

        char np_message[96];

        /**
         * @brief The palette index at @p x, @p y of an 8bpp interleaved bitmap.
         */
        u8 np_pixel_at(tBitMap const* bitmap, u16 x, u16 y)
        {
            u16 const byteWidth = bitmapGetByteWidth(bitmap);
            u8 const* row       = bitmap->Planes[0] + y * bitmap->BytesPerRow;
            u8 const mask       = to<u8>(0x80 >> (x & 7));

            u8 value = 0;
            for (u8 plane = 0; plane < bitmap->Depth; ++plane)
            {
                if (row[plane * byteWidth + (x >> 3)] & mask) value |= to<u8>(1 << plane);
            }
            return value;
        }

        /**
         * @brief A frame whose every pixel is NP_INK, so any other value is a fault.
         *
         * Real frame art would hide a misplaced piece behind another piece of the
         * same colour. One flat colour cannot: whatever a blit disturbs shows up.
         */
        ace::bitmap_ptr np_make_source()
        {
            auto source = ace::bitmapCreate(
                NP_SOURCE_SIZE, NP_SOURCE_SIZE, 8, BMF_CLEAR | BMF_INTERLEAVED);
            if (source)
            {
                blitRect(source.get(), 0, 0, NP_SOURCE_SIZE, NP_SOURCE_SIZE, NP_INK);
            }
            return source;
        }

        /**
         * @brief Renders a patch of @p width and reports the first pixel that is
         *        not NP_INK, or null when every one of them is.
         */
        char const* np_check_render(u16 width, u16 height)
        {
            auto source = np_make_source();
            if (!source) return "could not create the source bitmap";

            auto patch = nine_patch(source, NP_BORDER, NP_BORDER, NP_BORDER, NP_BORDER);
            auto result = patch.render(width, height, 0);
            if (!result) return "could not render the patch";

            for (u16 y = 0; y < height; ++y)
            {
                for (u16 x = 0; x < width; ++x)
                {
                    u8 const value = np_pixel_at(result.get(), x, y);
                    if (value == NP_INK) continue;

                    snprintf(np_message,
                        sizeof(np_message),
                        "%hux%hu patch: pixel %hu,%hu is %hhu, not %hhu",
                        width,
                        height,
                        x,
                        y,
                        value,
                        NP_INK);
                    return np_message;
                }
            }
            return nullptr;
        }
    }  // namespace

    /// The width a real message box asks for, which width_for has to change.
    TEST_IMPL(test_nine_patch_fills_an_unaligned_box)
    {
        char const* fault = np_check_render(254, 94);
        TEST_ASSERT(fault == nullptr, fault);
        TEST_SUCCESS;
    }

    /// A width width_for leaves alone.
    TEST_IMPL(test_nine_patch_fills_an_aligned_box)
    {
        char const* fault = np_check_render(256, 94);
        TEST_ASSERT(fault == nullptr, fault);
        TEST_SUCCESS;
    }

    /// Too narrow for the middle to tile twice, so no tile meets another.
    TEST_IMPL(test_nine_patch_fills_a_single_tile_box)
    {
        char const* fault = np_check_render(64, 64);
        TEST_ASSERT(fault == nullptr, fault);
        TEST_SUCCESS;
    }

    TEST_SUITE_BEGIN(nine_patch)
    TEST(test_nine_patch_fills_an_unaligned_box)
    TEST(test_nine_patch_fills_an_aligned_box)
    TEST(test_nine_patch_fills_a_single_tile_box)
    TEST_SUITE_END

}  // namespace NEONengine

#endif  // __NINE_PATCH_TESTS_H__INCLUDED__
