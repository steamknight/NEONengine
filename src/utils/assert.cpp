#include "assert.h"
#include <ace/managers/system.h>
#include <ace/managers/log.h>

// szFile and ulLine reach nothing when ACE compiles logWrite away.
void aceAssert(char const* szMessage, [[maybe_unused]] char const* szFile, [[maybe_unused]] ULONG ulLine)
{
    logWrite("ERR: Assertion failed in %s:%ld -- %s", szFile, ulLine, szMessage);
    systemKill(szMessage);
}