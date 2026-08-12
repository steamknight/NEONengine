#include "core/overlay_system.h"

#include "core/logger.h"
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
        if (_kind != overlay_kind::none || request.text.is_empty()) return;

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
        if (_kind != overlay_kind::none) return;

        _dialogue = dialogue_id;
        _kind     = overlay_kind::dialogue;
    }

    void overlay_system::present(tStateManager* states) noexcept
    {
        if (_kind == overlay_kind::none || !_message_state || !_dialogue_state) return;

        tState const* const on_top = states->pCurrent;
        if (on_top == _message_state || on_top == _dialogue_state) return;

        switch (_kind)
        {
            case overlay_kind::message: statePush(states, _message_state); break;
            case overlay_kind::dialogue: statePush(states, _dialogue_state); break;
            case overlay_kind::none: break;
        }
    }

    message_request overlay_system::take_message() noexcept
    {
        if (_kind != overlay_kind::message) return message_request{};

        clear();
        return _message;
    }

    u16 overlay_system::take_dialogue() noexcept
    {
        if (_kind != overlay_kind::dialogue) return 0;

        clear();
        return _dialogue;
    }
}  // namespace NEONengine
