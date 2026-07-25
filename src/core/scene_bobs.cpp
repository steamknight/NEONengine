#include "scene_bobs.h"

#include <ace/managers/bob.h>
#include <ace/managers/log.h>

#include <ace++/bitmap.h>
#include <ace++/palette.h>

#include <mtl/span.h>
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
        // Hotspot state graphics reuse this: they share the frame-strip layout
        // and blit addressing, and simply leave frame_delay/tick inert because
        // their frame comes from the hotspot's state rather than a timer.
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

        /// Hotspot state graphics, drawn above the instances.
        mtl::vector<graphic_asset> s_hotspotAssets;

        constexpr uint16_t NO_BOB = 0xFFFF;

        /// Scene-local hotspot index -> index into s_hotspotAssets, or NO_BOB.
        /// Most hotspots carry no graphic, so the mapping is kept out of line.
        mtl::vector<uint16_t> s_hotspotBob;

        bool s_managerReady = false;

        /**
         * @brief Loads @p gfx's frame strip, optional mask and palette band into @p asset.
         *
         * Leaves @p asset untouched and returns false when the graphic names a
         * bitmap that is missing or unreadable, so the caller can skip it.
         */
        bool load_graphic(data::graphic const& gfx, ULONG* pPalette, graphic_asset& asset)
        {
            game_data& gd = g_pEngine->data();
            auto bitmaps  = gd.bitmaps();
            if (gfx.source_bitmap_id >= bitmaps.size()) return false;
            data::bitmap_ref const& ref = bitmaps[gfx.source_bitmap_id];

            ace::bitmap_ptr frames = ace::bitmapCreateFromFd(gd.open_asset(ref.bitmap_hash), 0);
            if (!frames) return false;

            ace::bitmap_ptr mask{ nullptr };
            if (ref.mask_hash != data::NO_ASSET)
            {
                mask = ace::bitmapCreateFromFd(gd.open_asset(ref.mask_hash), 0);
                if (!mask) return false;
            }

            // Overlay this graphic's colors into its palette-slot band. The
            // read is capped so a malformed palette can't spill past index 255.
            uint8_t const base = mtl::to<uint8_t>(BOB_PALETTE_SLOT0_BASE - gfx.palette_slot * 8);
            ace::paletteLoadFromFd(gd.open_asset(ref.palette_hash),
                mtl::force_to<UWORD*>(pPalette + base), mtl::to<UWORD>(PALETTE_SIZE - base));

            asset.frames       = mtl::move(frames);
            asset.mask         = mtl::move(mask);
            asset.frame_count  = gfx.frame_count;
            asset.frame_height = gfx.frame_height;
            return true;
        }

        /**
         * @brief Registers a bob for every hotspot in @p scene that has a state graphic.
         *
         * Builds the scene-local index -> bob mapping alongside, so a hotspot
         * without a graphic costs one lookup rather than a search. Each bob is
         * placed at its hotspot's origin at the graphic's native size: the
         * Amiga cannot scale a bob, so the rectangle and the artwork are
         * expected to agree.
         */
        void load_hotspot_graphics(data::scene const& scene, mtl::span<data::graphic> graphics,
            ULONG* pPalette, UWORD screenOffsetY)
        {
            auto const all       = g_pEngine->data().hotspots();
            uint16_t const first = scene.hotspot_range.start;
            uint16_t const count = scene.hotspot_range.count;
            if (count == 0 || mtl::to<size_t>(first) + count > all.size()) return;

            // Sized to every hotspot so the mapping is indexed directly; the
            // assets themselves are reserved for the worst case so that neither
            // vector moves after bobInit() has taken pointers into them.
            s_hotspotBob.reserve(count);
            s_hotspotAssets.reserve(count);

            for (uint16_t i = 0; i < count; ++i)
            {
                s_hotspotBob.push_back(NO_BOB);

                data::hotspot const& hotspot = all[first + i];
                if (hotspot.graphic_id == data::NO_ID) continue;
                if (hotspot.graphic_id >= graphics.size()) continue;

                data::graphic const& gfx = graphics[hotspot.graphic_id];

                // One frame per state is the contract the editor enforces; a
                // shorter strip cannot express the states, so skip it rather
                // than draw an arbitrary frame.
                if (gfx.frame_count < HOTSPOT_STATE_FRAMES)
                {
                    NE_LOG("scene_bobs: hotspot %hu graphic has %hhu frame(s), needs %hhu",
                        mtl::to<UWORD>(first + i), gfx.frame_count, HOTSPOT_STATE_FRAMES);
                    continue;
                }

                graphic_asset asset{};
                if (!load_graphic(gfx, pPalette, asset)) continue;

                asset.visible = true;
                s_hotspotAssets.push_back(mtl::move(asset));
                s_hotspotBob[i] = mtl::to<uint16_t>(s_hotspotAssets.size() - 1);

                graphic_asset& stored = s_hotspotAssets.back();
                auto const [pFrame, pMask] = frame_at(stored, 0);
                bobInit(&stored.bob, gfx.frame_width, gfx.frame_height, 1, pFrame, pMask,
                    hotspot.bounds.pos.x, mtl::to<UWORD>(hotspot.bounds.pos.y + screenOffsetY));
            }
        }
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
        s_hotspotAssets.clear();
        s_hotspotBob.clear();
        if (s_managerReady)
        {
            bobManagerDestroy();
            s_managerReady = false;
        }
    }

    void scene_bobs_load(data::scene const& scene)
    {
        // Forget the previous scene's bobs so a fresh set can be registered; ACE
        // forbids bobInit() once bobReallocateBuffers() has run for a set. Both
        // kinds are registered below, between this reset and that single call.
        bobManagerReset();
        s_assets.clear();
        s_hotspotAssets.clear();
        s_hotspotBob.clear();

        game_data& gd  = g_pEngine->data();
        auto instances = gd.graphic_instances();
        auto graphics  = gd.graphics();
        uint16_t const first = scene.graphic_instance_range.start;
        uint16_t const count = scene.graphic_instance_range.count;

        // AGA palette entries are ULONG; screenGetPalette() types the buffer
        // as UWORD* for the ACE API, so index it with ULONG stride to land on
        // the right entry.
        ULONG* pPalette = mtl::force_to<ULONG*>(screenGetPalette(g_mainScreen));
        UWORD const screenOffsetY = screenGetVerticalOffset(g_mainScreen);

        // Build the set only for a valid, non-empty range; an empty or
        // out-of-bounds range simply yields no bobs.
        if (count > 0 && mtl::to<size_t>(first) + count <= instances.size())
        {
            // The bob's frame pointer and the manager's pointer to the bob must
            // both stay valid, so the storage must not move after bobInit():
            // reserve the worst case up front.
            s_assets.reserve(count);

            for (uint16_t i = 0; i < count; ++i)
            {
                data::graphic_instance const& inst = instances[first + i];
                if (inst.graphic_id >= graphics.size()) continue;
                data::graphic const& gfx = graphics[inst.graphic_id];

                graphic_asset asset{};
                if (!load_graphic(gfx, pPalette, asset)) continue;

                asset.visible     = (inst.flags & GFXI_FLAG_VISIBLE) != 0;
                asset.frame_delay = gfx.frame_delay;
                s_assets.push_back(mtl::move(asset));

                // frames/mask now live in the asset; resolve frame 0 from that
                // stable storage and register the bob on it.
                graphic_asset& stored = s_assets.back();
                auto const [pFrame, pMask] = frame_at(stored, 0);
                bobInit(&stored.bob, gfx.frame_width, gfx.frame_height, 1, pFrame, pMask,
                    inst.pos.x, mtl::to<UWORD>(inst.pos.y + screenOffsetY));
            }
        }

        load_hotspot_graphics(scene, graphics, pPalette, screenOffsetY);

        // ACE sizes its bob queues and undraw buffers from what was registered,
        // so with nothing registered it allocates zero bytes and builds
        // zero-height bitmaps, which it reports as errors. A scene with no
        // graphics at all simply leaves the manager unsized; scene_bobs_draw()
        // skips it as well.
        if (!s_assets.empty() || !s_hotspotAssets.empty()) bobReallocateBuffers();

        NE_LOG("scene_bobs: %lu bob(s), %lu hotspot graphic(s) loaded",
            mtl::to<ULONG>(s_assets.size()), mtl::to<ULONG>(s_hotspotAssets.size()));
    }

    void scene_bobs_set_hotspot_frame(uint16_t index, hotspot_frame frame)
    {
        if (index >= s_hotspotBob.size()) return;
        uint16_t const slot = s_hotspotBob[index];
        if (slot == NO_BOB) return;

        graphic_asset& asset = s_hotspotAssets[slot];
        auto const wanted    = mtl::to<uint8_t>(frame);
        if (asset.frame_index == wanted) return;

        asset.frame_index = wanted;
        auto const [pFrame, pMask] = frame_at(asset, wanted);
        bobSetFrame(&asset.bob, pFrame, pMask);
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
        if (s_assets.empty() && s_hotspotAssets.empty()) return;

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
        // Hotspot graphics sit above the placed instances: they are interactive
        // affordances, so being hidden behind scenery would misinform the player.
        for (auto& asset : s_hotspotAssets)
        {
            bobPush(&asset.bob);
        }
        bobEnd();
    }
}  // namespace NEONengine
