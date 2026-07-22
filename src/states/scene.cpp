#include "neonengine.h"

#include <ace/managers/blit.h>
#include <ace/managers/log.h>
#include <ace/managers/system.h>

#include <ace++/bitmap.h>
#include <ace++/palette.h>

#include <mtl/utility.h>

#include "core/game_data.h"
#include "core/music.h"
#include "core/noir_vm.h"
#include "core/screen.h"

namespace NEONengine
{
    namespace
    {
        constexpr uint16_t NO_BITMAP   = 0xFFFF;
        constexpr uint16_t NO_FUNCTION = 0xFFFF;
        constexpr uint32_t MUSIC_KEEP    = 0xFFFFFFFF;
        constexpr uint32_t MUSIC_SILENCE = 0xFFFFFFFE;

        // These scenes are full-screen (no UI overlay yet), so the background
        // palette is loaded straight into indices 0.. rather than the 80..255
        // scene band; revisit when UI-overlay scenes arrive.
        constexpr UWORD SCENE_PALETTE_COLORS = 256;

        /**
         * @brief Present scene @p scene_id: background, music, and entry script.
         *
         * Serves both the initial boot scene and every GOTO_SCENE transition. Pak
         * access runs under a system_use_guard; music playback and the VM run
         * afterwards with the system released (the game-loop baseline).
         */
        void load_scene(uint16_t scene_id)
        {
            auto& gd          = g_pEngine->data();
            auto const scenes = gd.scenes();
            if (scene_id >= scenes.size())
            {
                NE_LOG("scene: invalid scene id %hu (%lu scenes)",
                    scene_id,
                    mtl::to<ULONG>(scenes.size()));
                return;
            }

            auto const& sc = scenes[scene_id];
            g_pEngine->set_current_scene(scene_id);

            bool const silence = sc.music_hash == MUSIC_SILENCE;
            bool music_started = false;
            {
                mtl::system_use_guard guard;  // OS on for pak/disk access

                if (sc.background_bitmap_id != NO_BITMAP)
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
                            mtl::to<WORD>(sc.offset.x),
                            mtl::to<WORD>(sc.offset.y),
                            mtl::to<WORD>(sc.width),
                            mtl::to<WORD>(sc.height),
                            MINTERM_COPY);
                    }
                }

                if (!silence && sc.music_hash != MUSIC_KEEP)
                {
                    auto mod = gd.open_asset(sc.music_hash);
                    if (mod)
                    {
                        musicLoad(mtl::move(mod));
                        music_started = true;
                    }
                }
            }

            if (silence) musicFree();
            if (music_started) musicPlayCurrent(1);

            g_pEngine->vm().reset_execution();
            if (sc.on_enter_function_id != NO_FUNCTION
                && g_pEngine->vm().execute(sc.on_enter_function_id) == noir_status::ok)
            {
                // Run the entry script's first opcodes (its FADE_IN) this frame,
                // before screenProcess renders, so the freshly loaded palette
                // fades up from black instead of flashing at full brightness.
                g_pEngine->vm().update();
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
        load_scene(g_pEngine->data().config().boot_scene_id);
    }

    void sceneProcess(void)
    {
        g_pEngine->vm().update();
        if (g_pEngine->has_pending_scene())
        {
            load_scene(g_pEngine->take_pending_scene());
        }
    }

    void sceneDestroy(void)
    {
        musicFree();
        logBlockEnd("State: Scene");
    }

    tState g_stateScene = {
        .cbCreate  = sceneCreate,
        .cbLoop    = sceneProcess,
        .cbDestroy = sceneDestroy,
    };
}  // namespace NEONengine
