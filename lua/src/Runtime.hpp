// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The Lua mods together: found and ordered by the host's rules, each in its own state, fed from
// one native subscription per event, reloaded when the files their code comes from change.

#ifndef WXL_LUA_RUNTIME_HPP
#define WXL_LUA_RUNTIME_HPP

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <vector>

#include "Args.hpp"
#include "ScriptMod.hpp"
#include "wxl/host/discovery.hpp"
#include "wxl/reflect.hpp"
#include "wxl/sdk.hpp"

namespace wxl::lua
{
    /**
     * @brief The runtime of the Lua mods.
     *
     * One per process, built in wxl_mod_load on the main thread. Every handler of every Lua
     * mod runs on that thread: an event raised elsewhere is copied, queued, and delivered at
     * the next Update.
     */
    class Runtime
    {
    public:
        static constexpr const char* kTag = "wxl.lua";

        /// How often the mod folders are scanned for changes.
        static constexpr std::chrono::milliseconds kPollInterval{1000};

        explicit Runtime(const wxl_host* host);
        ~Runtime();

        Runtime(const Runtime&)            = delete;
        Runtime& operator=(const Runtime&) = delete;

        /// The live runtime, for the service thunks.
        static Runtime* Instance() { return instance_; }

        /// Finds the script mods and loads them. Called once, from wxl_mod_load.
        void Start();

        /// WXL_STAGE_READY runs every mod's onInit; WXL_STAGE_SHUTDOWN every onShutdown.
        void Stage(wxl_stage stage);

        /// Closes every mod. Called from wxl_mod_unload.
        void Stop();

        const wxl_host* Host() const { return host_; }

        /// The contract a Lua mod is served: the runtime's, no further than what this binary
        /// was built against, since its event table ends there.
        host::AbiVersion Abi() const;

        // --- what a mod's Observe and Override do -------------------------------------------

        /// Keeps a handler for an event and returns its id. @p self is the table a family
        /// method is called on, as a registry ref, or LUA_NOREF for a plain function. Handlers
        /// run by the mods' load order, then in the order each mod subscribed them; a mod that
        /// reloads keeps its place.
        uint32_t Observe(ScriptMod& mod, const reflect::Event& event, int ref, bool override,
                         int self = LUA_NOREF);

        /// Drops a handler by id. False when the mod has none with that id.
        bool Unobserve(ScriptMod& mod, uint32_t id);

        /// Drops a mod's handlers and stops its code, logging why.
        void Disable(ScriptMod& mod, std::string_view why);

        /// Drops every handler of a mod: before its state closes.
        void DropHandlers(ScriptMod& mod);

        // --- the service ------------------------------------------------------------------

        uint32_t   Count() const { return static_cast<uint32_t>(mods_.size()); }
        /// Every script mod, running or not, in load order.
        const std::vector<std::unique_ptr<ScriptMod>>& Mods() const { return mods_; }
        uint32_t   Poll();
        wxl_result Reload(const char* id);

        void Log(wxl_log_level level, std::string_view message) const;

        /// The opcode numbers the manifests declared, as the host found them: a script mod's are
        /// its alone among the Lua mods.
        const host::OpcodeDeclarations& Opcodes() const { return opcodes_; }

    private:
        host::OpcodeDeclarations opcodes_;
        struct Handler
        {
            ScriptMod* mod;
            int        ref;      ///< the function, in the mod's registry
            int        self;     ///< the table it is a method of, or LUA_NOREF
            bool       override; ///< its answer is taken
            uint32_t   id;
            bool       live;
            uintptr_t  zone;     ///< the location of its profiler zone, "<mod>: On<Event>"
        };

        /// One event: the native subscription and the Lua handlers it feeds, in order.
        struct Slot
        {
            Runtime*                           runtime = nullptr;
            const reflect::Event*              event   = nullptr;
            std::vector<const reflect::Field*> writable; ///< what an Override answers through, in order
            wxl::Subscription                  native;
            std::vector<Handler>               handlers; ///< by the mod's rank, then subscription order
            std::vector<Handler>               arriving; ///< subscribed during a dispatch, placed after it
            int                                depth = 0;     ///< dispatches walking `handlers` right now
            bool                               dirty = false; ///< a handler died or arrived during a dispatch
        };

        /// An event raised off the main thread, waiting for the next Update.
        struct Queued
        {
            Slot*    slot;
            ArgsCopy copy;
        };

        static Runtime* instance_;

        static void WXL_CALL OnNativeEvent(void* user, const void* args);

        /// ObjectDestroy, after every handler: each state forgets the object (ForgetObject).
        static void WXL_CALL OnObjectGone(void* user, const void* args);

        void OnEvent(Slot& slot, const void* args);
        void Dispatch(Slot& slot, const void* args, bool answerable);
        /// Writes an Override's return values, at @p first on the stack, through the slot's
        /// writable members: the first value to the first member, and so on.
        void TakeAnswers(lua_State* L, ScriptMod& mod, const Slot& slot, const void* args, int first,
                         int count);
        /// Drops the dead handlers and places the arriving ones. Never during a dispatch.
        void Compact(Slot& slot);

        /// Puts a handler after every handler of its mod and of the mods ranked before it.
        static void Place(Slot& slot, const Handler& handler);

        /// Subscribes the native handler for a slot when it has none. @p requester names the
        /// mod in the warning when the runtime does not raise the event; null for nobody.
        void EnsureNative(Slot& slot, const ScriptMod* requester);

        /// Whether any dispatch is on the stack: states may not be closed then.
        bool Dispatching() const;

        /// Logs a line once per mod and event: for an answer that cannot be taken, a return
        /// value of the wrong kind, an event the runtime does not raise.
        void WarnOnce(const ScriptMod& mod, const reflect::Event& event, int what,
                      const std::string& message);

        void Housekeeping();
        void DrainQueue();

        /// Reloads a mod: onShutdown, handlers dropped, state closed, entry run, onInit.
        void ReloadMod(ScriptMod& mod);
        uint32_t CheckReloads();
        void     SettleReloads();

        /// Checks a candidate the way the host checks a binary mod, and keeps it when it passes.
        void Admit(const host::ModCandidate& candidate);

        /// Why a script mod's manifest cannot be served, or empty: what Admit checks, and what a
        /// reload checks again when the manifest changed.
        std::string Refusal(const host::Manifest& manifest) const;

        /// Adds the folders holding an init.lua and no manifest, as mods named after them.
        void AddBareFolders(const std::filesystem::path& directory,
                            std::vector<host::ModCandidate>& candidates);

        /// Where the mods are: the runtime says, or the folder above this mod's own.
        std::filesystem::path ModsDirectory() const;

        const wxl_host* host_;
        std::thread::id mainThread_;
        Slot            slots_[WXL_EV_COUNT];

        /// ObjectDestroy at the lowest priority, so every mod's handlers see the object first.
        wxl::Subscription objectGone_;

        std::vector<std::unique_ptr<ScriptMod>> mods_;
        std::vector<ScriptMod*>                 pendingReloads_;

        std::mutex          queueLock_;
        std::vector<Queued> queue_;
        std::atomic<bool>   queued_{false};

        std::set<std::tuple<std::string, uint32_t, int>> warned_;

        uint32_t                              nextId_ = 0;
        bool                                  ready_  = false;
        std::chrono::steady_clock::time_point lastPoll_;
    };
} // namespace wxl::lua

#endif // WXL_LUA_RUNTIME_HPP
