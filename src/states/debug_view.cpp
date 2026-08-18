#include "neonengine.h"

#include <printf.h>

#include <proto/exec.h>  // Bartman's compiler needs this

#include <ace/managers/blit.h>
#include <ace/managers/system.h>
#include <ace/managers/timer.h>
#include <ace/utils/font.h>
#include <ace/utils/palette.h>

#include <ace++/font.h>

#include "core/logger.h"
#include "core/mouse_pointer.h"
#include "core/screen.h"
#include "core/state_host.h"

NE_LOG_DOMAIN(Core);

namespace NEONengine
{
    namespace
    {
        /// Timer ticks between readings.
        constexpr ULONG READING_INTERVAL = 50;

        /// Palette index the figures are drawn in.
        constexpr UBYTE STATS_COLOR = 24;

        /** @brief What is left of each kind of memory, and how long we have run. */
        class debug_view_state
        {
            public:  //////////////////////////////////////////////////////////////////////////////
            bool create();
            bool loop();
            ~debug_view_state();

            private:  /////////////////////////////////////////////////////////////////////////////
            void take_reading(ULONG now);

            /// Where the elapsed time sits, which its own height decides.
            WORD elapsed_y() const { return to<WORD>(SCREEN_HEIGHT - _elapsed->uwActualHeight); }

            tFont*               _font{ nullptr };  ///< The engine's, borrowed.
            ace::text_bitmap_ptr _memory{ nullptr };
            ace::text_bitmap_ptr _elapsed{ nullptr };

            ULONG _last_reading{ 0 };
            ULONG _fps{ 0 };

            char _memory_text[64]{};
            char _elapsed_text[32]{};

            freeze_display _freeze_display{ g_pEngine->main_screen() };
        };

        bool debug_view_state::create()
        {
            _font = g_pEngine->default_font();

            logBlockBegin("debug view");
            _memory  = ace::fontCreateTextBitMap(160, to<UWORD>(_font->uwHeight * 3));
            _elapsed = ace::fontCreateTextBitMap(160, _font->uwHeight);
            logBlockEnd("debug view");

            if (!_memory || !_elapsed)
            {
                NE_LOG("no room for the debug view's text");
                return false;
            }

            _last_reading = timerGet();
            _fps          = systemIsPal() ? 50 : 60;

            return true;
        }

        bool debug_view_state::loop()
        {
            // Nothing else runs while the figures are up, so the sprite would sit
            // where it was until they came down.
            g_pEngine->pointer().update();

            ULONG const now = timerGet();
            if (timerGetDelta(_last_reading, now) >= READING_INTERVAL) take_reading(now);

            return true;
        }

        void debug_view_state::take_reading(ULONG now)
        {
            _last_reading = now;

            sprintf(_memory_text,
                "Chip: %ld KB \nFast: %ld KB \nAny:  %ld KB ",
                memGetFreeChipSize() >> 10,
                AvailMem(MEMF_FAST) >> 10,
                AvailMem(MEMF_ANY) >> 10);
            fontFillTextBitMap(_font, _memory.get(), _memory_text);

            sprintf(_elapsed_text, "Elapsed Time: %lds.", now / _fps);
            fontFillTextBitMap(_font, _elapsed.get(), _elapsed_text);

            g_pEngine->main_screen().restore_rect(0, 0, _memory->uwActualWidth, _memory->uwActualHeight);
            g_pEngine->main_screen().text_copy(_memory.get(), 0, 0, STATS_COLOR, FONT_COOKIE);

            g_pEngine->main_screen().restore_rect(
                0,
                elapsed_y(),
                _elapsed->uwActualWidth,
                _elapsed->uwActualHeight);
            g_pEngine->main_screen().text_copy(
                _elapsed.get(), 0, to<UWORD>(elapsed_y()), STATS_COLOR, FONT_COOKIE);
        }

        debug_view_state::~debug_view_state()
        {
            // Nothing was drawn when the text could not be made.
            if (!_memory || !_elapsed) return;

            // Zero-sized until the first reading, which g_pEngine->main_screen().restore_rect refuses.
            g_pEngine->main_screen().restore_rect(0, 0, _memory->uwActualWidth, _memory->uwActualHeight);
            g_pEngine->main_screen().restore_rect(
                0,
                elapsed_y(),
                _elapsed->uwActualWidth,
                _elapsed->uwActualHeight);
        }
    }  // namespace

    tState g_stateDebugView = state_host<debug_view_state>::table();
}  // namespace NEONengine
