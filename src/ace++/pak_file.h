#ifndef __ACEPP__PAK_FILE_H___INCLUDED__
#define __ACEPP__PAK_FILE_H___INCLUDED__

#include <ace/utils/file.h>
#include <ace/utils/pak_file.h>

#include <mtl/memory.h>

namespace ace
{
    using pakfile_ptr = mtl::unique_ptr<tPakFile, ::pakFileClose>;
    using file_ptr    = mtl::unique_ptr<tFile, ::fileClose>;

    /**
     * @brief Opens a pak archive, owning the handle.
     *
     * @param szPath          Path to the pak file.
     * @param isUninterrupted Non-zero for a fast, uninterrupted read.
     * @return An owning handle; test it for validity before use.
     */
    inline pakfile_ptr pakFileOpen(char const* szPath, UBYTE isUninterrupted)
    {
        return pakfile_ptr(::pakFileOpen(szPath, isUninterrupted));
    }

    /**
     * @brief Opens a subfile within a pak by its internal path.
     *
     * @param pPakFile        The open pak archive.
     * @param szInternalPath  The subfile's pak-relative path.
     * @return An owning subfile handle; test it for validity before use.
     */
    inline file_ptr pakFileGetFile(pakfile_ptr& pPakFile, char const* szInternalPath)
    {
        return file_ptr(::pakFileGetFile(pPakFile.get(), szInternalPath));
    }
}  // namespace ace

#endif  // __ACEPP__PAK_FILE_H___INCLUDED__
