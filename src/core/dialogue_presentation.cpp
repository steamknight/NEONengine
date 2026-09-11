#include "dialogue_presentation.h"

#include "neonengine.h"

#include <ace/managers/blit.h>
#include <ace/managers/key.h>
#include <ace/managers/mouse.h>

#include "core/logger.h"

NE_LOG_DOMAIN(Dialogue);

namespace NEONengine
{
    static_assert(DIALOGUE_MAX_CHOICES_SHOWN == dialogue_session::MAX_CHOICES_SHOWN);

    namespace
    {
        /**
         * @brief Rounds a height down to complete font lines.
         */
        u16 whole_lines(u16 height, u16 line_height)
        {
            return to<u16>(height / line_height * line_height);
        }

        void consume_input()
        {
            // We don't care about the return values, these functions side-effect and consume the input.
            DISCARD_RESULT(mouseUse(MOUSE_PORT_1, MOUSE_LMB));
            DISCARD_RESULT(keyUse(KEY_SPACE));

            for (u8 i = 0; i < DIALOGUE_MAX_CHOICES_SHOWN; ++i)
                DISCARD_RESULT(keyUse(to<u8>(KEY_1 + i)));
        }
    }

    dialogue_presentation::dialogue_presentation(engine& engine) noexcept
        : _engine(engine)
        , _display(engine.main_screen())
    { }

    dialogue_presentation::~dialogue_presentation()
    {
        close();
    }

    bstr_view dialogue_presentation::speaker_name(u16 speaker_id) const
    {
        auto const speakers = _engine.data().speakers();
        if (speaker_id >= speakers.size()) return {};

        return _engine.strings().get_string(speakers[speaker_id].name_string_id);
    }

    void dialogue_presentation::apply_speaker_color(u16 speaker_id)
    {
        auto* palette = force_to<ULONG*>(_engine.main_screen().palette());
        auto const speakers = _engine.data().speakers();

        palette[DIALOGUE_TEXT_COLOR] = speaker_id < speakers.size()
            ? to<ULONG>(speakers[speaker_id].dialogue_color >> 8) 
            : _held_text_color;
        
        _engine.main_screen().publish_palette(DIALOGUE_TEXT_COLOR, 1);
    }

    void dialogue_presentation::erase_frame()
    {
        if (_display)
        {
            _engine.main_screen().restore_rect(
                to<WORD>(_frame_x),
                to<WORD>(_frame_y),
                to<WORD>(_frame_width),
                to<WORD>(_frame_height)
            );
        }

        _frame_width = 0;
        _frame_height = 0;
    }

    void dialogue_presentation::erase_portrait()
    {
        if (_display && _portrait_width)
        {
            _engine.main_screen().restore_rect(
                DIALOGUE_MARGIN, DIALOGUE_MARGIN,
                PORTRAIT_WIDTH, PORTRAIT_HEIGHT
            );
        }

        _portrait_width = 0;
    }

    void dialogue_presentation::release_page()
    {
        mtl::system_use_guard guard;

        clear_slots();
        _frame.reset(nullptr);
        _heading.reset(nullptr);
        _text.reset(nullptr);
        _more.reset(nullptr);

        for (auto& choice : _choices)
        {
            choice.reset(nullptr);
        }

        _content_width = 0;
        _heading_height = 0;
        _choice_text_x = 0;
    }

    void dialogue_presentation::clear_slots()
    {
        for (auto& slot : _slots)
        {
            slot = {};
        }

        _slot_count = 0;
        _highlight = NO_SLOT;
        _pending = NO_SLOT;
        _hold_frames = 0;
    }

    dialogue_presentation::page_space dialogue_presentation::measure_page_space(data::page const& page)
    {
        // Make room for a portrait if the speaker has one.
        _frame_x = _portraits->has(page.speaker_id)
            ? to<u16>(DIALOGUE_MARGIN * 2 + PORTRAIT_WIDTH) : DIALOGUE_MARGIN;
        _frame_y = DIALOGUE_MARGIN;

        // Size the frame for the dialogue text and choices.
        auto frame_limit = to<u16>(SCREEN_WIDTH - _frame_x - DIALOGUE_MARGIN);
        if (auto* frame = _engine.default_nine_patch())
        {
            // Adjust the frame width to a multiple of 16 for blitCopy to draw correctly,
            // dropping a group of 16 rather than growing past the screen margin.
            auto const aligned = frame->width_for(frame_limit);
            frame_limit = aligned > frame_limit ? to<u16>(aligned - 16) : aligned;
        }

        return {
            to<u16>(frame_limit - MESSAGE_MARGIN * 2),
            to<u16>(SCREEN_HEIGHT - DIALOGUE_MARGIN * 2 - MESSAGE_MARGIN * 2)
        };
    }

    mtl::expected<void, char const*> dialogue_presentation::build_page_text
    (
        data::page const& page,
        bstr_view const& text_string,
        page_space const& space
    )
    {
        auto* renderer = _engine.default_text_renderer();
        u16 const max_content_width = space.max_content_width;

        _text = renderer->create_text(text_string, max_content_width, text_justify::LEFT);
        if (!_text)
        {
            return mtl::unexpected("Cannot allocate dialogue page text");
        }

        auto const name = speaker_name(page.speaker_id);
        auto const name_extent = renderer->measure_text(name, max_content_width);
        if (name_extent.width && name_extent.height)
        {
            _heading = renderer->create_text(name, max_content_width, text_justify::CENTER);
            if (!_heading)
            {
                return mtl::unexpected("Cannot allocate dialogue speaker name");
            }
        }

        _content_width = _text->uwActualWidth;
        if (_heading)
        {
            _content_width = MAX(_content_width, _heading->uwActualWidth);
        }

        // Names and body text use the screen first; choices never shorten them.
        u16 const line_height = renderer->line_height();
        if (_heading)
        {
            u16 const minimum = to<u16>(line_height + MESSAGE_CAPTION_GAP);
            u16 const room = space.content_height > minimum
                ? to<u16>(space.content_height - minimum) : u16(0);
            _heading->uwActualHeight = whole_lines(MIN(_heading->uwActualHeight, room), line_height);
        }

        _heading_height = _heading && _heading->uwActualHeight
            ? to<u16>(_heading->uwActualHeight + MESSAGE_CAPTION_GAP) : u16(0);

        u16 const text_room = to<u16>(space.content_height - _heading_height);
        _text->uwActualHeight = whole_lines(MIN(_text->uwActualHeight, text_room), line_height);
        if (!_text->uwActualHeight)
        {
            NE_LOG("No room for dialogue page text");
            return mtl::unexpected("No room for dialogue page text");
        }

        return {};
    }

    mtl::expected<void, char const*> dialogue_presentation::build_choices
    (
        dialogue_session& session,
        page_space const& space
    )
    {
        auto* renderer = _engine.default_text_renderer();
        u16 heights[dialogue_session::MAX_CHOICES]{};
        if (!session.choice_count())
        {
            session.fit_choices({}, 0, DIALOGUE_CHOICE_SPACING, 0);
            return {};
        }

        u16 number_width = 0;
        for (u8 i = 0; i < DIALOGUE_MAX_CHOICES_SHOWN; ++i)
        {
            auto* number = _engine.dialogue_number(i);
            if (!number)
            {
                return mtl::unexpected("Cannot allocate dialogue choice number");
            }

            number_width = MAX(number_width, number->uwActualWidth);
        }

        _choice_text_x = to<u16>(DIALOGUE_CHOICE_INDENT + number_width + DIALOGUE_CHOICE_NUMBER_GAP);
        if (_choice_text_x >= space.max_content_width)
        {
            NE_LOG("page %hu has no room for dialogue choice text", session.current_page_index());
            return mtl::unexpected("No room for dialogue choice text");
        }

        u16 const text_width = to<u16>(space.max_content_width - _choice_text_x);
        u8 drawable = 0;
        for (u8 i = 0; i < session.choice_count(); ++i)
        {
            auto const text = _engine.strings().get_string(session.choice_at(i)->text_string_id);
            auto const measured = renderer->measure_text(text, text_width);
            if (!measured.width || !measured.height)
            {
                continue;
            }

            _choices[i] = renderer->create_text(text, text_width, text_justify::LEFT);
            if (!_choices[i])
            {
                return mtl::unexpected("Cannot allocate dialogue choice text");
            }

            heights[i] = _choices[i]->uwActualHeight;
            _content_width = MAX(_content_width, to<u16>(_choice_text_x + _choices[i]->uwActualWidth));
            ++drawable;
        }

        if (!drawable)
        {
            session.fit_choices(mtl::span<u16>(heights, session.choice_count()),
                0, DIALOGUE_CHOICE_SPACING, 0);
            return {};
        }

        auto const more = _engine.strings().more_choices();
        auto const more_extent = renderer->measure_text(more, text_width);
        if (more_extent.width && more_extent.height)
        {
            _more = renderer->create_text(more, text_width, text_justify::LEFT);
            if (!_more)
            {
                return mtl::unexpected("Cannot allocate dialogue choice control");
            }
        }

        u16 const used = to<u16>(_heading_height + _text->uwActualHeight + DIALOGUE_CHOICE_GAP);
        u16 const choice_space = space.content_height > used
            ? to<u16>(space.content_height - used) : u16(0);
        u16 const more_height = _more ? _more->uwActualHeight : u16(0);
        u16 const line_height = renderer->line_height();

        if (choice_space < line_height)
        {
            NE_LOG("page %hu has no room for dialogue choices", session.current_page_index());
            return mtl::unexpected("No room for dialogue choices");
        }

        // The first fit tells clipping whether it must leave room for the control.
        session.fit_choices(mtl::span<u16>(heights, session.choice_count()),
            choice_space, DIALOGUE_CHOICE_SPACING, more_height);

        u16 choice_limit = choice_space;
        if (session.has_more())
        {
            if (!_more)
            {
                NE_LOG("page %hu needs a localized dialogue choice control", session.current_page_index());
                return mtl::unexpected("Missing localized dialogue choice control");
            }

            u16 const reserved = to<u16>(more_height + DIALOGUE_CHOICE_SPACING);
            if (choice_space < reserved + line_height)
            {
                NE_LOG("page %hu has no room for a choice and its control", session.current_page_index());
                return mtl::unexpected("No room for dialogue choices");
            }

            choice_limit = to<u16>(choice_space - reserved);
        }

        bool clipped = false;
        for (u8 i = 0; i < session.choice_count(); ++i)
        {
            if (!_choices[i])
            {
                continue;
            }

            u16 const height = whole_lines(MIN(heights[i], choice_limit), line_height);
            clipped |= height != heights[i];
            heights[i] = height;
            _choices[i]->uwActualHeight = height;
        }

        if (clipped)
        {
            session.fit_choices(mtl::span<u16>(heights, session.choice_count()),
                choice_space, DIALOGUE_CHOICE_SPACING, more_height);
        }

        if (session.has_more())
        {
            _content_width = MAX(_content_width, to<u16>(_choice_text_x + _more->uwActualWidth));
        }

        return {};
    }

    u16 dialogue_presentation::build_slots(dialogue_session const& session)
    {
        clear_slots();

        u16 height = 0;
        u8 const count = to<u8>(session.slot_count() + (session.has_more() ? 1 : 0));
        for (u8 i = 0; i < count; ++i)
        {
            auto& slot = _slots[i];
            slot._choice = session.slot_choice(i);
            slot._rotates = slot._choice == dialogue_session::NO_CHOICE;
            slot._text = slot._rotates ? _more.get() : _choices[slot._choice].get();
            slot._number = _engine.dialogue_number(i);
            slot._height = slot._text->uwActualHeight;

            height += to<u16>(slot._height + (i ? DIALOGUE_CHOICE_SPACING : DIALOGUE_CHOICE_GAP));
        }

        _slot_count = count;
        return height;
    }

    mtl::expected<void, char const*> dialogue_presentation::build_frame(u16 choices_height)
    {
        u16 width = to<u16>(_content_width + MESSAGE_MARGIN * 2);
        u16 const height = to<u16>(_heading_height + _text->uwActualHeight
            + choices_height + MESSAGE_MARGIN * 2);

        if (auto* frame = _engine.default_nine_patch())
        {
            width = frame->width_for(width);
            _frame = frame->render(width, height, 0);
            if (!_frame)
            {
                return mtl::unexpected("Cannot allocate dialogue frame");
            }
        }

        _frame_width = width;
        _frame_height = height;
        return {};
    }

    dialogue_presentation::page_result dialogue_presentation::build_page(dialogue_session& session)
    {
        auto const* page = session.current_page();
        if (!page)
        {
            return false;
        }

        auto const text_string = _engine.strings().get_string(page->text_string_id);
        if (text_string.is_empty())
        {
            return false;
        }

        auto* renderer = _engine.default_text_renderer();
        if (!renderer || !renderer->line_height())
        {
            return mtl::unexpected("No text renderer for the dialogue");
        }

        auto const space = measure_page_space(*page);

        // Do nothing if the text is empty or cannot be drawn, so the caller can leave the scene alone.
        auto const extent = renderer->measure_text(text_string, space.max_content_width);
        if (!extent.width || !extent.height)
        {
            return false;
        }

        mtl::system_use_guard guard;

        auto const text = build_page_text(*page, text_string, space);
        if (!text)
        {
            return mtl::unexpected(text.error());
        }

        auto const choices = build_choices(session, space);
        if (!choices)
        {
            return mtl::unexpected(choices.error());
        }

        auto const framed = build_frame(build_slots(session));
        if (!framed)
        {
            return mtl::unexpected(framed.error());
        }

        return true;
    }

    void dialogue_presentation::draw_slot(u8 slot, u8 color)
    {
        auto& drawn = _slots[slot];
        _engine.main_screen().text_copy(drawn._number, drawn._x, drawn._y, color, FONT_COOKIE);
        _engine.main_screen().text_copy(drawn._text, drawn._text_x, drawn._y, color, FONT_COOKIE);
    }

    void dialogue_presentation::draw_page(bool draw_portrait)
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
        {
            screen.blit_copy(_frame.get(), 0, 0, to<WORD>(_frame_x), to<WORD>(_frame_y),
                to<WORD>(_frame_width), to<WORD>(_frame_height), MINTERM_COOKIE);
        }

        u16 const x = to<u16>(_frame_x + (_frame_width - _content_width) / 2);
        u16 const y = to<u16>(_frame_y + MESSAGE_MARGIN);
        if (_heading && _heading->uwActualHeight)
        {
            screen.text_copy(_heading.get(), to<u16>(x + (_content_width - _heading->uwActualWidth) / 2),
                y, DIALOGUE_NAME_COLOR, FONT_COOKIE);
        }

        if (_text->uwActualHeight)
        {
            screen.text_copy(_text.get(), x, to<u16>(y + _heading_height), DIALOGUE_TEXT_COLOR, FONT_COOKIE);
        }

        u16 choice_y = to<u16>(y + _heading_height + _text->uwActualHeight + DIALOGUE_CHOICE_GAP);
        for (u8 i = 0; i < _slot_count; ++i)
        {
            auto& slot = _slots[i];

            slot._x = to<u16>(x + DIALOGUE_CHOICE_INDENT);
            slot._text_x = to<u16>(x + _choice_text_x);
            slot._y = choice_y;
            slot._width = to<u16>(_content_width - DIALOGUE_CHOICE_INDENT);

            draw_slot(i, DIALOGUE_CHOICE_COLOR);
            choice_y = to<u16>(choice_y + slot._height + DIALOGUE_CHOICE_SPACING);
        }

        _frame.reset(nullptr);
    }

    mtl::expected<bool, char const*> dialogue_presentation::open(dialogue_session& session)
    {
        auto const* page = session.current_page();
        
        if (!page || _engine.strings().get_string(page->text_string_id).is_empty())
            return false;

        _portraits = mtl::make_unique<dialogue_portraits>(
            _engine.main_screen(), 
            _engine.data(), 
            _engine.assets());

        if (!_portraits)
            return mtl::unexpected("Cannot allocate dialogue portraits");

        _portraits->load(session.page_range());

        
        auto result = build_page(session);

        if (!result)
            return mtl::unexpected(result.error());
    
        if (!result.value())
            return false;

        _held_text_color = force_to<ULONG const*>(_engine.main_screen().palette())[DIALOGUE_TEXT_COLOR];

        if (!_display.acquire())
            return mtl::unexpected("Display already acquired");

        _speaker = session.current_page()->speaker_id;

        draw_page(true);

        consume_input();

        _engine.pointer().switch_to(pointer_role::DEFAULT);

        return true;
    }

    mtl::expected<bool, char const*> dialogue_presentation::refresh(dialogue_session& session)
    {
        auto const* page = session.current_page();

        if (!page)
            return false;
        
        erase_frame();

        u16 const speaker = page->speaker_id;
        bool const new_speaker = speaker != _speaker;
        if (new_speaker)
        {
            erase_portrait();
        }

        release_page();

        auto result = build_page(session);

        if (!result)
            return mtl::unexpected(result.error());
    
        if (!result.value())
            return false;

        _speaker = speaker;

        draw_page(new_speaker);

        return true;
    }

    dialogue_presentation::page_result dialogue_presentation::redraw(dialogue_session const& session)
    {
        if (!_display || !_text || !_frame_height)
        {
            return mtl::unexpected("Dialogue page is not open");
        }

        erase_frame();

        auto const framed = build_frame(build_slots(session));
        if (!framed)
        {
            return mtl::unexpected(framed.error());
        }

        draw_page(false);
        return true;
    }

    u8 dialogue_presentation::hit_test(u16 x, u16 y) const
    {
        u16 const offset = _engine.main_screen().offset();

        if (!_slot_count || y < offset)
            return NO_SLOT;

        u16 const scene_y = to<u16>(y - offset);
        for (u8 i = 0; i < _slot_count; ++i)
        {
            auto const& slot = _slots[i];
            if (x >= slot._x && x < slot._x + slot._width
                && scene_y >= slot._y && scene_y < slot._y + slot._height) return i;
        }

        return NO_SLOT;
    }

    void dialogue_presentation::set_highlight(u8 slot)
    {
        if (slot == _highlight)
            return;

        if (_highlight != NO_SLOT)
        {
            draw_slot(_highlight, DIALOGUE_CHOICE_COLOR);
        }

        _highlight = slot;
        if (_highlight != NO_SLOT)
        {
            draw_slot(_highlight, DIALOGUE_CHOICE_HIGHLIGHT_COLOR);
        }
    }

    dialogue_action dialogue_presentation::action_for_slot(u8 slot) const noexcept
    {
        if (slot >= _slot_count)
            return {};

        if (_slots[slot]._rotates)
            return { dialogue_action_kind::ROTATE_CHOICES };

        return { dialogue_action_kind::TAKE_CHOICE, _slots[slot]._choice };
    }

    dialogue_action dialogue_presentation::update()
    {
        if (!_display || !_frame_height)
        {
            return {};
        }

        // If we have hold frames, the user has made a choice with a keyboard rather than a mouse,
        // and in order show their selection, we're holding the focused state for a few frames
        // before taking their choice.
        if (_hold_frames)
        {
            consume_input();
            if (--_hold_frames)
                return {};

            u8 const slot = _pending;
            _pending = NO_SLOT;

            return action_for_slot(slot);
        }

        u8 const hovered = hit_test(
            to<u16>(mouseGetX(MOUSE_PORT_1)),
            to<u16>(mouseGetY(MOUSE_PORT_1)));

        set_highlight(hovered);
        _engine.pointer().switch_to(hovered == NO_SLOT ? pointer_role::DEFAULT : pointer_role::USE);

        bool const clicked = mouseUse(MOUSE_PORT_1, MOUSE_LMB);
        bool const pressed = keyUse(KEY_SPACE);

        u8 selected = NO_SLOT;
        for (u8 i = 0; i < DIALOGUE_MAX_CHOICES_SHOWN; ++i)
        {
            if (keyUse(to<u8>(KEY_1 + i)) && i < _slot_count && selected == NO_SLOT)
                selected = i;
        }

        // There are no choices drawn on this page, advance to the next.
        if (!_slot_count)
            return clicked || pressed ? dialogue_action{ dialogue_action_kind::ADVANCE } : dialogue_action{};

        if (selected != NO_SLOT)
        {
            set_highlight(selected);
            _pending = selected;
            _hold_frames = DIALOGUE_CHOICE_HOLD_FRAMES;
            return {};
        }

        if (clicked && hovered != NO_SLOT)
            return action_for_slot(hovered);
        
        return {};
    }

    void dialogue_presentation::close()
    {
        erase_frame();
        erase_portrait();

        // Erasure is asynchronous; restoring colors first would recolor the old overlay.
        if (_display)
        {
            blitWait();
        }
        
        _portraits.reset(nullptr);

        if (_display)
        {
            auto* palette = force_to<ULONG*>(_engine.main_screen().palette());
            palette[DIALOGUE_TEXT_COLOR] = _held_text_color;

            _engine.main_screen().publish_palette(DIALOGUE_TEXT_COLOR, 1);
            
            _display.release();

            _engine.pointer().switch_to(pointer_role::DEFAULT);
        }
        
        release_page();
        _speaker = data::NO_ID;
    }
}  // namespace NEONengine
