#ifndef __MTL__EXPECTED_H__INCLUDED__
#define __MTL__EXPECTED_H__INCLUDED__

#include <mtl/utility.h>

namespace mtl
{
    /**
     * @file expected.h
     * @brief Minimal constexpr-compatible expected<T, E> type (no exceptions,
     * no allocation).
     *
     * Usage:
     * @code
     * auto result = mtl::make_error<int>("fail");
     * if (result) { int v = result.value(); } else { puts(result.error()); }
     * @endcode
     */

    template<typename T, typename E>
    class expected
    {
        union
        {
            T _value;  ///< Value storage
            E _error;  ///< Error storage
        };
        bool _has;

        public:  ///////////////////////////////////////////////////////////////////////////////////
        /**
         * @brief Tag type for error construction
         */
        struct unexpected
        {};
        /**
         * @brief Static tag instance for error construction
         */
        static constexpr unexpected unexpect{};

        public:  ///////////////////////////////////////////////////////////////////////////////////
        /*
         *   _____                _                   _
         *  / ____|              | |                 | |
         * | |     ___  _ __  ___| |_ _ __ _   _  ___| |_ ___  _ __ ___
         * | |    / _ \| '_ \/ __| __| '__| | | |/ __| __/ _ \| '__/ __|
         * | |___| (_) | | | \__ \ |_| |  | |_| | (__| || (_) | |  \__ \
         *  \_____\___/|_| |_|___/\__|_|   \__,_|\___|\__\___/|_|  |___/
         */
        /**
         * @brief Construct with a value
         */
        constexpr expected(T const& v) : _value(v), _has(true) {}
        /**
         * @brief Construct with a value (rvalue)
         */
        constexpr expected(T&& v) : _value(static_cast<T&&>(v)), _has(true) {}

        /**
         * @brief Construct with an error
         */
        constexpr expected(unexpected, E const& e) : _error(e), _has(false) {}
        /**
         * @brief Construct with an error (rvalue)
         */
        constexpr expected(unexpected, E&& e) : _error(static_cast<E&&>(e)), _has(false) {}

        /**
         * @brief Copy constructor
         */
        constexpr expected(expected const& o) : _has(o._has)
        {
            if (_has)
                new (&_value) T(o._value);
            else
                new (&_error) E(o._error);
        }
        /**
         * @brief Move constructor
         */
        constexpr expected(expected&& o) : _has(o._has)
        {
            if (_has)
                new (&_value) T(static_cast<T&&>(o._value));
            else
                new (&_error) E(static_cast<E&&>(o._error));
        }
        /**
         * @brief Destructor
         */
        constexpr ~expected()
        {
            if (_has)
                _value.~T();
            else
                _error.~E();
        }

        /*
         *                    _                                  _
         *      /\           (_)                                | |
         *     /  \   ___ ___ _  __ _ _ __  _ __ ___   ___ _ __ | |_
         *    / /\ \ / __/ __| |/ _` | '_ \| '_ ` _ \ / _ \ '_ \| __|
         *   / ____ \\__ \__ \ | (_| | | | | | | | | |  __/ | | | |_
         *  /_/    \_\___/___/_|\__, |_| |_|_| |_| |_|\___|_| |_|\__|
         *                       __/ |
         *                      |___/
         */

        /**
         * @brief Assignment operator (copy-and-swap idiom)
         */
        constexpr expected& operator=(expected o)
        {
            swap(o);
            return *this;
        }
        /**
         * @brief Swap contents with another expected
         */
        constexpr void swap(expected& o)
        {
            if (this == &o) return;
            if (_has && o._has)
            {
                T tmp    = static_cast<T&&>(o._value);
                o._value = static_cast<T&&>(_value);
                _value   = static_cast<T&&>(tmp);
            }
            else if (!_has && !o._has)
            {
                E tmp    = static_cast<E&&>(o._error);
                o._error = static_cast<E&&>(_error);
                _error   = static_cast<E&&>(tmp);
            }
            else if (_has)
            {
                E tmp_err = static_cast<E&&>(o._error);
                o._error.~E();
                new (&o._value) T(static_cast<T&&>(_value));
                _value.~T();
                new (&_error) E(static_cast<E&&>(tmp_err));
                mtl::swap(_has, o._has);
            }
            else { o.swap(*this); }
        }

        /*
         *     /\
         *    /  \   ___ ___ ___  ___ ___  ___  _ __ ___
         *   / /\ \ / __/ __/ _ \/ __/ __|/ _ \| '__/ __|
         *  / ____ \ (_| (_|  __/\__ \__ \ (_) | |  \__ \
         * /_/    \_\___\___\___||___/___/\___/|_|  |___/
         *
         */

        /**
         * @brief True if value is present
         */
        constexpr bool has_value() const { return _has; }
        /**
         * @brief Explicit bool conversion
         */
        constexpr explicit operator bool() const { return _has; }
        /**
         * @brief Get value (unchecked)
         */
        constexpr T& value() { return _value; }
        /**
         * @brief Get value (const, unchecked)
         */
        constexpr T const& value() const { return _value; }
        /**
         * @brief Get error (unchecked)
         */
        constexpr E& error() { return _error; }
        /**
         * @brief Get error (const, unchecked)
         */
        constexpr E const& error() const { return _error; }
    };

    /*
     *  _    _ _   _ _ _ _
     * | |  | | | (_) (_) |
     * | |  | | |_ _| |_| |_ _   _
     * | |  | | __| | | | __| | | |
     * | |__| | |_| | | | |_| |_| |
     *  \____/ \__|_|_|_|\__|\__, |
     *                        __/ |
     *                       |___/
     */

    /**
     * @brief Helper to construct an success result
     */
    template<typename T, typename E>
    constexpr expected<T, E> make_success(T const& value)
    {
        return expected<T, E>(value);
    }

    /**
     * @brief Helper to construct an success result (rvalue)
     */
    template<typename T, typename E>
    constexpr expected<T, E> make_success(T&& value)
    {
        return expected<T, E>(static_cast<T&&>(value));
    }

    /**
     * @brief Helper to construct an error result
     */
    template<typename T, typename E>
    constexpr expected<T, E> make_error(E const& e)
    {
        return expected<T, E>(expected<T, E>::unexpect, e);
    }

    /**
     * @brief Helper to construct an error result (rvalue)
     */
    template<typename T, typename E>
    constexpr expected<T, E> make_error(E&& e)
    {
        return expected<T, E>(expected<T, E>::unexpect, static_cast<E&&>(e));
    }
}  // namespace mtl

#endif  // __MTL__EXPECTED_H__INCLUDED__