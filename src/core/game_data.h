#ifndef __GAME_DATA_H__INCLUDED__
#define __GAME_DATA_H__INCLUDED__

#include <ace/types.h>

namespace NEONengine
{
    /**
     * @brief Bounding box information for an area the user can interact with.
     */
    struct Interaction
    {
        tUwRect uwBounds;
        UWORD uwPointerId;
        UWORD uwGotoScene;
        UWORD uwDescriptionId;
        UWORD uwScriptOffset;
    };

    /**
     * @brief Degines a text box.
     */
    struct TextRegion
    {
        tUwCoordYX uwPosition;
        UWORD uwWidth;
        UWORD uwTextId;
        UBYTE ubJustify;
        UBYTE ubCenterVertical;
    };

    /**
     * @brief Generic range into an array using indices. Inclusive.
     */
    struct Range
    {
        UWORD uwFirstIndex;
        UWORD uwLastIndex;
    };

    /**
     * @brief Defines a scene in the game. Scenes are the various screens the user
     * can explore.
     */
    struct Scene
    {
        UWORD uwNameId;
        Range descriptions;
        UWORD uwOnEnterScriptId;
        UWORD uwOnExitScriptId;
        UWORD uwBackgroundId;
        Range interactiveAreas;
        Range textRegions;
        UWORD uwMusicId;
    };

    /**
     * @brief Locations are a series of scenes.
     */
    struct Location
    {
        UWORD uwNameId;
        Range backgrounds;
        Range scenes;
        Range shapes;
        UWORD uwShapesFileId;
        UWORD uwSpeakers[8];
    };

    /**
     * @brief Using BlitzBASIC nomenclature, these define BOBs with their own
     * custom palette.
     */
    struct Shape
    {
        UWORD uwShapeId;
        UWORD uwPaletteId;
    };

    /**
     * @brief A choice the player can make during a dialogue.
     */
    struct DialogueChoice
    {
        UWORD uwTextId;
        UWORD uwSetFlagIdOnSelection;
        UWORD uwClearFlagIdOnSelection;
        UWORD uwCheckFlag;
        UWORD uwGotoPageId;
        UWORD uwScriptOffset;
        UBYTE ubEnabled;
        UBYTE ubSelfDisable;
    };

    /**
     * @brief A page of dialogue.
     */
    struct DialoguePage
    {
        UWORD uwSpeakerId;
        UWORD uwTextId;
        UWORD uwSetFlagIdOnSelection;
        UWORD uwClearFlagIdOnSelection;
        UWORD uwCheckFlag;
        UWORD uwGotoPageId;
        Range choices;
        UBYTE ubEnabled;
        UBYTE ubSelfDisable;
    };

    /**
     * @brief Dialogues are a collection of dialogue pages.
     */
    struct Dialogue
    {
        UWORD uwFirstPageId;
        UWORD uwPageCount;
    };

    /**
     * @brief Defines one entry in a color palette
     */
    struct PaletteEntry
    {
        UBYTE ubR;
        UBYTE ubG;
        UBYTE ubB;
        UBYTE ubPadding;
    };

    /**
     * @brief Collects all the game object loaded into memory.
     */
    struct GameData
    {
        Location *pLocations;
        Scene *pScenes;
        Interaction *pIteractables;
        TextRegion *pTextRegions;
        Dialogue *pDialogues;
        DialoguePage *pDialoguePages;
        DialogueChoice *pDialogueChoices;
        UWORD *puwScriptData;
        Shape *pShapes;
        PaletteEntry *pPalettes;
        PaletteEntry *pUiPalette;
    };

    /**
     * @brief Possible results when reading a game data file.
     *
     * @see gameDataLoad()
     */
    enum class GameDataResult
    {
        SUCCESS = 0,
        FILE_NOT_FOUND,
        GENERIC_READ_ERROR,
        NOT_NEON_FILE,
        VERSION_NOT_SUPPORTED,
        OUT_OF_MEMORY,
        CORRUPTED_FILE,
    };

    /**
     * @brief Loads the specified game data file.
     * Currently only supports version 2.0
     *
     * @param szFilePath The file path of the game data file, generally ending in .NEON.
     * @return GameDataResult Indicates if an error occured while reading.
     */
    GameDataResult gameDataLoad(const char *szFilePath);

    /**
     * @brief Frees all the game data.
     *
     * @see gameDataLoad()
     */
    void gameDataDestroy();
}

#endif // __GAME_DATA_H__INCLUDED__