#include "music.h"

#include "neonengine.h"

#include <ace/managers/ptplayer.h>

namespace NEONengine
{
    static tPtplayerMod *s_currentMod;

    /**
     * @brief Stops whatever is playing and releases the current MOD.
     *
     * Clearing the pointer is the whole point: every caller tests it before
     * destroying, so leaving it set behind a destroy hands the next caller a
     * freed MOD to destroy a second time. Playback is stopped first because
     * the replayer reads the sample data straight out of the MOD, so freeing
     * one still being played pulls the ground out from under it.
     *
     * Does not touch systemUse/Unuse: one caller already holds the system and
     * the others take it around a wider region.
     */
    static void musicReleaseCurrent(void)
    {
        if (!s_currentMod) return;

        ptplayerStop();
        ptplayerModDestroy(s_currentMod);
        s_currentMod = 0;
    }

    void musicLoad(ace::file_ptr &&pFile)
    {
        // The caller holds the system for the whole scene load, so this does not
        // take it again. ptplayerModCreateFromFd closes the file, so ownership is
        // released into it.
        musicReleaseCurrent();

        s_currentMod = ptplayerModCreateFromFd(pFile.release());
    }

    void musicPlayCurrent(UBYTE ubLoop)
    {
        // A module that would not load leaves nothing to play, and
        // ptplayerLoadMod takes the pointer without ever testing it.
        if (!s_currentMod) return;

        ptplayerLoadMod(s_currentMod, NULL, 0);
        ptplayerConfigureSongRepeat(ubLoop, 0);
        ptplayerEnableMusic(1);
    }

    void musicStop(void)
    {
        // Not ptplayerStop(), which needs the module loading again afterwards
        // and would take the sound effects down with it. Disabling music leaves
        // the replayer running, so effects carry on and the song can resume.
        ptplayerEnableMusic(0);
    }

    void musicFree(void)
    {
        systemUse();
        musicReleaseCurrent();
        systemUnuse();
    }
}  // namespace NEONengine
