#ifndef __STATES_SCENE_H__INCLUDED__
#define __STATES_SCENE_H__INCLUDED__

#include "core/scene_runtime.h"
#include "core/state.h"
#include "core/string_table.h"
#include "types.h"

namespace NEONengine
{
    class engine;

    class scene_state : public state
    {
        public:
        scene_state(engine& engine, u16 scene_id) noexcept;
        ~scene_state() override;

        mtl::expected<void, char const*> pushed() override;
        void update(state_control& control) override;
        void suspended() override;
        void resumed() override;

        private:
        mtl::expected<void, char const*> load_scene();
        void start_entry_script();
        void start_pending_modal();
        bool apply_system_request(state_control& control);
        bool rebuild_for_language(bool after_scripts = false);

        engine& _engine;
        u16 _scene_id;
        scene_runtime_ptr _runtime{ nullptr };
        language _rendered_language{ language::COUNT };
        bool _log_open{ false };
    };
}  // namespace NEONengine

#endif  // __STATES_SCENE_H__INCLUDED__
