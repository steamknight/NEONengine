#ifndef __LAYER_H__INCLUDED__
#define __LAYER_H__INCLUDED__

#include <ace/types.h>

#include <mtl/list.h>
#include <mtl/memory.h>

#include "core/mouse_pointer.h"

namespace NEONengine
{
    struct hotspot;
    using cbHotspot  = void (*)(hotspot* self);
    using hotspot_id = UWORD;

    constexpr hotspot_id INVALID_HOTSPOT = 0xFFFF;

    enum class hotspot_state
    {
        IDLE,
        HOVERED,
        PRESSED
    };

    struct hotspot
    {
        tUwRect bounds;
        mouse_pointer pointerId;
        hotspot_state state;
        cbHotspot cbOnIdle;
        cbHotspot cbOnHovered;
        cbHotspot cbOnUnhovered;
        cbHotspot cbOnPressed;
        cbHotspot cbOnReleased;
        void* context;
        hotspot_id id;
    };

    class ui_layer
    {
        public:  //////////////////////////////////////////////////////////////////////////////////
        /**
         * @brief Update the layer and its contained sections.
         * By default the hotspots in the layer will not update unless the mouse is
         * inside the bounding area that contains all the hotspots.
         *
         * @see updates_outside_bounds()
         * @see set_update_outside_bounds()
         */
        void update() noexcept;

        bool is_enabled() const noexcept { return _is_enabled; };

        /**
         * @brief Enables or disabled a layer. A disabled layer will not update.
         *
         * @param enable 1 to enable, 0 to disable.
         */
        void set_enabled(bool enable) noexcept { _is_enabled = enable; };

        bool updates_outside_bounds() const noexcept { return _should_update_outside_bounds; };

        /**
         * @brief Changes when the layer will update. By default, it's not set and the
         * layer will only update if the mouse is over the bounding area encompassing
         * all hotspots. If set, it will update regardless.
         *
         * This could be useful if, while idling, the buttons need to do something
         * special, like updating an animation.
         *
         * @param update true for update outside of hotspot
         */
        void set_update_outside_bounds(bool update) noexcept
        {
            _should_update_outside_bounds = update;
        };

        /**
         * @brief Adds a new hotspot to the layer.
         * The hotspot data is copied to the layer, however, if using the context pointer,
         * it must be kept alive an it's the responsibility of the caller to free what
         * it points do.
         *
         * The function could potentially move the vertical location of the hotspot, so
         * if the hotspot is going to be used to display something on the screen,
         * it's best to request the one in the layer than using the hotspot structure
         * passed to this function.
         *
         * @param value The hotspot to add
         * @return hotspot_id An id that can be used to reference a specific hotspot. Will
         * be INVALID_HOTSPOT if it could not allocate storage for the new hotspot.
         *
         * @see get_hotspot()
         * @see remove_hotspot()
         */
        hotspot* add_hotspot(hotspot const& value);

        /**
         * @brief Gets a hotspot based on its hotspot id
         *
         * @param id Hotspot id received when adding the hotspot to the layer
         * @return Hotspot* A pointer ot the hotspot. It should not be modified. Retuns nullptr
         * if the no hotspot matches the id.
         *
         * @see add_hotspot()
         */
        hotspot* get_hotspot(hotspot_id id);

        /**
         * @brief Removes a hotspot from the layer.
         * If the context pointer was utilized, it must be freed by the caller.
         *
         * @param id Id of the hotspot to remove
         *
         * @see add_hotspot()
         */
        void remove_hotspot(hotspot_id);

        private:  //////////////////////////////////////////////////////////////////////////////////
        tUwRect _calculate_bounds();

        private:  //////////////////////////////////////////////////////////////////////////////////
        mtl::forward_list<hotspot> _hotspots;
        tUwRect _bounds{};
        bool _is_enabled{ false };
        bool _should_update_outside_bounds{ false };
        uint16_t _offsetY{ 28 };
    };

    using ui_layer_ptr = mtl::unique_ptr<ui_layer>;
}  // namespace NEONengine

#endif  // __LAYER_H__INCLUDED__