#include "mouse_pointer.h"

#include "neonengine.h"

#include <ace/managers/blit.h>
#include <ace/managers/mouse.h>
#include <ace/managers/sprite.h>
#include <ace/managers/system.h>
#include <ace/managers/viewport/simplebuffer.h>

#include <ace++/bitmap.h>

#include <mtl/utility.h>

#include "core/game_data.h"
#include "core/logger.h"
#include "core/screen.h"

NE_LOG_DOMAIN(Cursor);

namespace NEONengine
{
    namespace
    {
        constexpr auto POINTER_COUNT = mtl::to<int>(mouse_pointer::COUNT);
    }

    static tBitMap *s_pPointersLo[POINTER_COUNT];
    static tBitMap *s_pPointersHi[POINTER_COUNT];
    static tSprite *s_pCurrentPointer0;
    static tSprite *s_pCurrentPointer1;  // attached sprite.

    /// Each role's click offset within its frame, from its CURS record.
    static data::point<uint16_t> s_hotspots[POINTER_COUNT];
    static mouse_pointer s_current = mouse_pointer::DEFAULT;
    static bool s_ready            = false;
    static bool s_visible          = false;  ///< Tracks spriteSetEnabled to avoid needless regens.

#define POINTER_WIDTH  16
#define POINTER_HEIGHT 16
#define POINTER_BPP    4
#define SPRITE_BPP     2

    bool mousePointerCreate()
    {
        auto& gd        = g_pEngine->data();
        auto const& ui  = gd.ui_state();
        auto const bitmaps = gd.bitmaps();

        // The sheet's frames are fixed at 16x16 so each fits one sprite; a
        // project that says otherwise cannot be shown as a hardware pointer.
        if (ui.cursor_bitmap_id == data::NO_ID || ui.cursor_bitmap_id >= bitmaps.size()
            || ui.cursor_frame_width != POINTER_WIDTH
            || ui.cursor_frame_height != POINTER_HEIGHT)
        {
            NE_LOG("no usable cursor sheet (id %hu, %hux%hu)",
                ui.cursor_bitmap_id, ui.cursor_frame_width, ui.cursor_frame_height);
            return false;
        }

        auto const cursors = ui.cursor_range.of(gd.cursors());
        if (cursors.is_empty())
        {
            NE_LOG("cursor range %hu+%hu does not resolve",
                ui.cursor_range.start, ui.cursor_range.count);
            return false;
        }

        systemUse();
        auto pSheet = ace::bitmapCreateFromFd(gd.open_asset(bitmaps[ui.cursor_bitmap_id].bitmap_hash), 0);
        if (!pSheet)
        {
            NE_LOG("could not load the cursor sheet");
            systemUnuse();
            return false;
        }

        // Frames are packed row-major from the sheet's top-left, so a frame id
        // resolves to its column and row. Both divisions happen once per role.
        UWORD const columns = mtl::to<UWORD>(bitmapGetByteWidth(pSheet.get()) * 8 / POINTER_WIDTH);

        for (auto idx = 0; idx < POINTER_COUNT; idx++)
        {
            // Sprites need to have one extra line above and below the image.
            tBitMap *pPointer = bitmapCreate(
                POINTER_WIDTH, POINTER_HEIGHT + 2, POINTER_BPP, BMF_CLEAR | BMF_INTERLEAVED);

            s_pPointersHi[idx] = bitmapCreate(
                POINTER_WIDTH, POINTER_HEIGHT + 2, SPRITE_BPP, BMF_CLEAR | BMF_INTERLEAVED);

            s_pPointersLo[idx] = bitmapCreate(
                POINTER_WIDTH, POINTER_HEIGHT + 2, SPRITE_BPP, BMF_CLEAR | BMF_INTERLEAVED);

            // A role the project did not configure keeps an empty frame and a
            // zero offset rather than reaching outside the sheet.
            UWORD frame = 0;
            if (mtl::to<size_t>(idx) < cursors.size())
            {
                auto const& record = cursors[idx];
                frame           = record.frame;
                s_hotspots[idx] = record.hotspot;
            }

            blitCopyAligned(pSheet.get(),
                mtl::to<WORD>((frame % columns) * POINTER_WIDTH),
                mtl::to<WORD>((frame / columns) * POINTER_HEIGHT),
                pPointer,
                0,
                1,
                POINTER_WIDTH,
                POINTER_HEIGHT);

            // Convert the 4bpp bitmap to 2bpp.
            UWORD uwSourceWidth = bitmapGetByteWidth(pPointer);
            for (UWORD r = 0; r < pPointer->Rows; r++)
            {
                UWORD uwOffsetSrc = r * pPointer->BytesPerRow;
                UWORD uwOffsetDst = r * s_pPointersLo[idx]->BytesPerRow;
                memcpy(s_pPointersLo[idx]->Planes[0] + uwOffsetDst,
                       pPointer->Planes[0] + uwOffsetSrc,
                       uwSourceWidth);
                memcpy(s_pPointersLo[idx]->Planes[1] + uwOffsetDst,
                       pPointer->Planes[1] + uwOffsetSrc,
                       uwSourceWidth);
                memcpy(s_pPointersHi[idx]->Planes[0] + uwOffsetDst,
                       pPointer->Planes[2] + uwOffsetSrc,
                       uwSourceWidth);
                memcpy(s_pPointersHi[idx]->Planes[1] + uwOffsetDst,
                       pPointer->Planes[3] + uwOffsetSrc,
                       uwSourceWidth);
            }

            bitmapDestroy(pPointer);
        }

        // pSheet releases itself when it leaves scope.

        spriteManagerCreate(screenGetView(g_mainScreen), 0, NULL);
        systemSetDmaBit(DMAB_SPRITE, 1);

        s_current          = mouse_pointer::DEFAULT;
        // An attached pair indexes a 16-aligned bank, here entries 16..31. The
        // sheet only names values 8..15, so the pointer reads 24..31 and the
        // lower half of the bank stays free for the UI.
        s_pCurrentPointer0 = spriteAdd(0, s_pPointersLo[0]);
        spriteSetEnabled(s_pCurrentPointer0, 1);

        s_pCurrentPointer1 = spriteAdd(1, s_pPointersHi[0]);
        spriteSetEnabled(s_pCurrentPointer1, 1);
        spriteSetAttached(s_pCurrentPointer1, 1);

        systemUnuse();

        s_ready   = true;
        s_visible = true;
        NE_LOG("%lu cursor role(s) ready", mtl::to<ULONG>(cursors.size()));
        return true;
    }

    void mousePointerSwitch(mouse_pointer newPointer)
    {
        if (!s_ready || newPointer == s_current || newPointer >= mouse_pointer::COUNT) return;

        s_current       = newPointer;
        auto const idx  = mtl::to<int>(newPointer);
        spriteSetBitmap(s_pCurrentPointer0, s_pPointersLo[idx]);
        spriteSetBitmap(s_pCurrentPointer1, s_pPointersHi[idx]);
    }

    void mousePointerSetVisible(bool visible)
    {
        // spriteSetEnabled() schedules a copper regeneration for both buffers,
        // so this is guarded rather than left idempotent: every scene load would
        // otherwise pay for a regeneration it does not need.
        if (!s_ready || visible == s_visible) return;

        s_visible = visible;
        spriteSetEnabled(s_pCurrentPointer0, visible ? 1 : 0);
        spriteSetEnabled(s_pCurrentPointer1, visible ? 1 : 0);
    }

    void mousePointerUpdate(void)
    {
        if (!s_ready) return;

        // The sheet marks where inside the frame the cursor actually points, so
        // the sprite trails the mouse by that much for the tip to land on it.
        auto const& hotspot = s_hotspots[mtl::to<int>(s_current)];

        s_pCurrentPointer0->wX = mtl::to<WORD>(mouseGetX(MOUSE_PORT_1) - hotspot.x);
        s_pCurrentPointer0->wY = mtl::to<WORD>(mouseGetY(MOUSE_PORT_1) - hotspot.y);
        s_pCurrentPointer1->wX = s_pCurrentPointer0->wX;
        s_pCurrentPointer1->wY = s_pCurrentPointer0->wY;
        spriteRequestMetadataUpdate(s_pCurrentPointer0);
        spriteRequestMetadataUpdate(s_pCurrentPointer1);

        spriteProcessChannel(0);
        spriteProcessChannel(1);
        spriteProcess(s_pCurrentPointer0);
        spriteProcess(s_pCurrentPointer1);
    }

    void mousePointerDestroy(void)
    {
        if (!s_ready) return;
        s_ready = false;

        for (auto idx = 0; idx < POINTER_COUNT; idx++)
        {
            bitmapDestroy(s_pPointersLo[idx]);
            bitmapDestroy(s_pPointersHi[idx]);
        }

        spriteRemove(s_pCurrentPointer0);
        spriteRemove(s_pCurrentPointer1);

        systemSetDmaBit(DMAB_SPRITE, 0);
        spriteManagerDestroy();
    }
}  // namespace NEONengine
