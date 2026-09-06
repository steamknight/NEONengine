#ifndef __STATES_DEBUG_VIEW_H__INCLUDED__
#define __STATES_DEBUG_VIEW_H__INCLUDED__

#include <ace++/font.h>

#include "core/state.h"

namespace NEONengine
{
    class engine;

    class debug_view_state : public state
    {
        public:
        explicit debug_view_state(engine& engine) noexcept;
        ~debug_view_state() override;

        mtl::expected<void, char const*> pushed() override;
        void update(state_control& control) override;

        private:
        void take_reading(ULONG now);
        void erase();
        WORD elapsed_y() const;

        engine& _engine;
        tFont* _font{ nullptr };
        ace::text_bitmap_ptr _memory{ nullptr };
        ace::text_bitmap_ptr _elapsed{ nullptr };
        ULONG _last_reading{ 0 };
        ULONG _fps{ 0 };
        bool _frozen{ false };
        bool _drawn{ false };
        char _memory_text[64]{};
        char _elapsed_text[32]{};
    };
}

#endif
