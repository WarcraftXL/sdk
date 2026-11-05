// SPDX-License-Identifier: GPL-3.0-or-later
// wxl-settings-check.exe: wxl::Settings over the greeter example's traits and a fake settings service.

#include "FakeCore.hpp"

#include "GreeterSettings.settings.hpp"

#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace
{
    int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++g_failures; } } while (0)

    std::vector<WXL_SettingDecl>       g_declared;
    std::vector<std::string>           g_keep;
    std::map<std::string, std::string> g_values;

    void Changed(const std::string& key, const std::string& value)
    {
        g_values[key] = value;
        const wxl::events::SettingChangedArgs args{ "settings-check", key.c_str(), value.c_str() };
        fake::Raise<wxl::events::Event::OnSettingChanged>(args);
    }

    int __cdecl Declare(const WXL_SettingDecl* decls, uint32_t count)
    {
        for (uint32_t i = 0; i < count; ++i)
        {
            g_declared.push_back(decls[i]);
            g_keep.push_back(decls[i].key);
            g_values.emplace(decls[i].key, decls[i].value);
        }
        g_values["Times"] = "3"; // as if the ini said so
        return WXL_OK;
    }
    int __cdecl Get(const char* key, char* buffer, uint32_t capacity, uint32_t* length)
    {
        auto it = g_values.find(key);
        if (it == g_values.end()) return WXL_E_NOT_FOUND;
        *length = uint32_t(it->second.size());
        if (capacity <= it->second.size()) return WXL_E_INVALID_ARGUMENT;
        std::snprintf(buffer, capacity, "%s", it->second.c_str());
        return WXL_OK;
    }
    int __cdecl Set(const char* key, const char* value)
    {
        if (std::string(key) == "Times" && std::atoi(value) > 10) return WXL_E_INVALID_ARGUMENT;
        Changed(key, value);
        return WXL_OK;
    }
    const WXL_SettingsApi g_service = { sizeof(WXL_SettingsApi), WXL_SETTINGS_API_VERSION, &Declare, &Get, &Set };
}

int main()
{
    // Static: statics of the SDK (the scheduler's connection) still reach it at exit.
    static const WXL_Api api = fake::MakeApi();
    CHECK(wxl::Bind(&api, "settings-check"));

    wxl::Settings<GreeterSettings> settings;
    CHECK(settings.TryStart().Code() == wxl::Error::Unsupported); // no service yet
    fake::Publish(WXL_SETTINGS_API_NAME, WXL_SETTINGS_API_VERSION, const_cast<WXL_SettingsApi*>(&g_service));

    CHECK(settings.Start());
    CHECK(g_declared.size() == 4);
    CHECK(std::string(g_declared[0].section) == "Greeter" && std::string(g_declared[1].key) == "Delay");
    CHECK(g_declared[1].kind == WXL_SETTING_KIND_NUMBER && g_declared[1].max == 30.0 && g_declared[1].step == 0.5);
    CHECK(std::string(g_declared[3].ui) == "checkbox");
    CHECK(settings->Times == 3 && settings->Delay == 2.0f && settings->Enabled);
    CHECK(settings->Text == "Welcome back, level %u.");

    std::vector<std::string> seen;
    auto changed = settings.Changed().Connect([&](const wxl::events::SettingChangedArgs& e) {
        seen.push_back(std::string(e.key) + "=" + e.value);
    });
    CHECK(settings.Set(&GreeterSettings::Delay, 4.5f));
    CHECK(settings->Delay == 4.5f && seen.size() == 1 && seen[0] == "Delay=4.5");
    CHECK(settings.TrySet(&GreeterSettings::Times, 50).Code() == wxl::Error::InvalidArgument && settings->Times == 3);

    // Changed from outside (/wxl set, the panel, a profile).
    Changed("Enabled", "false");
    CHECK(!settings->Enabled);
    // Another extension's change is not ours.
    const wxl::events::SettingChangedArgs other{ "someone-else", "Enabled", "true" };
    fake::Raise<wxl::events::Event::OnSettingChanged>(other);
    CHECK(!settings->Enabled && seen.size() == 2);

    std::printf("%d failure(s)\n", g_failures);
    return g_failures ? 1 : 0;
}
