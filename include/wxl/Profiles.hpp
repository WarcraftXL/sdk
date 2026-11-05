// SPDX-License-Identifier: GPL-3.0-or-later
// Profiles: the core's settings profiles in C++ (the "wxl.profiles" service).

#pragma once

#include "wxl/CoreServices.hpp"

#include <type_traits>

namespace wxl
{
    /// Saved sets of every extension's settings, under WTF\WarcraftXL\profiles: `Service<Profiles> profiles;`.
    class Profiles
    {
    public:
        Profiles() = default;
        explicit Profiles(const WXL_ProfilesApi* api) : m_api(api) {}

        /// 1 to 48 letters, digits, spaces, '-', '_' or '.', not starting with '.'.
        bool ValidName(const char* name) const { return m_api->ValidName(name) == WXL_OK; }
        /// Writes every settings set under `name`, replacing it.
        bool Save(const char* name) const { return m_api->Save(name) == WXL_OK; }
        /// Applies a profile; false when there is none of that name.
        bool Load(const char* name) const { return m_api->Load(name) == WXL_OK; }
        bool Delete(const char* name) const { return m_api->Delete(name) == WXL_OK; }
        /// The profile last loaded or saved this session, "" for none.
        const char* Current() const { return m_api->Current(); }

        /// Calls f(const char* name) for every saved profile, sorted by name.
        template <class F>
        void List(F&& f) const
        {
            using Fn = std::remove_reference_t<F>;
            m_api->List([](void* user, const char* name) { (*static_cast<Fn*>(user))(name ? name : ""); }, &f);
        }

    private:
        const WXL_ProfilesApi* m_api = nullptr;
    };

    template <>
    struct ServiceTraits<Profiles>
    {
        using Table                           = WXL_ProfilesApi;
        static constexpr const char* kName    = WXL_PROFILES_API_NAME;
        static constexpr uint32_t    kVersion = WXL_PROFILES_API_VERSION;
    };
}
