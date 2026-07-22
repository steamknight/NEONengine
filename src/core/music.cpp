#include "music.h"

#include "neonengine.h"

#include <ace/managers/ptplayer.h>

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

    void musicLoad(ace::file_ptr &&pFile)
    {
        // The caller already holds the system (see scene loading), so unlike the
        // path overload this does not systemUse/Unuse: it runs inside a wider
        // system_use_guard region. ptplayerModCreateFromFd closes the file, so
        // ownership is released into it.
        if (s_currentMod)
        {
            ptplayerStop();
            ptplayerModDestroy(s_currentMod);
            s_currentMod = 0;
        }

        s_currentMod = ptplayerModCreateFromFd(pFile.release());
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
        if (s_currentMod) { ptplayerModDestroy(s_currentMod); }
        systemUnuse();
    }
}  // namespace NEONengine
