#ifndef __MUSIC_H__INCLUDED__
#define __MUSIC_H__INCLUDED__

#include <exec/types.h>

void music_load(char const *filename);
void music_play_current(UBYTE loop);
void music_free(void);

#endif //__MUSIC_H__INCLUDED__