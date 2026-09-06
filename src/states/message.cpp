#include "message.h"

#include "neonengine.h"

#include <ace/managers/blit.h>
#include <ace/managers/key.h>
#include <ace/managers/mouse.h>

namespace NEONengine
{
    namespace
    {
        bstr_view copy_text(char* target, size_t capacity, bstr_view source) noexcept
        {
            size_t const length = source.length() < capacity - 1 ? source.length() : capacity - 1;
            for (size_t i = 0; i < length; ++i) target[i] = source.data()[i];
            target[length] = '\0';
            return bstr_view(target, length);
        }

        u16 top_edge_for(message_align align, u16 height)
        {
            if (height + MESSAGE_MARGIN * 2 >= SCREEN_HEIGHT) return 0;
            switch (align)
            {
                case message_align::top: return MESSAGE_MARGIN;
                case message_align::middle: return to<u16>((SCREEN_HEIGHT - height) / 2);
                case message_align::bottom:
                default: return to<u16>(SCREEN_HEIGHT - height - MESSAGE_MARGIN);
            }
        }
    }

    message_state::message_state(engine& engine, noir_vm& vm, overlay_system& overlays,
        message_request const& request) noexcept
        : _engine(engine), _vm(vm), _overlays(overlays), _request(request)
    {
        _request.text = copy_text(_text, sizeof(_text), request.text);
        _request.caption = copy_text(_caption, sizeof(_caption), request.caption);
    }

    mtl::expected<void, char const*> message_state::pushed()
    {
        _overlays.clear();
        auto* renderer = _engine.default_text_renderer();
        if (!_request.text.is_empty() && !renderer)
            return mtl::unexpected("No text renderer for the message");

        u16 const box_limit = MIN(_request.max_width, to<u16>(SCREEN_WIDTH));
        u16 const allowance = box_limit > MESSAGE_MARGIN * 2
            ? to<u16>(box_limit - MESSAGE_MARGIN * 2) : u16(1);
        auto const extent = renderer
            ? renderer->measure_text(_request.text, allowance) : text_extent{};
        if (extent.width == 0 || extent.height == 0)
        {
            _entered = true;
            return {};
        }

        auto* frame = _engine.default_nine_patch();
        u16 content_width;
        u16 caption_height = 0;
        {
            mtl::system_use_guard guard;
            _text_bitmap = renderer->create_text(_request.text, allowance, _request.justify);
            if (!_text_bitmap) return mtl::unexpected("Cannot allocate message text");

            auto const caption_extent = renderer->measure_text(_request.caption, allowance);
            if (caption_extent.width != 0 && caption_extent.height != 0)
            {
                _caption_bitmap = renderer->create_text(
                    _request.caption, allowance, text_justify::CENTER);
                if (!_caption_bitmap) return mtl::unexpected("Cannot allocate message caption");
                caption_height = to<u16>(_caption_bitmap->uwActualHeight + MESSAGE_CAPTION_GAP);
            }

            content_width = _text_bitmap->uwActualWidth;
            if (_caption_bitmap && _caption_bitmap->uwActualWidth > content_width)
                content_width = _caption_bitmap->uwActualWidth;
            _box_width = to<u16>(content_width + MESSAGE_MARGIN * 2);
            if (frame) _box_width = frame->width_for(_box_width);
            _box_height = MIN(to<u16>(SCREEN_HEIGHT),
                to<u16>(caption_height + _text_bitmap->uwActualHeight + MESSAGE_MARGIN * 2));
            if (frame)
            {
                _frame_bitmap = frame->render(_box_width, _box_height, 0);
                if (!_frame_bitmap) return mtl::unexpected("Cannot allocate message frame");
            }
        }

        _box_x = to<u16>((SCREEN_WIDTH - _box_width) / 2);
        _box_y = top_edge_for(_request.align, _box_height);
        auto& screen = _engine.main_screen();
        screen.freeze_display();
        _frozen = true;
        if (_frame_bitmap)
        {
            screen.blit_copy(_frame_bitmap.get(), 0, 0,
                to<WORD>(_box_x), to<WORD>(_box_y),
                to<WORD>(_box_width), to<WORD>(_box_height), MINTERM_COOKIE);
        }

        u16 const content_x = to<u16>(_box_x + (_box_width - content_width) / 2);
        u16 const content_y = to<u16>(_box_y + MESSAGE_MARGIN);
        u16 const content_bottom = to<u16>(_box_y + _box_height - MESSAGE_MARGIN);
        if (_caption_bitmap)
        {
            _caption_bitmap->uwActualHeight = MIN(
                _caption_bitmap->uwActualHeight, to<u16>(content_bottom - content_y));
            screen.text_copy(_caption_bitmap.get(),
                to<u16>(content_x + (content_width - _caption_bitmap->uwActualWidth) / 2),
                content_y, _request.caption_color, FONT_COOKIE);
        }
        u16 const text_y = to<u16>(content_y + caption_height);
        if (text_y < content_bottom)
        {
            // Long wrapped messages must not write past the scene's bottom edge.
            _text_bitmap->uwActualHeight = MIN(
                _text_bitmap->uwActualHeight, to<u16>(content_bottom - text_y));
            screen.text_copy(_text_bitmap.get(), content_x, text_y, _request.color, FONT_COOKIE);
        }

        _engine.pointer().update();
        // The hotspot only checked the press that opened this message.
        (void)mouseUse(MOUSE_PORT_1, MOUSE_LMB);
        (void)keyUse(KEY_SPACE);
        _entered = true;
        return {};
    }

    void message_state::update(state_control& control)
    {
        _engine.pointer().update();
        bool const clicked = mouseUse(MOUSE_PORT_1, MOUSE_LMB);
        bool const pressed = keyUse(KEY_SPACE);
        if (!_frozen || clicked || pressed) control.pop();
    }

    message_state::~message_state()
    {
        if (_frozen)
        {
            _engine.main_screen().restore_rect(
                to<WORD>(_box_x), to<WORD>(_box_y),
                to<WORD>(_box_width), to<WORD>(_box_height));
        }
        if (_frozen) _engine.main_screen().resume_display();
        {
            mtl::system_use_guard guard;
            _frame_bitmap.reset(nullptr);
            _caption_bitmap.reset(nullptr);
            _text_bitmap.reset(nullptr);
        }
        if (_entered && _request.modal_wait) _vm.resume_execution();
    }
}  // namespace NEONengine
