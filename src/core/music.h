#ifndef __MUSIC_H__INCLUDED__
#define __MUSIC_H__INCLUDED__

#include <exec/types.h>

void musicLoad(char const *szFilePath);
void musicPlayCurrent(UBYTE ubLoop);
void musicFree(void);

#endif //__MUSIC_H__INCLUDED__