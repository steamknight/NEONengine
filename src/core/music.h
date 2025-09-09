#ifndef __MUSIC_H__INCLUDED__
#define __MUSIC_H__INCLUDED__

#include <exec/types.h>

namespace NEONengine
{
    void musicLoad(char const *szFilePath);
    void musicPlayCurrent(UBYTE ubLoop);
    void musicFree(void);
}  // namespace NEONengine

#endif  //__MUSIC_H__INCLUDED__