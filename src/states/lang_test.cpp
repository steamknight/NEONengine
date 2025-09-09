#include "neonengine.h"

#include <stdint.h>

#include <ace/managers/blit.h>
#include <ace/managers/system.h>
#include <ace/managers/timer.h>
#include <ace/managers/viewport/simplebuffer.h>
#include <ace/utils/font.h>
#include <ace/utils/palette.h>

#include <ace++/bitmap.h>
#include <ace++/font.h>

#include <mtl/utility.h>

#include "core/screen.h"
#include "core/string_table.h"
#include "core/text_render.h"
#include "utils/bstr_view.h"

namespace NEONengine
{
    void langTestCreate()
    {
        logBlockBegin("langTestCreate");

        screenFadeFromBlack(g_mainScreen, 25, 0, nullptr);
        screenClear(g_mainScreen, 0);

        paletteLoadFromPath("data/core/base.plt", screenGetPalette(g_mainScreen), 255);
        textRendererCreate("data/font.fnt");

        auto result = string_table::create_from_file("data/lang/test.noir");
        if (!result)
        {
            logWrite("Failed to load string table: Error code %d",
                     static_cast<int>(result.error()));
            return;
        }

        auto strings      = mtl::move(result.value());
        auto first_string = strings->get_string(0);

        auto pHelloWorld = textCreateFromString(first_string, 320, TextHJustify::CENTER);
        screenTextCopy(g_mainScreen, pHelloWorld.get(), 0, 0, 2, FONT_COOKIE);

        auto pSailor = textCreateFromString(strings->get_string(1), 320, TextHJustify::CENTER);
        screenTextCopy(g_mainScreen, pSailor.get(), 0, 30, 2, FONT_COOKIE);
    }

    void langTestProcess() {}

    void langTestDestroy()
    {
        textRendererDestroy();
    }

    tState g_stateLangTest = {
        .cbCreate  = langTestCreate,
        .cbLoop    = langTestProcess,
        .cbDestroy = langTestDestroy,
    };

}  // namespace NEONengine