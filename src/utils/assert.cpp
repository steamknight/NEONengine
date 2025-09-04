#include "assert.h"
#include <ace/managers/system.h>
#include <ace/managers/log.h>

void aceAssert(char const* szMessage, char const* szFile, ULONG ulLine)
{
    logWrite("ERR: Assertion failed in %s:%ld -- %s", szFile, ulLine, szMessage);
    systemKill(szMessage);
}