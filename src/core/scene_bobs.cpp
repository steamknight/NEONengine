#include "scene_bobs.h"

#include "neonengine.h"

#include <ace/managers/blit.h>
#include <ace/managers/bob.h>
#include <ace/utils/font.h>

#include <ace++/bitmap.h>
#include <ace++/font.h>
#include <ace++/palette.h>

#include <mtl/span.h>
#include <mtl/utility.h>
#include <mtl/vector.h>

#include "core/logger.h"
#include "game_data.h"
#include "screen.h"
#include "string_table.h"
#include "text_render.h"

NE_LOG_DOMAIN(Bobs);

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
            bool visible          = false;
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

        constexpr uint16_t NO_BOB = 0xFFFF;

        /// Scene-local GFXI index -> index into s_assets, or NO_BOB.
        /// An instance naming a missing graphic, or whose frames fail to load,
        /// registers no bob, so the two are not the same numbering.
        mtl::vector<uint16_t> s_instanceBob;

        /// Hotspot state graphics, drawn above the instances.
        mtl::vector<graphic_asset> s_hotspotAssets;

        /// Scene-local hotspot index -> index into s_hotspotAssets, or NO_BOB.
        /// Most hotspots carry no graphic, so the mapping is kept out of line.
        mtl::vector<uint16_t> s_hotspotBob;

        /**
         * @brief The instance bob at scene-local @p index, or nullptr.
         */
        graphic_asset* instance_at(uint16_t index)
        {
            if (index >= s_instanceBob.size()) return nullptr;
            uint16_t const slot = s_instanceBob[index];
            if (slot == NO_BOB) return nullptr;
            return &s_assets[slot];
        }

        bool s_managerReady = false;

        // Hover captions. A bob blits its frame through its mask, so a single
        // bitmap flooded with the caption color serves as the frame for every
        // caption and only the mask differs: showing one swaps the mask pointer
        // and the height, with nothing to render, allocate or blit at hover time.
        constexpr uint16_t CAPTION_MAX_WIDTH = SCREEN_WIDTH / 2;
        constexpr uint8_t CAPTION_MAX_LINES  = 2;
        constexpr uint8_t CAPTION_COLOR      = 1;  ///< UI palette index; authorable later.
        constexpr uint16_t CAPTION_AIR       = 4;  ///< Gap when the caption sits above the pointer.
        constexpr uint16_t CAPTION_GAP       = 12 + CAPTION_AIR;  ///< Below: clears the 16px cursor.
        constexpr UBYTE CAPTION_MASK_COLOR   = 0xFF;  ///< Sets every plane, making a bob mask.

        /// Every pixel is CAPTION_COLOR; the mask decides which ones land.
        ace::bitmap_ptr s_captionInk{ nullptr };

        /// One rendered caption: its bob mask and the height that mask fills.
        struct caption
        {
            ace::bitmap_ptr mask{ nullptr };
            uint16_t height = 0;
        };

        /// Scene-local hotspot index -> its caption; empty entries render nothing.
        mtl::vector<caption> s_captions;

        tBob s_captionBob{};
        bool s_captionReady   = false;  ///< A caption bob was registered this scene.
        bool s_captionVisible = false;  ///< It should be drawn this frame.

        /// The fixed height the caption bob and every mask are built at.
        uint16_t s_captionBoxHeight = 0;

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
                mtl::force_to<UWORD*>(pPalette + base),
                mtl::to<UWORD>(PALETTE_SIZE - base));

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
        void load_hotspot_graphics(data::scene const& scene,
            mtl::span<data::graphic> graphics,
            ULONG* pPalette,
            data::point<uint16_t> screenOffset)
        {
            auto const hotspots = scene.hotspot_range.of(g_pEngine->data().hotspots());
            if (hotspots.is_empty()) return;

            // Sized to every hotspot so the mapping is indexed directly; the
            // assets themselves are reserved for the worst case so that neither
            // vector moves after bobInit() has taken pointers into them.
            s_hotspotBob.reserve(hotspots.size());
            s_hotspotAssets.reserve(hotspots.size());

            for (uint16_t i = 0; i < hotspots.size(); ++i)
            {
                s_hotspotBob.push_back(NO_BOB);

                data::hotspot const& hotspot = hotspots[i];
                if (hotspot.graphic_id == data::NO_ID) continue;
                if (hotspot.graphic_id >= graphics.size()) continue;

                data::graphic const& gfx = graphics[hotspot.graphic_id];

                // One frame per state is the contract the editor enforces; a
                // shorter strip cannot express the states, so skip it rather
                // than draw an arbitrary frame.
                if (gfx.frame_count < HOTSPOT_STATE_FRAMES)
                {
                    NE_LOG("hotspot %hu graphic has %hhu frame(s), needs %hhu",
                        mtl::to<UWORD>(scene.hotspot_range.start + i),
                        gfx.frame_count,
                        HOTSPOT_STATE_FRAMES);
                    continue;
                }

                graphic_asset asset{};
                if (!load_graphic(gfx, pPalette, asset)) continue;

                asset.visible = true;
                s_hotspotAssets.push_back(mtl::move(asset));
                s_hotspotBob[i] = mtl::to<uint16_t>(s_hotspotAssets.size() - 1);

                graphic_asset& stored      = s_hotspotAssets.back();
                auto const [pFrame, pMask] = frame_at(stored, 0);
                auto const at              = hotspot.bounds.pos + screenOffset;
                bobInit(&stored.bob,
                    gfx.frame_width,
                    gfx.frame_height,
                    1,
                    pFrame,
                    pMask,
                    at.x,
                    at.y);
            }
        }

        /**
         * @brief The tallest caption the font can produce.
         *
         * create_text rounds its bitmap up to a multiple of 16 rows, so the bob
         * must be registered at that size for ACE to size its undraw buffer for
         * the worst case.
         */
        uint16_t caption_max_height()
        {
            auto const* pFont = g_pEngine->default_font();
            if (!pFont) return 0;
            return mtl::round_up<16>(mtl::to<uint16_t>(pFont->uwHeight * CAPTION_MAX_LINES));
        }

        /**
         * @brief Renders @p scene's hover captions from the language in force.
         *
         * Every hotspot gets a slot so the caption can be found by index; the
         * ones with no hover string simply leave theirs empty. Returns whether
         * any caption was produced. Allocates through ACE's font helpers, which
         * call systemUse(), so the caller must hold a system-use guard.
         *
         * @param maxHeight The tallest the caption bob was registered for. The
         *                  text renderer wraps to its own, larger line limit, so
         *                  a long string is clamped to this rather than allowed
         *                  to drive the bob past its undraw buffer.
         */
        bool render_captions(data::scene const& scene, uint16_t maxHeight)
        {
            s_captions.clear();

            auto const hotspots = scene.hotspot_range.of(g_pEngine->data().hotspots());
            if (hotspots.is_empty()) return false;

            // Reserved up front: the bob holds a pointer into a caption's mask,
            // so the storage must not move once a caption is shown.
            s_captions.reserve(hotspots.size());

            bool any          = false;
            UBYTE const depth = screenGetBackBuffer(g_mainScreen)->Depth;
            for (uint16_t i = 0; i < hotspots.size(); ++i)
            {
                s_captions.push_back(caption{});

                data::hotspot const& hotspot = hotspots[i];
                if (hotspot.hover_string_id == data::NO_ID) continue;

                auto const text = g_pEngine->strings().get_string(hotspot.hover_string_id);
                if (text.is_empty()) continue;

                auto glyphs = g_pEngine->default_text_renderer()->create_text(
                    text, CAPTION_MAX_WIDTH, text_justify::CENTER);
                if (!glyphs || glyphs->uwActualHeight == 0) continue;

                // create_text wraps to text_renderer's own line limit, which is
                // far higher than CAPTION_MAX_LINES. Clamping what gets drawn
                // keeps the blit inside a mask sized for the box.
                if (glyphs->uwActualHeight > maxHeight)
                {
                    NE_LOG("caption %hu wants %hu rows, clamped to %hu",
                        i,
                        glyphs->uwActualHeight,
                        maxHeight);
                    glyphs->uwActualHeight = maxHeight;
                }

                // An ACE bob mask is read with the frame's own stride, so it
                // must carry the glyph bits in every bitplane rather than the
                // one the renderer produces. Drawing the text in a color whose
                // every bit is set expands it into exactly that.
                //
                // Every mask is the full box height so the bob never changes
                // size: ACE saves and restores the undraw background through one
                // sequential cursor, sizing each blit from the bob's _uwBlitSize
                // at the time. Resizing between the save and the undraw two
                // frames later would restore the wrong byte count and either
                // smear stale background or strand the old caption on screen.
                // The unused rows are simply zero, and a zero mask draws nothing.
                auto mask = ace::bitmapCreate(
                    CAPTION_MAX_WIDTH, maxHeight, depth, BMF_INTERLEAVED | BMF_CLEAR);
                if (!mask) continue;
                fontDrawTextBitMap(mask.get(), glyphs.get(), 0, 0, CAPTION_MASK_COLOR, FONT_COOKIE);

                caption& slot = s_captions.back();
                // Kept for placement only; the bob is always the full box.
                slot.height = glyphs->uwActualHeight;
                slot.mask   = mtl::move(mask);
                any         = true;
            }

            return any;
        }

        /**
         * @brief Registers @p scene's caption bob, when it has any caption at all.
         *
         * A scene with nothing to say costs no bob and no undraw buffer. The ink
         * bitmap never changes, so it is built once and reused by every scene.
         */
        void load_captions(data::scene const& scene)
        {
            s_captionReady   = false;
            s_captionVisible = false;

            uint16_t const maxHeight = caption_max_height();
            if (maxHeight == 0) return;
            if (!render_captions(scene, maxHeight)) return;

            if (!s_captionInk)
            {
                s_captionInk = ace::bitmapCreate(CAPTION_MAX_WIDTH,
                    maxHeight,
                    screenGetBackBuffer(g_mainScreen)->Depth,
                    BMF_INTERLEAVED);
                if (!s_captionInk) return;
                blitRect(s_captionInk.get(), 0, 0, CAPTION_MAX_WIDTH, maxHeight, CAPTION_COLOR);
            }

            // One fixed size for the bob's whole life, so the undraw background
            // saved on one frame is always the same byte count restored on the
            // next. The mask is supplied when a caption is actually shown.
            bobInit(&s_captionBob,
                CAPTION_MAX_WIDTH,
                maxHeight,
                1,
                s_captionInk->Planes[0],
                nullptr,
                0,
                0);
            s_captionBoxHeight = maxHeight;
            s_captionReady     = true;
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
        s_instanceBob.clear();
        s_hotspotAssets.clear();
        s_hotspotBob.clear();
        s_captions.clear();
        s_captionInk     = nullptr;
        s_captionReady   = false;
        s_captionVisible = false;
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
        s_instanceBob.clear();
        s_hotspotAssets.clear();
        s_hotspotBob.clear();
        s_captions.clear();

        game_data& gd  = g_pEngine->data();
        auto instances = scene.graphic_instance_range.of(gd.graphic_instances());
        auto graphics  = gd.graphics();

        // AGA palette entries are ULONG; screenGetPalette() types the buffer
        // as UWORD* for the ACE API, so index it with ULONG stride to land on
        // the right entry.
        ULONG* pPalette           = mtl::force_to<ULONG*>(screenGetPalette(g_mainScreen));
        auto const screenOffset = screenGetOffset(g_mainScreen);

        // An empty or out-of-bounds range simply yields no bobs.
        if (!instances.is_empty())
        {
            // The bob's frame pointer and the manager's pointer to the bob must
            // both stay valid, so the storage must not move after bobInit():
            // reserve the worst case up front.
            s_assets.reserve(instances.size());
            s_instanceBob.reserve(instances.size());

            for (uint16_t i = 0; i < instances.size(); ++i)
            {
                // Every instance gets an entry, so a caller can index by the
                // scene-local id whether or not this one ended up drawable.
                s_instanceBob.push_back(NO_BOB);

                data::graphic_instance const& inst = instances[i];
                if (inst.graphic_id >= graphics.size()) continue;
                data::graphic const& gfx = graphics[inst.graphic_id];

                graphic_asset asset{};
                if (!load_graphic(gfx, pPalette, asset)) continue;

                asset.visible     = (inst.flags & GFXI_FLAG_VISIBLE) != 0;
                asset.frame_delay = gfx.frame_delay;
                s_assets.push_back(mtl::move(asset));
                s_instanceBob[i] = mtl::to<uint16_t>(s_assets.size() - 1);

                // frames/mask now live in the asset; resolve frame 0 from that
                // stable storage and register the bob on it.
                graphic_asset& stored      = s_assets.back();
                auto const [pFrame, pMask] = frame_at(stored, 0);
                auto const at              = inst.pos + screenOffset;
                bobInit(&stored.bob,
                    gfx.frame_width,
                    gfx.frame_height,
                    1,
                    pFrame,
                    pMask,
                    at.x,
                    at.y);
            }
        }

        load_hotspot_graphics(scene, graphics, pPalette, screenOffset);
        load_captions(scene);

        // ACE sizes its bob queues and undraw buffers from what was registered,
        // so with nothing registered it allocates zero bytes and builds
        // zero-height bitmaps, which it reports as errors. A scene with no
        // graphics at all simply leaves the manager unsized; scene_bobs_draw()
        // skips it as well.
        if (!s_assets.empty() || !s_hotspotAssets.empty() || s_captionReady)
        {
            bobReallocateBuffers();
        }

        NE_LOG("%lu bob(s), %lu hotspot graphic(s), caption %s",
            mtl::to<ULONG>(s_assets.size()),
            mtl::to<ULONG>(s_hotspotAssets.size()),
            s_captionReady ? "ready" : "none");
    }

    void scene_bobs_set_hotspot_frame(uint16_t index, hotspot_frame frame)
    {
        if (index >= s_hotspotBob.size()) return;
        uint16_t const slot = s_hotspotBob[index];
        if (slot == NO_BOB) return;

        graphic_asset& asset = s_hotspotAssets[slot];
        auto const wanted    = mtl::to<uint8_t>(frame);
        if (asset.frame_index == wanted) return;

        asset.frame_index          = wanted;
        auto const [pFrame, pMask] = frame_at(asset, wanted);
        bobSetFrame(&asset.bob, pFrame, pMask);
    }

    void scene_bobs_set_instance_pos(uint16_t index, data::point<uint16_t> pos)
    {
        graphic_asset* asset = instance_at(index);
        if (!asset) return;

        // ACE documents sPos as the one field a caller may write; the undraw
        // record is taken when the bob is pushed, so a moved bob still erases
        // itself from where it was last drawn in each buffer.
        asset->bob.sPos = pos + screenGetOffset(g_mainScreen);
    }

    void scene_bobs_set_instance_visible(uint16_t index, bool visible)
    {
        graphic_asset* asset = instance_at(index);
        if (!asset) return;
        asset->visible = visible;
    }

    bool scene_bobs_instance_pos(uint16_t index, data::point<uint16_t>& pos)
    {
        graphic_asset const* asset = instance_at(index);
        if (!asset) return false;

        pos = data::point<uint16_t>{ asset->bob.sPos.uwY, asset->bob.sPos.uwX }
            - screenGetOffset(g_mainScreen);
        return true;
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
        if (s_assets.empty() && s_hotspotAssets.empty() && !s_captionReady) return;

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
        // The caption names what the pointer is over, so nothing may cover it.
        if (s_captionVisible) bobPush(&s_captionBob);
        bobEnd();
    }

    void scene_bobs_show_caption(uint16_t index, uint16_t anchorX, uint16_t anchorY)
    {
        s_captionVisible = false;
        if (!s_captionReady || index >= s_captions.size()) return;

        caption const& text = s_captions[index];
        if (!text.mask || text.height == 0) return;

        // Centre the box on the pointer, then push it back inside the screen.
        WORD x = mtl::to<WORD>(anchorX) - CAPTION_MAX_WIDTH / 2;
        x      = MAX(mtl::to<WORD>(0), MIN(x, mtl::to<WORD>(SCREEN_WIDTH - CAPTION_MAX_WIDTH)));

        // Bobs are placed in display space, which the screen shifts down in PAL.
        // The box is always the full height even when the text is shorter, so it
        // is the box that has to stay on screen.
        WORD const top    = mtl::to<WORD>(screenGetOffset(g_mainScreen).y);
        WORD const bottom = mtl::to<WORD>(top + SCREEN_HEIGHT - s_captionBoxHeight);

        // Below the pointer by preference, above it when that would run off the
        // bottom, so the caption stays with the pointer instead of being pinned
        // to the edge by the clamp. Below must clear the cursor, which hangs
        // down from the pointer position; above only needs a little air. Above
        // uses the text height so short captions sit close to the pointer, with
        // the box's empty rows falling harmlessly past it.
        WORD y = mtl::to<WORD>(anchorY) + CAPTION_GAP;
        if (y > bottom) y = mtl::to<WORD>(anchorY) - CAPTION_AIR - mtl::to<WORD>(text.height);
        y = MAX(top, MIN(y, bottom));

        bobSetFrame(&s_captionBob, s_captionInk->Planes[0], text.mask->Planes[0]);
        s_captionBob.sPos.uwX = mtl::to<UWORD>(x);
        s_captionBob.sPos.uwY = mtl::to<UWORD>(y);
        s_captionVisible      = true;
    }

    void scene_bobs_hide_caption()
    {
        s_captionVisible = false;
    }

    void scene_bobs_reload_captions()
    {
        if (!s_captionReady) return;

        // The bob points into the old masks, so stop drawing it until the new
        // ones are in place and a hover re-supplies the pointer.
        s_captionVisible = false;

        auto const scene_id = g_pEngine->current_scene();
        auto const scenes   = g_pEngine->data().scenes();
        if (scene_id >= scenes.size()) return;

        render_captions(scenes[scene_id], caption_max_height());
    }
}  // namespace NEONengine
