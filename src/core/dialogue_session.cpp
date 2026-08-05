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
        collect_choices();
    }

    bool dialogue_session::begin(u16 dialogue_id) noexcept
    {
        _page_range   = data::range{};
        _current      = data::NO_ID;
        collect_choices();

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

    u16 dialogue_session::choice_index(u8 available_index) const noexcept
    {
        if (available_index >= _choice_count) return data::NO_ID;
        return _available[available_index];
    }

    u8 dialogue_session::slot_choice(u8 slot) const noexcept
    {
        if (slot >= _slot_count) return NO_CHOICE;
        return _slots[slot];
    }

    void dialogue_session::fit_choices(mtl::span<u16> heights,
        u16 space,
        u16 spacing,
        u16 more_height) noexcept
    {
        _space       = space;
        _spacing     = spacing;
        _more_height = more_height;

        for (u8 choice = 0; choice < _choice_count; ++choice)
        {
            _heights[choice] = choice < heights.size() ? heights[choice] : u16(0);
        }

        // The first pass, and the only one a page that fits ever needs: when
        // everything reaches the end of the list there is no "More..." at all,
        // and no set to rotate to.
        _has_more = build_set(0, space, MAX_CHOICES_SHOWN) < _choice_count;

        if (!_has_more)
        {
            _set_start = 0;
            _set_end   = _choice_count;
            return;
        }

        // The second pass, run for whichever set the player is on: the first on
        // arrival, and the one "More..." moved them to afterwards.
        rebuild_set();
    }

    void dialogue_session::rotate_choices() noexcept
    {
        if (!_has_more) return;

        _set_start = _set_end < _choice_count ? _set_end : u8(0);
        rebuild_set();
    }

    void dialogue_session::rebuild_set() noexcept
    {
        // One "More..." line and the gap above it, taken off the top before
        // anything is fitted, so no choice has to be dropped to make room for it.
        u16 const reserved = to<u16>(_more_height + _spacing);
        u16 const budget   = _space > reserved ? to<u16>(_space - reserved) : u16(0);

        _set_end = build_set(_set_start, budget, to<u8>(MAX_CHOICES_SHOWN - 1));

        // The page keeps room for one choice and a "More..." back before its
        // text is placed, so a set that had to be forced on screen means that
        // reservation was not honoured.
        if (_slot_count == 1 && _heights[_slots[0]] > budget)
        {
            NE_LOG("page %hu has no room for even one choice; showing it anyway", _current);
        }
    }

    u8 dialogue_session::build_set(u8 start, u16 budget, u8 cap) noexcept
    {
        _slot_count = 0;

        u16 used   = 0;
        u8 choice  = start;
        for (; choice < _choice_count; ++choice)
        {
            // A choice the caller could not measure is not drawn, and so is
            // never the reason a set ends: it belongs to whichever set it falls
            // in and shows nothing there.
            if (_heights[choice] == 0) continue;

            if (_slot_count >= cap) break;

            u16 const needed = to<u16>(_slot_count ? _spacing + _heights[choice] : _heights[choice]);
            if (used + needed > budget && _slot_count > 0) break;

            // A set showing nothing at all would leave the player rotating
            // through it forever, so the first measurable choice goes in
            // however little room there is.
            used                 = to<u16>(used + needed);
            _slots[_slot_count++] = choice;
        }

        // Past the unmeasurable choices trailing the set, so the next set never
        // opens on a run of choices that draw nothing.
        while (choice < _choice_count && _heights[choice] == 0) ++choice;

        return choice;
    }

    void dialogue_session::show_every_choice() noexcept
    {
        for (u8 choice = 0; choice < _choice_count; ++choice) _slots[choice] = choice;

        _slot_count  = _choice_count;
        _set_start   = 0;
        _set_end     = _choice_count;
        _has_more    = false;
        _space       = 0;
        _spacing     = 0;
        _more_height = 0;
    }

    data::choice const* dialogue_session::choice_at(u8 available_index) const noexcept
    {
        if (available_index >= _choice_count) return nullptr;
        return &_choices[_available[available_index]];
    }

    void dialogue_session::take_choice(u8 available_index) noexcept
    {
        if (available_index >= _choice_count) return;

        u16 const choice_id = _available[available_index];
        auto const& choice  = _choices[choice_id];

        // Burnt before the destination is followed, so a choice pointing back at
        // its own page finds itself already gone when that page is laid out
        // again - which is what "ask about each topic, then leave" is built out
        // of. Recorded whether or not the one-time bit is set, as a page's read
        // bit is: taking it is the fact, and only availability cares about the
        // bit.
        if (_bridge && _bridge->mark_choice_taken) _bridge->mark_choice_taken(choice_id);

        apply_flags(choice.set_flag_id, choice.clear_flag_id);

        // The destination is abandoned rather than followed: the script has left
        // the scene the conversation was read in, so the page it named belongs
        // somewhere the player no longer is.
        if (run_script(choice.click_function_id))
        {
            end();
            return;
        }

        go_to(choice.goto_page_id);
    }

    void dialogue_session::apply_flags(u16 set_flag_id, u16 clear_flag_id) noexcept
    {
        if (!_bridge || !_bridge->set_flag) return;

        if (set_flag_id != data::NO_ID) _bridge->set_flag(set_flag_id, true);
        if (clear_flag_id != data::NO_ID) _bridge->set_flag(clear_flag_id, false);
    }

    bool dialogue_session::run_script(u16 function_id) noexcept
    {
        if (function_id == data::NO_ID || !_bridge || !_bridge->run_script) return false;

        _bridge->run_script(function_id);

        return _bridge->is_terminated && _bridge->is_terminated();
    }

    void dialogue_session::end() noexcept
    {
        _current = data::NO_ID;
        collect_choices();
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

        // A range running past the loaded choices is truncated to what is there,
        // so a malformed pak costs the player a choice rather than a read off the
        // end of the chunk.
        u32 const start = page ? page->choice_range.start : 0;
        u32 end         = page ? start + page->choice_range.count : 0;
        if (end > _choices.size()) end = _choices.size();

        for (u32 candidate = start; candidate < end; ++candidate)
        {
            if (!is_choice_available(to<u16>(candidate))) continue;

            if (_choice_count >= MAX_CHOICES)
            {
                NE_LOG("page %hu offers more than %hhu choices; the rest are dropped",
                    _current,
                    MAX_CHOICES);
                break;
            }

            _available[_choice_count++] = to<u16>(candidate);
        }

        // Arriving at a page starts on its first set, and shows the lot until
        // the drawing side has measured them and said how much room there is.
        show_every_choice();
    }

    void dialogue_session::go_to(u16 page_id) noexcept
    {
        if (!is_own_page(page_id))
        {
            end();
            return;
        }

        // Forward by index, never along next_page_id: adjacency is what makes a
        // one-time page fall through to its replacement, and a scan bounded by
        // the page range cannot loop the way a walk through the graph could.
        u16 const end_index = to<u16>(_page_range.start + _page_range.count);
        for (u16 candidate = page_id; candidate < end_index; ++candidate)
        {
            if (!is_available(candidate)) continue;

            _current = candidate;
            if (_bridge && _bridge->mark_page_read) _bridge->mark_page_read(candidate);

            // The author's flags first, then the script, so the script has the
            // last word over what the page was written to set.
            apply_flags(_pages[candidate].set_flag_id, _pages[candidate].clear_flag_id);

            if (run_script(_pages[candidate].display_function_id))
            {
                end();
                return;
            }

            // After the page is on show and its script has run, so what is
            // offered is worked out from every burn and every flag this arrival
            // has just made - a one-time choice taken on the way here is gone by
            // the time the player sees the page again, and a choice the script
            // unlocked is there on this visit rather than the next.
            collect_choices();
            return;
        }

        end();
    }
}  // namespace NEONengine
