#include "debug_view.h"

#include "neonengine.h"

#include <printf.h>
#include <proto/exec.h>

#include <ace/managers/key.h>
#include <ace/managers/system.h>
#include <ace/managers/timer.h>

namespace NEONengine
{
    namespace
    {
        constexpr ULONG READING_INTERVAL = 50;
        constexpr UBYTE STATS_COLOR = 24;
    }

    debug_view_state::debug_view_state(engine& engine) noexcept
        : _engine(engine), _display(engine.main_screen())
    {
    }

    mtl::expected<void, char const*> debug_view_state::pushed()
    {
        _font = _engine.default_font();
        if (!_font) return mtl::unexpected("No font for the debug view");
        {
            mtl::system_use_guard guard;
            _memory = ace::fontCreateTextBitMap(160, to<UWORD>(_font->uwHeight * 3));
            _elapsed = ace::fontCreateTextBitMap(160, _font->uwHeight);
        }
        if (!_memory || !_elapsed)
            return mtl::unexpected("Cannot allocate debug view text");

        _last_reading = timerGet();
        _fps = systemIsPal() ? 50 : 60;
        (void)keyUse(KEY_F1);
        if (!_display.acquire()) return mtl::unexpected("Display already acquired");
        return {};
    }

    void debug_view_state::update(state_control& control)
    {
        if (keyUse(KEY_F1))
        {
            control.pop();
            return;
        }

        _engine.pointer().update();
        ULONG const now = timerGet();
        if (!_drawn || timerGetDelta(_last_reading, now) >= READING_INTERVAL) take_reading(now);
    }

    WORD debug_view_state::elapsed_y() const
    {
        return to<WORD>(SCREEN_HEIGHT - _elapsed->uwActualHeight);
    }

    void debug_view_state::erase()
    {
        if (!_drawn) return;
        _engine.main_screen().restore_rect(0, 0, _memory->uwActualWidth, _memory->uwActualHeight);
        _engine.main_screen().restore_rect(
            0, elapsed_y(), _elapsed->uwActualWidth, _elapsed->uwActualHeight);
    }

    void debug_view_state::take_reading(ULONG now)
    {
        _last_reading = now;
        erase();
        sprintf(_memory_text,
            "Chip: %ld KB \nFast: %ld KB \nAny:  %ld KB ",
            memGetFreeChipSize() >> 10,
            AvailMem(MEMF_FAST) >> 10,
            AvailMem(MEMF_ANY) >> 10);
        fontFillTextBitMap(_font, _memory.get(), _memory_text);
        sprintf(_elapsed_text, "Elapsed Time: %lds.", now / _fps);
        fontFillTextBitMap(_font, _elapsed.get(), _elapsed_text);

        _engine.main_screen().text_copy(_memory.get(), 0, 0, STATS_COLOR, FONT_COOKIE);
        _engine.main_screen().text_copy(
            _elapsed.get(), 0, to<UWORD>(elapsed_y()), STATS_COLOR, FONT_COOKIE);
        _drawn = true;
    }

    debug_view_state::~debug_view_state()
    {
        if (_display)
        {
            erase();
            _display.release();
        }
        mtl::system_use_guard guard;
        _memory.reset(nullptr);
        _elapsed.reset(nullptr);
    }
}  // namespace NEONengine
