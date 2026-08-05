#include "core/dialogue_session.h"

#include "types.h"

#include "core/logger.h"

NE_LOG_DOMAIN(Dialogue);

namespace NEONengine
{
    void dialogue_session::create(mtl::span<data::dialogue> dialogues,
        mtl::span<data::page> pages,
        mtl::span<data::choice> choices,
        dialogue_bridge const* bridge) noexcept
    {
        _dialogues     = dialogues;
        _pages         = pages;
        _choices       = choices;
        _bridge        = bridge;
        _page_range    = data::range{};
        _current       = data::NO_ID;
        _choice_count  = 0;
    }

    bool dialogue_session::begin(u16 dialogue_id) noexcept
    {
        _page_range   = data::range{};
        _current      = data::NO_ID;
        _choice_count = 0;

        if (dialogue_id >= _dialogues.size()) return false;

        auto const& dialogue = _dialogues[dialogue_id];

        // A range running past the loaded pages is truncated to what is there,
        // so a malformed pak costs a short conversation rather than a read off
        // the end of the chunk.
        _page_range = dialogue.page_range;
        if (_page_range.start >= _pages.size())
        {
            _page_range = data::range{};
        }
        else if (_page_range.start + _page_range.count > _pages.size())
        {
            _page_range.count = to<u16>(_pages.size() - _page_range.start);
        }

        go_to(dialogue.first_page_id);

        // A conversation that could not open owns no pages: a caller reading
        // ahead over them - for the portraits its speakers name, say - would
        // otherwise work through a dialogue that is never put on screen.
        if (has_ended()) _page_range = data::range{};

        return !has_ended();
    }

    data::page const* dialogue_session::current_page() const noexcept
    {
        if (has_ended()) return nullptr;
        return &_pages[_current];
    }

    void dialogue_session::advance() noexcept
    {
        auto const* page = current_page();
        if (!page) return;

        go_to(page->next_page_id);
    }

    u16 dialogue_session::choice_index(u8 slot) const noexcept
    {
        if (slot >= _choice_count) return data::NO_ID;
        return _available[slot];
    }

    data::choice const* dialogue_session::choice_at(u8 slot) const noexcept
    {
        if (slot >= _choice_count) return nullptr;
        return &_choices[_available[slot]];
    }

    void dialogue_session::take_choice(u8 slot) noexcept
    {
        if (slot >= _choice_count) return;

        u16 const choice_id  = _available[slot];
        auto const& choice   = _choices[choice_id];

        // Burnt before the destination is followed, so a choice pointing back at
        // its own page finds itself already gone when that page is laid out
        // again - which is what "ask about each topic, then leave" is built out
        // of. Recorded whether or not the one-time bit is set, as a page's read
        // bit is: taking it is the fact, and only availability cares about the
        // bit.
        if (_bridge && _bridge->mark_choice_taken) _bridge->mark_choice_taken(choice_id);

        go_to(choice.goto_page_id);
    }

    bool dialogue_session::is_own_page(u16 page_id) const noexcept
    {
        if (page_id == data::NO_ID) return false;
        if (page_id < _page_range.start) return false;
        return page_id < _page_range.start + _page_range.count;
    }

    bool dialogue_session::is_available(u16 page_id) const noexcept
    {
        auto const& page = _pages[page_id];

        if ((page.flags & data::PAGE_FLAG_ENABLED) == 0) return false;

        if (page.check_flag_id != data::NO_ID && _bridge && _bridge->get_flag
            && !_bridge->get_flag(page.check_flag_id))
        {
            return false;
        }

        if ((page.flags & data::PAGE_FLAG_ONE_TIME) != 0 && _bridge && _bridge->is_page_read
            && _bridge->is_page_read(page_id))
        {
            return false;
        }

        return true;
    }

    bool dialogue_session::is_choice_available(u16 choice_id) const noexcept
    {
        auto const& choice = _choices[choice_id];

        if ((choice.flags & data::CHOICE_FLAG_ENABLED) == 0) return false;

        if (choice.check_flag_id != data::NO_ID && _bridge && _bridge->get_flag
            && !_bridge->get_flag(choice.check_flag_id))
        {
            return false;
        }

        if ((choice.flags & data::CHOICE_FLAG_ONE_TIME) != 0 && _bridge && _bridge->is_choice_taken
            && _bridge->is_choice_taken(choice_id))
        {
            return false;
        }

        return true;
    }

    void dialogue_session::collect_choices() noexcept
    {
        _choice_count = 0;

        auto const* page = current_page();
        if (!page) return;

        // A range running past the loaded choices is truncated to what is there,
        // so a malformed pak costs the player a reply rather than a read off the
        // end of the chunk.
        u32 const start = page->choice_range.start;
        if (start >= _choices.size()) return;

        u32 end = start + page->choice_range.count;
        if (end > _choices.size()) end = _choices.size();

        for (u32 candidate = start; candidate < end; ++candidate)
        {
            if (!is_choice_available(to<u16>(candidate))) continue;

            if (_choice_count >= MAX_CHOICES)
            {
                NE_LOG("page %hu offers more than %hhu choices; the rest are dropped",
                    _current,
                    MAX_CHOICES);
                return;
            }

            _available[_choice_count++] = to<u16>(candidate);
        }
    }

    void dialogue_session::go_to(u16 page_id) noexcept
    {
        if (!is_own_page(page_id))
        {
            _current      = data::NO_ID;
            _choice_count = 0;
            return;
        }

        // Forward by index, never along next_page_id: adjacency is what makes a
        // one-time page fall through to its replacement, and a scan bounded by
        // the page range cannot loop the way a walk through the graph could.
        u16 const end = to<u16>(_page_range.start + _page_range.count);
        for (u16 candidate = page_id; candidate < end; ++candidate)
        {
            if (!is_available(candidate)) continue;

            _current = candidate;
            if (_bridge && _bridge->mark_page_read) _bridge->mark_page_read(candidate);

            // After the page is on show, so what is offered is worked out from
            // the burns this arrival has just made - a one-time choice taken on
            // the way here is gone by the time the player sees the page again.
            collect_choices();
            return;
        }

        _current      = data::NO_ID;
        _choice_count = 0;
    }
}  // namespace NEONengine
