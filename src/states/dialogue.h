#ifndef __STATES_DIALOGUE_H__INCLUDED__
#define __STATES_DIALOGUE_H__INCLUDED__

#include <ace++/font.h>

#include "core/dialogue_portraits.h"
#include "core/dialogue_session.h"
#include "core/state.h"
#include "core/modal_wait.h"
#include "core/screen.h"
#include "types.h"
#include "utils/bstr_view.h"

namespace NEONengine
{
    class engine;
    class noir_vm;

    class dialogue_state : public state
    {
        public:
        dialogue_state(engine& engine, noir_vm& vm,
            u16 dialogue_id) noexcept;
        ~dialogue_state() override;

        mtl::expected<void, char const*> pushed() override;
        void update(state_control& control) override;

        private:
        static constexpr u8 NO_SLOT = 0xFF;
        using page_result = mtl::expected<bool, char const*>;

        // Keep drawn glyphs until navigation, even if the string table changes.
        struct drawn_slot
        {
            ace::text_bitmap_ptr _number{ nullptr };
            ace::text_bitmap_ptr _text{ nullptr };
            bool _rotates{ false };
            u8 _choice{ 0 };
            u16 _x{ 0 };
            u16 _text_x{ 0 };
            u16 _y{ 0 };
            u16 _width{ 0 };
            u16 _height{ 0 };
        };

        page_result build_page();
        mtl::expected<void, char const*> build_slot(u8 choice, bool rotates,
            u16 column, u16 allowance, u16 height, u16& widest, u16& total_height);
        void draw_page(bool draw_portrait);
        void refresh(state_control& control);
        void take_slot(state_control& control, u8 slot);
        void erase_frame();
        void erase_portrait();
        void release_page();
        void draw_slot(u8 slot, u8 color);
        void set_highlight(u8 slot);
        u8 slot_under_pointer() const;
        void consume_input();
        bstr_view speaker_name(u16 speaker_id) const;
        bstr_view more_text() const;
        void apply_speaker_color(u16 speaker_id);

        engine& _engine;
        noir_vm& _vm;
        modal_wait _wait;
        freeze_display _display;
        u16 _dialogue_id;
        dialogue_context _context;
        dialogue_session _session;
        dialogue_portraits_ptr _portraits{ nullptr };
        ace::text_bitmap_ptr _text{ nullptr };
        ace::text_bitmap_ptr _heading{ nullptr };
        ace::bitmap_ptr _frame{ nullptr };
        drawn_slot _slots[dialogue_session::MAX_CHOICES_SHOWN]{};
        u16 _frame_x{ 0 };
        u16 _frame_y{ 0 };
        u16 _frame_width{ 0 };
        u16 _frame_height{ 0 };
        u16 _content_width{ 0 };
        u16 _heading_height{ 0 };
        u16 _portrait_width{ 0 };
        u16 _speaker{ data::NO_ID };
        ULONG _held_text_color{ 0 };
        u8 _slot_count{ 0 };
        u8 _highlight{ NO_SLOT };
        u8 _pending{ NO_SLOT };
        u8 _hold_frames{ 0 };
        bool _entered{ false };
    };
}

#endif
