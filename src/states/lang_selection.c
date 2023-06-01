#include "neonengine.h"

#include <ace/managers/system.h>
#include <ace/managers/blit.h>
#include <ace/managers/viewport/simplebuffer.h>
#include <ace/utils/palette.h>
#include <ace/managers/mouse.h>
#include "core/screen.h"
#include "core/mouse_pointer.h"
#include "core/layer.h"

#define STATE_NAME "State: Language Selection"
#define FADE_DURATION 25

#define FLAG_IDLE 1
#define FLAG_HOVERED 0

#define EN 0
#define IT 2
#define DE 4

#define FLAG_WIDTH 40
#define FLAG_HEIGHT 32

static Layer *s_flagsLayer;
static tBitMap *s_pFlagsAtlas;
static tUwCoordYX s_flags[6] =
{
    {  .uwX = 0, .uwY =  0 }, { .uwX = FLAG_WIDTH, .uwY =  0 }, // English
    {  .uwX = 0, .uwY = 32 }, { .uwX = FLAG_WIDTH, .uwY = 32 }, // Italian
    {  .uwX = 0, .uwY = 64 }, { .uwX = FLAG_WIDTH, .uwY = 64 }, // German
};

void cbOnHovered(Region *pRegion)
{
    UBYTE id = ((UBYTE)(ULONG)pRegion->context) + FLAG_HOVERED;

    blitCopy(
        s_pFlagsAtlas, s_flags[id].uwX, s_flags[id].uwY,
        g_mainScreen->pBuffer->pBack, pRegion->bounds.uwX, pRegion->bounds.uwY,
        pRegion->bounds.uwWidth, pRegion->bounds.uwHeight,
        MINTERM_COOKIE
    );
}

void cbOnUnhovered(Region *pRegion)
{
    UBYTE id = ((UBYTE)(ULONG)pRegion->context) + FLAG_IDLE;

    blitCopy(
        s_pFlagsAtlas, s_flags[id].uwX, s_flags[id].uwY,
        g_mainScreen->pBuffer->pBack, pRegion->bounds.uwX, pRegion->bounds.uwY,
        pRegion->bounds.uwWidth, pRegion->bounds.uwHeight,
        MINTERM_COOKIE
    );
}

void cbOnPressed(Region *pRegion)
{
    logWrite("Pressing %d", (UWORD)(ULONG)pRegion->context);
}

void cbOnReleased(Region *pRegion)
{
    logWrite("Releasing %d", (UWORD)(ULONG)pRegion->context);
}

void langSelectCreate(void)
{
    logBlockBegin(STATE_NAME);
    screenFadeFromBlack(g_mainScreen, FADE_DURATION, 0, NULL);
    screenClear(g_mainScreen, 0);

    paletteLoad("data/core/base.plt", g_mainScreen->pFade->pPaletteRef, 255);
    s_pFlagsAtlas = bitmapCreateFromFile("data/core/flags.bm", 0);

    mousePointerCreate("data/core/pointers.bm");
    s_flagsLayer = layerCreate();

    UWORD uwX = (SCREEN_WIDTH - FLAG_WIDTH) >> 1;
    UWORD uwY = (SCREEN_HEIGHT - (FLAG_HEIGHT << 1)) >> 1;

    Region englishFlag = {
        .pointer = MOUSE_USE,
        .bounds = (tUwRect) { .uwX = uwX, .uwY = uwY, .uwWidth = FLAG_WIDTH, .uwHeight = FLAG_HEIGHT},
        .cbOnHovered = cbOnHovered,
        .cbOnUnhovered = cbOnUnhovered,
        .cbOnPressed = cbOnPressed,
        .cbOnReleased = cbOnReleased,
        .context = (void*)EN,
    };

    Region italianFlag = {
        .pointer = MOUSE_USE,
        .bounds = (tUwRect) { .uwX = uwX, .uwY = uwY + FLAG_HEIGHT, .uwWidth = FLAG_WIDTH, .uwHeight = FLAG_HEIGHT},
        .cbOnHovered = cbOnHovered,
        .cbOnUnhovered = cbOnUnhovered,
        .cbOnPressed = cbOnPressed,
        .cbOnReleased = cbOnReleased,
        .context = (void*)IT
    };

    RegionId en = layerAddRegion(s_flagsLayer, &englishFlag);
    RegionId it = layerAddRegion(s_flagsLayer, &italianFlag);

    const Region *pEnglish = layerGetRegion(s_flagsLayer, en);
    const Region *pItalian = layerGetRegion(s_flagsLayer, it);
    
    blitCopy(
        s_pFlagsAtlas, s_flags[EN + 1].uwX, s_flags[EN + 1].uwY,
        g_mainScreen->pBuffer->pBack, pEnglish->bounds.uwX, pEnglish->bounds.uwY,
        FLAG_WIDTH, FLAG_HEIGHT,
        MINTERM_COOKIE
    );

    blitCopy(
        s_pFlagsAtlas, s_flags[IT + 1].uwX, s_flags[IT + 1].uwY,
        g_mainScreen->pBuffer->pBack, pItalian->bounds.uwX, pItalian->bounds.uwY,
        FLAG_WIDTH, FLAG_HEIGHT,
        MINTERM_COOKIE
    );

    layerSetEnable(s_flagsLayer, 1);
    layerSetUpdateOutsideBounds(s_flagsLayer, 1);
}

void langSelectProcess(void)
{
    mousePointerUpdate();
    layerUpdate(s_flagsLayer);
}

void langSelectDestroy(void)
{
    logBlockEnd(STATE_NAME);

    bitmapDestroy(s_pFlagsAtlas);

    mousePointerDestroy();
    layerDestroy(s_flagsLayer);
}

tState g_stateLangSelect = {
    .cbCreate = langSelectCreate,
    .cbLoop = langSelectProcess,
    .cbDestroy = langSelectDestroy,
};