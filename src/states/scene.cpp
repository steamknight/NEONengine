#include "types.h"

#include "neonengine.h"

#include <ace/managers/blit.h>
#include <ace/managers/bob.h>
#include <ace/managers/copper.h>
#include <ace/managers/game.h>
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
#include "core/scene_runtime.h"
#include "core/scene_text.h"
#include "core/screen.h"
#include "core/state_host.h"
#include "core/string_table.h"
#include "utils/assert.h"

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

        /**
         * @brief The scene on screen: what it is made of, and what it is doing.
         *
         * Holds the runtime in a buffer of its own rather than allocating one,
         * the way state_host holds a state. The runtime is 260 bytes and the
         * scene it presents changes many times over a game, so the linker
         * reserves those bytes once instead of every load asking for them - and
         * a load can no longer fail for want of somewhere to put it.
         */
        class scene_state
        {
            public:  //////////////////////////////////////////////////////////////////////////////
            bool create();
            bool loop();
            void resume();
            ~scene_state();

            /**
             * @brief The runtime of the scene now on screen.
             *
             * Asserts what a caller has to have earned: the scene state is on
             * the stack, and it has a scene loaded. current_scene_runtime() is
             * how anything outside this file asks.
             */
            static scene_runtime& current_runtime();

            private:  /////////////////////////////////////////////////////////////////////////////
            /** @brief What this scene is made of. Live only once a scene has loaded. */
            scene_runtime& runtime();

            /**
             * @brief Builds the runtime for @p sc.
             *
             * Constructs only. A scene already loaded has to be dropped first,
             * and the caller does that, because what happens between the two -
             * the old scene's Chip RAM going back before the new one asks for
             * any - is the reason the order matters.
             */
            void construct_runtime(data::scene const& sc);

            /**
             * @brief Drops the runtime and everything it holds.
             *
             * Hands memory back to the OS, so call this under a system-use
             * guard. Does nothing when no scene is loaded.
             */
            void drop_runtime();

            bool load_scene(u16 scene_id);
            void redraw_backdrop();
            void rebuild_for_language();
            void apply_system_request(system_request const& request);

            alignas(scene_runtime) unsigned char _storage[sizeof(scene_runtime)];
            bool _runtime_live{ false };

            /// A language change left the backdrop's text regions in the old language.
            bool _backdrop_stale{ false };
        };

        /**
         * @brief Lets the scripts a fade parked run again.
         *
         * A fade is the one modal with nothing on the state stack to take it
         * down, so the callback is where it ends. A fade the engine starts for
         * itself passes no callback and never reaches this - it parked nothing,
         * and resume_execution() would be a no-op anyway.
         */
        void scene_fade_done()
        {
            g_pEngine->vm().resume_execution();
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
            g_pEngine->pointer().switch_to(pointer_role::WAIT);
            g_pEngine->pointer().update();
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

            if (sc.background_bitmap_id != data::NO_ID)
            {
                auto const& ref = gd.bitmaps()[sc.background_bitmap_id];
                ace::paletteLoadFromFd(g_pEngine->assets().open(ref.palette_hash),
                    g_mainScreen->palette(),
                    SCENE_PALETTE_COLORS);

                auto bg = ace::bitmapCreateFromFd(g_pEngine->assets().open(ref.bitmap_hash), 0);

                if (bg)
                {
                    // A planar blit only writes the source's own bitplanes, so a
                    // background that doesn't span the whole screen at full depth
                    // leaves the previous scene's pixels (and unwritten high
                    // planes) behind. Clear to index 0 first in that case.
                    bool const covers_screen = sc.offset.x == 0 && sc.offset.y == 0
                        && sc.width >= SCREEN_WIDTH && sc.height >= SCREEN_HEIGHT
                        && bg->Depth >= g_mainScreen->back_buffer()->Depth;
                    if (!covers_screen) g_mainScreen->clear(0);

                    g_mainScreen->blit_copy(
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
                ace::paletteLoadFromFd(g_pEngine->assets().open(gd.ui_state().ui_palette_hash),
                    g_mainScreen->palette(), UI_PALETTE_COLORS);
            }

            // Only a scene that shows the pointer holds its band out of the
            // fade. A scene without UI is free to author its background across
            // those entries, so there they have to dim like everything else -
            // and the pointer has to go, or it would draw itself in whatever
            // colors the background put in its band.
            g_mainScreen->set_cursor_band_exempt(showsUi);
            g_pEngine->pointer().set_visible(showsUi);

            scene_text_draw(sc);
            g_mainScreen->duplicate_back_to_front();
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
        void scene_state::redraw_backdrop()
        {
            auto const scenes = g_pEngine->data().scenes();
            u16 const id = g_pEngine->current_scene();
            if (id >= scenes.size()) return;

            // Nothing else in the backdrop is localized, so a scene without text
            // regions would decode its background only to draw the same picture.
            if (scenes[id].text_region_range.count == 0) return;

            show_wait_pointer();

            mtl::system_use_guard guard;  // OS on for pak/disk access

            // The palette about to be loaded lands on top of whatever the
            // timeline's cycles have turned, and their record of how far they
            // have turned would then describe colors that are no longer there.
            // The timeline stays live across a repaint, so this hands the bands
            // back without taking the scene down.
            runtime().timeline().restore_palette();
            draw_backdrop(scenes[id]);

            // Every bob standing over the old backdrop saved its background from
            // it, and that has just been painted over. Undrawing those saves
            // would stamp the old text back wherever a bob happened to be.
            bobDiscardUndraw();
        }

        /**
         * @brief Rebuild what the scene had built out of the string table.
         *
         * The engine has already swapped the table, in engine::frame(), before
         * any state ran this frame. What is left is the scene's own share of the
         * change: the text it drew out of the old table.
         */
        void scene_state::rebuild_for_language()
        {
            // Captions and text regions were both built from the table that just
            // went away. A pending scene is about to rebuild them anyway, so this
            // only pays for a language change that stays in the current scene.
            if (g_pEngine->pending_system_kind() == system_kind::load_scene) return;

            mtl::system_use_guard guard;  // OS on for pak/disk access
            runtime().bobs().reload_captions();

            // Repainting the backdrop is far more expensive than a caption,
            // so it is only marked here; loop() picks the moment.
            _backdrop_stale = true;
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
         *         that on its first slice. A bad scene id also returns false,
         *         with nothing loaded and the VM left as it was - loop() tears
         *         it down, because a script that asked to leave is over
         *         either way.
         */
        bool scene_state::load_scene(u16 scene_id)
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
            // discarded has nobody left to act for. The VM's own latch goes with
            // the sessions, in the reset_execution() further down.
            g_pEngine->take_pending_modal();

            // Everything this load asks the cache for is stamped as wanted now,
            // which is what stops the load evicting its own assets and what
            // makes the previous scene's leftovers the first to go.
            g_pEngine->assets().begin_load();

            LONG const chipBefore = to<LONG>(memGetFreeChipSize());
            LONG const fastBefore = free_fast_size();
            // The load lays the whole backdrop down in the current language, so
            // any repaint a language change was still owed is settled by it.
            _backdrop_stale = false;

            bool const silence = sc.music_hash == MUSIC_SILENCE;
            bool music_started = false;
            {
                mtl::system_use_guard guard;  // OS on for pak/disk access

                drop_runtime();
                draw_backdrop(sc);
                construct_runtime(sc);

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
                && g_pEngine->vm().execute(sc.on_enter_function_id))
            {
                return true;
            }

            // No entry script ran, so nothing would bring the palette up (the
            // loaded palette only reaches the display through a fade). Fade in
            // from black with the project's default so a script-less scene is
            // still visible.
            u16 const frames = gd.config().default_fade_frames;
            g_mainScreen->fade_from_black(
                to<UBYTE>(frames > 255 ? 255 : frames), 0, nullptr);
            return false;
        }

        /**
         * @brief Puts the modal a script asked for last frame on screen.
         *
         * A bare FADE_IN/FADE_OUT (no MOV D0, #frames) arrives as 0 frames, so
         * the project's configured default stands in and scripts can omit the
         * count.
         *
         * The VM is already latched by the call that recorded the request. Each
         * kind here only has to arrange for something to lift it: a fade does it
         * from its own callback, a dialogue from the state that answers it. A
         * kind that arranges neither leaves every script parked for the rest of
         * the scene.
         */
        void start_pending_modal()
        {
            auto const request = g_pEngine->take_pending_modal();

            auto fade_frames = [&] {
                u16 const frames = request.arg1 ? request.arg1
                                                : g_pEngine->data().config().default_fade_frames;
                return to<UBYTE>(frames > 255 ? 255 : frames);
            };

            switch (request.kind)
            {
                case modal_kind::fade_out:
                    g_mainScreen->fade_to_black(fade_frames(), 0, scene_fade_done);
                    return;

                case modal_kind::fade_in:
                    g_mainScreen->fade_from_black(fade_frames(), 0, scene_fade_done);
                    return;

                case modal_kind::show_dialog:
                    // The main loop pushes the dialogue state next frame and
                    // dialogueDestroy() lifts the latch when it comes down.
                    g_pEngine->overlays().post_dialogue(request.arg1);
                    return;

                case modal_kind::none: return;
            }
        }

        /**
         * @brief Act on a system request - a scene change, a save, a new game.
         *
         * A kind the engine cannot do yet is logged and dropped, and has to
         * undo the stop its bridge call put on the VM: a modal one by resuming
         * it here, a terminal one through the teardown loop() owes it
         * either way.
         */
        void scene_state::apply_system_request(system_request const& request)
        {
            switch (request.kind)
            {
                case system_kind::load_scene: load_scene(request.arg1); return;

                case system_kind::save_game:
                    NE_LOG("SAVE_GAME slot %hu: no save UI yet", request.arg1);
                    // A modal call with no screen to answer it. Until there is
                    // one, lift the latch here or the script never runs again.
                    g_pEngine->vm().resume_execution();
                    return;

                case system_kind::load_game:
                    NE_LOG("LOAD_GAME slot %hu: no save format yet", request.arg1);
                    return;

                case system_kind::new_game: NE_LOG("NEW_GAME: nothing to start yet"); return;

                case system_kind::quit_game: NE_LOG("QUIT_GAME: no way out yet"); return;

                case system_kind::none: return;
            }
        }
    }  // namespace

    scene_runtime& scene_state::runtime()
    {
        assert(_runtime_live, "reached a scene runtime before a scene loaded");
        return *reinterpret_cast<scene_runtime*>(_storage);
    }

    void scene_state::construct_runtime(data::scene const& sc)
    {
        assert(!_runtime_live, "built a scene runtime over one still standing");

        new (_storage) scene_runtime(
            g_pEngine->data(), g_pEngine->assets(), g_pEngine->pointer(), sc);
        _runtime_live = true;
    }

    void scene_state::drop_runtime()
    {
        if (!_runtime_live) return;

        runtime().~scene_runtime();
        _runtime_live = false;
    }

    scene_runtime& scene_state::current_runtime()
    {
        return state_host<scene_state>::instance().runtime();
    }

    scene_runtime& current_scene_runtime()
    {
        return scene_state::current_runtime();
    }

    bool scene_state::create()
    {
        logBlockBegin("State: Scene");
        // ACE leaves the system 'used' after engine setup; the game loop runs
        // 'unused'. Drop to that baseline once (as splash does); load_scene
        // re-enters 'used' briefly via a system_use_guard for each pak access.
        systemUnuse();
        // The boot load blocks for over a second with the pointer on screen, and
        // draw_backdrop only settles the band once it has the scene in hand.
        g_mainScreen->set_cursor_band_exempt(true);
        show_wait_pointer();
        // Every later load is followed by the same frame's own VM update; this
        // one runs before the loop starts, so the entry script's FADE_IN needs
        // priming here or the first frame renders at full brightness.
        if (load_scene(g_pEngine->data().config().boot_scene_id))
        {
            g_pEngine->vm().update();
        }

        // A boot scene that could not be loaded leaves nothing for loop() to
        // run against, and every frame below reads the runtime. engine::
        // initialize() already refuses an out-of-range boot id, so this is the
        // belt to that brace rather than a hole anyone can reach today. Ending
        // the game is what main.cpp does when startup fails, and it beats
        // popping the only state and looping on a black screen.
        if (!_runtime_live)
        {
            NE_LOG("the boot scene did not load; there is nothing to run");
            gameExit();
            return false;
        }

        return true;
    }

    bool scene_state::loop()
    {
        // ── Requests recorded last frame ─────────────────────────────────────

        // A transition runs from the script's fade-out through the load that
        // follows it, and in the frames between the two nothing is pending.
        // Treating the whole span as one event keeps the pointer on WAIT instead
        // of flicking back to its normal role in that gap.
        bool const transitioning = g_pEngine->language_changed()
            || g_pEngine->pending_system_kind() == system_kind::load_scene
            || g_mainScreen->is_fading_out();

        // Cheap and idempotent: the frame's own pointer update and
        // screenProcess() publish it, so the fade needs nothing special.
        if (transitioning) g_pEngine->pointer().switch_to(pointer_role::WAIT);

        // Language before scene: a script that switches language and then leaves
        // should present the next scene in the language it just chose. The
        // engine has already swapped the table by now, in engine::frame(); this
        // is the scene catching up with it.
        if (g_pEngine->language_changed())
        {
            // Re-renders every caption under a system-use guard, so it blocks
            // for long enough to need the pointer published first.
            show_wait_pointer();
            rebuild_for_language();
        }
        if (g_pEngine->has_pending_system())
        {
            show_wait_pointer();
            apply_system_request(g_pEngine->take_pending_system());
        }

        // A terminal call is owed a teardown. If answering its request did not
        // deliver one - the scene failed to load, or nothing implements it yet -
        // it is delivered here, or the VM would never run again.
        if (g_pEngine->vm().terminated()) g_pEngine->vm().reset_execution();

        if (g_pEngine->has_pending_modal()) start_pending_modal();

        // A language change owes the backdrop a repaint, which costs a full
        // background decode. A fade-out is the front half of a scene change -
        // scripts post their GOTO_SCENE from the fade's own callback, a frame or
        // more after the SET_LANG - and that load repaints the backdrop anyway,
        // so waiting for the fade to settle keeps the decode from being done
        // twice and thrown away once.
        if (_backdrop_stale && !g_mainScreen->is_fading_out())
        {
            _backdrop_stale = false;
            redraw_backdrop();
        }

        // ── Modals ───────────────────────────────────────────────────────────
        // Fades take themselves down through scene_fade_done(). A dismissible
        // box would be closed here, before the timeline and the VM are asked to
        // run, so that both start again in the frame it closes.

        // One flag, one owner. The VM latches itself the moment a script makes
        // a modal call and stays latched until whoever answered it resumes it,
        // so asking the VM is the same as asking whether a modal is up - and
        // there is no second copy of the answer to fall out of step.
        //
        // The scene stops with it, for a strong reason: a fade is the front half
        // of a scene change, so anything a track moved would be thrown away, a
        // keyframe running a script could queue a second scene change, and a
        // hovered hotspot would put its role back over the WAIT pointer. An
        // overlay has a further one - it restores the pixels it covered from a
        // buffer taken when it went up, so anything moving underneath would be
        // painted back where it no longer is.
        bool const holding = g_pEngine->vm().blocked();

        auto& scene = runtime();

        // ── Timeline ─────────────────────────────────────────────────────────
        if (!holding) scene.timeline().update();

        // ── UI ───────────────────────────────────────────────────────────────
        if (!holding) scene.hotspots().update();

        // ── Scripts ──────────────────────────────────────────────────────────
        // Last, so a session started this frame by a keyframe or a click runs
        // its first slice now instead of next frame, and so whatever it moves is
        // drawn below. A hotspot can leave the scene without a script; when one
        // has, this scene is already over and nothing here should touch it.
        if (!holding && g_pEngine->pending_system_kind() != system_kind::load_scene)
        {
            g_pEngine->vm().update();
        }

        g_pEngine->pointer().update();
        scene.bobs().update();
        scene.bobs().draw();

        // The scene stays up until something else takes it down: nothing here
        // ends it, and a scene change is a load, not a pop.
        return true;
    }

    scene_state::~scene_state()
    {
        g_pEngine->take_pending_modal();
        g_pEngine->take_pending_system();

        // A scene ending takes its scripts with it, latch and all, so a modal
        // that was still up owes nothing to anyone.
        g_pEngine->vm().reset_execution();

        {
            // The runtime hands its memory back to the OS on the way out.
            mtl::system_use_guard guard;
            drop_runtime();
        }

        musicFree();
        logBlockEnd("State: Scene");
    }

    void scene_state::resume()
    {
        // The button went unwatched while something was up over the scene, so a
        // change made in that time would read as a fresh click on whatever lies
        // under the pointer.
        runtime().hotspots().resync_button();
    }

    tState g_stateScene = state_host<scene_state>::table();
}  // namespace NEONengine
