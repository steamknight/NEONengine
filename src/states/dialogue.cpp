#include "dialogue.h"

#include "neonengine.h"

#include <ace/managers/blit.h>
#include <ace/managers/game.h>
#include <ace/managers/key.h>
#include <ace/managers/mouse.h>

#include "core/logger.h"

NE_LOG_DOMAIN(Dialogue);

namespace NEONengine
{
    static_assert(DIALOGUE_MAX_CHOICES_SHOWN == dialogue_session::MAX_CHOICES_SHOWN);

    dialogue_state::dialogue_state(engine& engine, noir_vm& vm, overlay_system& overlays,
        u16 dialogue_id) noexcept
        : _engine(engine), _vm(vm), _overlays(overlays), _dialogue_id(dialogue_id),
          _context{ engine.session(), vm }
    {
    }

    bstr_view dialogue_state::speaker_name(u16 speaker_id) const
    {
        auto const speakers = _engine.data().speakers();
        if (speaker_id >= speakers.size()) return {};
        return _engine.strings().get_string(speakers[speaker_id].name_string_id);
    }

    bstr_view dialogue_state::more_text() const
    {
        return _engine.strings().more_choices();
    }

    void dialogue_state::apply_speaker_color(u16 speaker_id)
    {
        auto* palette = force_to<ULONG*>(_engine.main_screen().palette());
        auto const speakers = _engine.data().speakers();
        palette[DIALOGUE_TEXT_COLOR] = speaker_id < speakers.size()
            ? to<ULONG>(speakers[speaker_id].dialogue_color >> 8) : _held_text_color;
        _engine.main_screen().publish_palette(DIALOGUE_TEXT_COLOR, 1);
    }

    void dialogue_state::erase_frame()
    {
        if (_frozen)
            _engine.main_screen().restore_rect(to<WORD>(_frame_x), to<WORD>(_frame_y),
                to<WORD>(_frame_width), to<WORD>(_frame_height));
        _frame_width = 0;
        _frame_height = 0;
    }

    void dialogue_state::erase_portrait()
    {
        if (_frozen && _portrait_width)
            _engine.main_screen().restore_rect(DIALOGUE_MARGIN, DIALOGUE_MARGIN,
                PORTRAIT_WIDTH, PORTRAIT_HEIGHT);
        _portrait_width = 0;
    }

    void dialogue_state::release_page()
    {
        mtl::system_use_guard guard;
        _frame.reset(nullptr);
        _heading.reset(nullptr);
        _text.reset(nullptr);
        for (auto& slot : _slots)
        {
            slot._number.reset(nullptr);
            slot._text.reset(nullptr);
        }
        _slot_count = 0;
        _highlight = NO_SLOT;
        _pending = NO_SLOT;
        _hold_frames = 0;
    }

    mtl::expected<void, char const*> dialogue_state::build_slot(u8 choice, bool rotates,
        u16 column, u16 allowance, u16 height, u16& widest, u16& total_height)
    {
        auto& slot = _slots[_slot_count];
        auto* renderer = _engine.default_text_renderer();
        auto const text = rotates ? more_text()
            : _engine.strings().get_string(_session.choice_at(choice)->text_string_id);
        slot._text = renderer->create_text(text, to<u16>(allowance - column), text_justify::LEFT);
        if (!slot._text) return mtl::unexpected("Cannot allocate dialogue choice text");
        char const number[] = { to<char>('1' + _slot_count), '.', '\0' };
        slot._number = renderer->create_text(number, allowance, text_justify::LEFT);
        if (!slot._number) return mtl::unexpected("Cannot allocate dialogue choice number");
        slot._text->uwActualHeight = MIN(slot._text->uwActualHeight, height);
        slot._number->uwActualHeight = MIN(slot._number->uwActualHeight, height);
        slot._height = height;
        slot._text_x = column;
        slot._choice = choice;
        slot._rotates = rotates;
        widest = MAX(widest, slot._text->uwActualWidth);
        if (_slot_count) total_height = to<u16>(total_height + DIALOGUE_CHOICE_SPACING);
        total_height = to<u16>(total_height + height);
        ++_slot_count;
        return {};
    }

    dialogue_state::page_result dialogue_state::build_page()
    {
        auto const* page = _session.current_page();
        if (!page || _vm.terminated()) return false;
        auto const text_string = _engine.strings().get_string(page->text_string_id);
        if (text_string.is_empty()) return false;
        auto* renderer = _engine.default_text_renderer();
        if (!renderer) return mtl::unexpected("No text renderer for the dialogue");

        _frame_x = _portraits->has(page->speaker_id)
            ? to<u16>(DIALOGUE_MARGIN * 2 + PORTRAIT_WIDTH) : DIALOGUE_MARGIN;
        _frame_y = DIALOGUE_MARGIN;
        auto* frame = _engine.default_nine_patch();
        u16 frame_limit = to<u16>(SCREEN_WIDTH - _frame_x - DIALOGUE_MARGIN);
        if (frame)
        {
            u16 const aligned = frame->width_for(frame_limit);
            frame_limit = aligned > frame_limit ? to<u16>(aligned - 16) : aligned;
        }
        u16 const allowance = to<u16>(frame_limit - MESSAGE_MARGIN * 2);
        auto const extent = renderer->measure_text(text_string, allowance);
        if (!extent.width || !extent.height) return false;

        mtl::system_use_guard guard;
        _text = renderer->create_text(text_string, allowance, text_justify::LEFT);
        if (!_text) return mtl::unexpected("Cannot allocate dialogue page text");
        auto const name = speaker_name(page->speaker_id);
        auto const name_extent = renderer->measure_text(name, allowance);
        if (name_extent.width && name_extent.height)
        {
            _heading = renderer->create_text(name, allowance, text_justify::CENTER);
            if (!_heading) return mtl::unexpected("Cannot allocate dialogue speaker name");
        }
        _content_width = _text->uwActualWidth;
        if (_heading) _content_width = MAX(_content_width, _heading->uwActualWidth);
        u16 const content_space = to<u16>(SCREEN_HEIGHT - DIALOGUE_MARGIN * 2 - MESSAGE_MARGIN * 2);
        u16 const line_height = renderer->measure_text("1.", allowance).height;
        if (_heading)
        {
            // Leave room for a reply and the set control even with an oversized name.
            u16 const reserved = to<u16>(line_height * 2 + DIALOGUE_CHOICE_GAP
                + DIALOGUE_CHOICE_SPACING + MESSAGE_CAPTION_GAP);
            _heading->uwActualHeight = MIN(_heading->uwActualHeight,
                content_space > reserved ? to<u16>(content_space - reserved) : u16(0));
        }
        _heading_height = _heading && _heading->uwActualHeight
            ? to<u16>(_heading->uwActualHeight + MESSAGE_CAPTION_GAP) : u16(0);
        u16 const text_space = to<u16>(content_space - _heading_height);

        u16 number_width = 0;
        u8 const numbered = MIN(_session.choice_count(), DIALOGUE_MAX_CHOICES_SHOWN);
        for (u8 i = 0; i < numbered; ++i)
        {
            char const label[] = { to<char>('1' + i), '.', '\0' };
            number_width = MAX(number_width, renderer->measure_text(label, allowance).width);
        }
        u16 const column = to<u16>(DIALOGUE_CHOICE_INDENT + number_width + DIALOGUE_CHOICE_NUMBER_GAP);
        if (_session.choice_count() && column >= allowance)
            return mtl::unexpected("No room for dialogue choice text");
        u16 const choice_allowance = column < allowance ? to<u16>(allowance - column) : u16(0);
        u16 const more_height = _session.choice_count()
            ? renderer->measure_text(more_text(), choice_allowance).height : u16(0);
        u16 const reserved = to<u16>(DIALOGUE_CHOICE_GAP + DIALOGUE_CHOICE_SPACING + more_height);
        u16 const choice_limit = text_space > reserved ? to<u16>(text_space - reserved) : u16(0);
        u16 heights[dialogue_session::MAX_CHOICES]{};
        u16 tallest = 0;
        for (u8 i = 0; i < _session.choice_count(); ++i)
        {
            auto const text = _engine.strings().get_string(_session.choice_at(i)->text_string_id);
            auto const measured = renderer->measure_text(text, choice_allowance);
            if (!measured.width || !measured.height) continue;
            if (!choice_limit) return mtl::unexpected("No room for dialogue choices");
            heights[i] = MIN(measured.height, choice_limit);
            tallest = MAX(tallest, heights[i]);
        }
        u16 const floor_height = tallest ? to<u16>(reserved + tallest) : u16(0);
        u16 const text_room = text_space > floor_height ? to<u16>(text_space - floor_height) : u16(0);
        _text->uwActualHeight = MIN(_text->uwActualHeight, text_room);
        u16 choice_space = to<u16>(text_space - _text->uwActualHeight);
        choice_space = choice_space > DIALOGUE_CHOICE_GAP
            ? to<u16>(choice_space - DIALOGUE_CHOICE_GAP) : u16(0);
        _session.fit_choices(mtl::span<u16>(heights, _session.choice_count()), choice_space,
            DIALOGUE_CHOICE_SPACING, more_height);
        if (_session.has_more() && !more_height)
            return mtl::unexpected("Missing localized dialogue choice control");

        u16 widest = 0;
        u16 choices_height = 0;
        for (u8 i = 0; i < _session.slot_count(); ++i)
        {
            u8 const choice = _session.slot_choice(i);
            auto result = build_slot(choice, false, column, allowance, heights[choice], widest, choices_height);
            if (!result) return mtl::unexpected(result.error());
        }
        if (_session.has_more())
        {
            auto result = build_slot(0, true, column, allowance, more_height, widest, choices_height);
            if (!result) return mtl::unexpected(result.error());
        }
        if (_slot_count)
        {
            _content_width = MAX(_content_width, to<u16>(column + widest));
            choices_height = to<u16>(choices_height + DIALOGUE_CHOICE_GAP);
        }
        _frame_width = to<u16>(_content_width + MESSAGE_MARGIN * 2);
        _frame_height = to<u16>(_heading_height + _text->uwActualHeight + choices_height + MESSAGE_MARGIN * 2);
        if (frame)
        {
            _frame_width = frame->width_for(_frame_width);
            _frame = frame->render(_frame_width, _frame_height, 0);
            if (!_frame) return mtl::unexpected("Cannot allocate dialogue frame");
        }
        return true;
    }

    void dialogue_state::draw_slot(u8 slot, u8 color)
    {
        auto& drawn = _slots[slot];
        if (drawn._number->uwActualHeight)
            _engine.main_screen().text_copy(drawn._number.get(), drawn._x, drawn._y, color, FONT_COOKIE);
        if (drawn._text->uwActualHeight)
            _engine.main_screen().text_copy(drawn._text.get(), drawn._text_x, drawn._y, color, FONT_COOKIE);
    }

    void dialogue_state::draw_page(bool draw_portrait)
    {
        auto& screen = _engine.main_screen();
        // Palette changes are immediate; don't recolor pixels still being erased.
        blitWait();
        apply_speaker_color(_speaker);
        if (draw_portrait)
        {
            _portraits->show(_speaker, DIALOGUE_MARGIN, DIALOGUE_MARGIN);
            _portrait_width = _portraits->has(_speaker) ? PORTRAIT_WIDTH : u16(0);
        }
        if (_frame)
            screen.blit_copy(_frame.get(), 0, 0, to<WORD>(_frame_x), to<WORD>(_frame_y),
                to<WORD>(_frame_width), to<WORD>(_frame_height), MINTERM_COOKIE);
        u16 const x = to<u16>(_frame_x + (_frame_width - _content_width) / 2);
        u16 const y = to<u16>(_frame_y + MESSAGE_MARGIN);
        if (_heading && _heading->uwActualHeight)
            screen.text_copy(_heading.get(), to<u16>(x + (_content_width - _heading->uwActualWidth) / 2),
                y, DIALOGUE_NAME_COLOR, FONT_COOKIE);
        if (_text->uwActualHeight)
            screen.text_copy(_text.get(), x, to<u16>(y + _heading_height), DIALOGUE_TEXT_COLOR, FONT_COOKIE);
        u16 choice_y = to<u16>(y + _heading_height + _text->uwActualHeight + DIALOGUE_CHOICE_GAP);
        for (u8 i = 0; i < _slot_count; ++i)
        {
            auto& slot = _slots[i];
            slot._x = to<u16>(x + DIALOGUE_CHOICE_INDENT);
            slot._text_x = to<u16>(x + slot._text_x);
            slot._y = choice_y;
            slot._width = to<u16>(_content_width - DIALOGUE_CHOICE_INDENT);
            draw_slot(i, DIALOGUE_CHOICE_COLOR);
            choice_y = to<u16>(choice_y + slot._height + DIALOGUE_CHOICE_SPACING);
        }
        mtl::system_use_guard guard;
        _frame.reset(nullptr);
        _heading.reset(nullptr);
        _text.reset(nullptr);
    }

    void dialogue_state::consume_input()
    {
        (void)mouseUse(MOUSE_PORT_1, MOUSE_LMB);
        (void)keyUse(KEY_SPACE);
        for (u8 i = 0; i < DIALOGUE_MAX_CHOICES_SHOWN; ++i)
            (void)keyUse(to<u8>(KEY_1 + i));
    }

    mtl::expected<void, char const*> dialogue_state::pushed()
    {
        _overlays.clear();
        _session.create(_engine.data().dialogues(), _engine.data().pages(),
            _engine.data().choices(), &engine_dialogue_bridge(), &_context);
        if (!_session.begin(_dialogue_id)
            || _engine.strings().get_string(_session.current_page()->text_string_id).is_empty())
        {
            _entered = true;
            return {};
        }
        {
            mtl::system_use_guard guard;
            _portraits = mtl::make_unique<dialogue_portraits>(
                _engine.main_screen(), _engine.data(), _engine.assets());
            if (!_portraits) return mtl::unexpected("Cannot allocate dialogue portraits");
            _portraits->load(_session.page_range());
        }
        auto result = build_page();
        if (!result) return mtl::unexpected(result.error());
        if (result.value())
        {
            _held_text_color = force_to<ULONG const*>(_engine.main_screen().palette())[DIALOGUE_TEXT_COLOR];
            _engine.main_screen().freeze_display();
            _frozen = true;
            _speaker = _session.current_page()->speaker_id;
            draw_page(true);
            _engine.pointer().switch_to(pointer_role::DEFAULT);
            _engine.pointer().update();
            consume_input();
        }
        _entered = true;
        return {};
    }

    void dialogue_state::refresh(state_control& control)
    {
        if (_session.has_ended() || _vm.terminated())
        {
            control.pop();
            return;
        }
        u16 const speaker = _session.current_page()->speaker_id;
        bool const new_speaker = speaker != _speaker;
        erase_frame();
        if (new_speaker) erase_portrait();
        release_page();
        auto result = build_page();
        if (!result)
        {
            NE_LOG_ALWAYS("Fatal dialogue error: %s", result.error());
            gameExit();
            return;
        }
        if (!result.value())
        {
            control.pop();
            return;
        }
        _speaker = speaker;
        draw_page(new_speaker);
        consume_input();
    }

    void dialogue_state::take_slot(state_control& control, u8 slot)
    {
        if (_slots[slot]._rotates) _session.rotate_choices();
        else _session.take_choice(_slots[slot]._choice);
        refresh(control);
    }

    void dialogue_state::set_highlight(u8 slot)
    {
        if (slot == _highlight) return;
        if (_highlight != NO_SLOT) draw_slot(_highlight, DIALOGUE_CHOICE_COLOR);
        _highlight = slot;
        if (_highlight != NO_SLOT) draw_slot(_highlight, DIALOGUE_CHOICE_HIGHLIGHT_COLOR);
    }

    u8 dialogue_state::slot_under_pointer() const
    {
        u16 const offset = _engine.main_screen().offset();
        auto const pointer_y = mouseGetY(MOUSE_PORT_1);
        if (pointer_y < offset) return NO_SLOT;
        u16 const x = to<u16>(mouseGetX(MOUSE_PORT_1));
        u16 const y = to<u16>(pointer_y - offset);
        for (u8 i = 0; i < _slot_count; ++i)
        {
            auto const& slot = _slots[i];
            if (x >= slot._x && x < slot._x + slot._width
                && y >= slot._y && y < slot._y + slot._height) return i;
        }
        return NO_SLOT;
    }

    void dialogue_state::update(state_control& control)
    {
        if (!_frozen || _session.has_ended() || _vm.terminated())
        {
            control.pop();
            return;
        }
        if (!_hold_frames)
        {
            u8 const hovered = _slot_count ? slot_under_pointer() : NO_SLOT;
            set_highlight(hovered);
            _engine.pointer().switch_to(hovered == NO_SLOT ? pointer_role::DEFAULT : pointer_role::USE);
        }
        _engine.pointer().update();
        if (_hold_frames)
        {
            consume_input();
            if (--_hold_frames) return;
            u8 const slot = _pending;
            _pending = NO_SLOT;
            if (slot < _slot_count) take_slot(control, slot);
            return;
        }

        bool const clicked = mouseUse(MOUSE_PORT_1, MOUSE_LMB);
        bool const pressed = keyUse(KEY_SPACE);
        u8 selected = NO_SLOT;
        for (u8 i = 0; i < DIALOGUE_MAX_CHOICES_SHOWN; ++i)
        {
            if (keyUse(to<u8>(KEY_1 + i)) && i < _slot_count && selected == NO_SLOT)
                selected = i;
        }
        if (!_slot_count)
        {
            if (clicked || pressed)
            {
                _session.advance();
                refresh(control);
            }
            return;
        }
        if (selected != NO_SLOT)
        {
            set_highlight(selected);
            _pending = selected;
            _hold_frames = DIALOGUE_CHOICE_HOLD_FRAMES;
            return;
        }
        if (clicked && _highlight != NO_SLOT) take_slot(control, _highlight);
    }

    dialogue_state::~dialogue_state()
    {
        erase_frame();
        erase_portrait();
        // Erasure is asynchronous; restoring colors first would recolor the old overlay.
        if (_frozen) blitWait();
        {
            mtl::system_use_guard guard;
            _portraits.reset(nullptr);
        }
        if (_frozen)
        {
            auto* palette = force_to<ULONG*>(_engine.main_screen().palette());
            palette[DIALOGUE_TEXT_COLOR] = _held_text_color;
            _engine.main_screen().publish_palette(DIALOGUE_TEXT_COLOR, 1);
            _engine.main_screen().resume_display();
            _engine.pointer().switch_to(pointer_role::DEFAULT);
            _engine.pointer().update();
        }
        release_page();
        if (_entered) _vm.resume_execution();
    }
}  // namespace NEONengine
