#include "core/dialogue_session.h"

#include "types.h"

namespace NEONengine
{
    void dialogue_session::create(mtl::span<data::dialogue> dialogues,
        mtl::span<data::page> pages,
        dialogue_bridge const* bridge) noexcept
    {
        _dialogues  = dialogues;
        _pages      = pages;
        _bridge     = bridge;
        _page_range = data::range{};
        _current    = data::NO_ID;
    }

    bool dialogue_session::begin(u16 dialogue_id) noexcept
    {
        _page_range = data::range{};
        _current    = data::NO_ID;

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

    void dialogue_session::go_to(u16 page_id) noexcept
    {
        if (!is_own_page(page_id))
        {
            _current = data::NO_ID;
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
            return;
        }

        _current = data::NO_ID;
    }
}  // namespace NEONengine
