#include "core/dialogue_bridge.h"

#include "types.h"

#include "neonengine.h"

namespace NEONengine
{
    namespace
    {
        void db_mark_page_read(u16 page_index)
        {
            if (g_pEngine) g_pEngine->session().mark_page_read(page_index);
        }

        bool db_is_page_read(u16 page_index)
        {
            return g_pEngine && g_pEngine->session().is_page_read(page_index);
        }

        void db_mark_choice_taken(u16 choice_index)
        {
            if (g_pEngine) g_pEngine->session().mark_choice_taken(choice_index);
        }

        bool db_is_choice_taken(u16 choice_index)
        {
            return g_pEngine && g_pEngine->session().is_choice_taken(choice_index);
        }

        bool db_get_flag(u16 flag_id)
        {
            // Flags still live in the buffer the VM borrows rather than in the
            // game session, so that is where the answer comes from.
            return g_pEngine && g_pEngine->get_flag(flag_id);
        }

        dialogue_bridge const g_engine_dialogue_bridge = {
            .mark_page_read    = db_mark_page_read,
            .is_page_read      = db_is_page_read,
            .mark_choice_taken = db_mark_choice_taken,
            .is_choice_taken   = db_is_choice_taken,
            .get_flag          = db_get_flag,
        };
    }  // namespace

    dialogue_bridge const& engine_dialogue_bridge() noexcept { return g_engine_dialogue_bridge; }
}  // namespace NEONengine
