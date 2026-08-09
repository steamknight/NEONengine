#include "game_session.h"

#include "core/game_data.h"
#include "core/logger.h"

NE_LOG_DOMAIN(Core);

namespace NEONengine
{
    void game_session::create_from(game_data const& data)
    {
        _page_count   = data.pages().size();
        _choice_count = data.choices().size();
        _item_count   = data.inventory_items().size();

        // assign() clears first, so a session created a second time starts the
        // playthrough's memory over rather than carrying the old bits forward.
        _pages_read.assign(byte_count(_page_count), to<u8>(0));
        _choices_taken.assign(byte_count(_choice_count), to<u8>(0));
        _item_counts.assign(_item_count, to<u16>(0));

        // Values consist of the initial values from the game pak plus engine supplied ones.
        _values.reserve(data.initial_values().size());
        for (auto const& value : data.initial_values())
        {
            _values.push_back(value);
        }

        NE_LOG("Game session ready - %lu pages, %lu choices, %lu items",
            to<ULONG>(_page_count),
            to<ULONG>(_choice_count),
            to<ULONG>(_item_count));
    }

    void game_session::create_initial_values(mtl::vector<u16> const& initial_values,
        data::header const& header) noexcept
    {
        _values.reserve(initial_values.size() + to<size_t>(engine_values::COUNT));
        for (auto const& value : initial_values)
        {
            _values.push_back(value);
        }

        _values.push_back(to<u16>(header.build_number));
        _values.push_back(to<u16>(header.version));
        _values.push_back(to<u16>(NO_SELECTED_ITEM));
    }
}  // namespace NEONengine
