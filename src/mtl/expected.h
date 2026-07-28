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
     * auto result = mtl::unexpected("fail");
     * if (result) { int v = result.value(); } else { puts(result.error()); }
     * @endcode
     */

    /**
     * @brief Wrapper for unexpected values (error results)
     */
    template<typename E>
    struct unexpected_t
    {
        E error;
        constexpr explicit unexpected_t(E const& e) : error(e) {}
        
        template<typename U>
        constexpr explicit unexpected_t(U&& e) : error(static_cast<U&&>(e)) {}
    };

    /**
     * @brief Helper to create unexpected values - special case for string literals
     */
    template<size_t N>
    constexpr unexpected_t<char const*> unexpected(char const (&e)[N])
    {
        return unexpected_t<char const*>(e);
    }
    
    /**
     * @brief Helper to create unexpected values - general case
     */
    template<typename E>
    constexpr unexpected_t<E> unexpected(E const& e)
    {
        return unexpected_t<E>(e);
    }

    template<typename E>
    constexpr unexpected_t<E> unexpected(E&& e)
    {
        return unexpected_t<E>(static_cast<E&&>(e));
    }

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
        struct error_tag {};
        static constexpr error_tag unexpect{};

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
        constexpr expected(error_tag, E const& e) : _error(e), _has(false) {}
        /**
         * @brief Construct with an error (rvalue)
         */
        constexpr expected(error_tag, E&& e) : _error(static_cast<E&&>(e)), _has(false) {}

        /**
         * @brief Construct from unexpected_t - compatible type conversion
         */
        template<typename U>
        constexpr expected(unexpected_t<U> const& u) : _error(static_cast<E>(u.error)), _has(false) {}
        
        /**
         * @brief Construct from unexpected_t (rvalue) - compatible type conversion
         */
        template<typename U>
        constexpr expected(unexpected_t<U>&& u) : _error(static_cast<E>(u.error)), _has(false) {}

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
         * @brief Assignment from unexpected_t
         */
        template<typename U>
        constexpr expected& operator=(unexpected_t<U> const& u)
        {
            if (_has)
            {
                _value.~T();
                new (&_error) E(static_cast<E>(u.error));
                _has = false;
            }
            else
            {
                _error = static_cast<E>(u.error);
            }
            return *this;
        }

        /**
         * @brief Assignment from unexpected_t (rvalue)
         */
        template<typename U>
        constexpr expected& operator=(unexpected_t<U>&& u)
        {
            if (_has)
            {
                _value.~T();
                new (&_error) E(static_cast<E>(u.error));
                _has = false;
            }
            else
            {
                _error = static_cast<E>(u.error);
            }
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

}  // namespace mtl

#endif  // __MTL__EXPECTED_H__INCLUDED__