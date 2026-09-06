#ifndef __MESSAGE_REQUEST_H__INCLUDED__
#define __MESSAGE_REQUEST_H__INCLUDED__

#include "core/text_render.h"
#include "types.h"
#include "utils/bstr_view.h"

namespace NEONengine
{
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

}  // namespace NEONengine

#endif
