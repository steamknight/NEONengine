#include "music.h"

#include "neonengine.h"
#include <ace/managers/system.h>
#include <ace/managers/ptplayer.h>

static tPtplayerMod *current_mod_;

void music_load(char const *filename)
{
    BEGIN_USE_SYSTEM
    if (current_mod_)
    {
        ptplayerStop();
        ptplayerModDestroy(current_mod_);
        current_mod_ = 0;
    }

    current_mod_ = ptplayerModCreate(filename);

    END_USE_SYSTEM
}

void music_play_current(UBYTE loop)
{
    ptplayerLoadMod(current_mod_, NULL, 0);
    ptplayerConfigureSongRepeat(loop, 0);
    ptplayerEnableMusic(1);
}

void music_free(void)
{
    BEGIN_USE_SYSTEM
    if (current_mod_)
    {
        ptplayerModDestroy(current_mod_);
    }
    END_USE_SYSTEM
}
