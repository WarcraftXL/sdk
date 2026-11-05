// SPDX-License-Identifier: GPL-3.0-or-later
// The extension's own wxl.json, beside its DLL: extension.id and extension.version, read when the core asks.

#pragma once

#include "wxl/Common.hpp"

#include <cstddef>
#include <cstdint>

namespace wxl::detail
{
    /// What Export reports to the core; the name falls back to the DLL's file name.
    struct Identity
    {
        char     name[64]    = "extension";
        char     version[32] = "0.0.0";
        uint32_t packed      = 0; ///< major << 16 | minor << 8 | patch, WXL_PluginInfo::pluginVersion
    };

    /// A small reader over the manifest text: strings, nesting, and the members of one object.
    class ManifestText
    {
    public:
        explicit ManifestText(const char* text) : m_at(text) {}

        /// Copies the string member `key` of the object `outer` (a member of the root) into `out`.
        bool Find(const char* outer, const char* key, char* out, std::size_t capacity)
        {
            int  depth   = 0;
            bool inOuter = false;
            char last[64] = "";
            while (*m_at)
            {
                const char c = *m_at++;
                if (c == '{')
                {
                    ++depth;
                    if (depth == 2) inOuter = Same(last, outer);
                }
                else if (c == '}')
                {
                    if (depth == 2) inOuter = false;
                    --depth;
                }
                else if (c == '"')
                {
                    char text[64];
                    String(text, sizeof text);
                    Skip();
                    if (*m_at == ':')
                    {
                        ++m_at;
                        Skip();
                        if (depth == 2 && inOuter && Same(text, key) && *m_at == '"')
                        {
                            ++m_at;
                            String(out, capacity);
                            return true;
                        }
                        Copy(last, text, sizeof last);
                    }
                }
            }
            return false;
        }

    private:
        static bool Same(const char* a, const char* b)
        {
            while (*a && *a == *b) ++a, ++b;
            return *a == *b;
        }

        static void Copy(char* out, const char* in, std::size_t capacity)
        {
            std::size_t i = 0;
            for (; in[i] && i + 1 < capacity; ++i) out[i] = in[i];
            out[i] = '\0';
        }

        void Skip()
        {
            while (*m_at == ' ' || *m_at == '\t' || *m_at == '\r' || *m_at == '\n') ++m_at;
        }

        /// Reads a string whose opening quote was consumed; escapes are kept as their second character.
        void String(char* out, std::size_t capacity)
        {
            std::size_t n = 0;
            while (*m_at && *m_at != '"')
            {
                if (*m_at == '\\' && m_at[1]) ++m_at;
                if (n + 1 < capacity) out[n++] = *m_at;
                ++m_at;
            }
            if (*m_at) ++m_at;
            out[n] = '\0';
        }

        const char* m_at;
    };

    /// "1.2.3" as major << 16 | minor << 8 | patch.
    inline uint32_t PackVersion(const char* text)
    {
        uint32_t part[3] = {};
        for (int i = 0; i < 3 && *text; ++i)
        {
            while (*text >= '0' && *text <= '9') part[i] = part[i] * 10 + uint32_t(*text++ - '0');
            if (*text != '.') break;
            ++text;
        }
        const auto clamp = [](uint32_t v, uint32_t max) { return v > max ? max : v; };
        return clamp(part[0], 0xFFFF) << 16 | clamp(part[1], 0xFF) << 8 | clamp(part[2], 0xFF);
    }

    /// The identity of the DLL holding `anchor`: its wxl.json's id and version, else its file name.
    inline Identity ReadIdentity(const void* anchor)
    {
        Identity id;
        HMODULE  module = nullptr;
        char     path[MAX_PATH];
        if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                static_cast<LPCSTR>(anchor), &module) ||
            !GetModuleFileNameA(module, path, MAX_PATH))
            return id;

        char* slash = path;
        for (char* p = path; *p; ++p)
            if (*p == '\\' || *p == '/') slash = p + 1;
        std::size_t n = 0;
        for (; slash[n] && slash[n] != '.' && n + 1 < sizeof id.name; ++n) id.name[n] = slash[n];
        id.name[n] = '\0';

        constexpr char kManifest[] = "wxl.json";
        if (std::size_t(slash - path) + sizeof kManifest > sizeof path) return id;
        for (std::size_t i = 0; i < sizeof kManifest; ++i) slash[i] = kManifest[i];

        char   text[8192];
        DWORD  read = 0;
        HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (file == INVALID_HANDLE_VALUE) return id;
        const BOOL ok = ReadFile(file, text, sizeof text - 1, &read, nullptr);
        CloseHandle(file);
        if (!ok) return id;
        text[read] = '\0';

        char name[sizeof id.name];
        if (ManifestText(text).Find("extension", "id", name, sizeof name) && *name)
            for (std::size_t i = 0; i < sizeof name; ++i) id.name[i] = name[i];
        ManifestText(text).Find("extension", "version", id.version, sizeof id.version);
        id.packed = PackVersion(id.version);
        return id;
    }
}
