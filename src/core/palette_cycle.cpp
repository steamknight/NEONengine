#include "palette_cycle.h"

#include "types.h"

#include "neonengine.h"

#include <ace/utils/extview.h>
#include <ace/utils/palette.h>

#include <mtl/utility.h>

#include "core/screen.h"

namespace NEONengine
{
    namespace
    {
        /// No entry has been turned since the last commit.
        constexpr u16 NO_ENTRY = 0xFFFF;

        /// Lowest and highest entry turned since the last commit, inclusive.
        u16 s_dirty_first = NO_ENTRY;
        u16 s_dirty_last  = 0;

        /**
         * @brief Reverses the reference palette entries [@p from, @p to].
         *
         * The building block of the rotation: turning a band right by k is the
         * whole band reversed, then each of its two pieces reversed back. That
         * is one pass over the band and a bit, whatever k is, and needs no
         * scratch buffer - which matters, because a band may be the entire
         * 256-entry palette and the stack is not the place for a copy of it.
         */
        void reverse(ULONG* entries, u16 first, u16 last)
        {
            while (first < last)
            {
                ULONG const held = entries[first];
                entries[first]   = entries[last];
                entries[last]    = held;
                ++first;
                --last;
            }
        }
    }  // namespace

    void palette_cycle_rotate(u8 first, u8 last, u16 steps)
    {
        // Not a band. The editor rejects it, so it means the pak was built by
        // something else; rotating a reversed range would walk off the end of
        // the palette, which is worth refusing rather than reporting.
        if (last <= first) return;

        u16 const width = to<u16>(last - first + 1);

        steps %= width;
        if (steps == 0) return;

        auto* entries = force_to<ULONG*>(screenGetPalette(g_mainScreen));

        reverse(entries, first, last);
        reverse(entries, first, to<u16>(first + steps - 1));
        reverse(entries, to<u16>(first + steps), last);

        if (s_dirty_first == NO_ENTRY || first < s_dirty_first) s_dirty_first = first;
        if (s_dirty_last < last) s_dirty_last = last;
    }

    void palette_cycle_commit()
    {
        if (s_dirty_first == NO_ENTRY) return;

        u16 const first = s_dirty_first;
        u16 const last  = s_dirty_last;
        s_dirty_first        = NO_ENTRY;
        s_dirty_last         = 0;

        // A fade rebuilds the display from the reference for the whole palette
        // this frame anyway, and it runs after this does, so it will carry the
        // rotation across on its own. Doing it here as well would rebuild the
        // hardware palette a second time for nothing.
        if (screenFadeOwnsPalette(g_mainScreen)) return;

        auto const* reference = force_to<ULONG const*>(screenGetPalette(g_mainScreen));
        auto* display = force_to<ULONG*>(screenGetView(g_mainScreen)->pFirstVPort->pPalette);

        // Scaled to whatever brightness the screen currently stands at, not
        // copied straight across. A fade leaves the display wherever it
        // finished, so a screen faded down to black and left there still has a
        // bright reference behind it; copying raw would light the turned band up
        // on its own against the black.
        UBYTE const ratio = screenPaletteRatio(g_mainScreen);
        for (u16 entry = first; entry <= last; ++entry)
        {
            display[entry] = paletteColorDimAga(reference[entry], ratio);
        }

        viewUpdateGlobalPalette(screenGetView(g_mainScreen));
    }
}  // namespace NEONengine
