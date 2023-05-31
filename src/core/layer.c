#include "layer.h"

#include <ace/managers/log.h>
#include <ace/managers/memory.h>
#include <ace/managers/mouse.h>

#include "core/screen.h"
#include "neonengine.h"

enum eRegionState
{
    REGION_IDLE,
    REGION_HOVERED,
    REGION_PRESSED
};

typedef struct _RegionInternal
{
    RegionId id;
    Region region;
    enum eRegionState state;
    struct _RegionInternal *pNext;
} RegionInternal;

typedef struct _Layer
{
    tUwRect bounds;
    UWORD uwRegionCount;
    RegionId nextRegionId;
    RegionInternal *pFirstRegion;
    UBYTE ubIsEnabled;
    UBYTE ubUpdateOutsideBounds;
} Layer;

tUwRect calculateLayerBounds(Layer * pLayer);

Layer* layerCreate(void)
{
    logBlockBegin("layerCreate");
    Layer *pLayer = memAllocFastClear(sizeof(Layer));
    logBlockEnd("layerCreate");

    return pLayer;
}

void layerUpdate(Layer* pLayer)
{
    //logBlockBegin("layerUpdate");
    if (!pLayer)
    {
        logWrite("layerUpdate: layer cannot be null");
        return;
    }

    if (
        !pLayer->ubIsEnabled ||
        (!pLayer->ubUpdateOutsideBounds && !mouseInRect(MOUSE_PORT_1, pLayer->bounds)))
    {
        return;
    }

    RegionInternal *pCurrent = pLayer->pFirstRegion;
    UBYTE ubMousePressed = mouseCheck(MOUSE_PORT_1, MOUSE_LMB);
    while (pCurrent)
    {
        UBYTE ubOverRegion = mouseInRect(MOUSE_PORT_1, pCurrent->region.bounds);
        switch (pCurrent->state)
        {
            case REGION_IDLE:
                if (ubOverRegion)
                {
                    if (ubMousePressed)
                    {
                        /*
                         * If the mouse is pressed while hovering, go straight to
                         * pressed.
                         */
                        pCurrent->state = REGION_PRESSED;
                        SAFE_CB_CALL(pCurrent->region.cbOnPressed, &pCurrent->region);
                    }
                    else
                    {
                        /*
                         * If hovering without pressing, just go to hovered.
                         */
                        pCurrent->state = REGION_HOVERED;
                        SAFE_CB_CALL(pCurrent->region.cbOnHovered, &pCurrent->region);
                    }
                }
                else
                {
                    /*
                     * If not hovered, keep idling.
                     */
                    SAFE_CB_CALL(pCurrent->region.cbOnIdle, &pCurrent->region);
                }
                break;

            case REGION_HOVERED:
                if (!ubOverRegion)
                {
                    /*
                     * Regardless of mouse state, if we are no longer on the
                     * region, it should go back to idle. Note that "hot-hovered"
                     * and "pressed" are not a valid combination for this state.
                     */
                    pCurrent->state = REGION_IDLE;
                    SAFE_CB_CALL(pCurrent->region.cbOnUnhovered ,&pCurrent->region);
                }
                else if (ubOverRegion && ubMousePressed)
                {
                    /*
                     * If hovering and pressed, then the region was pressed.
                     */
                    pCurrent->state = REGION_PRESSED;
                    SAFE_CB_CALL(pCurrent->region.cbOnPressed, &pCurrent->region);
                }
                break;

            case REGION_PRESSED:
                if (ubOverRegion)
                {
                    if (ubMousePressed)
                    {
                        /*
                        * If the mouse is pressed and we're over the region, stop
                        * processing so we don't keep sending onPressed events.
                        */
                        break;
                    }
                    else
                    {
                        /* 
                        * If the mouse is not pressed and we're over the region,
                        * we must have released.
                        */
                        pCurrent->state = REGION_HOVERED;
                        SAFE_CB_CALL(pCurrent->region.cbOnReleased, &pCurrent->region);
                    }
                }
                else
                {
                    /*
                     * Regardless of the mouse state, if we move off the region
                     * it should be considered idle. This allows us to "cancel",
                     * a press on region my moving off of it. If we're no longer
                     * over the region, we must unhover
                     */
                    pCurrent->state = REGION_IDLE;
                    SAFE_CB_CALL(pCurrent->region.cbOnUnhovered, &pCurrent->region);
                }

                break;

            default:
                logWrite("layerUpdate: Unknown region state %d", pCurrent->state);
                break;
        };

        pCurrent = pCurrent->pNext;
    }
}

void layerDestroy(Layer* pLayer)
{
    logBlockBegin("layerDestroy");
    if (pLayer)
    {
        RegionInternal *pCurrent = pLayer->pFirstRegion;
        while (pCurrent)
        {
            RegionInternal *pNext = pCurrent->pNext;
            memFree(pCurrent, sizeof(RegionInternal));

            pCurrent = pNext;
        }

        memFree(pLayer, sizeof(Layer));
    }

    logBlockEnd("layerDestroy");
}

void layerSetEnable(Layer *pLayer, UBYTE ubIsEnabled)
{
    if (!pLayer)
    {
        logWrite("layerSetEnable: layer cannot be null");
    }

    pLayer->ubIsEnabled = ubIsEnabled;
}

void layerSetUpdateOutsideBounds(Layer *pLayer, UBYTE ubUpdateOutsideBounds)
{
    if (!pLayer)
    {
        logWrite("layerSetUpdateOutsideBounds: layer cannot be null");
    }

    pLayer->ubUpdateOutsideBounds = ubUpdateOutsideBounds;
}

RegionId layerAddRegion(Layer *pLayer, Region *pRegion)
{
    RegionInternal * pLastRegion = NULL;

    logBlockBegin("layerAddRegion");
    if (!pLayer)
    {
        logWrite("layerAddRegion: layer cannot be null");
        return INVALID_REGION;
    }

    if (!pRegion)
    {
        logWrite("layerAddRegion: region cannot be null");
        return INVALID_REGION;
    }

    RegionInternal *pNewRegion = memAllocFastClear(sizeof(RegionInternal));
    if (!pNewRegion)
    {
        logWrite("layerAddRegion: Unable to allocate memory for region");
        return INVALID_REGION;
    }

    pNewRegion->id = pLayer->nextRegionId;
    pLayer->nextRegionId++;

    memcpy(&pNewRegion->region, pRegion, sizeof(Region));
    pNewRegion->region.bounds.uwY += g_mainScreen->uwOffset;
    pNewRegion->pNext = 0;

    if (!pLayer->pFirstRegion)
    {
        pLayer->pFirstRegion = pNewRegion;
    }
    else
    {
        pLastRegion = pLayer->pFirstRegion;
        while (pLastRegion->pNext)
        {
            pLastRegion = pLastRegion->pNext;
        }

        pLastRegion->pNext = pNewRegion;
    }

    pLayer->bounds = calculateLayerBounds(pLayer);

    logBlockEnd("layerAddRegion");

    return pNewRegion->id;
}

const Region *layerGetRegion(Layer *pLayer, RegionId id)
{
    if (!pLayer)
    {
        logWrite("layerGetRegion: layer cannot be null");
        return 0;
    }

    RegionInternal *pCurrent = pLayer->pFirstRegion;
    while (pCurrent && pCurrent->id != id)
    {
        pCurrent = pCurrent->pNext;
    }

    return (pCurrent) ? &pCurrent->region : 0;
}

void layerRemoveRegion(Layer *pLayer, RegionId id)
{
    logBlockBegin("layerRemoveRegion");
    if (!pLayer)
    {
        logWrite("layerRemoveRegion: layer cannot be null");
        return;
    }

    RegionInternal *pPrev, *pCurrent;
    pCurrent = pLayer->pFirstRegion;

    if (pCurrent->id == id)
    {
        pLayer->pFirstRegion = pCurrent->pNext;
        memFree(pCurrent, sizeof(RegionInternal));
        return;
    }

    while(pCurrent != 0 && pCurrent->id != id)
    {
        pPrev = pCurrent;
        pCurrent = pCurrent->pNext;
    }

    if (pCurrent == 0)
    {
        logWrite("layerRemoveRegion: no region with id %d", id);
        return;
    }

    pPrev->pNext = pCurrent->pNext;

    pLayer->bounds = calculateLayerBounds(pLayer);

    logBlockEnd("layerRemoveRegion");
}

/*
 * Internal function.
 * Assumptions:
 *   - layer is not null
 *   - layer hast at least one region
 */
tUwRect calculateLayerBounds(Layer *pLayer)
{
    tUwRect *pBounds = &pLayer->pFirstRegion->region.bounds;

    UWORD minX = pBounds->uwX;
    UWORD minY = pBounds->uwY;
    UWORD maxX = minX + pBounds->uwWidth;
    UWORD maxY = minY + pBounds->uwHeight;

    RegionInternal *pLastRegion = pLayer->pFirstRegion->pNext;
    while (pLastRegion)
    {
        pBounds = &pLastRegion->region.bounds;
        minX = MIN(minX, pBounds->uwX);
        minY = MIN(minY, pBounds->uwY);
        maxX = MAX(maxX, pBounds->uwX + pBounds->uwWidth);
        maxY = MAX(maxY, pBounds->uwY + pBounds->uwHeight);

        pLastRegion = pLastRegion->pNext;
    }

    return (tUwRect) {
        .uwX = minX, .uwY = minY,
        .uwWidth = maxX - minX, .uwHeight = maxY - minY
    };
}
