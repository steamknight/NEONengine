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

    void dialogue_session::go_to(u16 page_id) noexcept
    {
        if (!is_own_page(page_id))
        {
            _current = data::NO_ID;
            return;
        }

        _current = page_id;
        if (_bridge && _bridge->mark_page_read) _bridge->mark_page_read(page_id);
    }
}  // namespace NEONengine
