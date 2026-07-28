#ifndef __MOUSE_POINTER_H__INCLUDED__
#define __MOUSE_POINTER_H__INCLUDED__

#include "types.h"

#include <stdint.h>

namespace NEONengine
{
    /**
     * @brief The fixed cursor roles, in the order 'CURS' assigns them.
     *
     * The enumerators are the record's own ids, so a hotspot's cursor field
     * converts straight to one of these. Only the engine selects WAIT; a
     * hotspot may name any of the roles before it.
     */
    enum class mouse_pointer : u16
    {
        DEFAULT,
        LOOK,
        USE,
        TALK,
        GOTO,
        WAIT,
        COUNT
    };

    /**
     * @brief Builds the cursors from the project's own 'CURS' sheet.
     *
     * Reads the sheet named by 'UIST' out of the pak, cuts the frame each role
     * points at, and records that role's click offset. The sheet's colors are
     * baked against the shared UI palette that the engine loads at startup, so
     * none travels with it. Reads the pak, so call under a system-use guard.
     *
     * @return True when the cursors are ready to show.
     */
    bool mousePointerCreate();

    /**
     * @brief Shows @p newPointer, keeping its click offset aligned.
     *
     * @param newPointer The role to display.
     */
    void mousePointerSwitch(mouse_pointer newPointer);

    /**
     * @brief Shows or hides the hardware pointer.
     *
     * A scene that shows no UI shows no cursor either, which is what SCEN's
     * flags bit 1 describes. Hiding matters beyond taste there: such a scene may
     * author its background across the pointer's palette band, so a cursor left
     * on screen would draw itself in the background's colors.
     *
     * The sprites are published by the next mousePointerUpdate(), so nothing has
     * to be done here to make it take effect.
     */
    void mousePointerSetVisible(bool visible);

    /**
     * @brief Moves the pointer to the mouse. Call once per frame.
     */
    void mousePointerUpdate();

    /**
     * @brief Destroys and deallocates the memory used by this module.
     */
    void mousePointerDestroy();
}  // namespace NEONengine

#endif  // __MOUSE_POINTER_H__INCLUDED__
