#ifndef __STATES_MESSAGE_H__INCLUDED__
#define __STATES_MESSAGE_H__INCLUDED__

#include <ace++/bitmap.h>

#include "core/overlay_system.h"
#include "core/state.h"

namespace NEONengine
{
    class engine;

    class message_state : public state
    {
        public:
        message_state(engine& engine, noir_vm& vm, overlay_system& overlays,
            message_request const& request) noexcept;
        ~message_state() override;

        mtl::expected<void, char const*> pushed() override;
        void update(state_control& control) override;

        private:
        engine& _engine;
        noir_vm& _vm;
        overlay_system& _overlays;
        message_request _request;
        ace::text_bitmap_ptr _text_bitmap{ nullptr };
        ace::text_bitmap_ptr _caption_bitmap{ nullptr };
        ace::bitmap_ptr _frame_bitmap{ nullptr };
        u16 _box_x{ 0 };
        u16 _box_y{ 0 };
        u16 _box_width{ 0 };
        u16 _box_height{ 0 };
        bool _entered{ false };
        bool _frozen{ false };
        char _text[MESSAGE_TEXT_CAPACITY]{};
        char _caption[MESSAGE_CAPTION_CAPACITY]{};
    };
}

#endif
