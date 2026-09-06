#ifndef __OVERLAY_SYSTEM_H__INCLUDED__
#define __OVERLAY_SYSTEM_H__INCLUDED__

#include "core/text_render.h"
#include "types.h"
#include "utils/bstr_view.h"

namespace NEONengine
{
    class engine;
    class noir_vm;
    class state_control;

    constexpr u16 MESSAGE_MAX_WIDTH = 256;
    constexpr u16 MESSAGE_MARGIN = 8;
    constexpr u16 MESSAGE_CAPTION_GAP = 4;
    constexpr u8 MESSAGE_TEXT_COLOR = 1;
    constexpr u8 MESSAGE_ERROR_COLOR = 3;
    constexpr size_t MESSAGE_TEXT_CAPACITY = 512;
    constexpr size_t MESSAGE_CAPTION_CAPACITY = 32;

    enum class message_align : u8
    {
        top,
        middle,
        bottom,
    };

    struct message_request
    {
        bstr_view text{};
        bstr_view caption{};
        u16 max_width{ MESSAGE_MAX_WIDTH };
        text_justify justify{ text_justify::LEFT };
        u8 color{ MESSAGE_TEXT_COLOR };
        u8 caption_color{ MESSAGE_TEXT_COLOR };
        message_align align{ message_align::bottom };
        bool modal_wait{ false };
    };

    enum class overlay_kind : u8
    {
        none,
        message,
        dialogue,
    };

    class overlay_system
    {
        public:
        // The first request owns the slot until state entry. Text is copied.
        void post_message(message_request const& request) noexcept;
        void post_dialogue(u16 dialogue_id) noexcept;

        // Only the scene calls this. True includes a failed transition allocation.
        bool present(state_control& control, noir_vm& vm, engine& engine) noexcept;
        overlay_kind kind() const noexcept { return _kind; }

        void clear() noexcept
        {
            _kind = overlay_kind::none;
            _skipped_modal_wait = false;
        }

        private:
        void skip_modal_wait() noexcept;

        message_request _message{};
        u16 _dialogue{ 0 };
        overlay_kind _kind{ overlay_kind::none };
        bool _skipped_modal_wait{ false };
        char _text[MESSAGE_TEXT_CAPACITY]{};
        char _caption[MESSAGE_CAPTION_CAPACITY]{};
    };
}  // namespace NEONengine

#endif
