#include "bstr.h"

#include <ace/managers/memory.h>
#include <mini_std/printf.h>
#include <mini_std/string.h>

#include "assert.h"

__attribute__((optimize("no-tree-loop-distribute-patterns"))) 
void* memcopy(void *dest, const void *src, unsigned long len) {
	char *d = (char *)dest;
	const char *s = (const char *)src;
	while(len--)
		*d++ = *s++;
	return dest;
}

struct _Bstring
{
    ULONG ulLength;
    char pBuffer[];
};

// Helper function to calculate the allocation size for a Bstring
static inline ULONG bstrAllocSize(ULONG ulLength)
{
    return sizeof(_Bstring) + ulLength + 1;
}

Bstring bstrCreate(char const *szValue, ULONG ulFlags)
{
    assert(szValue, "Input string is null.");

    ULONG ulLength = strlen(szValue);

    // strlen does not account for the terminator, so compensate here rather
    // than adding it to ulLength, so our length will always be the number of
    // characters.
    Bstring string = (Bstring)memAlloc(bstrAllocSize(ulLength), ulFlags);

    assert(string, "Allocating memory for string failed!");

    memcopy(string->pBuffer, szValue, ulLength);
    string->pBuffer[ulLength] = '\0';
    string->ulLength = ulLength;

    return string;
}

void bstrDestroy(Bstring *pString)
{
    if (!pString || !*pString)
        return;

    ULONG ulTotalSize = bstrAllocSize((*pString)->ulLength);
    memFree(*pString, ulTotalSize);
    *pString = NULL;
}

ULONG bstrLength(Bstring string)
{
    assert(string, "Trying to get the length of a NULL string.");
    return string->ulLength;
}

Bstring bstrClone(Bstring source, ULONG ulFlags)
{
    assert(source, "Source string is NULL.");
    return bstrCreate(source->pBuffer, ulFlags);
}

Bstring bstrCopy(Bstring source, Bstring target)
{
    assert(source, "Source string is NULL.");
    assert(target, "Target string is NULL.");
    assert(target->ulLength >= source->ulLength, "Target string is smaller than source string.");

    memcpy(target->pBuffer, source->pBuffer, source->ulLength);

    return target;
}

Bstring bstrCopyN(Bstring source, Bstring destination, ULONG count)
{
    assert(source, "Source string is NULL.");
    assert(destination, "Destination string is NULL.");
    assert(source->ulLength >= count, "Trying to copy more characters than the source string possesses.");
    assert(destination->ulLength >= count, "Destination string can't contain the input characters.");

    memcpy(destination->pBuffer, source->pBuffer, count);

    return destination;
}

Bstring bstrConcat(Bstring lhs, Bstring rhs, ULONG ulFlags)
{
    assert(lhs, "Left-hand string is NULL.");
    assert(rhs, "Right-hand string is NULL.");

    ULONG ulLength = lhs->ulLength + rhs->ulLength;
    ULONG ulTotalSize = bstrAllocSize(ulLength);
    Bstring result = (Bstring)memAlloc(ulTotalSize, ulFlags);

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

char *bstrGetData(Bstring bstr)
{
    if (bstr)
        return bstr->pBuffer;
    else
        return NULL;
}

Bstring bstrCreateF(ULONG ulFlags, const char *szFormat, ...)
{
    assert(szFormat, "Format string is NULL.");

    // Allocate a temporary buffer on the stack for formatting
    char buffer[1024];
    va_list args;
    va_start(args, szFormat);
    int needed = vsnprintf(buffer, sizeof(buffer), szFormat, args);
    va_end(args);

    if (needed < 0)
    {
        // Encoding error
        return NULL;
    }
    else if ((size_t)needed < sizeof(buffer))
    {
        // Fits in the buffer
        return bstrCreate(buffer, ulFlags);
    }
    else
    {
        // Allocate a larger buffer on the heap
        size_t size = needed + 1; // +1 for null terminator
        char *heapBuffer = (char*)memAlloc(size, ulFlags);
        if (!heapBuffer)
            return NULL;

        va_start(args, szFormat);
        vsnprintf(heapBuffer, size, szFormat, args);
        va_end(args);

        Bstring result = bstrCreate(heapBuffer, ulFlags);
        memFree(heapBuffer, size);
        return result;
    }
}
