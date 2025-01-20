#include "neonengine.h"

#include <ace/managers/system.h>
#include <ace/utils/font.h>
#include <ace/utils/palette.h>

#include "core/screen.h"
#include "core/text.h"

static tFont* s_pFont;

void fontTestCreate(void)
{
    logBlockBegin("fontTestCreate");

    s_pFont = fontCreateFromPath("data/font.fnt");

    logBlockEnd("fontTestCreate");
}

void fontTestProcess(void)
{
}

void fontTestDestroy(void)
{
    fontDestroy(s_pFont);
}

tState g_stateFontTest = {
    .cbCreate = fontTestCreate,
    .cbLoop = fontTestProcess,
    .cbDestroy = fontTestDestroy,
};