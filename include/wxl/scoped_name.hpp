// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// Names in a mod's scope, the rule wxl.storage's scoped_name documents: the mod's id in brackets
// before the extension of the file name, "Creature\\Foo\\Foo[my.mod].m2". The runtime resolves
// them by this header and a mod's provider reads them with it, so both sides read one rule.
// Header-only, nothing but the standard library.

#ifndef WXL_SCOPED_NAME_HPP
#define WXL_SCOPED_NAME_HPP

#include <cstddef>
#include <string>
#include <string_view>

namespace wxl
{
    /// The longest scoped name, terminator excluded: the client's paths are 260 bytes.
    inline constexpr size_t kScopedNameMax = 259;

    /// The longest mod id a scope holds, as the manifest allows it.
    inline constexpr size_t kScopeMax = 64;

    /// Whether @p mod can be a scope: letters, digits, '.', '-' and '_', at most kScopeMax.
    inline bool IsScope(std::string_view mod)
    {
        if (mod.empty() || mod.size() > kScopeMax) return false;
        for (const char c : mod)
        {
            const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
            const bool digit  = c >= '0' && c <= '9';
            if (!letter && !digit && c != '.' && c != '-' && c != '_') return false;
        }
        return true;
    }

    namespace detail
    {
        /// Where the file name of a path starts: after its last separator.
        inline size_t LeafAt(std::string_view path)
        {
            const size_t slash = path.find_last_of("\\/");
            return slash == std::string_view::npos ? 0 : slash + 1;
        }
    } // namespace detail

    /// A scoped name taken apart: the mod whose scope it is, and the name without the scope.
    struct ScopedParts
    {
        std::string mod;
        std::string base;
    };

    /**
     * @brief Takes a scoped name apart: true, with @p out filled, when the file name of @p name
     *        holds a scope -- the first "[...]" in it whose inside IsScope accepts.
     *
     * Works on what the client derives from a scoped name as well: "Foo[my.mod]00.skin" is in
     * the scope of my.mod, its base "Foo00.skin".
     */
    inline bool SplitScopedName(std::string_view name, ScopedParts* out = nullptr)
    {
        const size_t leaf  = detail::LeafAt(name);
        size_t       open  = name.find('[', leaf);
        while (open != std::string_view::npos)
        {
            const size_t close = name.find(']', open + 1);
            if (close == std::string_view::npos) return false;
            const std::string_view mod = name.substr(open + 1, close - open - 1);
            if (IsScope(mod))
            {
                if (out)
                {
                    out->mod.assign(mod.data(), mod.size());
                    out->base.assign(name.data(), open);
                    out->base.append(name.data() + close + 1, name.size() - close - 1);
                }
                return true;
            }
            open = name.find('[', open + 1);
        }
        return false;
    }

    /**
     * @brief The name of @p path in @p mod's scope: "[<mod>]" before the extension of its file
     *        name, or at its end when it has none.
     *
     * Empty for an empty path, a mod IsScope refuses, a path already in a scope, or a result
     * longer than kScopedNameMax.
     */
    inline std::string ScopedName(std::string_view mod, std::string_view path)
    {
        if (path.empty() || !IsScope(mod) || SplitScopedName(path)) return std::string();
        const size_t leaf = detail::LeafAt(path);
        size_t       dot  = path.rfind('.');
        if (dot == std::string_view::npos || dot < leaf) dot = path.size();
        std::string scoped;
        scoped.reserve(path.size() + mod.size() + 2);
        scoped.append(path.data(), dot);
        scoped += '[';
        scoped.append(mod.data(), mod.size());
        scoped += ']';
        scoped.append(path.data() + dot, path.size() - dot);
        if (scoped.size() > kScopedNameMax) return std::string();
        return scoped;
    }
} // namespace wxl

#endif // WXL_SCOPED_NAME_HPP
