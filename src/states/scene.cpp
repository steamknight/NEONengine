#include "types.h"

#include "neonengine.h"

#include <ace/managers/blit.h>
#include <ace/managers/bob.h>
#include <ace/managers/copper.h>
#include <ace/managers/log.h>
#include <ace/managers/system.h>

#include <ace++/bitmap.h>
#include <ace++/palette.h>

#include <mtl/utility.h>

#include "core/asset_cache.h"
#include "core/game_data.h"
#include "core/logger.h"
#include "core/music.h"
#include "core/noir_vm.h"
#include "core/mouse_pointer.h"
#include "core/scene_audio.h"
#include "core/scene_bobs.h"
#include "core/scene_hotspots.h"
#include "core/scene_text.h"
#include "core/scene_timeline.h"
#include "core/screen.h"
#include "core/string_table.h"

NE_LOG_DOMAIN(Scene);

namespace NEONengine
{
    namespace
    {
        constexpr u32 MUSIC_KEEP    = 0xFFFFFFFF;
        constexpr u32 MUSIC_SILENCE = 0xFFFFFFFE;

        /// UI colors occupy palette indices 0..31.
        constexpr UWORD UI_PALETTE_COLORS = 32;

        // Backgrounds are authored against all 256 entries rather than the
        // 80..255 scene band, so the whole palette is loaded and the UI band is
        // restored afterwards for scenes that show UI. Revisit if a background
        // ever needs to keep its own colors in 0..31 alongside the UI.
        constexpr UWORD SCENE_PALETTE_COLORS = 256;

        /// A language change left the backdrop's text regions in the old language.
        bool s_backdropStale = false;

        /// The modal on screen now, or kind::none when the scene is free.
        modal_request s_modal{};

        /**
         * @brief Ends the fade modal that a script asked for.
         *
         * The scene owns script-driven fades, so it hears about the end of one
         * here rather than through the VM. A fade the engine starts for itself
         * passes no callback and never reaches this.
         */
        void scene_fade_done()
        {
            s_modal = modal_request{};
        }

        /**
         * @brief Puts the pointer into its WAIT role and makes it visible.
         *
         * Two things have to happen before the loop blocks: the sprite has to be
         * published (spriteProcessChannel only writes the copper back buffer, so
         * without copProcessBlocks the swap never happens and the old cursor
         * stays), and the pointer has to be positioned. Its colors need nothing
         * - the fade steps over the cursor band, so they are always lit.
         */
        void show_wait_pointer()
        {
            mousePointerSwitch(mouse_pointer::WAIT);
            mousePointerUpdate();
            copProcessBlocks();
        }

        /**
         * @brief Paints @p sc's static backdrop into both screen buffers.
         *
         * The backdrop is the background bitmap, the palette it comes with, and
         * the scene's text regions. Text regions live here rather than in the
         * bob set because they never move and the format lets one cover the
         * whole screen, which as a bob would want more undraw space than the
         * screen buffers themselves.
         *
         * Mirroring back to front last leaves the bob manager an intact
         * background to undraw against in either frame, so this must run before
         * any bob is registered. Reads the pak: call under a system-use guard.
         */
        void draw_backdrop(data::scene const& sc)
        {
            auto& gd = g_pEngine->data();

            // The palette about to be loaded lands on top of whatever the
            // timeline's palette cycles have turned, and their record of how far
            // they have turned would then describe colors that are no longer
            // there. Handing the bands back first keeps the two in agreement.
            // This runs for a language repaint as well as a scene load, and it
            // has to: that repaint reloads the very same palette, mid-scene,
            // with cycles still running.
            scene_timeline_restore_palette();

            if (sc.background_bitmap_id != data::NO_ID)
            {
                auto const& ref = gd.bitmaps()[sc.background_bitmap_id];
                ace::paletteLoadFromFd(gd.open_asset(ref.palette_hash),
                    screenGetPalette(g_mainScreen),
                    SCENE_PALETTE_COLORS);

                auto bg = ace::bitmapCreateFromFd(gd.open_asset(ref.bitmap_hash), 0);

                if (bg)
                {
                    // A planar blit only writes the source's own bitplanes, so a
                    // background that doesn't span the whole screen at full depth
                    // leaves the previous scene's pixels (and unwritten high
                    // planes) behind. Clear to index 0 first in that case.
                    bool const covers_screen = sc.offset.x == 0 && sc.offset.y == 0
                        && sc.width >= SCREEN_WIDTH && sc.height >= SCREEN_HEIGHT
                        && bg->Depth >= screenGetBackBuffer(g_mainScreen)->Depth;
                    if (!covers_screen) screenClear(g_mainScreen, 0);

                    screenBlitCopy(g_mainScreen,
                        bg.get(),
                        0,
                        0,
                        to<WORD>(sc.offset.x),
                        to<WORD>(sc.offset.y),
                        to<WORD>(sc.width),
                        to<WORD>(sc.height),
                        MINTERM_COPY);
                }
            }

            // The background claims all 256 entries, which buries the UI
            // colors loaded at boot. A scene that shows UI needs them back,
            // so reload the band over the top; a scene that does not keeps
            // the background's own colors there. Text regions name colors in
            // this band, so it has to be settled before they are drawn.
            bool const showsUi = (sc.flags & data::SCENE_FLAG_SHOW_UI) != 0;
            if (showsUi)
            {
                ace::paletteLoadFromFd(gd.open_asset(gd.ui_state().ui_palette_hash),
                    screenGetPalette(g_mainScreen), UI_PALETTE_COLORS);
            }

            // Only a scene that shows the pointer holds its band out of the
            // fade. A scene without UI is free to author its background across
            // those entries, so there they have to dim like everything else -
            // and the pointer has to go, or it would draw itself in whatever
            // colors the background put in its band.
            screenSetCursorBandExempt(g_mainScreen, showsUi);
            mousePointerSetVisible(showsUi);

            scene_text_draw(sc);
            screenDuplicateBackToFront(g_mainScreen);
        }

        /**
         * @brief Lays the current scene's backdrop down again, in place.
         *
         * A language change rewrites what the text regions say, and those are
         * painted into the backdrop rather than drawn each frame, so the only
         * way to change them is to repaint it. Everything reloaded is what the
         * scene already had - the same background, the same palette values - so
         * nothing fades and nothing else on screen moves.
         *
         * Takes its own system-use guard, and blocks for as long as decoding the
         * background takes, so it declines the work when the scene has no text
         * regions to restate.
         */
        void redraw_backdrop()
        {
            auto const scenes = g_pEngine->data().scenes();
            u16 const id = g_pEngine->current_scene();
            if (id >= scenes.size()) return;

            // Nothing else in the backdrop is localized, so a scene without text
            // regions would decode its background only to draw the same picture.
            if (scenes[id].text_region_range.count == 0) return;

            show_wait_pointer();

            mtl::system_use_guard guard;  // OS on for pak/disk access
            draw_backdrop(scenes[id]);

            // Every bob standing over the old backdrop saved its background from
            // it, and that has just been painted over. Undrawing those saves
            // would stamp the old text back wherever a bob happened to be.
            bobDiscardUndraw();
        }

        /**
         * @brief Apply a script's SET_LANG request, reporting what took effect.
         *
         * engine::set_language keeps the current table when the new one cannot
         * be loaded, so the log names the language actually in force afterwards.
         */
        void apply_pending_language()
        {
            auto const requested = g_pEngine->take_pending_language();
            g_pEngine->set_language(requested);

            // Captions and text regions were both built from the table that just
            // went away. A pending scene is about to rebuild them anyway, so this
            // only pays for a language change that stays in the current scene.
            if (!g_pEngine->has_pending_scene())
            {
                mtl::system_use_guard guard;  // OS on for pak/disk access
                scene_bobs_reload_captions();

                // Repainting the backdrop is far more expensive than a caption,
                // so it is only marked here; sceneProcess picks the moment.
                s_backdropStale = true;
            }

            // The language's own name proves the table really swapped. Records
            // always carry a terminator, so the view goes straight to "%s".
            NE_LOG("language is now '%s' (%s)",
                language_code(g_pEngine->current_language()),
                g_pEngine->strings().language_name().data());
        }

        /**
         * @brief Free Fast RAM in bytes.
         *
         * ACE exposes free Chip and free total but not free Fast, and reaching
         * for AvailMem() directly would mean pulling exec's protos into this
         * file. Any memory that is not Chip is Fast, so the difference is it.
         */
        LONG free_fast_size()
        {
            return to<LONG>(memGetFreeSize()) - to<LONG>(memGetFreeChipSize());
        }

        /**
         * @brief Reports what scene @p scene_id cost in Chip and Fast RAM.
         *
         * Measured as free memory before and after the load rather than by
         * asking each subsystem what it allocated. That catches everything,
         * including the bob manager's undraw buffers, which ACE sizes and
         * allocates inside bobReallocateBuffers() where no tally of ours could
         * see them.
         *
         * "used" is a net figure: the previous scene is torn down during this
         * load, so a scene cheaper than the one before it reports a negative
         * number. Read the free figures for whether a scene fits at all.
         */
        void log_scene_memory(u16 scene_id, LONG chipBefore, LONG fastBefore)
        {
            LONG const chipAfter = to<LONG>(memGetFreeChipSize());
            LONG const fastAfter = free_fast_size();

            NE_LOG("scene %hu memory: chip %ld KB free (used %ld KB), fast %ld KB free (used %ld KB)",
                scene_id,
                chipAfter / 1024,
                (chipBefore - chipAfter) / 1024,
                fastAfter / 1024,
                (fastBefore - fastAfter) / 1024);
        }

        /**
         * @brief Present scene @p scene_id: background, music, and entry script.
         *
         * Serves both the initial boot scene and every GOTO_SCENE transition. Pak
         * access runs under a system_use_guard; music playback and the VM run
         * afterwards with the system released (the game-loop baseline).
         *
         * @return Whether an entry script is now running. When it is, nothing
         *         has brought the palette up yet: the script's own FADE_IN does
         *         that on its first slice.
         */
        bool load_scene(u16 scene_id)
        {
            auto& gd          = g_pEngine->data();
            auto const scenes = gd.scenes();
            if (scene_id >= scenes.size())
            {
                NE_LOG("invalid scene id %hu (%lu scenes)",
                    scene_id,
                    to<ULONG>(scenes.size()));
                return false;
            }

            auto const& sc = scenes[scene_id];
            g_pEngine->set_current_scene(scene_id);

            // GOTO_SCENE ends everything: anything on screen belonged to the old
            // scene, and a request recorded by a script that is about to be
            // discarded has nobody left to act for.
            s_modal = modal_request{};
            g_pEngine->take_pending_modal();

            // Everything this load asks the cache for is stamped as wanted now,
            // which is what stops the load evicting its own assets and what
            // makes the previous scene's leftovers the first to go.
            asset_cache_begin_load();

            LONG const chipBefore = to<LONG>(memGetFreeChipSize());
            LONG const fastBefore = free_fast_size();
            // The load lays the whole backdrop down in the current language, so
            // any repaint a language change was still owed is settled by it.
            s_backdropStale = false;

            bool const silence = sc.music_hash == MUSIC_SILENCE;
            bool music_started = false;
            {
                mtl::system_use_guard guard;  // OS on for pak/disk access

                draw_backdrop(sc);
                scene_bobs_load(sc);
                scene_audio_load(sc);
                scene_hotspots_load(sc);
                scene_timeline_load(sc);

                if (!silence && sc.music_hash != MUSIC_KEEP)
                {
                    // Uncached on purpose: see game_data::open_asset_uncached.
                    auto mod = gd.open_asset_uncached(sc.music_hash);
                    if (mod)
                    {
                        musicLoad(mtl::move(mod));
                        music_started = true;
                    }
                }
            }

            log_scene_memory(scene_id, chipBefore, fastBefore);

            if (silence) musicFree();
            if (music_started) musicPlayCurrent(1);

            g_pEngine->vm().reset_execution();
            if (sc.on_enter_function_id != data::NO_ID
                && g_pEngine->vm().execute(sc.on_enter_function_id) == noir_status::ok)
            {
                return true;
            }

            // No entry script ran, so nothing would bring the palette up (the
            // loaded palette only reaches the display through a fade). Fade in
            // from black with the project's default so a script-less scene is
            // still visible.
            u16 const frames = gd.config().default_fade_frames;
            screenFadeFromBlack(
                g_mainScreen, to<UBYTE>(frames > 255 ? 255 : frames), 0, nullptr);
            return false;
        }

        /**
         * @brief Puts the modal a script asked for last frame on screen.
         *
         * A bare FADE_IN/FADE_OUT (no MOV D0, #frames) arrives as 0 frames, so
         * the project's configured default stands in and scripts can omit the
         * count.
         *
         * A kind with nothing to draw yet is logged and dropped rather than
         * left standing, because a modal nobody can dismiss would hold the VM
         * for the rest of the scene.
         */
        void start_pending_modal()
        {
            auto const request = g_pEngine->take_pending_modal();

            auto fade_frames = [&] {
                u16 const frames = request.a ? request.a
                                             : g_pEngine->data().config().default_fade_frames;
                return to<UBYTE>(frames > 255 ? 255 : frames);
            };

            switch (request.kind)
            {
                case modal_kind::fade_out:
                    s_modal = request;
                    screenFadeToBlack(g_mainScreen, fade_frames(), 0, scene_fade_done);
                    return;

                case modal_kind::fade_in:
                    s_modal = request;
                    screenFadeFromBlack(g_mainScreen, fade_frames(), 0, scene_fade_done);
                    return;

                case modal_kind::say:
                    NE_LOG("SAY string %hu, speaker %hu: no dialogue box yet",
                        request.a,
                        request.b);
                    return;

                case modal_kind::show_dialog:
                    NE_LOG("SHOW_DIALOG %hu: no dialogue box yet", request.a);
                    return;

                case modal_kind::save_game:
                    NE_LOG("SAVE_GAME slot %hu: no save UI yet", request.a);
                    return;

                case modal_kind::none: return;
            }
        }
    }  // namespace

    void sceneCreate(void)
    {
        logBlockBegin("State: Scene");
        // ACE leaves the system 'used' after engine setup; the game loop runs
        // 'unused'. Drop to that baseline once (as splash does); load_scene
        // re-enters 'used' briefly via a system_use_guard for each pak access.
        systemUnuse();
        scene_bobs_create();
        {
            mtl::system_use_guard guard;  // OS on for the pak read.
            mousePointerCreate();
        }
        // The boot load blocks for over a second with the pointer on screen, and
        // draw_backdrop only settles the band once it has the scene in hand.
        screenSetCursorBandExempt(g_mainScreen, true);
        show_wait_pointer();
        // Every later load is followed by the same frame's own VM update; this
        // one runs before the loop starts, so the entry script's FADE_IN needs
        // priming here or the first frame renders at full brightness.
        if (load_scene(g_pEngine->data().config().boot_scene_id))
        {
            g_pEngine->vm().update();
        }
    }

    void sceneProcess(void)
    {
        // ── Requests recorded last frame ─────────────────────────────────────

        // A transition runs from the script's fade-out through the load that
        // follows it, and in the frames between the two nothing is pending.
        // Treating the whole span as one event keeps the pointer on WAIT instead
        // of flicking back to its normal role in that gap.
        bool const transitioning = g_pEngine->has_pending_language()
            || g_pEngine->has_pending_scene() || screenIsFadingOut(g_mainScreen);

        // Cheap and idempotent: the frame's own mousePointerUpdate() and
        // screenProcess() publish it, so the fade needs nothing special.
        if (transitioning) mousePointerSwitch(mouse_pointer::WAIT);

        // Language before scene: a script that switches language and then leaves
        // should present the next scene in the language it just chose.
        if (g_pEngine->has_pending_language())
        {
            // Re-renders every caption under a system-use guard, so it blocks
            // for long enough to need the pointer published first.
            show_wait_pointer();
            apply_pending_language();
        }
        if (g_pEngine->has_pending_scene())
        {
            show_wait_pointer();
            load_scene(g_pEngine->take_pending_scene());
        }
        if (g_pEngine->has_pending_modal()) start_pending_modal();

        // A language change owes the backdrop a repaint, which costs a full
        // background decode. A fade-out is the front half of a scene change -
        // scripts post their GOTO_SCENE from the fade's own callback, a frame or
        // more after the SET_LANG - and that load repaints the backdrop anyway,
        // so waiting for the fade to settle keeps the decode from being done
        // twice and thrown away once.
        if (s_backdropStale && !screenIsFadingOut(g_mainScreen))
        {
            s_backdropStale = false;
            redraw_backdrop();
        }

        // ── Modals ───────────────────────────────────────────────────────────
        // Fades take themselves down through scene_fade_done(). A dismissible
        // box would be closed here, before the timeline and the VM are asked to
        // run, so that both start again in the frame it closes.

        // Every request is answered by now, so a modal is all that can still
        // hold the scene, and it holds it for a strong reason: a fade is the
        // front half of a scene change, so anything a track moved would be
        // thrown away, a keyframe running a script could queue a second scene
        // change, and a hovered hotspot would put its role back over the WAIT
        // pointer.
        bool const holding = s_modal.kind != modal_kind::none;

        // ── Timeline ─────────────────────────────────────────────────────────
        if (!holding) scene_timeline_update();

        // ── UI ───────────────────────────────────────────────────────────────
        if (!holding) scene_hotspots_update();

        // ── Scripts ──────────────────────────────────────────────────────────
        // Last, so a session started this frame by a keyframe or a click runs
        // its first slice now instead of next frame, and so whatever it moves is
        // drawn below. A hotspot can leave the scene without a script; when one
        // has, this scene is already over and nothing here should touch it.
        if (!holding && !g_pEngine->has_pending_scene()) g_pEngine->vm().update();

        mousePointerUpdate();
        scene_bobs_update();
        scene_bobs_draw();
    }

    void sceneDestroy(void)
    {
        s_backdropStale = false;
        s_modal         = modal_request{};
        scene_hotspots_clear();
        scene_timeline_clear();
        mousePointerDestroy();
        scene_bobs_destroy();
        scene_audio_clear();
        musicFree();
        logBlockEnd("State: Scene");
    }

    tState g_stateScene = {
        .cbCreate  = sceneCreate,
        .cbLoop    = sceneProcess,
        .cbDestroy = sceneDestroy,
    };
}  // namespace NEONengine
