#include "mouse_pointer.h"

#include "types.h"

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
#define POINTER_WIDTH  16
#define POINTER_HEIGHT 16
#define POINTER_BPP    4
#define SPRITE_BPP     2

    mouse_pointer::mouse_pointer(game_data const& data, asset_cache& cache)
    {
        auto const& ui     = data.ui_state();
        auto const bitmaps = data.bitmaps();

        // The sheet's frames are fixed at 16x16 so each fits one sprite; a
        // project that says otherwise cannot be shown as a hardware pointer.
        if (ui.cursor_bitmap_id == data::NO_ID || ui.cursor_bitmap_id >= bitmaps.size()
            || ui.cursor_frame_width != POINTER_WIDTH
            || ui.cursor_frame_height != POINTER_HEIGHT)
        {
            NE_LOG("no usable cursor sheet (id %hu, %hux%hu)",
                ui.cursor_bitmap_id, ui.cursor_frame_width, ui.cursor_frame_height);
            return;
        }

        auto const cursors = ui.cursor_range.of(data.cursors());
        if (cursors.is_empty())
        {
            NE_LOG("cursor range %hu+%hu does not resolve",
                ui.cursor_range.start, ui.cursor_range.count);
            return;
        }

        auto pSheet = ace::bitmapCreateFromFd(
            cache.open(bitmaps[ui.cursor_bitmap_id].bitmap_hash), 0);
        if (!pSheet)
        {
            NE_LOG("could not load the cursor sheet");
            return;
        }

        // Frames are packed row-major from the sheet's top-left, so a frame id
        // resolves to its column and row. Both divisions happen once per role.
        UWORD const columns = to<UWORD>(bitmapGetByteWidth(pSheet.get()) * 8 / POINTER_WIDTH);

        for (auto idx = 0; idx < ROLE_COUNT; idx++)
        {
            // Sprites need to have one extra line above and below the image.
            tBitMap *pPointer = bitmapCreate(
                POINTER_WIDTH, POINTER_HEIGHT + 2, POINTER_BPP, BMF_CLEAR | BMF_INTERLEAVED);

            _high[idx] = bitmapCreate(
                POINTER_WIDTH, POINTER_HEIGHT + 2, SPRITE_BPP, BMF_CLEAR | BMF_INTERLEAVED);

            _low[idx] = bitmapCreate(
                POINTER_WIDTH, POINTER_HEIGHT + 2, SPRITE_BPP, BMF_CLEAR | BMF_INTERLEAVED);

            // A role the project did not configure keeps an empty frame and a
            // zero offset rather than reaching outside the sheet.
            UWORD frame = 0;
            if (to<size_t>(idx) < cursors.size())
            {
                auto const& record = cursors[idx];
                frame           = record.frame;
                _hotspots[idx] = record.hotspot;
            }

            blitCopyAligned(pSheet.get(),
                to<WORD>((frame % columns) * POINTER_WIDTH),
                to<WORD>((frame / columns) * POINTER_HEIGHT),
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
                UWORD uwOffsetDst = r * _low[idx]->BytesPerRow;
                memcpy(_low[idx]->Planes[0] + uwOffsetDst,
                       pPointer->Planes[0] + uwOffsetSrc,
                       uwSourceWidth);
                memcpy(_low[idx]->Planes[1] + uwOffsetDst,
                       pPointer->Planes[1] + uwOffsetSrc,
                       uwSourceWidth);
                memcpy(_high[idx]->Planes[0] + uwOffsetDst,
                       pPointer->Planes[2] + uwOffsetSrc,
                       uwSourceWidth);
                memcpy(_high[idx]->Planes[1] + uwOffsetDst,
                       pPointer->Planes[3] + uwOffsetSrc,
                       uwSourceWidth);
            }

            bitmapDestroy(pPointer);
        }

        // pSheet releases itself when it leaves scope.

        spriteManagerCreate(screenGetView(g_mainScreen), 0, NULL);
        systemSetDmaBit(DMAB_SPRITE, 1);

        // An attached pair indexes a 16-aligned bank, here entries 16..31. The
        // sheet only names values 8..15, so the pointer reads 24..31 and the
        // lower half of the bank stays free for the UI.
        _sprite0 = spriteAdd(0, _low[0]);
        spriteSetEnabled(_sprite0, 1);

        _sprite1 = spriteAdd(1, _high[0]);
        spriteSetEnabled(_sprite1, 1);
        spriteSetAttached(_sprite1, 1);

        _ready   = true;
        _visible = true;
        NE_LOG("%lu cursor role(s) ready", to<ULONG>(cursors.size()));
    }

    mouse_pointer::~mouse_pointer()
    {
        if (!_ready) return;

        for (auto idx = 0; idx < ROLE_COUNT; idx++)
        {
            bitmapDestroy(_low[idx]);
            bitmapDestroy(_high[idx]);
        }

        spriteRemove(_sprite0);
        spriteRemove(_sprite1);

        systemSetDmaBit(DMAB_SPRITE, 0);
        spriteManagerDestroy();
    }

    void mouse_pointer::switch_to(pointer_role role)
    {
        if (!_ready || role == _current || role >= pointer_role::COUNT) return;

        _current       = role;
        auto const idx = to<int>(role);
        spriteSetBitmap(_sprite0, _low[idx]);
        spriteSetBitmap(_sprite1, _high[idx]);
    }

    void mouse_pointer::set_visible(bool visible)
    {
        // spriteSetEnabled() schedules a copper regeneration for both buffers,
        // so this is guarded rather than left idempotent: every scene load would
        // otherwise pay for a regeneration it does not need.
        if (!_ready || visible == _visible) return;

        _visible = visible;
        spriteSetEnabled(_sprite0, visible ? 1 : 0);
        spriteSetEnabled(_sprite1, visible ? 1 : 0);
    }

    void mouse_pointer::update()
    {
        if (!_ready) return;

        // The sheet marks where inside the frame the cursor actually points, so
        // the sprite trails the mouse by that much for the tip to land on it.
        auto const& hotspot = _hotspots[to<int>(_current)];

        _sprite0->wX = to<WORD>(mouseGetX(MOUSE_PORT_1) - hotspot.x);
        _sprite0->wY = to<WORD>(mouseGetY(MOUSE_PORT_1) - hotspot.y);
        _sprite1->wX = _sprite0->wX;
        _sprite1->wY = _sprite0->wY;
        spriteRequestMetadataUpdate(_sprite0);
        spriteRequestMetadataUpdate(_sprite1);

        spriteProcessChannel(0);
        spriteProcessChannel(1);
        spriteProcess(_sprite0);
        spriteProcess(_sprite1);
    }
}  // namespace NEONengine
