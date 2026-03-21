#include "layer.h"

#include "neonengine.h"

#include <ace/managers/blit.h>
#include <ace/managers/log.h>
#include <ace/managers/memory.h>
#include <ace/managers/mouse.h>

#include <ace++/log.h>

#include <mtl/utility.h>

#include "core/mouse_pointer.h"
#include "core/screen.h"

namespace NEONengine
{
    using namespace mtl;

    void ui_layer::update() noexcept
    {
        if (!_is_enabled || (!_should_update_outside_bounds && !mouseInRect(MOUSE_PORT_1, _bounds)))
        {
            return;
        }

        UBYTE isMousePressed         = mouseCheck(MOUSE_PORT_1, MOUSE_LMB);
        mouse_pointer mousePointerId = mouse_pointer::POINTER;

        blitRect(screenGetBackBuffer(g_mainScreen),
            _bounds.uwX + 20,
            _bounds.uwY,
            _bounds.uwWidth,
            _bounds.uwHeight,
            1);

        // auto color = 1;
        for (auto& hotspot : _hotspots)
        {
            // blitRect(screenGetBackBuffer(g_mainScreen),
            //     hotspot.bounds.uwX + 20,
            //     hotspot.bounds.uwY + _offsetY,
            //     hotspot.bounds.uwWidth,
            //     hotspot.bounds.uwHeight,
            //     color++);

            auto isOverHotspot = mouseInRect(MOUSE_PORT_1, hotspot.bounds);
            if (isOverHotspot)
            {
                mousePointerId = hotspot.pointerId;
            }

            // Simplified state machine using current conditions
            auto currentState = hotspot.state;

            if (isOverHotspot)
            {
                switch (currentState)
                {
                    case hotspot_state::IDLE:
                        if (isMousePressed)
                        {
                            hotspot.state = hotspot_state::PRESSED;
                            SAFE_CB_CALL(hotspot.cbOnPressed, &hotspot);
                        }
                        else
                        {
                            hotspot.state = hotspot_state::HOVERED;
                            SAFE_CB_CALL(hotspot.cbOnHovered, &hotspot);
                        }
                        break;

                    case hotspot_state::HOVERED:
                        if (isMousePressed)
                        {
                            hotspot.state = hotspot_state::PRESSED;
                            SAFE_CB_CALL(hotspot.cbOnPressed, &hotspot);
                        }
                        // Stay hovered if not pressed
                        break;

                    case hotspot_state::PRESSED:
                        if (!isMousePressed)
                        {
                            hotspot.state = hotspot_state::HOVERED;
                            SAFE_CB_CALL(hotspot.cbOnReleased, &hotspot);
                        }
                        // Stay pressed if still pressing
                        break;
                }
            }
            else  // Not over hotspot
            {
                switch (currentState)
                {
                    case hotspot_state::IDLE: SAFE_CB_CALL(hotspot.cbOnIdle, &hotspot); break;

                    case hotspot_state::HOVERED:
                    case hotspot_state::PRESSED:
                        hotspot.state = hotspot_state::IDLE;
                        SAFE_CB_CALL(hotspot.cbOnUnhovered, &hotspot);
                        break;
                }
            }
        }

        mousePointerSwitch(mousePointerId);
    }

    hotspot* ui_layer::add_hotspot(hotspot const& value)
    {
        ACE_TRACE();

        _hotspots.push_back(value);
        _hotspots.back().bounds.uwY += _offsetY;
        _bounds = _calculate_bounds();

        return &_hotspots.back();
    }

    hotspot* ui_layer::get_hotspot(hotspot_id id)
    {
        ACE_TRACE();
        return (id < _hotspots.size()) ? &_hotspots[mtl::to<size_t>(id)] : nullptr;
    }

    void ui_layer::remove_hotspot(hotspot_id id)
    {
        ACE_TRACE();

        auto pPrev    = _hotspots.begin();
        auto pCurrent = _hotspots.begin();

        while (pCurrent != _hotspots.end() && pCurrent->id != id)
        {
            pPrev = pCurrent;
            ++pCurrent;
        }

        if (pCurrent == _hotspots.end()) return;

        if (pCurrent == _hotspots.begin())
        {
            _hotspots.pop_front();
        }
        else
        {
            _hotspots.erase_after(pPrev);
        }

        _bounds = _calculate_bounds();
    }

    tUwRect ui_layer::_calculate_bounds()
    {
        auto bounds = _hotspots.front().bounds;

        auto minX = bounds.uwX;
        auto minY = bounds.uwY;
        auto maxX = minX + bounds.uwWidth;
        auto maxY = minY + bounds.uwHeight;

        for (auto const& current : _hotspots)
        {
            auto bound = current.bounds;
            minX       = MIN(minX, bound.uwX);
            minY       = MIN(minY, bound.uwY);
            maxX       = MAX(maxX, bound.uwX + bound.uwWidth);
            maxY       = MAX(maxY, bound.uwY + bound.uwHeight);
        }

        return { .uwY = mtl::to<UWORD>(minY),
            .uwX      = minX,
            .uwWidth  = mtl::to<UWORD>(maxX - minX),
            .uwHeight = mtl::to<UWORD>(maxY - minY) };
    }
}  // namespace NEONengine