// SPDX-License-Identifier: GPL-3.0-or-later
// Observable properties: `unit.HealthChanged()` is a signal raised when the read changes.

#pragma once

#include "wxl/Result.hpp"
#include "wxl/Signal.hpp"

#include <optional>
#include <utility>

namespace wxl
{
    /// What a change signal carries.
    template <class T>
    struct Change
    {
        T previous{};
        T value{};
    };

    namespace detail
    {
        /// A change signal over a read, checked each frame against the last value seen; it ends when the
        /// read answers Expired or NullHandle.
        template <class T, class Read>
        Signal<Change<T>> Watch(const char* name, Read read)
        {
            std::optional<T> last;
            if (auto now = read()) last = *now;
            auto source = [read, last](const void*, Change<T>& out) mutable -> Deliver {
                Result<T> now = read();
                if (!now)
                    return now.Code() == Error::Expired || now.Code() == Error::NullHandle ? Deliver::End
                                                                                          : Deliver::Skip;
                if (!last)
                {
                    last = *now;
                    return Deliver::Skip;
                }
                if (*last == *now) return Deliver::Skip;
                out  = Change<T>{ *last, *now };
                last = *now;
                return Deliver::Yes;
            };
            return Signal<Change<T>>(events::Event::OnUpdate, WXL_Handle{}, std::move(source), name);
        }
    }
}
