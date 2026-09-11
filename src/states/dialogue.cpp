#include "dialogue.h"

#include "neonengine.h"

#include <ace/managers/game.h>

#include <mtl/utility.h>

#include "core/logger.h"


NE_LOG_DOMAIN(Dialogue);

namespace NEONengine
{
    dialogue_state::dialogue_state(engine& engine, noir_vm& vm, u16 dialogue_id, modal_wait wait) noexcept
        : _engine(engine)
        , _vm(vm)
        , _wait(wait)
        , _dialogue_id(dialogue_id)
        , _context{ engine.session(), vm }
        , _presentation(engine)
    {
    }

    mtl::expected<void, char const*> dialogue_state::pushed()
    {
        _session.create(_engine.data().dialogues(), _engine.data().pages(),
            _engine.data().choices(), &engine_dialogue_bridge(), &_context);
    
        if (!_session.begin(_dialogue_id) || _vm.terminated())
        {
            return {};
        }

        auto result = _presentation.open(_session);
        if (!result) return mtl::unexpected(result.error());

        _engine.pointer().switch_to(pointer_role::WAIT);

        return {};
    }

    void dialogue_state::update(state_control& control)
    {
        if (!_presentation.is_open() || _session.has_ended() || _vm.terminated())
        {
            control.pop();
            return;
        }

        auto const action = _presentation.update();
        switch (action.kind)
        {
            case dialogue_action_kind::NONE:
                return;
            case dialogue_action_kind::ADVANCE:
                _session.advance();
                break;
            case dialogue_action_kind::TAKE_CHOICE:
                _session.take_choice(action.choice);
                break;
            case dialogue_action_kind::ROTATE_CHOICES:
                _session.rotate_choices();
                break;
        }

        if (_session.has_ended() || _vm.terminated())
        {
            control.pop();
            return;
        }

        auto result = action.kind == dialogue_action_kind::ROTATE_CHOICES
            ? _presentation.redraw(_session)
            : _presentation.refresh(_session);

        if (!result)
        {
            NE_LOG_ALWAYS("Fatal dialogue error: %s", result.error());
            // TODO: report the error to the state manager.
            return;
        }

        if (!result.value())
        {
            control.pop();
            return;
        }
    }

    dialogue_state::~dialogue_state()
    {
        // Member destruction comes too late to restore the display before completing the wait.
        _presentation.close();
        _wait.complete();
    }
}  // namespace NEONengine
