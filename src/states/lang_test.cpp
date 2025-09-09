#include "neonengine.h"

#include <stdint.h>

#include <ace/managers/timer.h>
#include <ace/utils/palette.h>

#include <ace++/font.h>
#include <ace++/log.h>

#include <mtl/utility.h>

#include "core/screen.h"
#include "core/string_table.h"
#include "core/text_render.h"

namespace NEONengine
{
    void langTestCreate()
    {
        ACE_LOG_BLOCK("langTestCreate");

        screenFadeFromBlack(g_mainScreen, 25, 0, nullptr);
        screenClear(g_mainScreen, 0);

        paletteLoadFromPath("data/core/base.plt", screenGetPalette(g_mainScreen), 255);

        auto string_result = string_table::create_from_file("data/lang/test.noir");
        if (!string_result)
        {
            NE_LOG("Failed to load string table: Error code %d",
                   mtl::to<int>(string_result.error()));
            return;
        }

        auto pRenderer = g_pEngine->default_text_renderer();
        auto pStrings  = mtl::move(string_result.value());

        auto pHelloWorld
            = pRenderer->create_text(pStrings->get_string(0), 320, text_justify::CENTER);
        screenTextCopy(g_mainScreen, pHelloWorld.get(), 0, 0, 2, FONT_COOKIE);

        auto pSailor = pRenderer->create_text(pStrings->get_string(1), 320, text_justify::CENTER);
        screenTextCopy(g_mainScreen, pSailor.get(), 0, 30, 2, FONT_COOKIE);
    }

    void langTestProcess() {}

    void langTestDestroy() {}

    tState g_stateLangTest = {
        .cbCreate  = langTestCreate,
        .cbLoop    = langTestProcess,
        .cbDestroy = langTestDestroy,
    };

}  // namespace NEONengine