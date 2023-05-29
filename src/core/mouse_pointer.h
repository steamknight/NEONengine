#ifndef __MOUSE_POINTER_H__INCLUDED__
#define __MOUSE_POINTER_H__INCLUDED__

typedef enum mouse_pointer
{
    MOUSE_TEST,
    MOUSE_POINTER,
    MOUSE_EXAMINE,
    MOUSE_USE,
    MOUSE_EXIT,
    MOUSE_WAIT,
    MOUSE_MAX_COUNT,
} mouse_pointer_t;

/**
 * @brief Create the mouse pointers based on an atlas image. It generates
 * mouse pointers to correspond to the ones listed in the mouse_pointer enum
 *
 * @param filepath Path to the bitmap atlas to use. The cursors must be 16x16
 * and be lined up horizontally.
 *
 * @see mouse_pointer
 */
void mouse_pointer_create(char const *filepath);

/**
 * @brief Changes the active mouse pointer.
 *
 * @param new_pointer The new pointer to show.
 *
 * @see mouse_pointer
 */
void mouse_pointer_switch(mouse_pointer_t new_pointer);

/**
 * @brief Updates the position of the mouse, must be called once per frame.
 */
void mouse_pointer_update(void);

/**
 * @brief Destroys and deallocates the memory used by this module.
 */
void mouse_pointer_destroy(void);

#endif // __MOUSE_POINTER_H__INCLUDED__