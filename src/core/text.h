#ifndef __TEXT_H__INCLUDED__
#define __TEXT_H__INCLUDED__

#include <ace/types.h>

typedef struct _UwPoint
{
    UWORD uwX;
    UWORD uwY;
} UwPoint;

typedef struct _UwSize
{
    UWORD uwWidth;
    UWORD uwHeight;
} UwSize;

/**
 * @brief Defines how the text should be justified
 */
typedef enum _TextJustify
{
    TX_LEFT_JUSTIFY,
    TX_RIGHT_JUSTIFY,
    TX_CENTER_JUSTIFY,
} TextJustify;

/**
 * @brief Contains information for the current text frame
 */
typedef struct _TextContext
{
    UWORD uwMaxWidth;
    UWORD uwY;
    UBYTE ubAddFrame;
    UBYTE ubCenterVertical;
} TextContext;

void textCreate(const char *szFontName, const char* szFrameBitmap);
void textDestroy();

/**
 * @brief Starts a new text frame
 * 
 * @param pContext Pointer to the context to initialize.
 * @param ubAddFrame if TRUE, a frame will be draw around the text
 * @param ubCenterVertical if TRUE, the text frame will be centered vertically.
 * 
 * @see textEnd()
 * @see textPut()
 */
void textBegin(
        TextContext *pContext,
        UBYTE ubAddFrame,
        UBYTE ubCenterVertical);

/**
 * @brief Displays the text frame
 * 
 * @param pContext The text context.
 * @param uwXY Coordinate to draw the text frame
 * @param ubShouldWait If TRUE, waits for user input and then clears the text.
 * 
 * @see textBegin()
 * @see textPut()
 */
void textEnd(TextContext *pContext, UwPoint uwXY, UBYTE ubShouldWait);

/**
 * @brief Adds a string (by string id) to the text frame.
 * 
 * @param pContext The text context.
 * @param uwStringId The id of the string to display.
 * @param uwMaxLength The maximum length of the line, causing the text to wrap
 *                  if necessary.
 * @param justification Left, center, or right text justification.
 * @param ubColorIdx Palette color index to use when drawing the text.
 * @return The size, of the displayed text.
 * 
 * @see textBegin()
 * @see textEnd()
 * @see UwSize
 * @see TextJustify
 */
UwSize textPut(
        TextContext *pContext,
        UWORD uwStringId,
        UWORD uwMaxLength,
        TextJustify justification,
        UBYTE ubColorIdx);

/**
 * @brief Adds a number of new lines to the frame.
 *
 * @param pContext The text context.
 * @param uwCount The number of new lines to add.
 *
 * @see TextContext
 * @see textBegin()
 * @see textPut()
 */
void textPutNewLine(TextContext *pContext, UWORD uwCount);

/**
 * @brief Draw a string at the given coordinate.
 * 
 * @param uwStringId The id of the string to draw.
 * @param uwXY The coordinate of the string.
 * @param justification The text justification.
 * @param ubColorIdx The text color.
 */
void textDraw(
        UWORD uwStringId,
        UwPoint uwXY,
        TextJustify justification,
        UBYTE ubColorIdx);

/**
 * @brief Draw a string at the given coordinate.
 * 
 * @param pString Pointer to the string to draw.
 * @param uwXY The coordinate of the string.
 * @param justification The text justification.
 * @param ubColorIdx The text color.
 */
void textDrawString(const char *pString, UwPoint uwXY, TextJustify justification, UBYTE ubColorIdx);

#endif // __TEXT_H__INCLUDED__