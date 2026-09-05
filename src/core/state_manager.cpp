#include "state_manager.h"

namespace NEONengine
{
    state_manager::~state_manager() { clear(); }

    void state_manager::clear()
    {
        if (_phase != phase::idle)
        {
            fail("Cannot clear states during a callback");
            return;
        }
        _started = true;
        if (_result != state_update_result::error) _result = state_update_result::finished;
        _phase = phase::clearing;
        _transition = transition::none;
        _pending.reset(nullptr);
        while (_top) remove_top();
        _phase = phase::idle;
    }

    void state_control::pop()
    {
        _manager.request(state_manager::transition::pop);
    }

    bool state_manager::request(transition operation)
    {
        if (_result == state_update_result::error) return false;
        if (_phase != phase::updating)
        {
            fail("State control used outside update");
            return false;
        }
        if (_transition != transition::none)
        {
            fail("More than one state transition requested");
            return false;
        }
        _transition = operation;
        return true;
    }

    void state_manager::remove_top()
    {
        auto* previous = _top->_previous.release();
        _top.reset(previous);
    }

    void state_manager::apply_pending()
    {
        auto const operation = _transition;
        _transition = transition::none;
        if (operation == transition::none) return;

        if (operation == transition::pop)
        {
            remove_top();
            if (_result == state_update_result::ok && _top) _top->resumed();
            return;
        }

        if (operation == transition::replace) remove_top();
        else if (_top) _top->suspended();
        if (_result != state_update_result::ok) return;

        _pending->_previous = mtl::move(_top);
        _top = mtl::move(_pending);
        auto const result = _top->pushed();
        if (!result)
        {
            fail(result.error());
            remove_top();
        }
    }

    void state_manager::fail(char const* message)
    {
        if (_result == state_update_result::error) return;
        _result = state_update_result::error;
        if (!message || !*message) message = "State initialization failed";

        size_t length = 0;
        while (length + 1 < sizeof(_error) && message[length])
        {
            _error[length] = message[length];
            ++length;
        }
        _error[length] = '\0';
    }

    state_update_result state_manager::update()
    {
        if (_phase != phase::idle)
        {
            fail("State manager update is reentrant");
            return _result;
        }
        if (_result != state_update_result::ok) return _result;

        if (_transition != transition::none)
        {
            _phase = phase::transitioning;
            apply_pending();
            _phase = phase::idle;
            if (_result != state_update_result::ok) return _result;
        }
        if (!_top)
        {
            _started = true;
            _result = state_update_result::finished;
            return _result;
        }

        _phase = phase::updating;
        _top->update(_control);
        _phase = phase::idle;
        return _result;
    }
}
