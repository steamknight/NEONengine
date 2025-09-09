#ifndef __LAYER_H__INCLUDED__
#define __LAYER_H__INCLUDED__

#include <ace/types.h>

#include "core/mouse_pointer.h"

namespace NEONengine
{
    struct Hotspot;
    using cbHotspot = void (*)(Hotspot *self);
    using HotspotId = UWORD;

    struct Layer;

    #define INVALID_REGION 0xFFFF

    struct Hotspot
    {
        tUwRect bounds;
        mouse_pointer pointer;
        cbHotspot cbOnIdle;
        cbHotspot cbOnHovered;
        cbHotspot cbOnUnhovered;
        cbHotspot cbOnPressed;
        cbHotspot cbOnReleased;
        void *context;
    };

    /**
     * @brief Creates a new layer.
     * 
     * @return Layer* The new layer
     * 
     * @see layerUpdate()
     * @see layerDestroy()
     */
    Layer *layerCreate(void);

    /**
     * @brief Update the layer and its contained sections.
     * By default the regions in the layer will not update unless the mouse is
     * inside the bounding area that contains all the regions.
     * 
     * @param pLayer The layer to update
     * 
     * @see layerSetUpdateOutsideBounds()
     */
    void layerUpdate(Layer *pLayer);

    /**
     * @brief Destroys the given layer and all the regions inside.
     * 
     * @param pLayer The layer to destroy
     * 
     * @see layerCreate()
     */
    void layerDestroy(Layer *pLayer);

    /**
     * @brief Enables or disabled a layer. A disabled layer will not update.
     * 
     * @param pLayer Layer to enable or disable
     * @param ubIsEnabled 1 to enable, 0 to disable.
     */
    void layerSetEnable(Layer *pLayer, UBYTE ubIsEnabled);

    /**
     * @brief Changes when the layer will update. By default, it's not set and the
     * layer will only update if the mouse is over the bounding area encompassing 
     * all regions. If set, it will update regardless.
     * 
     * This could be useful if, while idling, the buttons need to do something
     * special, like updating an animation.
     * 
     * @param pLayer The layer to set the flag on
     * @param ubUpdateOutsideBounds 1 for update outside of region
     */
    void layerSetUpdateOutsideBounds(Layer *pLayer, UBYTE ubUpdateOutsideBounds);

    /**
     * @brief Adds a new region to the layer.
     * The region data is copied to the layer, however, if using the context pointer,
     * it must be kept alive an it's the responsibility of the caller to free what
     * it points do.
     * 
     * The function could potentially move the vertical location of the region, so
     * if the region is going to be used to display something on the screen,
     * it's best to request the one in the layer than using the region structure
     * passed to this function.
     * 
     * @param pLayer Layer to add a region to
     * @param pHotspot The region to add
     * @return HotspotId An id that can be used to reference a specific region. Will
     * be INVALID_REGION if it could not allocate storage for the new region.
     * 
     * @see layerGetHotspot()
     * @see layerRemoveHotspot()
     */
    HotspotId layerAddHotspot(Layer *pLayer, Hotspot *pHotspot);

    /**
     * @brief Gets a region based on its region id
     * 
     * @param pLayer Layer containing the region
     * @param id Hotspot id received when adding the region to the layer
     * @return Hotspot* A pointer ot the region. It should not be modified. Retuns 0
     * if the no region matches the id.
     * 
     * @see layerAddHotspot()
     */
    const Hotspot* layerGetHotspot(Layer *pLayer, HotspotId id);

    /**
     * @brief Removes a region from the layer.
     * If the context pointer was utilized, it must be freed by the caller.
     * 
     * @param pLayer Layer to remove a region from
     * @param id Id of the region to remove
     * 
     * @see layerAddHotspot()
     */
    void layerRemoveHotspot(Layer *pLayer, HotspotId id);
}

#endif // __LAYER_H__INCLUDED__