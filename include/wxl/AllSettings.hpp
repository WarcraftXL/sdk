// SPDX-License-Identifier: GPL-3.0-or-later
// AllSettings: every extension's settings, as a settings screen lists and changes them.

#pragma once

#include "wxl/CoreServices.hpp"

#include <cstdint>
#include <type_traits>

namespace wxl
{
    enum class SettingKind : uint32_t
    {
        Bool    = WXL_SETTING_KIND_BOOL,
        Integer = WXL_SETTING_KIND_INTEGER,
        Number  = WXL_SETTING_KIND_NUMBER,
        Text    = WXL_SETTING_KIND_TEXT,
    };

    /// One setting during a visit; the strings are valid for the call only, never null.
    struct SettingView
    {
        const char* extension;
        const char* section;
        const char* key;
        SettingKind kind;
        const char* value; ///< the current value, as text
        const char* doc;
        double      min;   ///< with max < min: no range
        double      max;
        double      step;  ///< 0: none
        const char* ui;    ///< "slider", "checkbox", "input" or ""

        bool Ranged() const { return max >= min && kind != SettingKind::Bool && kind != SettingKind::Text; }
    };

    /// The settings of every extension (the "wxl.settings" service): `Service<AllSettings> settings;`.
    class AllSettings
    {
    public:
        AllSettings() = default;
        explicit AllSettings(const WXL_SettingsApi* api) : m_api(api) {}

        /// Whether this core lists them (Visit is newer than the table's first members).
        bool Listed() const { return m_api->structSize >= sizeof(WXL_SettingsApi) && m_api->Visit; }

        /// Calls f(const SettingView&) for every setting, by extension then in declaration order.
        template <class F>
        void Visit(F&& f) const
        {
            if (!Listed()) return;
            using Fn = std::remove_reference_t<F>;
            m_api->Visit(
                [](void* user, const char* extension, const WXL_SettingDecl* d) {
                    const auto text = [](const char* s) { return s ? s : ""; };
                    const SettingView view{ text(extension), text(d->section), text(d->key), SettingKind(d->kind),
                                            text(d->value),  text(d->doc),     d->min,       d->max,
                                            d->step,         text(d->ui) };
                    (*static_cast<Fn*>(user))(view);
                },
                &f);
        }

        /// Changes a setting of any extension, as /wxl set does; false when refused.
        bool Set(const char* extension, const char* key, const char* value) const
        {
            return Listed() && m_api->SetOf(extension, key, value) == WXL_OK;
        }

    private:
        const WXL_SettingsApi* m_api = nullptr;
    };

    template <>
    struct ServiceTraits<AllSettings>
    {
        using Table                           = WXL_SettingsApi;
        static constexpr const char* kName    = WXL_SETTINGS_API_NAME;
        static constexpr uint32_t    kVersion = WXL_SETTINGS_API_VERSION;
    };
}
