#include "music.h"

#include <ace/managers/ptplayer.h>
#include "neonengine.h"

namespace NEONengine
{
    static tPtplayerMod *s_currentMod;

    void musicLoad(char const *szFilePath)
    {
        systemUse();
        if (s_currentMod)
        {
            ptplayerStop();
            ptplayerModDestroy(s_currentMod);
            s_currentMod = 0;
        }

        s_currentMod = ptplayerModCreateFromPath(szFilePath);

        systemUnuse();
    }

    void musicPlayCurrent(UBYTE ubLoop)
    {
        ptplayerLoadMod(s_currentMod, NULL, 0);
        ptplayerConfigureSongRepeat(ubLoop, 0);
        ptplayerEnableMusic(1);
    }

    void musicFree(void)
    {
        systemUse();
        if (s_currentMod)
        {
            ptplayerModDestroy(s_currentMod);
        }
        systemUnuse();
    }
}
