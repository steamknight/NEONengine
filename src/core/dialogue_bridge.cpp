#include "core/dialogue_bridge.h"

#include "types.h"

#include <mtl/utility.h>

#include "core/game_session.h"
#include "core/noir_vm.h"

namespace NEONengine
{
    namespace
    {
        void db_mark_page_read(dialogue_context* host, u16 page_index)
        {
            mtl::to<dialogue_context*>(host)->_session.mark_page_read(page_index);
        }

        bool db_is_page_read(dialogue_context* host, u16 page_index)
        {
            return mtl::to<dialogue_context*>(host)->_session.is_page_read(page_index);
        }

        void db_mark_choice_taken(dialogue_context* host, u16 choice_index)
        {
            mtl::to<dialogue_context*>(host)->_session.mark_choice_taken(choice_index);
        }

        bool db_is_choice_taken(dialogue_context* host, u16 choice_index)
        {
            return mtl::to<dialogue_context*>(host)->_session.is_choice_taken(choice_index);
        }

        bool db_get_flag(dialogue_context* host, u16 flag_id)
        {
            return mtl::to<dialogue_context*>(host)->_session.get_flags().test(flag_id);
        }

        void db_set_flag(dialogue_context* host, u16 flag_id, bool value)
        {
            mtl::to<dialogue_context*>(host)->_session.get_flags().set(flag_id, value);
        }

        void db_run_script(dialogue_context* host, u16 function_id)
        {
            // Nonterminal errors stay queued until the dialogue closes.
            (void)mtl::to<dialogue_context*>(host)->_vm.execute_now(function_id);
        }

        bool db_is_terminated(dialogue_context* host)
        {
            return mtl::to<dialogue_context*>(host)->_vm.terminated();
        }

        constexpr dialogue_bridge g_engine_dialogue_bridge = {
            .mark_page_read    = db_mark_page_read,
            .is_page_read      = db_is_page_read,
            .mark_choice_taken = db_mark_choice_taken,
            .is_choice_taken   = db_is_choice_taken,
            .get_flag          = db_get_flag,
            .set_flag          = db_set_flag,
            .run_script        = db_run_script,
            .is_terminated     = db_is_terminated,
        };
    }  // namespace

    dialogue_bridge const& engine_dialogue_bridge() noexcept { return g_engine_dialogue_bridge; }

    constexpr bool is_dialogue_bridge_ready() noexcept
    {
        auto bridge = g_engine_dialogue_bridge;
        return bridge.mark_page_read && bridge.is_page_read && bridge.mark_choice_taken
            && bridge.is_choice_taken && bridge.get_flag && bridge.set_flag
            && bridge.run_script && bridge.is_terminated;
    }

    static_assert(is_dialogue_bridge_ready(), "the engine's dialogue bridge is not fully wired");
}  // namespace NEONengine
