#include "scene_bobs.h"

#include <ace/managers/bob.h>
#include <ace/managers/log.h>

#include <ace++/bitmap.h>
#include <ace++/palette.h>

#include <mtl/utility.h>
#include <mtl/vector.h>

#include "game_data.h"
#include "neonengine.h"
#include "screen.h"

namespace NEONengine
{
    namespace
    {
        // BOB palette band: slot 0 sits at indices 72..79 and each higher slot's
        // base drops 8 toward 32. Frame pixel indices are baked into this band at
        // build time, so the engine only overlays each graphic's colors here.
        constexpr uint8_t BOB_PALETTE_SLOT0_BASE = 72;
        constexpr uint16_t PALETTE_SIZE          = 256;
        constexpr uint32_t NO_ASSET              = 0xFFFFFFFFu;
        constexpr uint8_t GFXI_FLAG_VISIBLE      = 1;  // graphic_instance::flags bit 0

        // A graphic's frame strip and optional 1-plane mask (both in Chip RAM for
        // the blitter) plus its initial visibility. Parallel to s_bobs and kept
        // alive as long as the bobs referencing them exist.
        struct GraphicAsset
        {
            ace::bitmap_ptr frames;
            ace::bitmap_ptr mask;
            bool visible;
        };

        mtl::vector<GraphicAsset> s_assets;  ///< Backing bitmaps for the live bobs.
        mtl::vector<tBob> s_bobs;            ///< One bob per instance; stable once built.
        bool s_managerReady = false;
    }  // namespace

    void scene_bobs_create()
    {
        tBitMap* pFront = screenGetFrontBuffer(g_mainScreen);
        tBitMap* pBack  = screenGetBackBuffer(g_mainScreen);
        bobManagerCreate(pFront, pBack, pBack->Rows);
        s_managerReady = true;
    }

    void scene_bobs_destroy()
    {
        s_bobs.clear();
        s_assets.clear();
        if (s_managerReady)
        {
            bobManagerDestroy();
            s_managerReady = false;
        }
    }

    void scene_bobs_load(data::scene const& scene)
    {
        // Forget the previous scene's bobs so a fresh set can be registered; ACE
        // forbids bobInit() once bobReallocateBuffers() has run for a set.
        bobManagerReset();
        s_bobs.clear();
        s_assets.clear();

        uint16_t const count = scene.graphic_instance_range.count;
        if (count == 0)
        {
            bobReallocateBuffers();
            return;
        }

        game_data& gd   = g_pEngine->data();
        auto graphics   = gd.graphics();
        auto instances  = gd.graphic_instances();
        auto bitmaps    = gd.bitmaps();
        // AGA palette entries are ULONG; screenGetPalette() types the buffer as
        // UWORD* for the ACE API, so index it with ULONG stride to land on the
        // right entry.
        ULONG* pPalette = mtl::force_to<ULONG*>(screenGetPalette(g_mainScreen));
        UWORD const offsetY = screenGetVerticalOffset(g_mainScreen);

        uint16_t const first = scene.graphic_instance_range.start;
        if (mtl::to<size_t>(first) + count > instances.size())
        {
            // Malformed range: don't index past the collection.
            bobReallocateBuffers();
            return;
        }

        // The manager keeps a pointer to each bob, so the storage must not move
        // after bobInit(): reserve the worst case up front.
        s_bobs.reserve(count);
        s_assets.reserve(count);

        for (uint16_t i = 0; i < count; ++i)
        {
            data::graphic_instance const& inst = instances[first + i];
            if (inst.graphic_id >= graphics.size()) continue;
            data::graphic const& gfx = graphics[inst.graphic_id];
            if (gfx.source_bitmap_id >= bitmaps.size()) continue;
            data::bitmap_ref const& ref = bitmaps[gfx.source_bitmap_id];

            ace::bitmap_ptr frames = ace::bitmapCreateFromFd(gd.open_asset(ref.bitmap_hash), 0);
            if (!frames) continue;

            ace::bitmap_ptr mask{ nullptr };
            if (ref.mask_hash != NO_ASSET)
            {
                mask = ace::bitmapCreateFromFd(gd.open_asset(ref.mask_hash), 0);
                if (!mask) continue;
            }

            // Overlay this graphic's colors into its palette-slot band. The read
            // is capped so a malformed palette can never spill past index 255.
            uint8_t const base = mtl::to<uint8_t>(BOB_PALETTE_SLOT0_BASE - gfx.palette_slot * 8);
            ace::paletteLoadFromFd(gd.open_asset(ref.palette_hash),
                mtl::force_to<UWORD*>(pPalette + base), mtl::to<UWORD>(PALETTE_SIZE - base));

            UBYTE* pFrame = bobCalcFrameAddress(frames.get(), 0);
            UBYTE* pMask  = mask ? bobCalcFrameAddress(mask.get(), 0) : nullptr;

            bool const visible = (inst.flags & GFXI_FLAG_VISIBLE) != 0;
            s_assets.push_back(GraphicAsset{ mtl::move(frames), mtl::move(mask), visible });
            tBob& bob = s_bobs.emplace_back();
            bobInit(&bob, gfx.frame_width, gfx.frame_height, 1, pFrame, pMask, inst.pos.x,
                mtl::to<UWORD>(inst.pos.y + offsetY));
        }

        bobReallocateBuffers();
        NE_LOG("scene_bobs: %lu bob(s) loaded", mtl::to<ULONG>(s_bobs.size()));
    }

    void scene_bobs_draw()
    {
        if (s_bobs.empty()) return;

        // The main loop flips the double buffer every frame, but the bob manager
        // only advances while this state runs. An overlay state (e.g. the F1 debug
        // view) suspends us for a stretch, so on resume the back buffer can be out
        // of phase with the manager. Re-sync before drawing; it's a no-op when
        // already aligned. ACE only auto-corrects this under ACE_DEBUG, so without
        // it a release build would keep drawing bobs into the wrong buffer.
        tBitMap* pBack = screenGetBackBuffer(g_mainScreen);
        bobSetCurrentBuffer(pBack);
        bobBegin(pBack);
        for (size_t i = 0; i < s_bobs.size(); ++i)
        {
            if (s_assets[i].visible) bobPush(&s_bobs[i]);
        }
        bobEnd();
    }
}  // namespace NEONengine
