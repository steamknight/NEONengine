#ifndef __TYPES_H__INCLUDED__
#define __TYPES_H__INCLUDED__
#include <ace/types.h>

/**
 * @brief Generic range into an array using indices. Inclusive.
 */
typedef struct _Range
{
    UWORD uwFirstIndex;
    UWORD uwLastIndex;
} Range;

#endif // __TYPES_H__INCLUDED__