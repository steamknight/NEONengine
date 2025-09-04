#include "neonengine.h"
#include "lang.h"

#include <ace/managers/log.h>
#include <ace/managers/system.h>
#include <ace/utils/disk_file.h>

#include "utils/bstr.h"

namespace NEONengine
{
    typedef struct LocHeader
    {
        UWORD uwVersion;
        UWORD uwLanguage;
    };

    typedef struct NeonStringTable
    {
        ULONG ulStringCount;
        ULONG ulStringDataSize;
        Bstring pStrings[];
    };

    typedef struct NeonWordTable
    {
        ULONG ulWordListCount;
        ULONG ulWordDataSize;
        NeonWordList *pWords[];
    };

    const char locFileMagic[4] = { 'N', 'O', 'I', 'R' };
    const char stringsChunk[4] = { 'S', 'T', 'R', 'G' };
    const char wordsChunk[4] =   { 'W', 'O', 'R', 'D' };

    static NeonStringTable *s_pStringTable;
    static NeonWordTable *s_pWordTable;

    static Bstring s_pStringData;
    static NeonWordList *s_pWordData;

    int createStringTable(tFile *pFile);
    int createWordsTable(tFile *pFile);

    LanguageCode langLoad(const char *szFilePath)
    {
        ULONG ulMagic;
        UWORD uwVersion, uwLanguage;

        systemUse();
        logBlockBegin("langLoad()");

        tFile *pFile = diskFileOpen(szFilePath, DISK_FILE_MODE_READ, 0);
        if (!pFile)
        {
            logWrite("ERROR: could not find file '%s'\n", szFilePath);
            logBlockEnd("langLoad()");
            systemUnuse();

            return LC_ERROR;
        }

        // Read header
        fileRead(pFile, &ulMagic, sizeof(ULONG));
        fileRead(pFile, &uwVersion, sizeof(UWORD));
        fileRead(pFile, &uwLanguage, sizeof(UWORD));

        if (ulMagic != *(ULONG*)locFileMagic)
        {
            logWrite("ERROR: Not a NOIR lang '%s'\n", szFilePath);
            logBlockEnd("langLoad()");
            systemUnuse();

            return LC_ERROR;
        }

        if (uwVersion != LOC_SUPPORTED_VERSION)
        {
            logWrite("ERROR: Unsupported verion. Expected '%d', got '%d'",
                LOC_SUPPORTED_VERSION, uwVersion);
            logBlockEnd("langLoad()");
            systemUnuse();

            return LC_ERROR;
        }

        if (!createStringTable(pFile))
        {
            logWrite("ERROR: Failed to read STRING chunk.");
            logBlockEnd("langLoad()");
            systemUnuse();

            return LC_ERROR;
        }

        if (!createWordsTable(pFile))
        {
            logWrite("ERROR: Failed to read WORD chunk.");
            logBlockEnd("langLoad()");
            systemUnuse();

            return LC_ERROR;
        }

        fileClose(pFile);
        systemUnuse();
        logBlockEnd("langLoad()");

        return static_cast<LanguageCode>(uwLanguage);
    }

    void langDestroy()
    {
        if (s_pStringData)
        {
            memFree(s_pStringData, s_pStringTable->ulStringDataSize);
        }

        if (s_pStringTable)
        {
            memFree(s_pStringTable, sizeof(NeonStringTable) + 
                s_pStringTable->ulStringCount * sizeof(Bstring));
        }

        if (s_pWordData)
        {
            memFree(s_pWordData, s_pWordTable->ulWordDataSize);
        }

        if (s_pWordTable)
        {
            memFree(s_pWordTable, sizeof(NeonWordTable) + 
                s_pWordTable->ulWordListCount * sizeof(NeonWordList*));
        }
    }

    Bstring langGetStringById(UWORD uwStringId)
    {
        return (Bstring)s_pStringTable->pStrings[uwStringId];
    }

    const NeonWordList *langGetStringWordsById(UWORD uwStringId)
    {
        return (NeonWordList*)s_pWordTable->pWords[uwStringId];
    }

    int createStringTable(tFile *pFile)
    {
        ULONG ulChunkName, ulStringCount, ulDataSize;
        logBlockBegin("createStringTable()");

        // Read chunk header
        fileRead(pFile, &ulChunkName, sizeof(ULONG));
        fileRead(pFile, &ulStringCount, sizeof(ULONG));
        fileRead(pFile, &ulDataSize, sizeof(ULONG));

        if (ulChunkName != *(ULONG*)stringsChunk)
        {
            logWrite("ERROR: Expected a STRING chunk header\n");
            logBlockEnd("createStringTable()");

            return FALSE;
        }

        // Allocate the string table, this will contain pointers to all the strings
        s_pStringTable = allocBufferFastClear<NeonStringTable*>(sizeof(NeonStringTable) + ulStringCount * sizeof(Bstring));
        s_pStringTable->ulStringCount = ulStringCount;

        // Allocate space for all the strings with and extra byte to null-terminate
        // the last string
        s_pStringTable->ulStringDataSize = ulDataSize + 1;
        s_pStringData = allocBufferFastClear<Bstring>(s_pStringTable->ulStringDataSize);
        fileRead(pFile, s_pStringData, ulDataSize);

        ULONG pCurrentString = (ULONG)(void*)s_pStringData;

        for (ULONG ulId = 0; ulId < ulStringCount; ulId++)
        {
            s_pStringTable->pStrings[ulId] = (Bstring)pCurrentString;
            pCurrentString += (sizeof(ULONG) + bstrLength((Bstring)pCurrentString) + 1);
        }

        logBlockEnd("createStringTable()");
        return TRUE;
    }

    int createWordsTable(tFile *pFile)
    {
        ULONG ulChunkName, ulWordListCount, ulDataSize;
        logBlockBegin("createWordsTable()");

        // Read chunk header
        fileRead(pFile, &ulChunkName, sizeof(ULONG));
        fileRead(pFile, &ulWordListCount, sizeof(ULONG));
        fileRead(pFile, &ulDataSize, sizeof(ULONG));

        if (ulChunkName != *(ULONG*)wordsChunk)
        {
            logWrite("ERROR: Expected a WORD chunk header\n");
            logBlockEnd("createWordsTable()");

            return FALSE;
        }

        // Allocate the words table, this will contain pointers to all the word lists
        s_pWordTable = (NeonWordTable*)memAllocFastClear(sizeof(NeonWordTable) + ulWordListCount * sizeof(Bstring));
        s_pWordTable->ulWordListCount = ulWordListCount;
        s_pWordTable->ulWordDataSize = ulDataSize;

        // Allocate space and read all the words
        s_pWordData = (NeonWordList*)memAllocFastClear(ulDataSize);
        fileRead(pFile, s_pWordData, ulDataSize);

        ULONG pCurrentList = (ULONG)(void*)s_pWordData;

        for (ULONG ulId = 0; ulId < ulWordListCount; ulId++)
        {
            s_pWordTable->pWords[ulId] = (NeonWordList*)pCurrentList;
            pCurrentList += (sizeof(ULONG) 
                + ((NeonWordList*)pCurrentList)->ulSize * sizeof(NeonWord));
        }

        logBlockEnd("createWordsTable()");
        return TRUE;
    }
}