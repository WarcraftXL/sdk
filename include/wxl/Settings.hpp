// SPDX-License-Identifier: GPL-3.0-or-later
// An extension's settings, declared as an annotated struct and kept in its ini by the core.

#pragma once

#include "wxl/Annotate.hpp"
#include "wxl/Result.hpp"
#include "wxl/CoreServices.hpp"
#include "wxl/Log.hpp"
#include "wxl/Signal.hpp"
#include "wxl/abi/Settings.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <string>
#include <type_traits>
#include <vector>

namespace wxl
{
    /// One setting, as the generated traits describe it.
    struct SettingInfo
    {
        const char* key;
        uint32_t    kind; ///< WXL_SETTING_KIND_*
        const char* doc;
        double      min;  ///< with max < min: no range
        double      max;
        double      step; ///< 0: none
        const char* ui;
    };

    /// Generated for each WXL_SETTINGS struct S: kSection, kInfos and Each(s, f), which calls
    /// f(info, field) for every setting in declaration order.
    template <class S>
    struct SettingsTraits;

    namespace detail
    {
        inline const WXL_SettingsApi* SettingsService()
        {
            static const Service<WXL_SettingsApi> service;
            return service.Get();
        }

        constexpr std::size_t kNumberText = 32;

        /// A field's value as the core's text.
        template <class T>
        std::string SettingText(const T& value)
        {
            if constexpr (std::is_same_v<T, bool>) return value ? "true" : "false";
            else if constexpr (std::is_integral_v<T>) return std::to_string(value);
            else if constexpr (std::is_floating_point_v<T>)
            {
                char buffer[kNumberText];
                std::snprintf(buffer, sizeof buffer, "%.9g", static_cast<double>(value));
                return buffer;
            }
            else return std::string(value);
        }

        /// The core's text of a value, back into the field; false when it does not read.
        template <class T>
        bool SettingValue(const std::string& s, T& out)
        {
            if constexpr (std::is_same_v<T, bool>)
            {
                out = s == "true";
                return s == "true" || s == "false";
            }
            else if constexpr (std::is_integral_v<T>)
            {
                errno               = 0;
                char*           end = nullptr;
                const long long v   = std::strtoll(s.c_str(), &end, 10);
                if (s.empty() || *end || errno) return false;
                out = static_cast<T>(v);
                return true;
            }
            else if constexpr (std::is_floating_point_v<T>)
            {
                char*        end = nullptr;
                const double v   = std::strtod(s.c_str(), &end);
                if (s.empty() || *end) return false;
                out = static_cast<T>(v);
                return true;
            }
            else
            {
                out = s;
                return true;
            }
        }

        constexpr std::size_t kSmallValue = 128;

        /// A value from the core: in a stack buffer, or a second call with one of its length.
        inline Result<std::string> SettingGet(const WXL_SettingsApi* api, const char* key)
        {
            char     small[kSmallValue];
            uint32_t length = 0;
            int status = api->Get(key, small, sizeof small, &length);
            if (status == WXL_OK) return std::string(small, length);
            if (status != WXL_E_INVALID_ARGUMENT || length < sizeof small) return static_cast<Error>(status);
            std::string big(length, '\0');
            status = api->Get(key, big.data(), length + 1, &length);
            if (status != WXL_OK) return static_cast<Error>(status);
            big.resize(length);
            return big;
        }
    }

    /// The settings of an extension, a struct S annotated WXL_SETTINGS (see docs/add-settings.md):
    /// declared to the core, read from the ini, kept current. Read through `->`, change with Set().
    /// Start() in OnStart; until it has run, or when it fails, the values are S's defaults.
    template <class S>
    class Settings
    {
    public:
        using Traits = SettingsTraits<S>;

        /// Declares every setting with S's initializers as defaults, reads the ini, follows the changes.
        /// A failure is logged, and the defaults apply.
        bool Start()
        {
            const auto started = TryStart();
            if (!started) Log::Warn("settings unavailable ({}): the defaults apply", started.Message());
            return started.HasValue();
        }

        /// Start, answering why it failed rather than logging it.
        Result<void> TryStart()
        {
            const WXL_SettingsApi* api = detail::SettingsService();
            if (!api) return Error::Unsupported;
            const S                     defaults{};
            std::vector<std::string>    texts;
            std::vector<WXL_SettingDecl> decls;
            texts.reserve(std::size(Traits::kInfos));
            Traits::Each(defaults, [&](const SettingInfo& info, const auto& field) {
                texts.push_back(detail::SettingText(field));
                WXL_SettingDecl d{};
                d.structSize = sizeof d;
                d.section    = Traits::kSection;
                d.key        = info.key;
                d.kind       = info.kind;
                d.value      = texts.back().c_str();
                d.doc        = info.doc;
                d.min        = info.min;
                d.max        = info.max;
                d.step       = info.step;
                d.ui         = info.ui;
                decls.push_back(d);
            });
            if (const int status = api->Declare(decls.data(), uint32_t(decls.size()))) return static_cast<Error>(status);
            Traits::Each(m_values, [&](const SettingInfo& info, auto& field) {
                if (auto text = detail::SettingGet(api, info.key)) detail::SettingValue(*text, field);
            });
            auto follow = SignalOf<SettingChanged>().Priority(priority::First - 1).Connect(
                [this](const events::SettingChangedArgs& e) {
                    if (!e.extension || std::strcmp(e.extension, ExtensionName()) != 0) return;
                    Traits::Each(m_values, [&](const SettingInfo& info, auto& field) {
                        if (std::strcmp(e.key, info.key) == 0) detail::SettingValue(std::string(e.value), field);
                    });
                });
            if (!follow) return follow.Code();
            m_follow = std::move(*follow);
            return {};
        }

        /// The current values.
        const S& Values() const { return m_values; }
        const S* operator->() const { return &m_values; }
        const S& operator*() const { return m_values; }

        /// Changes a setting, saved and raised: `settings.Set(&S::MinShown, 50)`. A refusal is logged.
        template <class T, class V>
        bool Set(T S::*member, const V& value)
        {
            const auto set = TrySet(member, value);
            if (!set) Log::Warn("setting not changed: {}", set.Message());
            return set.HasValue();
        }

        /// Set, answering why it was refused (InvalidArgument out of range) rather than logging it.
        template <class T, class V>
        Result<void> TrySet(T S::*member, const V& value)
        {
            const char* key = nullptr;
            Traits::Each(m_values, [&](const SettingInfo& info, auto& field) {
                if (static_cast<const void*>(&field) == static_cast<const void*>(&(m_values.*member))) key = info.key;
            });
            const WXL_SettingsApi* api = detail::SettingsService();
            if (!key) return Error::NotFound;
            if (!api) return Error::Unsupported;
            const std::string text = detail::SettingText(static_cast<T>(value));
            if (const int status = api->Set(key, text.c_str())) return static_cast<Error>(status);
            return {};
        }

        /// This extension's changes, raised once Values() holds the new value: On(settings.Changed(), ...).
        Signal<SettingChangedArgs> Changed() const
        {
            return SignalOf<SettingChanged>().Filter([](const events::SettingChangedArgs& e) {
                return e.extension && std::strcmp(e.extension, ExtensionName()) == 0;
            });
        }

    private:
        S          m_values{};
        Connection m_follow;
    };
}
