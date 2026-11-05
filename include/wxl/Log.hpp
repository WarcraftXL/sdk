// SPDX-License-Identifier: GPL-3.0-or-later
// Log::Info("loaded {} tables", n): std::format syntax, formatted on the stack, under the extension's name.

#pragma once

#include "wxl/Core.hpp"

#include <cstddef>
#include <format>
#include <utility>

namespace wxl
{
    /// One line in the core's log, tagged with the extension's name. Nothing is formatted below the
    /// core's threshold (WXL_LOG_LEVEL); a line longer than kLineSize - 1 characters is cut.
    class Log
    {
    public:
        static constexpr std::size_t kLineSize = 1024;

        template <class... A>
        static void Trace(std::format_string<A...> format, A&&... args)
        {
            Write(WXL_LOG_TRACE, format, std::forward<A>(args)...);
        }
        template <class... A>
        static void Debug(std::format_string<A...> format, A&&... args)
        {
            Write(WXL_LOG_DEBUG, format, std::forward<A>(args)...);
        }
        template <class... A>
        static void Info(std::format_string<A...> format, A&&... args)
        {
            Write(WXL_LOG_INFO, format, std::forward<A>(args)...);
        }
        template <class... A>
        static void Warn(std::format_string<A...> format, A&&... args)
        {
            Write(WXL_LOG_WARN, format, std::forward<A>(args)...);
        }
        template <class... A>
        static void Error(std::format_string<A...> format, A&&... args)
        {
            Write(WXL_LOG_ERROR, format, std::forward<A>(args)...);
        }

        /// Whether a line at `level` (WXL_LOG_*) reaches the log.
        static bool Enabled(int level) { return Api() && level >= detail::g_link.level; }

        /// A line at a level known only at run time.
        template <class... A>
        static void Write(int level, std::format_string<A...> format, A&&... args)
        {
            if (!Enabled(level)) return;
            char line[kLineSize];
            const auto end = std::format_to_n(line, kLineSize - 1, format, std::forward<A>(args)...);
            const std::size_t length = end.size < 0 ? 0 : static_cast<std::size_t>(end.size);
            line[length < kLineSize - 1 ? length : kLineSize - 1] = '\0';
            Api()->Log(level, ExtensionName(), "%s", line);
        }
    };
}
