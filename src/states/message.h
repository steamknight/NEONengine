#ifndef __STATES_MESSAGE_H__INCLUDED__
#define __STATES_MESSAGE_H__INCLUDED__

#include <ace++/bitmap.h>

#include "core/message_request.h"
#include "core/modal_wait.h"
#include "core/screen.h"
#include "core/state.h"

namespace NEONengine
{
    class engine;

    class message_state : public state
    {
        public:
        message_state(engine& engine, noir_vm& vm,
            message_request const& request) noexcept;
        ~message_state() override;

        mtl::expected<void, char const*> pushed() override;
        void update(state_control& control) override;

        private:
        engine& _engine;
        modal_wait _wait;
        freeze_display _display;
        message_request _request;
        ace::text_bitmap_ptr _text_bitmap{ nullptr };
        ace::text_bitmap_ptr _caption_bitmap{ nullptr };
        ace::bitmap_ptr _frame_bitmap{ nullptr };
        u16 _box_x{ 0 };
        u16 _box_y{ 0 };
        u16 _box_width{ 0 };
        u16 _box_height{ 0 };
        bool _entered{ false };
        char _text[MESSAGE_TEXT_CAPACITY]{};
        char _caption[MESSAGE_CAPTION_CAPACITY]{};
    };
}

#endif
