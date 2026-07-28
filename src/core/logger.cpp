#include "core/logger.h"

#include <stdarg.h>
#include <stdio.h>

#include <ace/managers/log.h>

namespace NEONengine
{
    ULONG g_logMask = NE_LOG_MASK_DEFAULT;

#if defined(NE_LOGGING)

    namespace
    {
        /**
         * One entry per domain, in the order NE_LOG_DOMAIN_LIST declares them,
         * so a domain indexes straight into this.
         */
        const char* const s_pDomainPrefixes[] = {
#define NE_LOG_DOMAIN_PREFIX(name) "[" #name "] ",
            NE_LOG_DOMAIN_LIST(NE_LOG_DOMAIN_PREFIX)
#undef NE_LOG_DOMAIN_PREFIX
        };

        static_assert(
            sizeof(s_pDomainPrefixes) / sizeof(s_pDomainPrefixes[0])
                == static_cast<ULONG>(log_domain::COUNT),
            "every domain needs a prefix");

        /**
         * Kickstart 1.3 gives a task about 10k of stack, which a line buffer
         * this size has no business taking a tenth of. ACE keeps its own in
         * static memory for the same reason.
         */
        char s_szLine[1024];

        /**
         * The debug console the emulator watches. This is the same address ACE
         * writes to under ACE_DEBUG_UAE, reached here directly so engine output
         * does not depend on ACE's build flags or on its mute state.
         */
        void print_uae(const char* szMsg)
        {
#if defined(BARTMAN_GCC)
            auto pfnLog = (long (*)(long, const char*))0xf0ff60;

            // The hook only exists when a debugger installed it. The first word
            // is a JSR or a line-A trap when it did, and junk when it did not.
            if (*((UWORD*)pfnLog) == 0x4eb9 || *((UWORD*)pfnLog) == 0xa00e)
            {
                pfnLog(86, szMsg);
            }
#else
            volatile ULONG* const pUaeFormat = (ULONG*)0xBFFF04;
            *pUaeFormat                      = (ULONG)(const UBYTE*)szMsg;
#endif
        }
    }  // namespace

    void log_write(log_domain domain, const char* szFormat, ...)
    {
        // The console ends a line on every write, so the prefix, the message and
        // the newline all have to leave here as one string.
        const char* szPrefix = s_pDomainPrefixes[static_cast<UBYTE>(domain)];

        ULONG ulOffset = 0;
        while (szPrefix[ulOffset])
        {
            s_szLine[ulOffset] = szPrefix[ulOffset];
            ++ulOffset;
        }

        // One byte held back so the newline always has somewhere to go.
        const ULONG ulRoom = sizeof(s_szLine) - ulOffset - 1;

        va_list vaArgs;
        va_start(vaArgs, szFormat);
        const int iWritten = vsnprintf(&s_szLine[ulOffset], ulRoom, szFormat, vaArgs);
        va_end(vaArgs);

        // vsnprintf reports the length the message wanted, not the length it
        // got, so a truncated line reports past the end of the buffer.
        ULONG ulEnd = ulOffset + (iWritten < 0 ? 0 : (ULONG)iWritten);
        if (ulEnd > sizeof(s_szLine) - 2)
        {
            ulEnd = sizeof(s_szLine) - 2;
        }

        s_szLine[ulEnd]     = '\n';
        s_szLine[ulEnd + 1] = '\0';

        print_uae(s_szLine);
    }

    void log_mute_ace(bool isMuted)
    {
#if defined(ACE_DEBUG)
        // _logWriteVa, _logBlockBegin and _logBlockEnd all return early on a
        // non-zero flag. Each bumps and drops it around its own body, so holding
        // it at one keeps ACE quiet without disturbing that.
        g_sLogManager.ubShutUp = isMuted ? 1 : 0;
#else
        (void)isMuted;
#endif
    }

    bool log_ace_muted()
    {
#if defined(ACE_DEBUG)
        return g_sLogManager.ubShutUp != 0;
#else
        return true;
#endif
    }

    void log_init()
    {
        log_mute_ace(NE_LOG_MUTE_ACE);

        // Say what is on, so a log missing the lines somebody expected shows why.
        log_write(log_domain::Core, "logging on, domain mask %08lx, ACE %s", g_logMask,
            log_ace_muted() ? "muted" : "logging");
    }

#else

    void log_write(log_domain, const char*, ...) {}

    void log_mute_ace(bool) {}

    bool log_ace_muted() { return false; }

    void log_init() {}

#endif  // defined(NE_LOGGING)
}  // namespace NEONengine
