#include "neonengine.h"

#include <stdint.h>

#include <ace/managers/mouse.h>
#include <ace/managers/system.h>
#include <ace/utils/palette.h>

#include <ace++/bitmap.h>
#include <ace++/log.h>

#include <mtl/memory.h>
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

    constexpr uint32_t EN = 0;
    constexpr uint32_t IT = 2;
    constexpr uint32_t DE = 4;

    constexpr uint16_t FLAG_WIDTH  = 40;
    constexpr uint16_t FLAG_HEIGHT = 32;

    using layer_ptr               = mtl::unique_ptr<ui_layer>;
    static layer_ptr s_flagsLayer = nullptr;

    static ace::bitmap_ptr s_pFlagsAtlas = nullptr;

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

    hotspot const* pEnglish = NULL;
    hotspot const* pItalian = NULL;

#define MAKE_CONTEXT(id, state) (intptr_t)(id) << 8 | (intptr_t)(state)
#define CONTEXT_GET_ID(ctx)     ((intptr_t)(ctx) & 0x0000FF00) >> 8
#define CONTEXT_GET_STATE(ctx)  ((intptr_t)(ctx)) & 0x000000FF

    void cbOnHovered(hotspot* pHotspot)
    {
        intptr_t ctx      = (intptr_t)pHotspot->context;
        intptr_t id       = CONTEXT_GET_ID(ctx);
        ctx               = MAKE_CONTEXT(id, id + FLAG_HOVERED);
        pHotspot->context = (void*)ctx;
    }

    void cbOnUnhovered(hotspot* pHotspot)
    {
        intptr_t ctx      = (intptr_t)pHotspot->context;
        intptr_t id       = CONTEXT_GET_ID(ctx);
        ctx               = MAKE_CONTEXT(id, id + FLAG_IDLE);
        pHotspot->context = (void*)ctx;
    }

    void cbOnPressed(hotspot* pHotspot)
    {
        logWrite("Pressing %d", (uint16_t)(ULONG)pHotspot->context);
    }

    void cbOnReleased(hotspot* pHotspot)
    {
        logWrite("Releasing %d", (uint16_t)(ULONG)pHotspot->context);
    }

    void langSelectCreate(void)
    {
        ACE_TRACE();

        screenFadeFromBlack(g_mainScreen, FADE_DURATION, 0, NULL);
        screenClear(g_mainScreen, 0);

        paletteLoadFromPath("data/core/base.plt", screenGetPalette(g_mainScreen), 255);
        s_pFlagsAtlas = ace::bitmapCreateFromPath("data/core/flags.bm", 0);

        mousePointerCreate("data/core/pointers.bm");
        s_flagsLayer = mtl::make_unique<ui_layer>();

        uint16_t uwX = 0;  //(SCREEN_WIDTH - FLAG_WIDTH) >> 1;
        uint16_t uwY = 0;  //(SCREEN_HEIGHT - (FLAG_HEIGHT << 1)) >> 1;

        ULONG enCtx         = MAKE_CONTEXT(EN, 1);
        hotspot englishFlag = {
            .bounds
            = (tUwRect){ .uwY = uwY, .uwX = uwX, .uwWidth = FLAG_WIDTH, .uwHeight = FLAG_HEIGHT },
            .pointerId     = mouse_pointer::USE,
            .cbOnHovered   = cbOnHovered,
            .cbOnUnhovered = cbOnUnhovered,
            .cbOnPressed   = cbOnPressed,
            .cbOnReleased  = cbOnReleased,
            .context       = (void*)enCtx,
            .id            = EN
        };

        ULONG itCtx         = MAKE_CONTEXT(IT, 1);
        hotspot italianFlag = { //
            .bounds        = (tUwRect){ .uwY = mtl::to<uint16_t>(uwY + FLAG_HEIGHT),
                       .uwX                  = uwX,
                       .uwWidth              = FLAG_WIDTH,
                       .uwHeight             = FLAG_HEIGHT },
            .pointerId     = mouse_pointer::USE,
            .cbOnHovered   = cbOnHovered,
            .cbOnUnhovered = cbOnUnhovered,
            .cbOnPressed   = cbOnPressed,
            .cbOnReleased  = cbOnReleased,
            .context       = (void*)itCtx,
            .id            = IT
        };

        s_flagsLayer = mtl::make_unique<ui_layer>();
        pEnglish     = s_flagsLayer->add_hotspot(englishFlag);
        pItalian     = s_flagsLayer->add_hotspot(italianFlag);

        s_flagsLayer->set_enabled(true);
        s_flagsLayer->set_update_outside_bounds(true);
    }

    void langSelectProcess(void)
    {
        mousePointerUpdate();

        s_flagsLayer->update();

        ULONG enState = CONTEXT_GET_STATE((uint16_t)(ULONG)pEnglish->context);
        screenBlitCopy(g_mainScreen,
            s_pFlagsAtlas.get(),
            s_flags[enState].uwX,
            s_flags[enState].uwY,
            pEnglish->bounds.uwX,
            pEnglish->bounds.uwY,
            FLAG_WIDTH,
            FLAG_HEIGHT,
            MINTERM_COOKIE);

        ULONG itState = CONTEXT_GET_STATE((uint16_t)(ULONG)pItalian->context);
        screenBlitCopy(g_mainScreen,
            s_pFlagsAtlas.get(),
            s_flags[itState].uwX,
            s_flags[itState].uwY,
            pItalian->bounds.uwX,
            pItalian->bounds.uwY,
            FLAG_WIDTH,
            FLAG_HEIGHT,
            MINTERM_COOKIE);
    }

    void langSelectDestroy(void)
    {
        ACE_TRACE();

        s_flagsLayer.release();
        s_pFlagsAtlas.release();

        mousePointerDestroy();
    }

    tState g_stateLangSelect = {
        .cbCreate  = langSelectCreate,
        .cbLoop    = langSelectProcess,
        .cbDestroy = langSelectDestroy,
    };
}  // namespace NEONengine