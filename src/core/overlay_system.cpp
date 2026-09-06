#include "core/overlay_system.h"

#include "core/logger.h"
#include "core/noir_vm.h"
#include "core/state_manager.h"
#include "states/dialogue.h"
#include "states/message.h"
#include "types.h"

NE_LOG_DOMAIN(Text);

namespace NEONengine
{
    namespace
    {
        /**
         * @brief Copies as much of @p source into @p buffer as it holds.
         *
         * @return A view of the copy, terminated so it can be read as a C string.
         */
        bstr_view copy_into(char* buffer, size_t capacity, bstr_view source) noexcept
        {
            size_t const limit  = capacity - 1;
            size_t const length = source.length() < limit ? source.length() : limit;

            for (size_t i = 0; i < length; ++i) buffer[i] = source.data()[i];
            buffer[length] = '\0';

            return bstr_view(buffer, length);
        }
    }  // namespace

    void overlay_system::post_message(message_request const& request) noexcept
    {
        if (_kind != overlay_kind::none || request.text.is_empty())
        {
            if (request.modal_wait) skip_modal_wait();
            return;
        }

        if (request.text.length() >= MESSAGE_TEXT_CAPACITY)
        {
            NE_LOG("message of %lu bytes cut to %lu",
                to<ULONG>(request.text.length()),
                to<ULONG>(MESSAGE_TEXT_CAPACITY - 1));
        }

        _message         = request;
        _message.text    = copy_into(_text, sizeof(_text), request.text);
        _message.caption = copy_into(_caption, sizeof(_caption), request.caption);
        _kind            = overlay_kind::message;
    }

    void overlay_system::post_dialogue(u16 dialogue_id) noexcept
    {
        if (_kind != overlay_kind::none)
        {
            skip_modal_wait();
            return;
        }

        _dialogue = dialogue_id;
        _kind     = overlay_kind::dialogue;
    }

    void overlay_system::skip_modal_wait() noexcept
    {
        NE_LOG("modal request skipped: empty text or an overlay is already pending");
        if (_kind == overlay_kind::none
            || (_kind == overlay_kind::message && !_message.modal_wait))
            _skipped_modal_wait = true;
    }

    bool overlay_system::present(state_control& control, noir_vm& vm, engine& engine) noexcept
    {
        // Bridge callbacks precede the VM's modal latch, so finish skipped calls here.
        if (_skipped_modal_wait)
        {
            _skipped_modal_wait = false;
            vm.resume_execution();
        }

        switch (_kind)
        {
            case overlay_kind::message:
                control.push<message_state>(engine, vm, *this, _message);
                return true;
            case overlay_kind::dialogue:
                control.push<dialogue_state>(engine, vm, *this, _dialogue);
                return true;
            case overlay_kind::none:
                return false;
        }
        return false;
    }
}  // namespace NEONengine
