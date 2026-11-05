// SPDX-License-Identifier: GPL-3.0-or-later
// wxl-fake-host: loads one extension as the core would, without a client, and reports what it asked for.

#include "FakeCore.hpp"

#include "wxl/Events.hpp"
#include "wxl/abi/Plugin.h"
#include "wxl/abi/Settings.h"
#include "wxl/abi/Storage.h"
#include "wxl/detail/Manifest.hpp"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++g_failures; } } while (0)

    // --- wxl.settings: the declared settings, at their defaults until set ---------------------------
    struct Setting
    {
        WXL_SettingDecl decl;
        std::string     section, key, value, doc, ui;
    };
    std::vector<Setting>* g_settings = new std::vector<Setting>; // left to the process exit

    Setting* FindSetting(const char* key)
    {
        for (Setting& s : *g_settings)
            if (key && s.key == key) return &s;
        return nullptr;
    }

    int __cdecl SettingsDeclare(const WXL_SettingDecl* decls, uint32_t count)
    {
        if (!decls && count) return WXL_E_INVALID_ARGUMENT;
        g_settings->clear();
        for (uint32_t i = 0; i < count; ++i)
        {
            const WXL_SettingDecl& d = decls[i];
            if (!d.key || !d.value || d.kind > WXL_SETTING_KIND_TEXT)
            {
                fake::g_refused.push_back("wxl.settings: a declaration without a key, a default or a known kind");
                return WXL_E_INVALID_ARGUMENT;
            }
            g_settings->push_back(Setting{ d, d.section ? d.section : "", d.key, d.value, d.doc ? d.doc : "",
                                           d.ui ? d.ui : "" });
        }
        std::printf("settings: %u declared\n", count);
        return WXL_OK;
    }

    int __cdecl SettingsGet(const char* key, char* buffer, uint32_t capacity, uint32_t* length)
    {
        const Setting* s = FindSetting(key);
        if (!s) return WXL_E_NOT_FOUND;
        if (!length) return WXL_E_INVALID_ARGUMENT;
        *length = uint32_t(s->value.size());
        if (!buffer || capacity <= s->value.size()) return WXL_E_INVALID_ARGUMENT;
        std::memcpy(buffer, s->value.c_str(), s->value.size() + 1);
        return WXL_OK;
    }

    int __cdecl SettingsSet(const char* key, const char* value)
    {
        Setting* s = FindSetting(key);
        if (!s) return WXL_E_NOT_FOUND;
        if (!value) return WXL_E_INVALID_ARGUMENT;
        s->value = value;
        const wxl::events::SettingChangedArgs args{ "extension", s->key.c_str(), s->value.c_str() };
        fake::Raise<wxl::events::Event::OnSettingChanged>(args);
        return WXL_OK;
    }

    int __cdecl SettingsVisit(WXL_SettingVisitFn fn, void* user)
    {
        if (!fn) return WXL_E_INVALID_ARGUMENT;
        for (const Setting& s : *g_settings)
        {
            WXL_SettingDecl d = s.decl;
            d.section = s.section.c_str();
            d.key     = s.key.c_str();
            d.value   = s.value.c_str();
            d.doc     = s.doc.c_str();
            d.ui      = s.ui.c_str();
            fn(user, "extension", &d);
        }
        return WXL_OK;
    }

    int __cdecl SettingsSetOf(const char*, const char* key, const char* value) { return SettingsSet(key, value); }

    const WXL_SettingsApi g_settingsApi = { sizeof(WXL_SettingsApi), WXL_SETTINGS_API_VERSION, &SettingsDeclare,
                                            &SettingsGet, &SettingsSet, &SettingsVisit, &SettingsSetOf };

    // --- wxl.storage: registrations, reported --------------------------------------------------------
    void __cdecl StorageProvider(WXL_ProvideFn fn)
    {
        CHECK(fn != nullptr);
        std::printf("storage: a provider\n");
    }

    void __cdecl StorageTransform(const char* suffix, WXL_TransformFn fn)
    {
        CHECK(suffix && *suffix && fn);
        std::printf("storage: a transform for '%s'\n", suffix ? suffix : "?");
    }

    void __cdecl StorageRedirect(WXL_RedirectFn fn)
    {
        CHECK(fn != nullptr);
        std::printf("storage: a redirect\n");
    }

    const WXL_StorageApi g_storageApi = { sizeof(WXL_StorageApi), WXL_STORAGE_API_VERSION, &StorageProvider,
                                          &StorageTransform, &StorageRedirect };

    // --- the manifest --------------------------------------------------------------------------------
    std::string StringMember(const std::string& json, const char* name)
    {
        const std::regex re(std::string("\"") + name + "\"\\s*:\\s*\"([^\"]*)\"");
        std::smatch m;
        return std::regex_search(json, m, re) ? m[1].str() : std::string();
    }

    bool TrueMember(const std::string& json, const char* name)
    {
        return std::regex_search(json, std::regex(std::string("\"") + name + "\"\\s*:\\s*true"));
    }

    void KnownHookPoints()
    {
#define WXL_HOOK_POINT_NAME(name) fake::g_points.insert(name);
#include "wxl/HookPoints.def"
#undef WXL_HOOK_POINT_NAME
    }

    void List(const char* what, const std::vector<std::string>& items)
    {
        for (const std::string& item : items) std::printf("%s: %s\n", what, item.c_str());
    }
}

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 3)
    {
        std::printf("usage: wxl-fake-host <extension.dll> <wxl.json> [--world <mapId>] [--expect-log \"<line>\"]...\n");
        return 2;
    }
    int                      world = -1;
    std::vector<std::string> expected;
    for (int i = 3; i < argc; ++i)
    {
        if (!std::strcmp(argv[i], "--world") && i + 1 < argc) world = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--expect-log") && i + 1 < argc) expected.push_back(argv[++i]);
        else
        {
            std::printf("unknown argument: %s\n", argv[i]);
            return 2;
        }
    }

    // The manifest, as the core reads it before loading the DLL.
    std::ifstream     file(argv[2], std::ios::binary);
    std::stringstream text;
    text << file.rdbuf();
    const std::string manifest = text.str();
    CHECK(!manifest.empty());
    char abi[32];
    std::snprintf(abi, sizeof abi, "%d.%d", WXL_SDK_VERSION_MAJOR, WXL_SDK_VERSION_MINOR);
    CHECK(StringMember(manifest, "abi") == abi);
    const std::string id      = StringMember(manifest, "id");
    const std::string version = StringMember(manifest, "version");
    const std::string load = StringMember(manifest, "load");
    CHECK(load.empty() || load == "engine" || load == "before-archives");
    fake::g_lowLevel = TrueMember(manifest, "low_level");
    KnownHookPoints();

    HMODULE module = LoadLibraryA(argv[1]);
    CHECK(module != nullptr);
    if (!module) return 1;

    const auto query = reinterpret_cast<WXL_QueryFn>(GetProcAddress(module, "WXL_Query"));
    const auto entry = reinterpret_cast<WXL_LoadFn>(GetProcAddress(module, "WXL_Load"));
    CHECK(query && entry);
    if (!query || !entry) return 1;

    const WXL_PluginInfo* info = query();
    CHECK(info != nullptr);
    if (!info) return 1;
    CHECK(info->structSize == sizeof(WXL_PluginInfo));
    CHECK(info->apiVersion == WXL_API_VERSION);
    CHECK(info->clientBuild == WXL_CLIENT_BUILD);
    CHECK(info->sdkVersion == WXL_SDK_VERSION);
    CHECK(info->name && *info->name);
    if (!id.empty() && info->name) CHECK(id == info->name);
    if (!version.empty()) CHECK(info->pluginVersion == wxl::detail::PackVersion(version.c_str()));
    std::printf("extension: %s %s, load %s, low_level %s\n", info->name ? info->name : "?",
                version.empty() ? "(no version)" : version.c_str(), load.empty() ? "engine" : load.c_str(),
                fake::g_lowLevel ? "yes" : "no");

    fake::Publish(WXL_SETTINGS_API_NAME, WXL_SETTINGS_API_VERSION, const_cast<WXL_SettingsApi*>(&g_settingsApi));
    fake::Publish("wxl.storage", WXL_STORAGE_API_VERSION, const_cast<WXL_StorageApi*>(&g_storageApi));
    const size_t ours = fake::g_services.size();

    const WXL_Api api    = fake::MakeApi();
    const int     loaded = entry(&api);
    CHECK(loaded != 0);
    std::printf("WXL_Load: %d\n", loaded);

    fake::Raise<wxl::events::Event::OnExtensionsLoaded>({ 1 });
    for (uint32_t i = 0; i < 3; ++i) fake::Raise<wxl::events::Event::OnUpdate>({ 0.016f, 100 + 16 * i });
    if (world >= 0) fake::Raise<wxl::events::Event::OnWorldEnter>({ uint32_t(world) });

    for (const fake::Entry* e : fake::g_entries)
        if (e && !e->ended && e->event < uint32_t(wxl::events::Event::Count))
            std::printf("connects: %s\n", wxl::events::Name(wxl::events::Event(e->event)));
    List("hook", fake::g_hooks);
    List("patch", fake::g_patches);
    for (size_t i = ours; i < fake::g_services.size(); ++i)
        std::printf("publishes: %s v%u\n", fake::g_services[i].name.c_str(), fake::g_services[i].version);
    List("asks", fake::g_lookups);

    // Unloading stops the extension: OnStop runs, and every connection it made ends.
    const size_t logged = fake::g_log.size();
    FreeLibrary(module);
    List("log", fake::g_log);
    if (fake::g_log.size() > logged) std::printf("stop: %zu line(s) logged\n", fake::g_log.size() - logged);
    for (const fake::Entry* e : fake::g_entries)
        if (e && !e->ended && e->event < uint32_t(wxl::events::Event::Count))
        {
            std::printf("FAIL still connected once unloaded: %s\n", wxl::events::Name(wxl::events::Event(e->event)));
            ++g_failures;
        }
    for (const std::string& why : fake::g_refused)
    {
        std::printf("FAIL refused: %s\n", why.c_str());
        ++g_failures;
    }
    for (const std::string& line : expected)
        if (!fake::Logged(line))
        {
            std::printf("FAIL not logged: %s\n", line.c_str());
            ++g_failures;
        }

    std::printf("%d failure(s)\n", g_failures);
    // Without this process's static destructors: the fake core outlives anything the extension left.
    ExitProcess(g_failures == 0 ? 0 : 1);
}
