#include "neonengine.h"
#include "game_data.h"

#include <ace/managers/log.h>
#include <ace/managers/system.h>
#include <ace/utils/disk_file.h>

namespace NEONengine
{
    typedef struct _GameDataCounts
    {
        ULONG ulLocationCount;
        ULONG ulSceneCount;
        ULONG ulInteractableCount;
        ULONG ulTextRegionCount;
        ULONG ulDialogueCount;
        ULONG ulDialoguePageCount;
        ULONG ulDialogueChoiceCount;
        ULONG ulScriptDataSize;
        ULONG ulShapeCount;
        ULONG ulPaletteCount;
        ULONG ulUiPaletteSize;
    } GameDataCounts;

    static GameData *g_pGameData;
    static GameDataCounts *s_pGameDataCounts;

    const char dataFileMagic[4] = {'N', 'E', 'O', 'N'};

    #define GDL_SUPPORTED_VERSION 0x00020000

    #define GDL_CHUNK_NAME(a, b, c, d) (a << 24) | (b << 16) | (c << 8) | d

    #define GDL_VERIFY(expression, error)                              \
        if (!(expression))                                             \
        {                                                              \
            logWrite("ERROR: could not find file '%s'\n", szFilePath); \
            logBlockEnd("gameDataLoad()");                             \
            gameDataDestroy();                                         \
            systemUnuse();                                             \
            return (error);                                            \
        }

    GameDataResult gameDataLoadChunk(tFile *pFile, const char *szChunkName, void **pChunkData, ULONG *pulCount, ULONG size);

    GameDataResult gameDataLoad(const char *szFilePath)
    {
        logBlockBegin("gameDataLoad: %s", szFilePath);
        systemUse();

        auto result = GameDataResult::SUCCESS;

        // Get rid of any existing game data
        if (g_pGameData)
            gameDataDestroy();

        tFile *pFile = diskFileOpen(szFilePath, DISK_FILE_MODE_READ, 0);
        GDL_VERIFY(pFile, GameDataResult::FILE_NOT_FOUND);

        // Read in the header and verify it's a valid file
        ULONG ulMagic, ulVersion;
        fileRead(pFile, &ulMagic, sizeof(ULONG));
        fileRead(pFile, &ulVersion, sizeof(ULONG));

        GDL_VERIFY(ulMagic == *(ULONG *)dataFileMagic, GameDataResult::NOT_NEON_FILE);
        GDL_VERIFY(ulVersion == GDL_SUPPORTED_VERSION, GameDataResult::VERSION_NOT_SUPPORTED);

        g_pGameData = allocFastAndClear<GameData>();
        s_pGameDataCounts = allocFastAndClear<GameDataCounts>();

        // Load the chunks
        while (!fileIsEof(pFile))
        {
            ULONG ulChunkHeader;
            GameDataResult chunkResult = GameDataResult::SUCCESS;

            fileRead(pFile, &ulChunkHeader, sizeof(ULONG));

            switch (ulChunkHeader)
            {
            case GDL_CHUNK_NAME('L', 'O', 'C', 'S'):
                chunkResult = gameDataLoadChunk(
                    pFile, "Locations",
                    (void **)&g_pGameData->pLocations, &s_pGameDataCounts->ulLocationCount,
                    sizeof(Location));
                break;

            case GDL_CHUNK_NAME('S', 'C', 'N', 'S'):
                chunkResult = gameDataLoadChunk(
                    pFile, "Scenes",
                    (void **)&g_pGameData->pScenes, &s_pGameDataCounts->ulSceneCount,
                    sizeof(Scene));
                break;

            case GDL_CHUNK_NAME('R', 'G', 'N', 'S'):
                chunkResult = gameDataLoadChunk(
                    pFile, "Interactions",
                    (void **)&g_pGameData->pIteractables, &s_pGameDataCounts->ulInteractableCount,
                    sizeof(Interaction));
                break;

            case GDL_CHUNK_NAME('T', 'E', 'X', 'T'):
                chunkResult = gameDataLoadChunk(
                    pFile, "TextRegions",
                    (void **)&g_pGameData->pTextRegions, &s_pGameDataCounts->ulTextRegionCount,
                    sizeof(TextRegion));
                break;

            case GDL_CHUNK_NAME('D', 'L', 'G', 'S'):
                chunkResult = gameDataLoadChunk(
                    pFile, "Dialogues",
                    (void **)&g_pGameData->pDialogues, &s_pGameDataCounts->ulDialogueCount,
                    sizeof(Dialogue));
                break;

            case GDL_CHUNK_NAME('P', 'A', 'G', 'E'):
                chunkResult = gameDataLoadChunk(
                    pFile, "Dialogue Pages",
                    (void **)&g_pGameData->pDialoguePages, &s_pGameDataCounts->ulDialoguePageCount,
                    sizeof(DialoguePage));
                break;

            case GDL_CHUNK_NAME('C', 'H', 'C', 'E'):
                chunkResult = gameDataLoadChunk(
                    pFile, "Dialogue Choices",
                    (void **)&g_pGameData->pDialogueChoices, &s_pGameDataCounts->ulDialogueChoiceCount,
                    sizeof(DialogueChoice));
                break;

            case GDL_CHUNK_NAME('B', 'Y', 'T', 'E'):
                chunkResult = gameDataLoadChunk(
                    pFile, "Bytecode",
                    (void **)&g_pGameData->puwScriptData, &s_pGameDataCounts->ulScriptDataSize,
                    sizeof(UWORD));
                break;

            case GDL_CHUNK_NAME('S', 'H', 'P', 'E'):
                chunkResult = gameDataLoadChunk(
                    pFile, "Shapes",
                    (void **)&g_pGameData->pShapes, &s_pGameDataCounts->ulShapeCount,
                    sizeof(Shape));
                break;

            case GDL_CHUNK_NAME('P', 'A', 'L', 'S'):
                chunkResult = gameDataLoadChunk(
                    pFile, "Shape Palettes",
                    (void **)&g_pGameData->pPalettes, &s_pGameDataCounts->ulPaletteCount,
                    sizeof(PaletteEntry) * 32 /* Number of colors per palette*/
                );
                break;

            case GDL_CHUNK_NAME('P', 'A', 'L', 'U'):
                chunkResult = gameDataLoadChunk(
                    pFile, "UI Palette",
                    (void **)&g_pGameData->pUiPalette, &s_pGameDataCounts->ulUiPaletteSize,
                    sizeof(PaletteEntry));
                break;

            default:
                logWrite("Unknown chunk '%s'", (char *)&ulChunkHeader);
                break;
            }

            GDL_VERIFY(chunkResult == GameDataResult::SUCCESS, chunkResult);
        }

        fileClose(pFile);

        systemUnuse();
        logBlockEnd("gameDataLoad");

        return result;
    }

    void gameDataDestroy()
    {
        logBlockBegin("gameDataDestroy");

        if (!g_pGameData)
            return;

        // Free individual resources
        memFree(g_pGameData->pLocations, s_pGameDataCounts->ulLocationCount * sizeof(Location));
        memFree(g_pGameData->pScenes, s_pGameDataCounts->ulSceneCount * sizeof(Scene));
        memFree(g_pGameData->pIteractables, s_pGameDataCounts->ulInteractableCount * sizeof(Interaction));
        memFree(g_pGameData->pTextRegions, s_pGameDataCounts->ulTextRegionCount * sizeof(TextRegion));
        memFree(g_pGameData->pDialogues, s_pGameDataCounts->ulDialogueCount * sizeof(Dialogue));
        memFree(g_pGameData->pDialoguePages, s_pGameDataCounts->ulDialoguePageCount * sizeof(DialoguePage));
        memFree(g_pGameData->pDialogueChoices, s_pGameDataCounts->ulDialogueChoiceCount * sizeof(DialogueChoice));
        memFree(g_pGameData->puwScriptData, s_pGameDataCounts->ulScriptDataSize * sizeof(UWORD));
        memFree(g_pGameData->pShapes, s_pGameDataCounts->ulShapeCount * sizeof(Shape));
        memFree(g_pGameData->pPalettes, s_pGameDataCounts->ulPaletteCount * sizeof(PaletteEntry));
        memFree(g_pGameData->pUiPalette, s_pGameDataCounts->ulUiPaletteSize * sizeof(PaletteEntry));

        // Free the data container
        memFree(g_pGameData, sizeof(GameData));
        g_pGameData = NULL;

        memFree(s_pGameDataCounts, sizeof(GameDataCounts));
        s_pGameDataCounts = NULL;

        logBlockEnd("gameDataDestroy");
    }

    GameDataResult gameDataLoadChunk(tFile *pFile, const char *szChunkName, void **pChunkData, ULONG *pulCount, ULONG size)
    {
        ULONG ulChunkSize = 0;

        logWrite("Loading '%s' chunk...", szChunkName);
        fileRead(pFile, pulCount, sizeof(ULONG));
        ulChunkSize = *pulCount * size;

        // Guard against empty chunks, return success so we continue with the next one
        if (ulChunkSize == 0)
            return GameDataResult::SUCCESS;

        *pChunkData = memAllocFastClear(ulChunkSize);
        if (!*pChunkData)
            return GameDataResult::OUT_OF_MEMORY;

        ULONG ulBytesRead = fileRead(pFile, *pChunkData, ulChunkSize);
        if (ulBytesRead != ulChunkSize)
            return GameDataResult::CORRUPTED_FILE;

        return GameDataResult::SUCCESS;
    }
}