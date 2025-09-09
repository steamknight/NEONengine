#include "neonengine.h"

#include <stdint.h>

#include <ace/managers/blit.h>
#include <ace/managers/mouse.h>
#include <ace/managers/system.h>
#include <ace/managers/viewport/simplebuffer.h>
#include <ace/utils/palette.h>

#include <mtl/utility.h>

#include "core/layer.h"
#include "core/mouse_pointer.h"
#include "core/screen.h"

namespace NEONengine
{
#define STATE_NAME    "State: Language Selection"
#define FADE_DURATION 25

#define FLAG_IDLE    1
#define FLAG_HOVERED 0

#define EN 0
#define IT 2
#define DE 4

    constexpr UWORD FLAG_WIDTH  = 40;
    constexpr UWORD FLAG_HEIGHT = 32;

    static Layer *s_flagsLayer;
    static tBitMap *s_pFlagsAtlas;
    static tUwCoordYX s_flags[6] = {
        { .uwY = 0, .uwX = 0 },
        {
            .uwY = 0,
            .uwX = FLAG_WIDTH,
        },  // English
        { .uwY = 32, .uwX = 0 },
        { .uwY = 32, .uwX = FLAG_WIDTH },  // Italian
        {
            .uwY = 64,
            .uwX = 0,
        },
        {
            .uwY = 64,
            .uwX = FLAG_WIDTH,
        },  // German
    };

    Hotspot const *pEnglish = NULL;
    Hotspot const *pItalian = NULL;

#define MAKE_CONTEXT(id, state) (intptr_t)(id) << 8 | (intptr_t)(state)
#define CONTEXT_GET_ID(ctx)     ((intptr_t)(ctx) & 0x0000FF00) >> 8
#define CONTEXT_GET_STATE(ctx)  ((intptr_t)(ctx)) & 0x000000FF

    void cbOnHovered(Hotspot *pHotspot)
    {
        intptr_t ctx      = (intptr_t)pHotspot->context;
        intptr_t id       = CONTEXT_GET_ID(ctx);
        ctx               = MAKE_CONTEXT(id, id + FLAG_HOVERED);
        pHotspot->context = (void *)ctx;
    }

    void cbOnUnhovered(Hotspot *pHotspot)
    {
        intptr_t ctx      = (intptr_t)pHotspot->context;
        intptr_t id       = CONTEXT_GET_ID(ctx);
        ctx               = MAKE_CONTEXT(id, id + FLAG_IDLE);
        pHotspot->context = (void *)ctx;
    }

    void cbOnPressed(Hotspot *pHotspot)
    {
        logWrite("Pressing %d", (UWORD)(ULONG)pHotspot->context);
    }

    void cbOnReleased(Hotspot *pHotspot)
    {
        logWrite("Releasing %d", (UWORD)(ULONG)pHotspot->context);
    }

    void langSelectCreate(void)
    {
        logBlockBegin(STATE_NAME);
        screenFadeFromBlack(g_mainScreen, FADE_DURATION, 0, NULL);
        screenClear(g_mainScreen, 0);

        paletteLoadFromPath("data/core/base.plt", screenGetPalette(g_mainScreen), 255);
        s_pFlagsAtlas = bitmapCreateFromPath("data/core/flags.bm", 0);

        mousePointerCreate("data/core/pointers.bm");
        s_flagsLayer = layerCreate();

        UWORD uwX = (SCREEN_WIDTH - FLAG_WIDTH) >> 1;
        UWORD uwY = (SCREEN_HEIGHT - (FLAG_HEIGHT << 1)) >> 1;

        ULONG enCtx         = MAKE_CONTEXT(EN, 1);
        Hotspot englishFlag = {
            .bounds
            = (tUwRect){ .uwY = uwY, .uwX = uwX, .uwWidth = FLAG_WIDTH, .uwHeight = FLAG_HEIGHT },
            .pointer       = mouse_pointer::USE,
            .cbOnHovered   = cbOnHovered,
            .cbOnUnhovered = cbOnUnhovered,
            .cbOnPressed   = cbOnPressed,
            .cbOnReleased  = cbOnReleased,
            .context       = (void *)enCtx,
        };

        ULONG itCtx         = MAKE_CONTEXT(IT, 1);
        Hotspot italianFlag = { .bounds      = (tUwRect){ .uwY      = mtl::to<UWORD>(uwY + FLAG_HEIGHT),
                                                          .uwX      = uwX,
                                                          .uwWidth  = FLAG_WIDTH,
                                                          .uwHeight = FLAG_HEIGHT },
                                .pointer     = mouse_pointer::USE,
                                .cbOnHovered = cbOnHovered,
                                .cbOnUnhovered = cbOnUnhovered,
                                .cbOnPressed   = cbOnPressed,
                                .cbOnReleased  = cbOnReleased,
                                .context       = (void *)itCtx };

        HotspotId en = layerAddHotspot(s_flagsLayer, &englishFlag);
        HotspotId it = layerAddHotspot(s_flagsLayer, &italianFlag);

        pEnglish = layerGetHotspot(s_flagsLayer, en);
        pItalian = layerGetHotspot(s_flagsLayer, it);

        layerSetEnable(s_flagsLayer, 1);
        layerSetUpdateOutsideBounds(s_flagsLayer, 1);
    }

    void langSelectProcess(void)
    {
        mousePointerUpdate();
        layerUpdate(s_flagsLayer);

        ULONG enState = CONTEXT_GET_STATE((UWORD)(ULONG)pEnglish->context);
        blitCopy(s_pFlagsAtlas,
                 s_flags[enState].uwX,
                 s_flags[enState].uwY,
                 screenGetBackBuffer(g_mainScreen),
                 pEnglish->bounds.uwX,
                 pEnglish->bounds.uwY,
                 FLAG_WIDTH,
                 FLAG_HEIGHT,
                 MINTERM_COOKIE);

        ULONG itState = CONTEXT_GET_STATE((UWORD)(ULONG)pItalian->context);
        blitCopy(s_pFlagsAtlas,
                 s_flags[itState].uwX,
                 s_flags[itState].uwY,
                 screenGetBackBuffer(g_mainScreen),
                 pItalian->bounds.uwX,
                 pItalian->bounds.uwY,
                 FLAG_WIDTH,
                 FLAG_HEIGHT,
                 MINTERM_COOKIE);
    }

    void langSelectDestroy(void)
    {
        logBlockEnd(STATE_NAME);

        bitmapDestroy(s_pFlagsAtlas);

        mousePointerDestroy();
        layerDestroy(s_flagsLayer);
    }

    tState g_stateLangSelect = {
        .cbCreate  = langSelectCreate,
        .cbLoop    = langSelectProcess,
        .cbDestroy = langSelectDestroy,
    };
}  // namespace NEONengine