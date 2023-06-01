#include "music.h"

#include <ace/managers/ptplayer.h>
#include "neonengine.h"

static tPtplayerMod *s_currentMod;

void musicLoad(char const *szFilePath)
{
    BEGIN_USE_SYSTEM
    if (s_currentMod)
    {
        ptplayerStop();
        ptplayerModDestroy(s_currentMod);
        s_currentMod = 0;
    }

    s_currentMod = ptplayerModCreate(szFilePath);

    END_USE_SYSTEM
}

void musicPlayCurrent(UBYTE ubLoop)
{
    ptplayerLoadMod(s_currentMod, NULL, 0);
    ptplayerConfigureSongRepeat(ubLoop, 0);
    ptplayerEnableMusic(1);
}

void musicFree(void)
{
    BEGIN_USE_SYSTEM
    if (s_currentMod)
    {
        ptplayerModDestroy(s_currentMod);
    }
    END_USE_SYSTEM
}
