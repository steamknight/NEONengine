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

#include "core/nine_patch.h"
#include "core/screen.h"
#include "core/text_render.h"

namespace NEONengine
{
    ace::font_ptr s_pFont{ nullptr };
    text_renderer_ptr s_pTextRenderer{ nullptr };

    void dialogueTestCreate(void)
    {
        logBlockBegin("dialogueTestCreate");

        screenFadeFromBlack(g_mainScreen, 25, 0, nullptr);
        screenClear(g_mainScreen, 0);

        paletteLoadFromPath("data/core/base.plt", screenGetPalette(g_mainScreen), 255);
        auto pFont = ace::fontCreateFromPath("data/font.fnt");

        auto renderer_result = text_renderer::create(pFont.get());
        if (!renderer_result)
        {
            NE_LOG("Failed to create text renderer: Error code %d",
                   mtl::to<int>(renderer_result.error()));
            return;
        }

        auto pPatchBitmap = ace::bitmapCreateFromPath("data/core/frame_9.bm", 0);

        bstr_view text
            = "I'm the love child of Icarus and Sisyphus; no matter how hard I try to rise above, "
              "my hubris crashes me face first back into the Gutter.\n\nAnd the cycle continues.";

        uint16_t uwMargins = 8;
        uint16_t uwWidth   = 240;

        uint32_t ulStartText = timerGetPrec();
        auto pTextBitmap
            = s_pTextRenderer->create_text(text, uwWidth - uwMargins * 2, text_justify::LEFT);
        uint32_t ulEndText = timerGetPrec();

        auto patch        = nine_patch(pPatchBitmap, 16, 16, 16, 16);
        uint16_t uwHeight = pTextBitmap->uwActualHeight + uwMargins * 2;

        uint32_t ulStartPatch = timerGetPrec();
        auto pRenderedPatch   = patch.render(uwWidth, uwHeight, 0);
        uint32_t ulEndPatch   = timerGetPrec();

        uint32_t ulStartRender = timerGetPrec();
        screenBlitCopy(
            g_mainScreen, pRenderedPatch.get(), 0, 0, 0, 0, uwWidth, uwHeight, MINTERM_COOKIE);
        screenTextCopy(
            g_mainScreen, pTextBitmap.get(), uwMargins, uwMargins, 1, FONT_COOKIE | FONT_SHADOW);
        uint32_t ulEndRender = timerGetPrec();

        char timerBuffer[64];
        char renderBuffer[128];

        timerFormatPrec(timerBuffer, timerGetDelta(ulStartText, ulEndText));
        snprintf(renderBuffer, sizeof(renderBuffer), "Text created in %s", timerBuffer);
        auto pTextCreate = s_pTextRenderer->create_text(renderBuffer, 320, text_justify::CENTER);

        timerFormatPrec(timerBuffer, timerGetDelta(ulStartPatch, ulEndPatch));
        snprintf(renderBuffer, sizeof(renderBuffer), "Patch created in %s", timerBuffer);
        auto pPatchCreate = s_pTextRenderer->create_text(renderBuffer, 320, text_justify::CENTER);

        timerFormatPrec(timerBuffer, timerGetDelta(ulStartRender, ulEndRender));
        snprintf(renderBuffer, sizeof(renderBuffer), "Rendered in %s", timerBuffer);
        auto pRender = s_pTextRenderer->create_text(renderBuffer, 320, text_justify::CENTER);

        screenTextCopy(g_mainScreen, pTextCreate.get(), 0, 180, 1, FONT_COOKIE);
        screenTextCopy(g_mainScreen, pPatchCreate.get(), 0, 191, 1, FONT_COOKIE);
        screenTextCopy(g_mainScreen, pRender.get(), 0, 202, 1, FONT_COOKIE);
    }

    void dialogueTestProcess(void) {}

    void dialogueTestDestroy(void)
    {
        s_pTextRenderer.release();
        s_pFont.release();
    }

    tState g_stateDialogueTest = {
        .cbCreate  = dialogueTestCreate,
        .cbLoop    = dialogueTestProcess,
        .cbDestroy = dialogueTestDestroy,
    };

}  // namespace NEONengine