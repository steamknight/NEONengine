#include "scene_bobs.h"

#include "types.h"

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
        constexpr u8 BOB_PALETTE_SLOT0_BASE = 72;
        constexpr u16 PALETTE_SIZE          = 256;
        constexpr u8 GFXI_FLAG_VISIBLE      = 1;  // graphic_instance::flags bit 0

        constexpr u16 NO_BOB = 0xFFFF;

        constexpr u16 CAPTION_MAX_WIDTH = SCREEN_WIDTH / 2;
        constexpr u8 CAPTION_MAX_LINES  = 2;
        constexpr u8 CAPTION_COLOR      = 1;  ///< UI palette index; authorable later.
        constexpr u16 CAPTION_AIR       = 4;  ///< Gap when the caption sits above the pointer.
        constexpr u16 CAPTION_GAP       = 12 + CAPTION_AIR;  ///< Below: clears the 16px cursor.
        constexpr UBYTE CAPTION_MASK_COLOR   = 0xFF;  ///< Sets every plane, making a bob mask.

        /**
         * @brief The tallest caption the font can produce.
         *
         * create_text rounds its bitmap up to a multiple of 16 rows, so the bob
         * must be registered at that size for ACE to size its undraw buffer for
         * the worst case.
         */
        u16 caption_max_height()
        {
            auto const* pFont = g_pEngine->default_font();
            if (!pFont) return 0;
            return mtl::round_up<16>(to<u16>(pFont->uwHeight * CAPTION_MAX_LINES));
        }
    }  // namespace

    scene_bobs::scene_bobs(game_data& data, asset_cache& cache) : _data(data), _cache(cache)
    {
        auto* pFront = screenGetFrontBuffer(g_mainScreen);
        auto* pBack  = screenGetBackBuffer(g_mainScreen);
        bobManagerCreate(pFront, pBack, pBack->Rows);
        _managerReady = true;
    }

    scene_bobs::~scene_bobs()
    {
        _assets.clear();
        _instanceBob.clear();
        _hotspotAssets.clear();
        _hotspotBob.clear();
        _captions.clear();
        _captionInk     = nullptr;
        _captionReady   = false;
        _captionVisible = false;
        if (_managerReady)
        {
            bobManagerDestroy();
            _managerReady = false;
        }
    }

    scene_bobs::frame_ptrs scene_bobs::frame_at(graphic_asset& asset, u8 index)
    {
        UWORD const rowOffset = to<UWORD>(index * asset.frame_height);
        return {
            bobCalcFrameAddress(asset.frames.get(), rowOffset),
            asset.mask ? bobCalcFrameAddress(asset.mask.get(), rowOffset) : nullptr,
        };
    }

    scene_bobs::graphic_asset* scene_bobs::instance_at(u16 index)
    {
        if (index >= _instanceBob.size()) return nullptr;
        u16 const slot = _instanceBob[index];
        if (slot == NO_BOB) return nullptr;
        return &_assets[slot];
    }

    bool scene_bobs::load_graphic(data::graphic const& gfx, ULONG* pPalette, graphic_asset& asset)
    {
        auto bitmaps = _data.bitmaps();
        if (gfx.source_bitmap_id >= bitmaps.size()) return false;
        data::bitmap_ref const& ref = bitmaps[gfx.source_bitmap_id];

        ace::bitmap_ptr frames = ace::bitmapCreateFromFd(_cache.open(ref.bitmap_hash), 0);
        if (!frames) return false;

        ace::bitmap_ptr mask{ nullptr };
        if (ref.mask_hash != data::NO_ASSET)
        {
            mask = ace::bitmapCreateFromFd(_cache.open(ref.mask_hash), 0);
            if (!mask) return false;
        }

        // Overlay this graphic's colors into its palette-slot band. The
        // read is capped so a malformed palette can't spill past index 255.
        u8 const base = to<u8>(BOB_PALETTE_SLOT0_BASE - gfx.palette_slot * 8);
        ace::paletteLoadFromFd(_cache.open(ref.palette_hash),
            force_to<UWORD*>(pPalette + base),
            to<UWORD>(PALETTE_SIZE - base));

        asset.frames       = mtl::move(frames);
        asset.mask         = mtl::move(mask);
        asset.frame_count  = gfx.frame_count;
        asset.frame_height = gfx.frame_height;
        return true;
    }

    void scene_bobs::load_hotspot_graphics(data::scene const& scene,
        mtl::span<data::graphic> graphics,
        ULONG* pPalette,
        data::point<u16> screenOffset)
    {
        auto const hotspots = scene.hotspot_range.of(_data.hotspots());
        if (hotspots.is_empty()) return;

        // Sized to every hotspot so the mapping is indexed directly; the
        // assets themselves are reserved for the worst case so that neither
        // vector moves after bobInit() has taken pointers into them.
        _hotspotBob.reserve(hotspots.size());
        _hotspotAssets.reserve(hotspots.size());

        for (u16 i = 0; i < hotspots.size(); ++i)
        {
            _hotspotBob.push_back(NO_BOB);

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
                    to<UWORD>(scene.hotspot_range.start + i),
                    gfx.frame_count,
                    HOTSPOT_STATE_FRAMES);
                continue;
            }

            graphic_asset asset{};
            if (!load_graphic(gfx, pPalette, asset)) continue;

            asset.visible = true;
            _hotspotAssets.push_back(mtl::move(asset));
            _hotspotBob[i] = to<u16>(_hotspotAssets.size() - 1);

            graphic_asset& stored      = _hotspotAssets.back();
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

    bool scene_bobs::render_captions(data::scene const& scene, u16 maxHeight)
    {
        _captions.clear();

        auto const hotspots = scene.hotspot_range.of(_data.hotspots());
        if (hotspots.is_empty()) return false;

        // Reserved up front: the bob holds a pointer into a caption's mask,
        // so the storage must not move once a caption is shown.
        _captions.reserve(hotspots.size());

        bool any          = false;
        UBYTE const depth = screenGetBackBuffer(g_mainScreen)->Depth;
        for (u16 i = 0; i < hotspots.size(); ++i)
        {
            _captions.push_back(caption{});

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

            caption& slot = _captions.back();
            // Kept for placement only; the bob is always the full box.
            slot.height = glyphs->uwActualHeight;
            slot.mask   = mtl::move(mask);
            any         = true;
        }

        return any;
    }

    void scene_bobs::load_captions(data::scene const& scene)
    {
        _captionReady   = false;
        _captionVisible = false;

        u16 const maxHeight = caption_max_height();
        if (maxHeight == 0) return;
        if (!render_captions(scene, maxHeight)) return;

        if (!_captionInk)
        {
            _captionInk = ace::bitmapCreate(CAPTION_MAX_WIDTH,
                maxHeight,
                screenGetBackBuffer(g_mainScreen)->Depth,
                BMF_INTERLEAVED);
            if (!_captionInk) return;
            blitRect(_captionInk.get(), 0, 0, CAPTION_MAX_WIDTH, maxHeight, CAPTION_COLOR);
        }

        // One fixed size for the bob's whole life, so the undraw background
        // saved on one frame is always the same byte count restored on the
        // next. The mask is supplied when a caption is actually shown.
        bobInit(&_captionBob,
            CAPTION_MAX_WIDTH,
            maxHeight,
            1,
            _captionInk->Planes[0],
            nullptr,
            0,
            0);
        _captionBoxHeight = maxHeight;
        _captionReady     = true;
    }

    void scene_bobs::load(data::scene const& scene)
    {
        // Forget the previous scene's bobs so a fresh set can be registered; ACE
        // forbids bobInit() once bobReallocateBuffers() has run for a set. Both
        // kinds are registered below, between this reset and that single call.
        bobManagerReset();
        _assets.clear();
        _instanceBob.clear();
        _hotspotAssets.clear();
        _hotspotBob.clear();
        _captions.clear();

        auto instances = scene.graphic_instance_range.of(_data.graphic_instances());
        auto graphics  = _data.graphics();

        // AGA palette entries are ULONG; screenGetPalette() types the buffer
        // as UWORD* for the ACE API, so index it with ULONG stride to land on
        // the right entry.
        ULONG* pPalette           = force_to<ULONG*>(screenGetPalette(g_mainScreen));
        auto const screenOffset = screenGetOffset(g_mainScreen);

        // An empty or out-of-bounds range simply yields no bobs.
        if (!instances.is_empty())
        {
            // The bob's frame pointer and the manager's pointer to the bob must
            // both stay valid, so the storage must not move after bobInit():
            // reserve the worst case up front.
            _assets.reserve(instances.size());
            _instanceBob.reserve(instances.size());

            for (u16 i = 0; i < instances.size(); ++i)
            {
                // Every instance gets an entry, so a caller can index by the
                // scene-local id whether or not this one ended up drawable.
                _instanceBob.push_back(NO_BOB);

                data::graphic_instance const& inst = instances[i];
                if (inst.graphic_id >= graphics.size()) continue;
                data::graphic const& gfx = graphics[inst.graphic_id];

                graphic_asset asset{};
                if (!load_graphic(gfx, pPalette, asset)) continue;

                asset.visible     = (inst.flags & GFXI_FLAG_VISIBLE) != 0;
                asset.frame_delay = gfx.frame_delay;
                _assets.push_back(mtl::move(asset));
                _instanceBob[i] = to<u16>(_assets.size() - 1);

                // frames/mask now live in the asset; resolve frame 0 from that
                // stable storage and register the bob on it.
                graphic_asset& stored      = _assets.back();
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
        // graphics at all simply leaves the manager unsized; draw() skips it as
        // well.
        if (!_assets.empty() || !_hotspotAssets.empty() || _captionReady)
        {
            bobReallocateBuffers();
        }

        NE_LOG("%lu bob(s), %lu hotspot graphic(s), caption %s",
            to<ULONG>(_assets.size()),
            to<ULONG>(_hotspotAssets.size()),
            _captionReady ? "ready" : "none");
    }

    void scene_bobs::set_hotspot_frame(u16 index, hotspot_frame frame)
    {
        if (index >= _hotspotBob.size()) return;
        u16 const slot = _hotspotBob[index];
        if (slot == NO_BOB) return;

        graphic_asset& asset = _hotspotAssets[slot];
        auto const wanted    = to<u8>(frame);
        if (asset.frame_index == wanted) return;

        asset.frame_index          = wanted;
        auto const [pFrame, pMask] = frame_at(asset, wanted);
        bobSetFrame(&asset.bob, pFrame, pMask);
    }

    void scene_bobs::set_instance_pos(u16 index, data::point<u16> pos)
    {
        graphic_asset* asset = instance_at(index);
        if (!asset) return;

        // ACE documents sPos as the one field a caller may write; the undraw
        // record is taken when the bob is pushed, so a moved bob still erases
        // itself from where it was last drawn in each buffer.
        asset->bob.sPos = pos + screenGetOffset(g_mainScreen);
    }

    void scene_bobs::set_instance_visible(u16 index, bool visible)
    {
        graphic_asset* asset = instance_at(index);
        if (!asset) return;
        asset->visible = visible;
    }

    bool scene_bobs::instance_pos(u16 index, data::point<u16>& pos)
    {
        graphic_asset const* asset = instance_at(index);
        if (!asset) return false;

        pos = data::point<u16>{ asset->bob.sPos.uwY, asset->bob.sPos.uwX }
            - screenGetOffset(g_mainScreen);
        return true;
    }

    void scene_bobs::update()
    {
        // Advance each visible animated graphic's frame. frame_delay is in vblanks
        // (0 or a single frame means static); animations loop forever.
        for (auto& asset : _assets)
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

    void scene_bobs::draw()
    {
        if (_assets.empty() && _hotspotAssets.empty() && !_captionReady) return;

        // The main loop flips the double buffer every frame, but the bob manager
        // only advances while this state runs. An overlay state (e.g. the F1 debug
        // view) suspends us for a stretch, so on resume the back buffer can be out
        // of phase with the manager. Re-sync before drawing; it's a no-op when
        // already aligned. ACE only auto-corrects this under ACE_DEBUG, so without
        // it a release build would keep drawing bobs into the wrong buffer.
        auto* pBack = screenGetBackBuffer(g_mainScreen);
        bobSetCurrentBuffer(pBack);
        bobBegin(pBack);
        for (auto& asset : _assets)
        {
            if (asset.visible) bobPush(&asset.bob);
        }
        // Hotspot graphics sit above the placed instances: they are interactive
        // affordances, so being hidden behind scenery would misinform the player.
        for (auto& asset : _hotspotAssets)
        {
            bobPush(&asset.bob);
        }
        // The caption names what the pointer is over, so nothing may cover it.
        if (_captionVisible) bobPush(&_captionBob);
        bobEnd();
    }

    void scene_bobs::show_caption(u16 index, u16 anchorX, u16 anchorY)
    {
        _captionVisible = false;
        if (!_captionReady || index >= _captions.size()) return;

        caption const& text = _captions[index];
        if (!text.mask || text.height == 0) return;

        // Centre the box on the pointer, then push it back inside the screen.
        WORD x = to<WORD>(anchorX) - CAPTION_MAX_WIDTH / 2;
        x      = MAX(to<WORD>(0), MIN(x, to<WORD>(SCREEN_WIDTH - CAPTION_MAX_WIDTH)));

        // Bobs are placed in display space, which the screen shifts down in PAL.
        // The box is always the full height even when the text is shorter, so it
        // is the box that has to stay on screen.
        WORD const top    = to<WORD>(screenGetOffset(g_mainScreen).y);
        WORD const bottom = to<WORD>(top + SCREEN_HEIGHT - _captionBoxHeight);

        // Below the pointer by preference, above it when that would run off the
        // bottom, so the caption stays with the pointer instead of being pinned
        // to the edge by the clamp. Below must clear the cursor, which hangs
        // down from the pointer position; above only needs a little air. Above
        // uses the text height so short captions sit close to the pointer, with
        // the box's empty rows falling harmlessly past it.
        WORD y = to<WORD>(anchorY) + CAPTION_GAP;
        if (y > bottom) y = to<WORD>(anchorY) - CAPTION_AIR - to<WORD>(text.height);
        y = MAX(top, MIN(y, bottom));

        bobSetFrame(&_captionBob, _captionInk->Planes[0], text.mask->Planes[0]);
        _captionBob.sPos.uwX = to<UWORD>(x);
        _captionBob.sPos.uwY = to<UWORD>(y);
        _captionVisible      = true;
    }

    void scene_bobs::hide_caption()
    {
        _captionVisible = false;
    }

    void scene_bobs::reload_captions()
    {
        if (!_captionReady) return;

        // The bob points into the old masks, so stop drawing it until the new
        // ones are in place and a hover re-supplies the pointer.
        _captionVisible = false;

        auto const scene_id = g_pEngine->current_scene();
        auto const scenes   = _data.scenes();
        if (scene_id >= scenes.size()) return;

        render_captions(scenes[scene_id], caption_max_height());
    }
}  // namespace NEONengine
