#ifndef __GAME_DATA_H__INCLUDED__
#define __GAME_DATA_H__INCLUDED__

#include <ace/types.h>

/**
 * @brief Bounding box information for an area the user can interact with.
 */
typedef struct _Interaction
{
    tUwRect uwBounds;
    UWORD uwPointerId;
    UWORD uwGotoScene;
    UWORD uwDescriptionId;
    UWORD uwScriptOffset;
} Interaction;

/**
 * @brief Degines a text box.
 */
typedef struct _TextRegion
{
    tUwCoordYX uwPosition;
    UWORD uwWidth;
    UWORD uwTextId;
    UBYTE ubJustify;
    UBYTE ubCenterVertical;
} TextRegion;

/**
 * @brief Generic range into an array using indices. Inclusive.
 */
typedef struct _Range
{
    UWORD uwFirstIndex;
    UWORD uwLastIndex;
} Range;

/**
 * @brief Defines a scene in the game. Scenes are the various screens the user
 * can explore.
 */
typedef struct _Scene
{
    UWORD uwNameId;
    Range descriptions;
    UWORD uwOnEnterScriptId;
    UWORD uwOnExitScriptId;
    UWORD uwBackgroundId;
    Range interactiveAreas;
    Range textRegions;
    UWORD uwMusicId;
} Scene;

/**
 * @brief Locations are a series of scenes.
 */
typedef struct _Location
{
    UWORD uwNameId;
    Range backgrounds;
    Range scenes;
    Range shapes;
    UWORD uwShapesFileId;
    UWORD uwSpeakers[8];
} Location;

/**
 * @brief Using BlitzBASIC nomenclature, these define BOBs with their own
 * custom palette.
 */
typedef struct _Shape
{
    UWORD uwShapeId;
    UWORD uwPaletteId;
} Shape;

/**
 * @brief A choice the player can make during a dialogue.
 */
typedef struct _DialogueChoice
{
    UWORD uwTextId;
    UWORD uwSetFlagIdOnSelection;
    UWORD uwClearFlagIdOnSelection;
    UWORD uwCheckFlag;
    UWORD uwGotoPageId;
    UWORD uwScriptOffset;
    UBYTE ubEnabled;
    UBYTE ubSelfDisable;
} DialogueChoice;

/**
 * @brief A page of dialogue.
 */
typedef struct _DialoguePage
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
} DialoguePage;

/**
 * @brief Dialogues are a collection of dialogue pages.
 */
typedef struct _Dialogue
{
    UWORD uwFirstPageId;
    UWORD uwPageCount;
} Dialogue;

/**
 * @brief Defines one entry in a color palette
 */
typedef struct _PaletteEntry
{
    UBYTE ubR;
    UBYTE ubG;
    UBYTE ubB;
    UBYTE ubPadding;
} PaletteEntry;

/**
 * @brief Collects all the game object loaded into memory.
 */
typedef struct _GameData
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
} GameData;

/**
 * @brief Possible results when reading a game data file.
 *
 * @see gameDataLoad()
 */
typedef enum _GameDataResult
{
    GDR_SUCCESS = 0,
    GDR_FILE_NOT_FOUND,
    GDR_GENERIC_READ_ERROR,
    GDR_NOT_NEON_FILE,
    GDR_VERSION_NOT_SUPPORTED,
    GDR_OUT_OF_MEMORY,
    GDR_CORRUPTED_FILE,
} GameDataResult;

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

#endif // __GAME_DATA_H__INCLUDED__