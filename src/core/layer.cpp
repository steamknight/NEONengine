#include "layer.h"

#include <ace/managers/log.h>
#include <ace/managers/memory.h>
#include <ace/managers/mouse.h>

#include "core/mouse_pointer.h"
#include "core/screen.h"
#include "neonengine.h"

#include "mtl/utility.h"

namespace NEONengine
{
    using namespace mtl;
    
    enum class HotspotState
    {
        IDLE,
        HOVERED,
        PRESSED
    };

    struct HotspotInternal
    {
        HotspotId id;
        Hotspot hotspot;
        enum HotspotState state;
        struct HotspotInternal *pNext;
    };

    struct Layer
    {
        tUwRect bounds;
        UWORD uwHotspotCount;
        HotspotId nextHotspotId;
        HotspotInternal *pFirstHotspot;
        UBYTE ubIsEnabled;
        UBYTE ubUpdateOutsideBounds;
        UWORD uwOffsetY;
    };

    tUwRect calculateLayerBounds(Layer * pLayer);

    Layer* layerCreate()
    {
        logBlockBegin("layerCreate");
        Layer *pLayer = (Layer*)memAllocFastClear(sizeof(Layer));
        pLayer->uwOffsetY = systemIsPal() ? 28 : 0;
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

        HotspotInternal *pCurrent = pLayer->pFirstHotspot;
        UBYTE ubMousePressed = mouseCheck(MOUSE_PORT_1, MOUSE_LMB);
        MousePointer mousePointerId = MousePointer::POINTER;
        while (pCurrent)
        {
            UBYTE ubOverHotspot = mouseInRect(MOUSE_PORT_1, pCurrent->hotspot.bounds);
            if (ubOverHotspot)
            {
                mousePointerId = pCurrent->hotspot.pointer;
            }

            switch (pCurrent->state)
            {
                case HotspotState::IDLE:
                    if (ubOverHotspot)
                    {
                        if (ubMousePressed)
                        {
                            /*
                            * If the mouse is pressed while hovering, go straight to
                            * pressed.
                            */
                            pCurrent->state = HotspotState::PRESSED;
                            SAFE_CB_CALL(pCurrent->hotspot.cbOnPressed, &pCurrent->hotspot);
                        }
                        else
                        {
                            /*
                            * If hovering without pressing, just go to hovered.
                            */
                            pCurrent->state = HotspotState::HOVERED;
                            SAFE_CB_CALL(pCurrent->hotspot.cbOnHovered, &pCurrent->hotspot);
                        }
                    }
                    else
                    {
                        /*
                        * If not hovered, keep idling.
                        */
                        SAFE_CB_CALL(pCurrent->hotspot.cbOnIdle, &pCurrent->hotspot);
                    }
                    break;

                case HotspotState::HOVERED:
                    if (!ubOverHotspot)
                    {
                        /*
                        * Regardless of mouse state, if we are no longer on the
                        * hotspot, it should go back to idle. Note that "hot-hovered"
                        * and "pressed" are not a valid combination for this state.
                        */
                        pCurrent->state = HotspotState::IDLE;
                        SAFE_CB_CALL(pCurrent->hotspot.cbOnUnhovered ,&pCurrent->hotspot);
                    }
                    else if (ubOverHotspot && ubMousePressed)
                    {
                        /*
                        * If hovering and pressed, then the hotspot was pressed.
                        */
                        pCurrent->state = HotspotState::PRESSED;
                        SAFE_CB_CALL(pCurrent->hotspot.cbOnPressed, &pCurrent->hotspot);
                    }
                    break;

                case HotspotState::PRESSED:
                    if (ubOverHotspot)
                    {
                        if (ubMousePressed)
                        {
                            /*
                            * If the mouse is pressed and we're over the hotspot, stop
                            * processing so we don't keep sending onPressed events.
                            */
                            break;
                        }
                        else
                        {
                            /*
                            * If the mouse is not pressed and we're over the hotspot,
                            * we must have released.
                            */
                            pCurrent->state = HotspotState::HOVERED;
                            SAFE_CB_CALL(pCurrent->hotspot.cbOnReleased, &pCurrent->hotspot);
                        }
                    }
                    else
                    {
                        /*
                        * Regardless of the mouse state, if we move off the hotspot
                        * it should be considered idle. This allows us to "cancel",
                        * a press on hotspot my moving off of it. If we're no longer
                        * over the hotspot, we must unhover
                        */
                        pCurrent->state = HotspotState::IDLE;
                        SAFE_CB_CALL(pCurrent->hotspot.cbOnUnhovered, &pCurrent->hotspot);
                    }

                    break;

                default:
                    logWrite("layerUpdate: Unknown hotspot state %d", to<int>(pCurrent->state));
                    break;
            };

            // Lastly, if the hotspot is either hovered or pressed, changed the mouse
            // pointer.

            pCurrent = pCurrent->pNext;
        }

        mousePointerSwitch(mousePointerId);
    }

    void layerDestroy(Layer* pLayer)
    {
        logBlockBegin("layerDestroy");
        if (pLayer)
        {
            HotspotInternal *pCurrent = pLayer->pFirstHotspot;
            while (pCurrent)
            {
                HotspotInternal *pNext = pCurrent->pNext;
                memFree(pCurrent, sizeof(HotspotInternal));

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
            return;
        }

        pLayer->ubIsEnabled = ubIsEnabled;
    }

    void layerSetUpdateOutsideBounds(Layer *pLayer, UBYTE ubUpdateOutsideBounds)
    {
        if (!pLayer)
        {
            logWrite("layerSetUpdateOutsideBounds: layer cannot be null");
            return;
        }

        pLayer->ubUpdateOutsideBounds = ubUpdateOutsideBounds;
    }

    HotspotId layerAddHotspot(Layer *pLayer, Hotspot *pHotspot)
    {
        HotspotInternal * pLastHotspot = NULL;

        logBlockBegin("layerAddHotspot");
        if (!pLayer)
        {
            logWrite("layerAddHotspot: layer cannot be null");
            return INVALID_REGION;
        }

        if (!pHotspot)
        {
            logWrite("layerAddHotspot: hotspot cannot be null");
            return INVALID_REGION;
        }

        HotspotInternal *pNewHotspot = (HotspotInternal*)memAllocFastClear(sizeof(HotspotInternal));
        if (!pNewHotspot)
        {
            logWrite("layerAddHotspot: Unable to allocate memory for hotspot");
            return INVALID_REGION;
        }

        pNewHotspot->id = pLayer->nextHotspotId;
        pLayer->nextHotspotId++;

        memcpy(&pNewHotspot->hotspot, pHotspot, sizeof(Hotspot));
        pNewHotspot->pNext = 0;
        pNewHotspot->hotspot.bounds.uwY += pLayer->uwOffsetY;

        if (!pLayer->pFirstHotspot)
        {
            pLayer->pFirstHotspot = pNewHotspot;
        }
        else
        {
            pLastHotspot = pLayer->pFirstHotspot;
            while (pLastHotspot->pNext)
            {
                pLastHotspot = pLastHotspot->pNext;
            }

            pLastHotspot->pNext = pNewHotspot;
        }

        pLayer->bounds = calculateLayerBounds(pLayer);

        logBlockEnd("layerAddHotspot");

        return pNewHotspot->id;
    }

    const Hotspot *layerGetHotspot(Layer *pLayer, HotspotId id)
    {
        if (!pLayer)
        {
            logWrite("layerGetHotspot: layer cannot be null");
            return 0;
        }

        HotspotInternal *pCurrent = pLayer->pFirstHotspot;
        while (pCurrent && pCurrent->id != id)
        {
            pCurrent = pCurrent->pNext;
        }

        return (pCurrent) ? &pCurrent->hotspot : nullptr;
    }

    void layerRemoveHotspot(Layer *pLayer, HotspotId id)
    {
        logBlockBegin("layerRemoveHotspot");
        if (!pLayer)
        {
            logWrite("layerRemoveHotspot: layer cannot be null");
            return;
        }

        HotspotInternal *pPrev, *pCurrent;
        pCurrent = pLayer->pFirstHotspot;

        if (pCurrent->id == id)
        {
            pLayer->pFirstHotspot = pCurrent->pNext;
            memFree(pCurrent, sizeof(HotspotInternal));
            return;
        }

        while(pCurrent != 0 && pCurrent->id != id)
        {
            pPrev = pCurrent;
            pCurrent = pCurrent->pNext;
        }

        if (pCurrent == 0)
        {
            logWrite("layerRemoveHotspot: no hotspot with id %d", id);
            return;
        }

        pPrev->pNext = pCurrent->pNext;

        pLayer->bounds = calculateLayerBounds(pLayer);

        logBlockEnd("layerRemoveHotspot");
    }

    /*
    * Internal function.
    * Assumptions:
    *   - layer is not null
    *   - layer hast at least one hotspot
    */
    tUwRect calculateLayerBounds(Layer *pLayer)
    {
        tUwRect *pBounds = &pLayer->pFirstHotspot->hotspot.bounds;

        UWORD minX = pBounds->uwX;
        UWORD minY = pBounds->uwY;
        UWORD maxX = minX + pBounds->uwWidth;
        UWORD maxY = minY + pBounds->uwHeight;

        HotspotInternal *pLastHotspot = pLayer->pFirstHotspot->pNext;
        while (pLastHotspot)
        {
            pBounds = &pLastHotspot->hotspot.bounds;
            minX = MIN(minX, pBounds->uwX);
            minY = MIN(minY, pBounds->uwY);
            maxX = MAX(maxX, pBounds->uwX + pBounds->uwWidth);
            maxY = MAX(maxY, pBounds->uwY + pBounds->uwHeight);

            pLastHotspot = pLastHotspot->pNext;
        }

        return (tUwRect) {
            .uwY = minY, .uwX = minX,
            .uwWidth = static_cast<UWORD>(maxX - minX),
            .uwHeight = static_cast<UWORD>(maxY - minY)
        };
    }
}