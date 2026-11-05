// SPDX-License-Identifier: GPL-3.0-or-later
// The extension's link to the core: the table WXL_Load received, its name and version, the guard.

#pragma once

#include "wxl/Common.hpp"
#include "wxl/abi/Plugin.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace wxl
{
    namespace detail
    {
        struct Link
        {
            const WXL_Api* api     = nullptr;
            const char*    name    = "extension";
            const char*    version = "0.0.0";
            uint32_t       self    = 0; ///< this extension's id, for WXL_Api::Run
            int            level   = WXL_LOG_INFO; ///< the core's log threshold
        };
        inline Link g_link;

        /// Whether the core's table reaches `member`: a core older than these headers answers no.
        template <class Member>
        bool Has(Member WXL_Api::*member)
        {
            const WXL_Api* api = g_link.api;
            if (!api) return false;
            const auto end = static_cast<std::size_t>(reinterpret_cast<const char*>(&(api->*member)) -
                                                      reinterpret_cast<const char*>(api)) + sizeof(Member);
            return api->structSize >= end && api->*member != nullptr;
        }

        /// WXL_LOG_LEVEL as the core reads it: a digit or a name's first letter; -1 when unreadable.
        inline int LevelOf(const char* text)
        {
            if (*text >= '0' && *text <= '5') return *text - '0';
            switch (*text | 0x20)
            {
            case 't': return WXL_LOG_TRACE;
            case 'd': return WXL_LOG_DEBUG;
            case 'i': return WXL_LOG_INFO;
            case 'w': return WXL_LOG_WARN;
            case 'e': return WXL_LOG_ERROR;
            case 'o': return WXL_LOG_ERROR + 1;
            default: return -1;
            }
        }

        /// The value of `WXL_LOG_LEVEL=` in a WarcraftXL.cfg, or -1.
        inline int LevelInFile(const char* path)
        {
            char   text[4096];
            DWORD  read = 0;
            HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
            if (file == INVALID_HANDLE_VALUE) return -1;
            const BOOL ok = ReadFile(file, text, sizeof text - 1, &read, nullptr);
            CloseHandle(file);
            if (!ok) return -1;
            text[read] = '\0';
            constexpr char kKey[] = "WXL_LOG_LEVEL";
            for (char* line = text; line && *line;)
            {
                while (*line == ' ' || *line == '\t' || *line == '\r' || *line == '\n') ++line;
                char* next = line;
                while (*next && *next != '\n') ++next;
                bool match = true;
                for (std::size_t i = 0; i + 1 < sizeof kKey; ++i) match = match && line[i] == kKey[i];
                if (match)
                {
                    char* value = line + sizeof kKey - 1;
                    while (*value == ' ' || *value == '\t') ++value;
                    if (*value == '=')
                    {
                        ++value;
                        while (*value == ' ' || *value == '\t') ++value;
                        return LevelOf(value);
                    }
                }
                line = *next ? next + 1 : nullptr;
            }
            return -1;
        }

        /// The core's threshold: the environment, then WarcraftXL.cfg here or one folder up, then Info.
        inline int ReadLogLevel()
        {
            char value[16] = {};
            const DWORD length = GetEnvironmentVariableA("WXL_LOG_LEVEL", value, sizeof value);
            if (length > 0 && length < sizeof value)
                if (const int level = LevelOf(value); level >= 0) return level;
            constexpr const char* kFiles[] = { "WarcraftXL.cfg", "..\\WarcraftXL.cfg" };
            for (const char* path : kFiles)
                if (const int level = LevelInFile(path); level >= 0) return level;
            return WXL_LOG_INFO;
        }
    }

    /// Records the core's table; Export calls it. False for a table of another layout.
    inline bool Bind(const WXL_Api* api, const char* name, const char* version = nullptr)
    {
        if (!api || api->apiVersion != WXL_API_VERSION) return false;
        detail::g_link = { api, name ? name : "extension", version ? version : "0.0.0", 0, detail::ReadLogLevel() };
        if (detail::Has(&WXL_Api::Self)) detail::g_link.self = api->Self();
        return true;
    }

    /// The core's table; null before the extension starts.
    inline const WXL_Api* Api() { return detail::g_link.api; }

    /// The extension's name, from its wxl.json: the tag of its log lines.
    inline const char* ExtensionName() { return detail::g_link.name; }

    /// The extension's version, from its wxl.json.
    inline const char* ExtensionVersion() { return detail::g_link.version; }

    /// The extension's id, as the core numbers it (WXL_Api::Self while its own code runs).
    inline uint32_t ExtensionId() { return detail::g_link.self; }

    /// Runs f() under the core's guard: a fault disables the extension instead of ending the game.
    /// False when it faulted, or when the extension is disabled (f not run).
    template <class F>
    bool Guarded(F&& f)
    {
        if (!detail::Has(&WXL_Api::Run))
        {
            f();
            return true;
        }
        using Fn = std::remove_reference_t<F>;
        return Api()->Run(detail::g_link.self, [](void* p) { (*static_cast<Fn*>(p))(); },
                          const_cast<void*>(static_cast<const void*>(&f))) == WXL_OK;
    }
}
