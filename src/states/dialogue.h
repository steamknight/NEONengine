#ifndef __STATES_DIALOGUE_H__INCLUDED__
#define __STATES_DIALOGUE_H__INCLUDED__

#include "core/dialogue_presentation.h"
#include "core/dialogue_session.h"
#include "core/state.h"
#include "core/modal_wait.h"
#include "types.h"

namespace NEONengine
{
    class engine;
    class noir_vm;

    /**
     * @brief Runs a dialogue's session, its presentation and the script wait.
     */
    class dialogue_state : public state
    {
        public: //////////////////////////////////////////////////////////////////////////////////

        /**
         * @brief Stores the dialogue's dependencies and assigned wait without touching the screen.
         */
        dialogue_state(engine& engine, noir_vm& vm, u16 dialogue_id, modal_wait wait) noexcept;

        /**
         * @brief Closes the presentation, then completes the assigned wait, even before entry.
         */
        ~dialogue_state() override;

        /**
         * @brief Starts the session and opens its presentation, reporting setup errors.
         */
        mtl::expected<void, char const*> pushed() override;

        /**
         * @brief Runs what the presentation reports, requesting pop when the dialogue ends.
         */
        void update(state_control& control) override;

        private: //////////////////////////////////////////////////////////////////////////////////

        engine& _engine;
        noir_vm& _vm;
        modal_wait _wait;
        u16 _dialogue_id;
        dialogue_context _context;
        dialogue_session _session;
        dialogue_presentation _presentation;
    };
}

#endif
