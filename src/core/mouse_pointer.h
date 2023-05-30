#ifndef __MOUSE_POINTER_H__INCLUDED__
#define __MOUSE_POINTER_H__INCLUDED__

typedef enum _MousePointer
{
    MOUSE_TEST,
    MOUSE_POINTER,
    MOUSE_EXAMINE,
    MOUSE_USE,
    MOUSE_EXIT,
    MOUSE_WAIT,
    MOUSE_MAX_COUNT,
} eMousePointer;

/**
 * @brief Create the mouse pointers based on an atlas image. It generates
 * mouse pointers to correspond to the ones listed in the mouse_pointer enum
 *
 * @param szFilePath Path to the bitmap atlas to use. The cursors must be 16x16
 * and be lined up horizontally.
 *
 * @see eMousePointer
 */
void mousePointerCreate(char const *szFilePath);

/**
 * @brief Changes the active mouse pointer.
 *
 * @param newPointer The new pointer to show.
 *
 * @see eMousePointer
 */
void mousePointerSwitch(eMousePointer newPointer);

/**
 * @brief Updates the position of the mouse, must be called once per frame.
 */
void mousePointerUpdate(void);

/**
 * @brief Destroys and deallocates the memory used by this module.
 */
void mousePointerDestroy(void);

#endif // __MOUSE_POINTER_H__INCLUDED__