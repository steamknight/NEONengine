#include "neonengine.h"

#include <ace/managers/system.h>
#include <ace/managers/blit.h>
#include <ace/managers/viewport/simplebuffer.h>
#include <ace/utils/palette.h>
#include <ace/managers/mouse.h>

#include <stdint.h>

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

const Region *pEnglish = NULL;
const Region *pItalian = NULL;

#define MAKE_CONTEXT(id, state) (intptr_t)(id) << 8 | (intptr_t)(state)
#define CONTEXT_GET_ID(ctx) ((intptr_t)(ctx) & 0x0000FF00) >> 8
#define CONTEXT_GET_STATE(ctx) ((intptr_t)(ctx)) & 0x000000FF

void cbOnHovered(Region *pRegion)
{
    intptr_t ctx = (intptr_t)pRegion->context;
    intptr_t id = CONTEXT_GET_ID(ctx);
    ctx = MAKE_CONTEXT(id, id + FLAG_HOVERED);
    pRegion->context = (void*)ctx;
}

void cbOnUnhovered(Region *pRegion)
{
    intptr_t ctx = (intptr_t)pRegion->context;
    intptr_t id = CONTEXT_GET_ID(ctx);
    ctx = MAKE_CONTEXT(id, id + FLAG_IDLE);
    pRegion->context = (void*)ctx;
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

    paletteLoad("data/core/base.plt", screenGetPalette(g_mainScreen), 255);
    s_pFlagsAtlas = bitmapCreateFromFile("data/core/flags.bm", 0);

    mousePointerCreate("data/core/pointers.bm");
    s_flagsLayer = layerCreate();

    UWORD uwX = (SCREEN_WIDTH - FLAG_WIDTH) >> 1;
    UWORD uwY = (SCREEN_HEIGHT - (FLAG_HEIGHT << 1)) >> 1;

    ULONG enCtx = MAKE_CONTEXT(EN, 1);
    Region englishFlag = {
        .pointer = MOUSE_USE,
        .bounds = (tUwRect) { .uwX = uwX, .uwY = uwY, .uwWidth = FLAG_WIDTH, .uwHeight = FLAG_HEIGHT},
        .cbOnHovered = cbOnHovered,
        .cbOnUnhovered = cbOnUnhovered,
        .cbOnPressed = cbOnPressed,
        .cbOnReleased = cbOnReleased,
        .context = (void*)enCtx,
    };

    ULONG itCtx = MAKE_CONTEXT(IT, 1);
    Region italianFlag = {
        .pointer = MOUSE_USE,
        .bounds = (tUwRect) { .uwX = uwX, .uwY = uwY + FLAG_HEIGHT, .uwWidth = FLAG_WIDTH, .uwHeight = FLAG_HEIGHT},
        .cbOnHovered = cbOnHovered,
        .cbOnUnhovered = cbOnUnhovered,
        .cbOnPressed = cbOnPressed,
        .cbOnReleased = cbOnReleased,
        .context = (void*)itCtx
    };

    RegionId en = layerAddRegion(s_flagsLayer, &englishFlag);
    RegionId it = layerAddRegion(s_flagsLayer, &italianFlag);

    pEnglish = layerGetRegion(s_flagsLayer, en);
    pItalian = layerGetRegion(s_flagsLayer, it);

    layerSetEnable(s_flagsLayer, 1);
    layerSetUpdateOutsideBounds(s_flagsLayer, 1);
}

void langSelectProcess(void)
{
    mousePointerUpdate();
    layerUpdate(s_flagsLayer);

    ULONG enState = CONTEXT_GET_STATE((UWORD)(ULONG)pEnglish->context);
    blitCopy(
        s_pFlagsAtlas, s_flags[enState].uwX, s_flags[enState].uwY,
        screenGetBackBuffer(g_mainScreen),
        pEnglish->bounds.uwX, pEnglish->bounds.uwY,
        FLAG_WIDTH, FLAG_HEIGHT,
        MINTERM_COOKIE
    );

    ULONG itState = CONTEXT_GET_STATE((UWORD)(ULONG)pItalian->context);
    blitCopy(
        s_pFlagsAtlas, s_flags[itState].uwX, s_flags[itState].uwY,
        screenGetBackBuffer(g_mainScreen),
        pItalian->bounds.uwX, pItalian->bounds.uwY,
        FLAG_WIDTH, FLAG_HEIGHT,
        MINTERM_COOKIE
    );
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