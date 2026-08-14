/**
 * @file scene_bobs.h
 * @brief Draws a scene's GFXI graphic instances and hotspot state graphics as ACE BOBs.
 *
 * ACE allows one bob set per manager - reset, then every bobInit(), then a
 * single bobReallocateBuffers() - so this owns the whole scene's set rather than
 * splitting that sequence across callers. Instances animate on
 * graphic.frame_delay; hotspot graphics instead pick their frame from the
 * hotspot's state, which scene_hotspots reports through set_hotspot_frame().
 *
 * ACE does no off-screen clipping and holds bob counts and undraw size in 8-
 * and 16-bit fields, so the editor caps both.
 *
 * Instances draw in GFXI order, not by depth.
 */

#ifndef __SCENE_BOBS_H__INCLUDED__
#define __SCENE_BOBS_H__INCLUDED__

#include "types.h"

#include <stdint.h>

#include <ace/managers/bob.h>

#include <ace++/bitmap.h>

#include <mtl/span.h>
#include <mtl/vector.h>

#include "core/asset_cache.h"
#include "core/game_data.h"
#include "game_data_format.h"

namespace NEONengine
{
    /// Frames a hotspot state graphic must supply, one per hotspot state.
    constexpr u8 HOTSPOT_STATE_FRAMES = 4;

    /// Which frame of a hotspot's state graphic each state draws.
    enum class hotspot_frame : u8
    {
        NORMAL   = 0,
        HOVERED  = 1,
        PRESSED  = 2,
        DISABLED = 3,
    };

    class scene_bobs
    {
        public:  //////////////////////////////////////////////////////////////////////////////////
        /**
         * @brief Creates the ACE bob manager over the screen's double buffer.
         *
         * Built once the screen exists, which is why the scene state builds it
         * rather than the engine.
         */
        explicit scene_bobs(game_data& data, asset_cache& cache);
        scene_bobs(scene_bobs const&)            = delete;
        scene_bobs& operator=(scene_bobs const&) = delete;

        /**
         * @brief Destroys the bob manager and frees all per-scene bob assets.
         */
        ~scene_bobs();

        /**
         * @brief Builds the bob set for @p scene: its GFXI instances and hotspot graphics.
         *
         * Loads each graphic's frame strip and mask into Chip RAM, overlays its
         * colors into its BOB palette-slot band (the caller must place the
         * background palette first), and initializes one bob per instance and per
         * hotspot that carries a state graphic. Hotspot graphics start on the
         * normal frame; scene_hotspots::load() then applies each hotspot's initial
         * state. Reads the pak, so call under a system-use guard.
         */
        void load(data::scene const& scene);

        /**
         * @brief Shows @p frame of the hotspot @p index's state graphic.
         *
         * @param index Scene-local hotspot index, as scene_hotspots numbers them.
         *              Indices without a state graphic are ignored.
         */
        void set_hotspot_frame(u16 index, hotspot_frame frame);

        /**
         * @brief Moves the graphic instance at scene-local @p index to @p pos.
         *
         * @param index Scene-local GFXI index: a global id less the scene's
         *              graphic_instance_range.start. Indices the scene does not
         *              have, or whose graphic failed to load, are ignored.
         * @param pos   Position in scene space, as the GFXI record stores it. The
         *              screen's vertical centering is added here, so callers work
         *              in the same coordinates the data is authored in.
         */
        void set_instance_pos(u16 index, data::point<u16> pos);

        /**
         * @brief Shows or hides the graphic instance at scene-local @p index.
         *
         * A hidden instance is not drawn and its frame animation does not advance.
         */
        void set_instance_visible(u16 index, bool visible);

        /**
         * @brief Where the graphic instance at scene-local @p index currently is.
         *
         * The live position rather than the authored one, so a caller starting an
         * interpolation leads from where the instance actually stands.
         *
         * @param[out] pos Position in scene space; untouched when this returns false.
         * @return Whether the scene has a drawable instance at that index.
         */
        bool instance_pos(u16 index, data::point<u16>& pos);

        /**
         * @brief Advances every visible graphic instance's looping frame animation.
         *
         * Call from the scene loop each frame, before draw(). Static graphics
         * (frame_delay 0 or a single frame) are left untouched. Hotspot graphics
         * are driven by state, not time, so they are not advanced here.
         */
        void update();

        /**
         * @brief Draws every bob into the current back buffer.
         *
         * Call from the scene loop each frame, before the screen is flipped.
         */
        void draw();

        /**
         * @brief Shows the hotspot @p index's hover caption anchored near the pointer.
         *
         * @p anchorX and @p anchorY are the pointer position in display space; the
         * caption is centred on it and pushed inside the screen edges, flipping
         * above the pointer when it would not fit below. Hotspots without a hover
         * string, or whose string is empty, show nothing.
         *
         * @param index Scene-local hotspot index, as scene_hotspots numbers them.
         */
        void show_caption(u16 index, u16 anchorX, u16 anchorY);

        /**
         * @brief Stops drawing the hover caption.
         */
        void hide_caption();

        /**
         * @brief Re-renders every caption from the language now in force.
         *
         * Captions are rendered once per scene from the active string table, so a
         * language change must rebuild them. Reads nothing from disk but allocates
         * through ACE's font helpers, so call under a system-use guard.
         */
        void reload_captions();

        private:  /////////////////////////////////////////////////////////////////////////////////
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
            bool visible     = false;
            u8 frame_count   = 0;  ///< Frames in the strip (1 = static).
            u8 frame_delay   = 0;  ///< Vblanks per frame (0 = static).
            u16 frame_height = 0;  ///< Rows per frame, for the frame address.
            u8 frame_index   = 0;  ///< Current frame.
            u16 tick         = 0;  ///< Vblanks on the current frame (16-bit: delay 255 safe).
        };

        /// One animation frame's blit pointers: the frame plus its optional mask.
        struct frame_ptrs
        {
            UBYTE* frame;
            UBYTE* mask;
        };

        /// One rendered caption: its bob mask and the height that mask fills.
        struct caption
        {
            ace::bitmap_ptr mask{ nullptr };
            u16 height = 0;
        };

        /**
         * @brief The blit addresses for frame @p index of @p asset.
         *
         * Frames are stacked vertically and the mask (when present) shares that
         * layout, so both step by index * frame_height rows. bitmap_ptr::get()
         * is non-const, hence the mutable reference.
         */
        frame_ptrs frame_at(graphic_asset& asset, u8 index);

        /**
         * @brief The instance bob at scene-local @p index, or nullptr.
         */
        graphic_asset* instance_at(u16 index);

        /**
         * @brief Loads @p gfx's frame strip, optional mask and palette band into @p asset.
         *
         * Leaves @p asset untouched and returns false when the graphic names a
         * bitmap that is missing or unreadable, so the caller can skip it.
         */
        bool load_graphic(data::graphic const& gfx, ULONG* pPalette, graphic_asset& asset);

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
            data::point<u16> screenOffset);

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
        bool render_captions(data::scene const& scene, u16 maxHeight);

        /**
         * @brief Registers @p scene's caption bob, when it has any caption at all.
         *
         * A scene with nothing to say costs no bob and no undraw buffer. The ink
         * bitmap never changes, so it is built once and reused by every scene.
         */
        void load_captions(data::scene const& scene);

        game_data& _data;
        asset_cache& _cache;

        mtl::vector<graphic_asset> _assets;  ///< The live bobs and their backing data.

        /// Scene-local GFXI index -> index into _assets, or NO_BOB.
        /// An instance naming a missing graphic, or whose frames fail to load,
        /// registers no bob, so the two are not the same numbering.
        mtl::vector<u16> _instanceBob;

        /// Hotspot state graphics, drawn above the instances.
        mtl::vector<graphic_asset> _hotspotAssets;

        /// Scene-local hotspot index -> index into _hotspotAssets, or NO_BOB.
        /// Most hotspots carry no graphic, so the mapping is kept out of line.
        mtl::vector<u16> _hotspotBob;

        bool _managerReady = false;

        // Hover captions. A bob blits its frame through its mask, so a single
        // bitmap flooded with the caption color serves as the frame for every
        // caption and only the mask differs: showing one swaps the mask pointer
        // and the height, with nothing to render, allocate or blit at hover time.

        /// Every pixel is CAPTION_COLOR; the mask decides which ones land.
        ace::bitmap_ptr _captionInk{ nullptr };

        /// Scene-local hotspot index -> its caption; empty entries render nothing.
        mtl::vector<caption> _captions;

        tBob _captionBob{};
        bool _captionReady   = false;  ///< A caption bob was registered this scene.
        bool _captionVisible = false;  ///< It should be drawn this frame.

        /// The fixed height the caption bob and every mask are built at.
        u16 _captionBoxHeight = 0;
    };
}  // namespace NEONengine

#endif  // __SCENE_BOBS_H__INCLUDED__
