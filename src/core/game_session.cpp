#include "game_session.h"

#include "core/game_data.h"
#include "core/logger.h"

NE_LOG_DOMAIN(Core);

namespace NEONengine
{
    void game_session::create_for_test(u32 page_count, u32 choice_count, u32 item_count)
    {
        _pages_read    = mtl::bitset<u32>(page_count);
        _choices_taken = mtl::bitset<u32>(choice_count);
        _item_counts.assign(item_count, to<u16>(0));
        _flags = mtl::bitset<u32>(0);

        _values.clear();
        _values.push_back(to<u16>(0));          // build number
        _values.push_back(to<u16>(2));          // game data version
        _values.push_back(NO_SELECTED_ITEM);    // selected inventory item
    }

    void game_session::create_from(game_data const& data)
    {
        _pages_read = mtl::bitset<u32>(data.pages().size());
        _choices_taken = mtl::bitset<u32>(data.choices().size());

        _item_counts.assign(data.inventory_items().size(), to<u16>(0));

        _flags = mtl::bitset<u32>(data.initial_flags().size());
        for (u32 i = 0; i < data.flag_count(); ++i)
        {
            auto byte_index = i >> 3;
            auto bit_index = i % 8;
            _flags.set(i, (data.initial_flags()[byte_index] & (1 << bit_index)) != 0);
        }

        // Values consist of the initial values from the game pak plus engine supplied ones.
        create_initial_values(data.initial_values(), data.header());

        NE_LOG("Game session ready - %lu pages, %lu choices, %lu items, %lu values",
            to<ULONG>(_pages_read.size()),
            to<ULONG>(_choices_taken.size()),
            to<ULONG>(_item_counts.size()),
            to<ULONG>(_values.size()));
    }

    void game_session::create_initial_values(mtl::span<i16> initial_values,
        data::header const& header) noexcept
    {
        // The engine's own values go last, so their position is a fixed offset from the back
        _values.clear();
        _values.reserve(initial_values.size() + to<size_t>(engine_values::COUNT));
        for (auto const& value : initial_values)
        {
            _values.push_back(to<u16>(value));
        }

        _values.push_back(to<u16>(header.build_number));
        _values.push_back(to<u16>(header.version));
        _values.push_back(NO_SELECTED_ITEM);
    }
}  // namespace NEONengine
