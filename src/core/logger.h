/**
 * @file logger.h
 * @brief Engine logging, with one switchable domain per source file.
 *
 * Writes to the UAE debug console directly rather than through ACE's logWrite,
 * so ACE_DEBUG does not gate engine output. ACE_DEBUG can stay on for the
 * checks scene_bobs relies on while ACE itself says nothing.
 *
 * Each source file names its domain once and then logs:
 *
 *     NE_LOG_DOMAIN(Scene);
 *     ...
 *     NE_LOG("transition to scene %hu", id);   // [Scene] transition to scene 3
 *
 * Logging without naming a domain does not compile. NE_LOG_ALWAYS() writes
 * whether or not the domain is on.
 *
 * NE_LOGGING is a build flag: off, every macro here expands to nothing and the
 * format strings leave the executable. The domain mask is a runtime word, so a
 * disabled domain costs one AND and never evaluates its arguments.
 */

#ifndef __LOGGER_H__INCLUDED__
#define __LOGGER_H__INCLUDED__

#include "types.h"

#include <ace/types.h>

namespace NEONengine
{
    /**
     * @brief Every logging domain in the engine.
     *
     * The only place a domain exists. Order sets the bit, so appending is free
     * and reordering changes what a mask literal means.
     *
     * The mask is 32 bits wide.
     */
#define NE_LOG_DOMAIN_LIST(X) \
    X(Core)                   \
    X(Data)                   \
    X(Strings)                \
    X(Script)                 \
    X(Scene)                  \
    X(Timeline)               \
    X(Bobs)                   \
    X(Audio)                  \
    X(Text)                   \
    X(Hotspot)                \
    X(Cursor)                 \
    X(Cache)                  \
    X(Dialogue)               \
    X(Test)

    enum class log_domain : UBYTE
    {
#define NE_LOG_DOMAIN_ENUMERATOR(name) name,
        NE_LOG_DOMAIN_LIST(NE_LOG_DOMAIN_ENUMERATOR)
#undef NE_LOG_DOMAIN_ENUMERATOR
            COUNT
    };

    static_assert(to<ULONG>(log_domain::COUNT) <= 32,
        "the domain mask is 32 bits wide, so there is room for 32 domains");

    /**
     * @brief The mask bit @p domain is switched by.
     */
    constexpr ULONG log_bit(log_domain domain)
    {
        return 1UL << to<ULONG>(domain);
    }

    /**
     * @brief The domains that print when the engine starts.
     *
     * Clear a bit to silence a domain, as in ~log_bit(log_domain::Timeline).
     * g_logMask can also be changed at runtime.
     */
    constexpr ULONG NE_LOG_MASK_DEFAULT = 0xFFFFFFFFUL;

    /**
     * @brief Whether ACE's own log output starts silenced.
     *
     * ACE's writers check a mute flag first, so this needs no ACE patch and
     * leaves ACE_DEBUG in place. ACE_DEBUG also enables bobCheckGood and the
     * bob buffer auto-correction scene_bobs_draw depends on.
     *
     * The script VM's diagnostics and the assert and fatal paths still write
     * through ACE. Muting takes those with it.
     */
    constexpr bool NE_LOG_MUTE_ACE = false;

    /**
     * @brief The domains that currently print.
     */
    extern ULONG g_logMask;

    /**
     * @brief Whether @p domain currently prints.
     */
    inline bool log_enabled(log_domain domain)
    {
        return (g_logMask & log_bit(domain)) != 0;
    }

    /**
     * @brief Silences or restores ACE's own log output, leaving ours alone.
     */
    void log_mute_ace(bool isMuted);

    /**
     * @brief Whether ACE's own log output is currently silenced.
     */
    bool log_ace_muted();

    /**
     * @brief Applies the startup logging settings and says what they are.
     */
    void log_init();

    /**
     * @brief Writes one line, prefixed with the name of @p domain.
     *
     * The newline is added here. The UAE console ends a line on every write, so
     * a message must leave in one call.
     *
     * Over-long lines are truncated. The buffer is static, so this must not be
     * called from an interrupt.
     */
    void log_write(log_domain domain, const char* szFormat, ...)
        __attribute__((format(printf, 2, 3)));
}  // namespace NEONengine

#if defined(NE_LOGGING)

/**
 * @brief Names the domain every NE_LOG in this file writes to.
 *
 * Place it once at file scope. The name must be one of those in
 * NE_LOG_DOMAIN_LIST.
 */
#define NE_LOG_DOMAIN(name) \
    [[maybe_unused]] static constexpr ::NEONengine::log_domain s_neLogDomain = \
        ::NEONengine::log_domain::name

/**
 * @brief Writes one line when this file's domain is switched on.
 */
#define NE_LOG(fmt, ...)                                                    \
    do                                                                      \
    {                                                                       \
        if (::NEONengine::log_enabled(s_neLogDomain))                       \
        {                                                                   \
            ::NEONengine::log_write(s_neLogDomain, fmt, ##__VA_ARGS__);      \
        }                                                                   \
    } while (0)

/**
 * @brief Writes one line whether or not this file's domain is switched on.
 */
#define NE_LOG_ALWAYS(fmt, ...) \
    ::NEONengine::log_write(s_neLogDomain, fmt, ##__VA_ARGS__)

#else

#define NE_LOG_DOMAIN(name)     static_assert(true)
#define NE_LOG(fmt, ...)        do {} while (0)
#define NE_LOG_ALWAYS(fmt, ...) do {} while (0)

#endif  // defined(NE_LOGGING)

#endif  // __LOGGER_H__INCLUDED__
