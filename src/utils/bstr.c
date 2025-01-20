#include "bstr.h"

#include <ace/managers/memory.h>

#include <string.h>

#include "assert.h"

struct Bstring
{
    ULONG ulLength;
    char pBuffer[];
};

Bstring bstrCreate(char const* szValue, ULONG ulFlags)
{
    assert(szValue, "Input string is null.");

    ULONG ulLength = strlen(szValue);

    // strlen does not account for the terminator, so compensate here rather 
    // than adding it to ulLength, so our length will always be the number of
    // characters.
    Bstring string = memAlloc(sizeof(struct Bstring) + ulLength + 1, ulFlags);

    assert(string, "Allocating memory for string failed!");

    memcpy(string->pBuffer, szValue, ulLength);
    string->pBuffer[ulLength] = '\0';
    string->ulLength = ulLength;
    
    return string;
}

void bstrDestroy(Bstring* pString)
{
    assert(pString && *pString, "Trying to free a null string pointer.");

    ULONG ulTotalSize = sizeof(struct Bstring) + (*pString)->ulLength + 1;
    memFree(*pString, ulTotalSize);
    *pString = NULL;
}

ULONG bstrLength(Bstring string)
{
    assert(string, "Trying to get the length of a NULL string.");
    return string->ulLength;
}

Bstring bstrCopy(Bstring source, Bstring destination)
{
    assert(source, "Source string is NULL.");
    assert(destination, "Destination string is NULL.");
    assert(destination->ulLength < source->ulLength, "Destination string is smaller than source string.");

    memcpy(destination->pBuffer, source->pBuffer, source->ulLength);

    return destination;
}

Bstring bstrCopyAmount(Bstring source, Bstring destination, ULONG count)
{
    assert(source, "Source string is NULL.");
    assert(destination, "Destination string is NULL.");
    assert(source->ulLength < count, "Trying to copy more characters than the source string posesses.");
    assert(destination->ulLength < count, "Destination string can't contain the input characters.");

    memcpy(destination->pBuffer, source->pBuffer, count);
    
    return destination;
}

Bstring bstrConcat(Bstring lhs, Bstring rhs, ULONG ulFlags)
{
    assert(lhs, "Left-hand string is NULL.");
    assert(rhs, "Right-hand string is NULL.");

    ULONG ulLength = lhs->ulLength + rhs->ulLength;
    ULONG ulTotalSize = sizeof(struct Bstring) + ulLength + 1;
    Bstring result = memAlloc(ulTotalSize, ulFlags);

    assert(result, "Allocating memory for string failed!");

    memcpy(result->pBuffer, lhs->pBuffer, lhs->ulLength);
    memcpy(result->pBuffer + lhs->ulLength, rhs->pBuffer, rhs->ulLength);
    result->pBuffer[ulLength] = '\0';
    result->ulLength = ulLength;
    
    return result;
}

int bstrCompare(const Bstring lhs, const Bstring rhs)
{
    assert(lhs, "Left-hand string is NULL.");
    assert(rhs, "Right-hand string is NULL.");

    return strcmp(lhs->pBuffer, rhs->pBuffer);
}
