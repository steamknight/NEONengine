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
        constexpr uint8_t GFXI_FLAG_VISIBLE      = 1;  // graphic_instance::flags bit 0

        // One placed graphic: its ACE bob plus the frame strip and optional
        // 1-plane mask it draws from (both Chip RAM for the blitter), its
        // visibility, and looping-animation state. The bob points into
        // frames/mask and the manager points at the bob, so a graphic_asset must
        // not move once built - the vector is reserved to its final size up front.
        struct graphic_asset
        {
            ace::bitmap_ptr frames{ nullptr };
            ace::bitmap_ptr mask{ nullptr };
            tBob bob{};
            bool visible = false;
            uint8_t frame_count   = 0;  ///< Frames in the strip (1 = static).
            uint8_t frame_delay   = 0;  ///< Vblanks per frame (0 = static).
            uint16_t frame_height = 0;  ///< Rows per frame, for the frame address.
            uint8_t frame_index   = 0;  ///< Current frame.
            uint16_t tick         = 0;  ///< Vblanks on the current frame (16-bit: delay 255 safe).
        };

        /// One animation frame's blit pointers: the frame plus its optional mask.
        struct frame_ptrs
        {
            UBYTE* frame;
            UBYTE* mask;
        };

        // Resolve the blit addresses for frame @p index of @p a. Frames are
        // stacked vertically and the mask (when present) shares that layout, so
        // both step by index * frame_height rows. bitmap_ptr::get() is non-const,
        // hence the mutable reference.
        frame_ptrs frame_at(graphic_asset& a, uint8_t index)
        {
            UWORD const rowOffset = mtl::to<UWORD>(index * a.frame_height);
            return {
                bobCalcFrameAddress(a.frames.get(), rowOffset),
                a.mask ? bobCalcFrameAddress(a.mask.get(), rowOffset) : nullptr,
            };
        }

        mtl::vector<graphic_asset> s_assets;  ///< The live bobs and their backing data.
        bool s_managerReady = false;
    }  // namespace

    void scene_bobs_create()
    {
        auto* pFront = screenGetFrontBuffer(g_mainScreen);
        auto* pBack  = screenGetBackBuffer(g_mainScreen);
        bobManagerCreate(pFront, pBack, pBack->Rows);
        s_managerReady = true;
    }

    void scene_bobs_destroy()
    {
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
        s_assets.clear();

        game_data& gd  = g_pEngine->data();
        auto instances = gd.graphic_instances();
        uint16_t const first = scene.graphic_instance_range.start;
        uint16_t const count = scene.graphic_instance_range.count;

        // Build the set only for a valid, non-empty range; an empty or
        // out-of-bounds range simply yields no bobs. bobReallocateBuffers() runs
        // unconditionally afterwards - it must always follow the bobInit() calls
        // to size the undraw buffers for whatever was built.
        if (count > 0 && mtl::to<size_t>(first) + count <= instances.size())
        {
            auto graphics = gd.graphics();
            auto bitmaps  = gd.bitmaps();
            // AGA palette entries are ULONG; screenGetPalette() types the buffer
            // as UWORD* for the ACE API, so index it with ULONG stride to land on
            // the right entry.
            ULONG* pPalette = mtl::force_to<ULONG*>(screenGetPalette(g_mainScreen));
            UWORD const screenOffsetY = screenGetVerticalOffset(g_mainScreen);

            // The bob's frame pointer and the manager's pointer to the bob must
            // both stay valid, so the storage must not move after bobInit():
            // reserve the worst case up front.
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
                if (ref.mask_hash != data::NO_ASSET)
                {
                    mask = ace::bitmapCreateFromFd(gd.open_asset(ref.mask_hash), 0);
                    if (!mask) continue;
                }

                // Overlay this graphic's colors into its palette-slot band. The
                // read is capped so a malformed palette can't spill past index 255.
                uint8_t const base = mtl::to<uint8_t>(BOB_PALETTE_SLOT0_BASE - gfx.palette_slot * 8);
                ace::paletteLoadFromFd(gd.open_asset(ref.palette_hash),
                    mtl::force_to<UWORD*>(pPalette + base), mtl::to<UWORD>(PALETTE_SIZE - base));

                s_assets.push_back(graphic_asset{
                    .frames       = mtl::move(frames),
                    .mask         = mtl::move(mask),
                    .visible      = (inst.flags & GFXI_FLAG_VISIBLE) != 0,
                    .frame_count  = gfx.frame_count,
                    .frame_delay  = gfx.frame_delay,
                    .frame_height = gfx.frame_height,
                });

                // frames/mask now live in the asset; resolve frame 0 from that
                // stable storage and register the bob on it.
                graphic_asset& asset = s_assets.back();
                auto const [pFrame, pMask] = frame_at(asset, 0);
                bobInit(&asset.bob, gfx.frame_width, gfx.frame_height, 1, pFrame, pMask,
                    inst.pos.x, mtl::to<UWORD>(inst.pos.y + screenOffsetY));
            }
        }

        // ACE sizes its bob queues and undraw buffers from what was registered,
        // so with nothing registered it allocates zero bytes and builds
        // zero-height bitmaps, which it reports as errors. A scene with no
        // graphics simply leaves the manager unsized; scene_bobs_draw() skips
        // it as well.
        if (!s_assets.empty()) bobReallocateBuffers();

        NE_LOG("scene_bobs: %lu bob(s) loaded", mtl::to<ULONG>(s_assets.size()));
    }

    void scene_bobs_update()
    {
        // Advance each visible animated graphic's frame. frame_delay is in vblanks
        // (0 or a single frame means static); animations loop forever.
        for (auto& asset : s_assets)
        {
            if (!asset.visible || asset.frame_delay == 0 || asset.frame_count <= 1) continue;

            // Hold each frame - including frame 0 on scene entry - for frame_delay
            // vblanks, then advance and count this vblank as the new frame's first.
            if (++asset.tick <= asset.frame_delay) continue;
            asset.tick        = 1;
            asset.frame_index = (asset.frame_index + 1) % asset.frame_count;

            auto const [pFrame, pMask] = frame_at(asset, asset.frame_index);
            bobSetFrame(&asset.bob, pFrame, pMask);
        }
    }

    void scene_bobs_draw()
    {
        if (s_assets.empty()) return;

        // The main loop flips the double buffer every frame, but the bob manager
        // only advances while this state runs. An overlay state (e.g. the F1 debug
        // view) suspends us for a stretch, so on resume the back buffer can be out
        // of phase with the manager. Re-sync before drawing; it's a no-op when
        // already aligned. ACE only auto-corrects this under ACE_DEBUG, so without
        // it a release build would keep drawing bobs into the wrong buffer.
        auto* pBack = screenGetBackBuffer(g_mainScreen);
        bobSetCurrentBuffer(pBack);
        bobBegin(pBack);
        for (auto& asset : s_assets)
        {
            if (asset.visible) bobPush(&asset.bob);
        }
        bobEnd();
    }
}  // namespace NEONengine
