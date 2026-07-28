/**
 * @file logger.h
 * @brief Engine logging, with one switchable domain per source file.
 *
 * ACE gates its own logWrite on ACE_DEBUG, so routing engine lines through it
 * tied the two together: silencing ACE's chatter meant silencing ours, and
 * keeping ours meant keeping every ACE block and allocation line with it. This
 * writes to the UAE debug console directly, through the same door ACE knocks
 * on, so the two are independent. ACE_DEBUG can stay on for the safety checks
 * that scene_bobs relies on while ACE itself says nothing.
 *
 * Each source file names its domain once and then just logs:
 *
 *     NE_LOG_DOMAIN(Scene);
 *     ...
 *     NE_LOG("transition to scene %hu", id);   // [Scene] transition to scene 3
 *
 * A file that logs without naming a domain does not compile, which is the right
 * way to find out. NE_LOG_ALWAYS() writes whether or not the domain is on, for
 * the handful of lines that must never be missed.
 *
 * There are two switches. NE_LOGGING is a build flag: with it off every macro
 * here expands to nothing and the format strings leave the executable. The
 * domain mask is a runtime word, so a disabled domain costs one AND and its
 * arguments are never evaluated.
 */

#ifndef __LOGGER_H__INCLUDED__
#define __LOGGER_H__INCLUDED__

#include <ace/types.h>

namespace NEONengine
{
    /**
     * @brief Every logging domain in the engine.
     *
     * This list is the only place a domain exists. It gives out the bit, the
     * enumerator and the printed name together, so no file has to pick a bit by
     * hand, two files cannot claim the same one, and a name cannot drift from
     * what it switches. Order sets the bit, so appending is free and reordering
     * only changes which bit a mask literal refers to.
     *
     * The mask is 32 bits wide, so there is room for 32 domains.
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
    X(Test)

    enum class log_domain : UBYTE
    {
#define NE_LOG_DOMAIN_ENUMERATOR(name) name,
        NE_LOG_DOMAIN_LIST(NE_LOG_DOMAIN_ENUMERATOR)
#undef NE_LOG_DOMAIN_ENUMERATOR
            COUNT
    };

    static_assert(static_cast<ULONG>(log_domain::COUNT) <= 32,
        "the domain mask is 32 bits wide, so there is room for 32 domains");

    /**
     * @brief The mask bit @p domain is switched by.
     */
    constexpr ULONG log_bit(log_domain domain)
    {
        return 1UL << static_cast<ULONG>(domain);
    }

    /**
     * @brief The domains that print when the engine starts.
     *
     * Clear a bit to silence a domain, as in ~log_bit(log_domain::Timeline).
     * This belongs in the project eventually, next to the other debug settings,
     * but a constant costs the editor nothing meanwhile and g_logMask can be
     * changed at runtime by anything that wants to.
     */
    constexpr ULONG NE_LOG_MASK_DEFAULT = 0xFFFFFFFFUL;

    /**
     * @brief Whether ACE's own log output starts silenced.
     *
     * ACE keeps a mute flag its writers check first, so this costs nothing and
     * needs no ACE patch. Turning it on leaves ACE_DEBUG in place, which
     * matters: it also enables bobCheckGood and the bob buffer auto-correction
     * that scene_bobs_draw depends on.
     *
     * Some engine code still writes through ACE rather than through here: the
     * script VM's diagnostics are the largest part, and the assert and fatal
     * error paths are the ones worth remembering. Muting ACE takes those with
     * it.
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
     * The newline is added here. Every line is emitted in a single write
     * because the UAE console ends a line on each one, so a message split
     * across two calls arrives as two lines.
     *
     * A line longer than the buffer is truncated rather than overrunning it.
     * The buffer is static and shared, so this must not be called from an
     * interrupt.
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
