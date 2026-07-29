#include "memory_file.h"

#include "types.h"

#include <ace/managers/log.h>
#include <ace/managers/memory.h>

#include <mtl/utility.h>

#include <string.h>

namespace ace
{
    namespace
    {
        /// The borrowed range and the read cursor over it.
        struct memory_file_data
        {
            u8 const* base;
            ULONG size;
            ULONG pos;
        };

        void memoryFileClose(void* pData)
        {
            // Frees the cursor only. The bytes are borrowed, and ACE's
            // fileClose() frees the tFile itself right after this returns.
            memFree(pData, sizeof(memory_file_data));
        }

        ULONG memoryFileRead(void* pData, void* pDest, ULONG ulSize)
        {
            auto* pFileData = to<memory_file_data*>(pData);

            ULONG const ulRemaining = pFileData->size - pFileData->pos;
            if (ulSize > ulRemaining) ulSize = ulRemaining;
            if (!ulSize) return 0;

            memcpy(pDest, pFileData->base + pFileData->pos, ulSize);
            pFileData->pos += ulSize;
            return ulSize;
        }

        ULONG memoryFileWrite(void*, void const*, ULONG)
        {
            logWrite("ERR: Unsupported: memoryFileWrite()\n");
            return 0;
        }

        ULONG memoryFileSeek(void* pData, LONG lPos, WORD wMode)
        {
            auto* pFileData = to<memory_file_data*>(pData);

            // Signed, so a seek back past the start is caught as the negative
            // it is rather than the huge unsigned it would wrap to.
            LONG lTarget = to<LONG>(pFileData->pos);
            if (wMode == FILE_SEEK_SET) lTarget = lPos;
            else if (wMode == FILE_SEEK_CURRENT) lTarget += lPos;
            else if (wMode == FILE_SEEK_END) lTarget = to<LONG>(pFileData->size) + lPos;

            if (lTarget < 0 || to<ULONG>(lTarget) > pFileData->size)
            {
                logWrite("ERR: Seek position %ld out of range %lu for memory file %p\n",
                    lTarget, pFileData->size, pData);
                // EOF, so a caller that ignores the failure reads nothing
                // rather than the wrong bytes.
                pFileData->pos = pFileData->size;
                return 0;
            }

            pFileData->pos = to<ULONG>(lTarget);
            return 1;
        }

        ULONG memoryFileGetPos(void* pData)
        {
            return to<memory_file_data*>(pData)->pos;
        }

        ULONG memoryFileGetSize(void* pData)
        {
            return to<memory_file_data*>(pData)->size;
        }

        UBYTE memoryFileIsEof(void* pData)
        {
            auto* pFileData = to<memory_file_data*>(pData);
            return pFileData->pos >= pFileData->size;
        }

        void memoryFileFlush(void*)
        {
            // no-op
        }

        tFileCallbacks const s_sMemoryFileCallbacks = {
            .cbFileClose   = memoryFileClose,
            .cbFileRead    = memoryFileRead,
            .cbFileWrite   = memoryFileWrite,
            .cbFileSeek    = memoryFileSeek,
            .cbFileGetPos  = memoryFileGetPos,
            .cbFileGetSize = memoryFileGetSize,
            .cbFileIsEof   = memoryFileIsEof,
            .cbFileFlush   = memoryFileFlush,
        };
    }  // namespace

    tFile* memoryFileOpen(u8 const* pData, ULONG ulSize)
    {
        if (!pData) return nullptr;

        auto* pFileData = to<memory_file_data*>(memAllocFast(sizeof(memory_file_data)));
        if (!pFileData) return nullptr;

        pFileData->base = pData;
        pFileData->size = ulSize;
        pFileData->pos  = 0;

        // Allocated the way ACE allocates its own, because ACE's fileClose()
        // frees this with memFree(pFile, sizeof(*pFile)).
        auto* pFile = to<tFile*>(memAllocFast(sizeof(tFile)));
        if (!pFile)
        {
            memFree(pFileData, sizeof(memory_file_data));
            return nullptr;
        }

        pFile->pCallbacks = &s_sMemoryFileCallbacks;
        pFile->pData      = pFileData;
        return pFile;
    }
}  // namespace ace
