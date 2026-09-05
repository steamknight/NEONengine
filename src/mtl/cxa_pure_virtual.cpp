#include <ace/managers/log.h>

extern "C" [[noreturn]] void __cxa_pure_virtual()
{
    logWrite("FATAL ERROR: Pure virtual function call\n");
    __builtin_trap();
}
