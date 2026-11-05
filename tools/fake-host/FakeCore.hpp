// SPDX-License-Identifier: GPL-3.0-or-later
// A fake core for the checks and the fake host: the WXL_Api table, its bus, services, hooks and patches.

#pragma once

#include "wxl/Events.hpp"
#include "wxl/abi/Plugin.h"

#include <algorithm>
#include <set>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace fake
{
    struct Entry
    {
        uint32_t      event;
        WXL_Handle    subject;
        int32_t       priority;
        WXL_EventFn   fn;
        void*         user;
        WXL_ReleaseFn release;
        uint32_t      generation;
        bool          ended;
    };

    struct Service
    {
        std::string name;
        uint32_t    version;
        void*       iface;
    };

    inline std::vector<Entry*>      g_entries; // index = connection index
    inline std::vector<std::string> g_log;
    inline std::vector<Service>     g_services;
    inline std::vector<std::string> g_hooks;    // "point" or "label @0x..." per raw detour
    inline std::vector<std::string> g_patches;  // "label @0x... (n bytes)"
    inline std::vector<std::string> g_refused;  // what the core would have refused, and why
    inline std::vector<std::string> g_lookups;  // "name vN" per GetInterface
    inline bool                     g_lowLevel = true;   // permissions.low_level of the extension
    inline std::set<std::string>    g_points;            // the known hook points; empty: any
    inline int                      g_walking  = 0;
    inline uint32_t                 g_releases = 0;

    inline void __cdecl Log(int, const char* tag, const char* fmt, ...)
    {
        char line[2048];
        va_list args;
        va_start(args, fmt);
        std::vsnprintf(line, sizeof line, fmt, args);
        va_end(args);
        g_log.push_back(std::string(tag ? tag : "?") + ": " + line);
    }

    inline bool Logged(const std::string& line)
    {
        return std::find(g_log.begin(), g_log.end(), line) != g_log.end();
    }

    inline void Settle()
    {
        if (g_walking) return;
        for (Entry* e : g_entries)
            if (e && e->ended && e->release)
            {
                WXL_ReleaseFn release = std::exchange(e->release, nullptr);
                ++g_releases;
                release(e->user);
            }
    }

    inline int __cdecl Connect(uint32_t event, WXL_Handle subject, int32_t priority, WXL_EventFn fn, void* user,
                               WXL_ReleaseFn release, WXL_Connection* out)
    {
        static uint32_t generation = 1;
        g_entries.push_back(new Entry{ event, subject, priority, fn, user, release, generation, false });
        *out = WXL_Connection{ uint32_t(g_entries.size() - 1), generation };
        generation += 2;
        return WXL_OK;
    }

    inline Entry* Find(WXL_Connection c)
    {
        if (c.index >= g_entries.size()) return nullptr;
        Entry* e = g_entries[c.index];
        return e && e->generation == c.generation && !e->ended ? e : nullptr;
    }

    inline int __cdecl Disconnect(WXL_Connection c)
    {
        Entry* e = Find(c);
        if (!e) return WXL_E_EXPIRED;
        e->ended = true;
        Settle();
        return WXL_OK;
    }

    inline int __cdecl Connected(WXL_Connection c) { return Find(c) ? WXL_OK : WXL_E_EXPIRED; }

    /// Raises an event about `subject` (empty: to the connections without one).
    inline void EmitFor(uint32_t event, WXL_Handle subject, const void* args)
    {
        std::vector<Entry*> order;
        for (Entry* e : g_entries)
            if (e && e->event == event) order.push_back(e);
        std::stable_sort(order.begin(), order.end(), [](const Entry* a, const Entry* b) { return a->priority < b->priority; });
        ++g_walking;
        for (Entry* e : order)
        {
            if (e->ended) continue;
            if (e->subject.generation && (e->subject.index != subject.index || e->subject.generation != subject.generation))
                continue;
            e->fn(e->user, args);
        }
        --g_walking;
        Settle();
    }

    inline void __cdecl Emit(uint32_t event, const void* args) { EmitFor(event, WXL_Handle{}, args); }

    template <wxl::events::Event E>
    void Raise(const wxl::events::Args<E>& args)
    {
        Emit(uint32_t(E), &args);
    }

    /// Ends the connections about an object, as the core does when it expires.
    inline void Expire(WXL_Handle subject)
    {
        for (Entry* e : g_entries)
            if (e && !e->ended && e->subject.index == subject.index && e->subject.generation == subject.generation)
                e->ended = true;
        Settle();
    }

    inline uint32_t Live(uint32_t event)
    {
        uint32_t n = 0;
        for (Entry* e : g_entries)
            if (e && !e->ended && e->event == event) ++n;
        return n;
    }

    /// What a recorded detour's next link points at: there is no client function behind it.
    inline void Unreachable()
    {
        std::printf("FAIL a detour called the next link of its chain: there is no client in the fake host\n");
        std::fflush(stdout);
        std::_Exit(3);
    }

    inline void Link(void** original) { *original = reinterpret_cast<void*>(&Unreachable); }

    inline bool MayHook(const std::string& what)
    {
        if (g_lowLevel) return true;
        g_refused.push_back(what + ": needs \"permissions\": { \"low_level\": true } in wxl.json");
        return false;
    }

    inline int __cdecl HookAttach(const char* name, uintptr_t target, void* detour, void** original, int)
    {
        char what[160];
        std::snprintf(what, sizeof what, "%s @0x%08X", name ? name : "?", unsigned(target));
        if (!target || !detour || !original || !MayHook(what)) return 0;
        g_hooks.push_back(what);
        Link(original);
        return 1;
    }

    inline int __cdecl HookAttachEx(const char* name, uintptr_t target, void* detour, void** original, int priority,
                                    uint32_t flags)
    {
        if (!(flags & WXL_HOOK_RESIDENT)) return HookAttach(name, target, detour, original, priority);
        char what[160];
        std::snprintf(what, sizeof what, "%s @0x%08X (resident)", name ? name : "?", unsigned(target));
        if (!target || !detour || !original || !MayHook(what)) return 0;
        g_hooks.push_back(what);
        Link(original);
        return 1;
    }

    inline int __cdecl HookAttachByName(const char* point, void* detour, void** original, int)
    {
        const std::string name = point ? point : "";
        if (!detour || !original || !MayHook(name)) return 0;
        if (!g_points.empty() && !g_points.count(name))
        {
            g_refused.push_back(name + ": no such hook point (wxl/HookPoints.def)");
            return 0;
        }
        g_hooks.push_back(name);
        Link(original);
        return 1;
    }

    inline int __cdecl InterceptAttach(const char* name, uintptr_t target, void* detour, void** original)
    {
        if (!target || !detour || !original) return 0;
        char what[160];
        std::snprintf(what, sizeof what, "%s @0x%08X (interceptor)", name ? name : "?", unsigned(target));
        g_hooks.push_back(what);
        Link(original);
        return 1;
    }

    inline int __cdecl Patch(const char* name, uintptr_t address, const void*, const void* bytes, uint32_t length)
    {
        char what[160];
        std::snprintf(what, sizeof what, "%s @0x%08X (%u bytes)", name ? name : "?", unsigned(address), length);
        if (!address || !bytes || !length) return WXL_E_INVALID_ARGUMENT;
        if (!MayHook(what)) return WXL_E_DENIED;
        g_patches.push_back(what);
        return WXL_OK;
    }

    inline uint32_t __cdecl Self() { return 1; }

    inline int __cdecl Run(uint32_t, void(__cdecl* fn)(void*), void* context)
    {
        if (!fn) return WXL_E_INVALID_ARGUMENT;
        fn(context);
        return WXL_OK;
    }

    inline void __cdecl Publish(const char* name, uint32_t version, void* iface)
    {
        g_services.push_back({ name, version, iface });
    }

    inline void* __cdecl GetInterface(const char* name, uint32_t version)
    {
        g_lookups.push_back(std::string(name ? name : "?") + " v" + std::to_string(version));
        if (!name) return nullptr;
        const Service* best = nullptr;
        for (const Service& s : g_services)
            if (s.name == name && s.version >= version && (!best || s.version > best->version)) best = &s;
        return best ? best->iface : nullptr;
    }

    inline WXL_Api MakeApi()
    {
        WXL_Api api{};
        api.structSize       = sizeof api;
        api.apiVersion       = WXL_API_VERSION;
        api.Log              = &Log;
        api.Connect          = &Connect;
        api.Disconnect       = &Disconnect;
        api.Connected        = &Connected;
        api.Emit             = &Emit;
        api.HookAttach       = &HookAttach;
        api.HookAttachByName = &HookAttachByName;
        api.InterceptAttach  = &InterceptAttach;
        api.Self             = &Self;
        api.Run              = &Run;
        api.PublishInterface = &Publish;
        api.GetInterface     = &GetInterface;
        api.Patch            = &Patch;
        api.HookAttachEx     = &HookAttachEx;
        return api;
    }
}
