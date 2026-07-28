#ifndef __MUSIC_H__INCLUDED__
#define __MUSIC_H__INCLUDED__

#include <exec/types.h>

#include <ace++/file.h>

namespace NEONengine
{
    void musicLoad(ace::file_ptr &&pFile);
    void musicPlayCurrent(UBYTE ubLoop);

    /**
     * @brief Silences the song without unloading it.
     *
     * Sound effects keep playing: only the music is stopped. The module is
     * left loaded, so musicPlayCurrent() starts it again with no disk access.
     */
    void musicStop(void);

    void musicFree(void);
}  // namespace NEONengine

#endif  //__MUSIC_H__INCLUDED__