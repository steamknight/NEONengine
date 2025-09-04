#ifndef __BSTR_H__INCLUDED__
#define __BSTR_H__INCLUDED__

/**
 * @file "bstr.h"
 * @brief Implementation of a Basic-like string.
 *
 * A Bstring consists of a length, the string data, and a NULL terminator.
 *
 * @note Allocating a new Bstring retuns a pointer to the first character, and
 * therefore can be used in any C-style string functions provided it does not
 * alter the size of the string.
 *
 * @note This does not follow the spec of Windows' BSTR.
 */


/**
 * @brief Opaque pointer to the string
 */
struct _Bstring;
typedef _Bstring *Bstring;
typedef unsigned long ULONG;

/**
 * @brief Allocates a new string.
 *
 * @param szValue The string's initial value
 * @param ulFlags Memory allocation flags e.g. MEMF_FAST, MEMF_CHIP, MEMF_ANY
 * @return A new string
 *
 * @see bstrFree
 */
Bstring bstrCreate(char const* szValue, ULONG ulFlags);

/**
 * @brief Deallocate a Bstring.
 * Will set the pointer to NULL when done.
 *
 * @param pString Pointer to the string (not the string iself)
 */
void bstrDestroy(Bstring* pString);

/**
 * @brief Returns the length of the string.
 *
 * @param string The string
 * @return The number of characters in the array
 */
ULONG bstrLength(Bstring string);

/**
 * @brief Clones a Bstring.
 * 
 * @param source the string to clone
 * @param ulFlags Memory allocation flags e.g. MEMF_FAST, MEMF_CHIP, MEMF_ANY
 * @return A cloned string 
 */
Bstring bstrClone(Bstring source, ULONG ulFlags);

/**
 * @brief Copies the source string into the destination.
 *
 * @param source The source string.
 * @param destination The destination string. Must be as large or larger than the source
 * @return The destination string or NULL of the destination is smaller than the source
 */
Bstring bstrCopy(Bstring source, Bstring destination);

/**
 * @brief Copies a specified number of characters from the source string to the destination.
 *
 * @param source The source string.
 * @param destination The destination string. Must be as large or larger than
 *                    the number of characters to be copied
 * @param count The number of characters to copy.
 * @return The destination string or NULL of the destination is smaller than the
 *         number of characters to be copied.
 */
Bstring bstrCopyN(Bstring source, Bstring destination, ULONG count);

/**
 * @brief Concatenates two strings into a new string.
 *
 * @param lhs First string
 * @param rhs Second string
 * @param ulFlags Memory allocation flags e.g. MEMF_FAST, MEMF_CHIP, MEMF_ANY
 * @return A new string.
 */
Bstring bstrConcat(Bstring lhs, Bstring rhs, ULONG ulFlags);

/**
 * @brief Compares to strings
 *
 * @param lhs First string
 * @param rhs Second string
 * @return Negative value if lhs appears before rhs in lexicographical order.
 *        Zero if lhs and rhs compare equal.
 *        Positive value if lhs appears after rhs in lexicographical order.
 */
int bstrCompare(const Bstring lhs, const Bstring rhs);

/**
 * @brief Get the C-style string data from a Bstring.
 */
char *bstrGetData(Bstring bstr);

Bstring bstrCreateF(ULONG ulFlags, const char *szFormat, ...) __attribute__((format(printf, 2, 3)));

/**
 * @brief Convenience macros named similarly to the bonkers C standard string
 * functions.
 *
 * @note Functions that take a source and destination strings, take them
 * destination first like in the standard (bonkers) and will flip them to the
 * Bstring style (sensible).
 */

#define bstrlen(string) bstrLength(string)
#define bstrcpy(dest, source) bstrCopy(source, dest)
#define bstrncpy(dest, source, count) bstrCopyAmount(source, dest, count)
#define bstrcat(dest, source) bstrConcat(source, dest, MEMF_ANY)
#define bstrcmp(lhs, rhs) bstrCompare(lhs, rhs)

#define B(text) bstrCreate((text), MEMF_FAST)

#endif // __BSTR_H__INCLUDED__