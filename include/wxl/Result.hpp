// SPDX-License-Identifier: GPL-3.0-or-later
// wxl::Result<T>: a value, or the Error that stood in for it.

#pragma once

#include "wxl/abi/Common.h"

#include <cstdint>
#include <cstdlib>
#include <exception>
#include <optional>
#include <utility>

namespace wxl
{
    /// Why a call gave no value. The numbers are the WXL_E_* status codes of the C services.
    enum class Error : int32_t
    {
        /// The object is gone: it left view, was destroyed, or the world was unloaded.
        Expired = 1,
        /// The handle is empty: default-constructed, or a lookup found nothing to put in it.
        NullHandle = 2,
        /// The core does not provide this call: it is older than the SDK the extension was built with.
        Unsupported = 3,
        /// The object is not of the kind the call needs.
        WrongKind = 4,
        /// The player is not in the world: login screen or loading screen.
        NotInWorld = 5,
        /// Nothing matches what was asked for.
        NotFound = 6,
        /// An argument is out of range or malformed.
        InvalidArgument = 7,
        /// A wait ended before what it waited for came.
        Timeout = 8,
        /// The client's bytes are not the ones expected: another build, or someone patched them first.
        Mismatch = 9,
        /// The extension's wxl.json does not grant the permission the call needs.
        Denied = 10,
    };

    /// What an Error means, in one sentence that says what to do about it.
    constexpr const char* Message(Error error)
    {
        switch (error)
        {
        case Error::Expired:         return "the object is gone (left view, destroyed, or the world unloaded); look it up again";
        case Error::NullHandle:      return "the handle is empty; a lookup found nothing";
        case Error::Unsupported:     return "this core does not provide the call; update WarcraftXL";
        case Error::WrongKind:       return "the object is not of the kind the call needs; check it with Is() or As<T>()";
        case Error::NotInWorld:      return "the player is not in the world; wait for OnWorldEnter";
        case Error::NotFound:        return "nothing matches what was asked for";
        case Error::InvalidArgument: return "an argument is out of range or malformed";
        case Error::Timeout:         return "the wait ended before what it waited for came";
        case Error::Mismatch:        return "the client's bytes are not the ones expected; nothing was written";
        case Error::Denied:          return "the call needs a permission: add it to \"permissions\" in wxl.json";
        }
        return "unknown error";
    }

    /// Thrown by Result::Value() on an error; uncaught, the core's guard disables the extension.
    class BadResult : public std::exception
    {
    public:
        explicit BadResult(Error error) : m_error(error) {}
        Error       Code() const noexcept { return m_error; }
        const char* what() const noexcept override { return Message(m_error); }

    private:
        Error m_error;
    };

    namespace detail
    {
        [[noreturn]] inline void BadAccess(Error error)
        {
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
            throw BadResult(error);
#else
            (void)error;
            std::abort();
#endif
        }
    }

    /// A value of type T, or the Error that stood in for it. Value(), `*` and `->` throw BadResult on
    /// an error; ValueOr(x) never throws:
    ///     if (auto pos = unit.Position()) Use(*pos); else Log::Warn("{}", pos.Message());
    template <class T>
    class [[nodiscard]] Result
    {
    public:
        using value_type = T;

        /// A value.
        Result(const T& value) : m_value(value) {}
        /// A value, moved in.
        Result(T&& value) : m_value(std::move(value)) {}
        /// No value, and why.
        Result(Error error) : m_error(error) {}

        /// True when there is a value.
        bool HasValue() const { return m_value.has_value(); }
        /// True when there is a value: `if (auto r = ...)`.
        explicit operator bool() const { return HasValue(); }

        /// The error; meaningful only when there is no value.
        Error Code() const { return m_error; }
        /// The error's message; empty when there is a value.
        const char* Message() const { return HasValue() ? "" : wxl::Message(m_error); }

        /// The value; throws BadResult on an error.
        T& Value() &
        {
            if (!m_value) detail::BadAccess(m_error);
            return *m_value;
        }
        const T& Value() const&
        {
            if (!m_value) detail::BadAccess(m_error);
            return *m_value;
        }
        T&& Value() &&
        {
            if (!m_value) detail::BadAccess(m_error);
            return std::move(*m_value);
        }

        /// The value, as Value() gives it.
        T&       operator*() & { return Value(); }
        const T& operator*() const& { return Value(); }
        T&&      operator*() && { return std::move(*this).Value(); }
        /// A member of the value, as Value() gives it.
        T*       operator->() { return &Value(); }
        const T* operator->() const { return &Value(); }

        /// The value, or `fallback` on an error.
        template <class U>
        T ValueOr(U&& fallback) const
        {
            return m_value ? *m_value : static_cast<T>(std::forward<U>(fallback));
        }

    private:
        std::optional<T> m_value;
        Error            m_error{};
    };

    /// A call that answers nothing but whether it worked.
    template <>
    class [[nodiscard]] Result<void>
    {
    public:
        using value_type = void;

        /// It worked.
        Result() = default;
        /// It did not, and why.
        Result(Error error) : m_ok(false), m_error(error) {}

        /// True when it worked.
        bool HasValue() const { return m_ok; }
        /// True when it worked: `if (auto r = ...)`.
        explicit operator bool() const { return m_ok; }
        /// The error; meaningful only when it did not work.
        Error       Code() const { return m_error; }
        /// The error's message; empty when it worked.
        const char* Message() const { return m_ok ? "" : wxl::Message(m_error); }

        /// Throws BadResult when it did not work.
        void Value() const
        {
            if (!m_ok) detail::BadAccess(m_error);
        }

    private:
        bool  m_ok = true;
        Error m_error{};
    };
}
