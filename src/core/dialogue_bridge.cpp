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

        dialogue_bridge const g_engine_dialogue_bridge = {
            .mark_page_read = db_mark_page_read,
        };
    }  // namespace

    dialogue_bridge const& engine_dialogue_bridge() noexcept { return g_engine_dialogue_bridge; }
}  // namespace NEONengine
