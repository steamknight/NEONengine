#include "game_session.h"

#include "core/game_data.h"
#include "core/logger.h"

NE_LOG_DOMAIN(Core);

namespace NEONengine
{
    void game_session::create(u32 page_count, u32 choice_count)
    {
        m_page_count   = page_count;
        m_choice_count = choice_count;

        // assign() clears first, so a session created a second time starts the
        // playthrough's memory over rather than carrying the old bits forward.
        m_pages_read.assign(byte_count(page_count), to<u8>(0));
        m_choices_taken.assign(byte_count(choice_count), to<u8>(0));

        NE_LOG("Game session ready - %lu pages, %lu choices",
            to<ULONG>(page_count),
            to<ULONG>(choice_count));
    }

    void game_session::create_from(game_data const& data)
    {
        create(data.pages().size(), data.choices().size());
    }
}  // namespace NEONengine
