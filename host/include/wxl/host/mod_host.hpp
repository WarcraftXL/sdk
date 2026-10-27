// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The runtime's handle on its mods: discovery, admission, loading, stages, events, services and
// the wxl_host table handed to every mod. Everything two runtimes would otherwise write twice.

#ifndef WXL_HOST_MOD_HOST_HPP
#define WXL_HOST_MOD_HOST_HPP

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "wxl/host.h"
#include "wxl/host/admission.hpp"
#include "wxl/host/discovery.hpp"
#include "wxl/host/event_bus.hpp"
#include "wxl/host/guard.hpp"
#include "wxl/host/loader.hpp"
#include "wxl/host/log.hpp"
#include "wxl/host/manifest.hpp"
#include "wxl/host/registry.hpp"
#include "wxl/mod.h"

namespace wxl::host
{
    /// What a runtime says about itself and what it offers. Read once, at construction.
    struct HostConfig
    {
        std::filesystem::path mods_directory;

        std::string runtime_id;          ///< "core", "client", "engine"
        std::string runtime_name;        ///< for logs
        uint32_t    runtime_version = 0; ///< opaque to a mod
        std::string target = CurrentTarget(); ///< which "binaries" entry to load

        LogSink*      log_sink    = nullptr; ///< not owned; null drops every line
        wxl_log_level log_minimum = WXL_LOG_TRACE;

        /// The events this runtime raises. A subscribe to any other gets WXL_SUBSCRIPTION_NONE,
        /// and so does one from a mod whose binary was built against headers older than the
        /// event (wxl_mod_info's abi against events.def's WXL_EVENTS_SINCE rows).
        std::vector<wxl_event> events;

        /// Published before any mod loads, so a manifest's `requires` can be answered.
        std::vector<PublishedService> services;

        /// Null means the OS loader. A test injects its own.
        LibraryLoader* loader = nullptr;
    };

    /// A mod whose wxl_mod_load returned WXL_OK.
    struct LoadedMod
    {
        std::string           id;
        std::string           name;
        std::string           directory_name;
        std::filesystem::path directory;
        std::filesystem::path binary;
        Manifest              manifest;
        AbiVersion            built_abi;          ///< what wxl_mod_info reports: the headers it was built against
        bool                  has_stage  = false; ///< exports wxl_mod_stage
        bool                  has_unload = false; ///< exports wxl_mod_unload
        bool                  disabled   = false; ///< faulted or disabled itself; no more of its code is called
        std::string           fault;              ///< what it was doing when it faulted, or the reason it gave
    };

    /// A folder that did not become a loaded mod, and why.
    struct RefusedMod
    {
        std::string           id; ///< empty when the manifest could not be read
        std::string           directory_name;
        std::filesystem::path directory;
        Verdict               verdict = Verdict::Refuse; ///< Skip or Refuse
        std::string           reason;
    };

    /**
     * @brief The mod host.
     *
     * One per process: the wxl_host table is plain C function pointers with nowhere to carry an
     * instance, so the live host is the one the table routes to. Construct it with the runtime's
     * configuration, call @ref LoadAll once the mods directory is known, @ref Stage as the boot
     * advances, @ref Raise at the runtime's own seams, and @ref Shutdown on the way out.
     *
     * Every call into a mod -- its four entry points and every event handler -- runs under the
     * crash guard. A mod that faults is logged with what it was doing and disabled: its
     * subscriptions are dropped, its later stages and its unload are skipped, its binary stays
     * mapped, and the host goes on. The services it published stay published, since another mod
     * may hold the pointer. A mod that fails in a way the guard cannot see, such as a C++
     * exception reaching its entry point, disables itself the same way through the table's
     * `disable`.
     *
     * Threads. The host is built, loads, reports stages and unloads on one thread, the main
     * one: the thread that constructs it. Events may be raised and handlers may subscribe from
     * any thread; the current mod is tracked per thread. A mod that faults on another thread has
     * its subscriptions dropped at once, and the rest of its disabling -- the log, the flag,
     * the disable handler -- waits for the main thread's next @ref Raise or @ref Stage.
     */
    class ModHost
    {
    public:
        /// A mod as the host refers to it from inside a call: its position in @ref Loaded plus
        /// one, so that 0 means nobody.
        using ModToken = uint32_t;

        /// Told when a mod is disabled, after its subscriptions are dropped.
        using DisableFn = void (*)(void* user, ModToken token, const char* id);

        explicit ModHost(HostConfig config);
        ~ModHost();

        ModHost(const ModHost&)            = delete;
        ModHost& operator=(const ModHost&) = delete;

        /**
         * @brief Finds, admits and loads every mod in the mods directory, in load order.
         *
         * Each outcome is logged and kept for @ref Loaded and @ref Refused. A second call does
         * nothing: mods load once.
         */
        void LoadAll();

        /**
         * @brief Reports a boot stage to every loaded mod that exports wxl_mod_stage.
         *
         * Load order, except WXL_STAGE_SHUTDOWN, which runs in reverse. A disabled mod is skipped.
         */
        void Stage(wxl_stage stage);

        /// Calls wxl_mod_unload in reverse load order and frees the binaries. A disabled mod's
        /// unload is not called and its binary is left mapped.
        void UnloadAll();

        /// WXL_STAGE_SHUTDOWN, then @ref UnloadAll.
        void Shutdown();

        /// Dispatches an event. What the runtime calls at its own seams, from any thread.
        void Raise(wxl_event event, const void* args);

        /**
         * @brief Whether a @ref Raise of @p event would reach any handler.
         *
         * One atomic load and no lock, from any thread: a seam asks it before reading the
         * client for the event's args, so an event nobody listens to costs only the question.
         * See EventBus::Listening.
         */
        bool Listening(wxl_event event) const { return events_.Listening(event); }

        /// Has @p handler told when an event gains its first handler or loses its last; see
        /// EventBus::SetListenHandler.
        void SetListenHandler(EventBus::ListenFn handler, void* user)
        {
            events_.SetListenHandler(handler, user);
        }

        /// The same, to the handlers of the mods @p accept takes (by ModToken): an event that
        /// concerns some mods only.
        void RaiseTo(wxl_event event, const void* args, EventBus::AcceptFn accept, void* user);

        /// One of the last events raised while someone listened, as @ref Recent reports it.
        struct RecentEvent
        {
            wxl_event event;
            bool      main_thread; ///< raised on the thread that built the host
            uint32_t  ms_ago;      ///< how long before the call to Recent
        };

        /// How many raises @ref Recent remembers.
        static constexpr size_t kRecentEvents = 32;

        /**
         * @brief The last events raised to at least one handler, oldest first; returns how many
         *        were written.
         *
         * What a crash report says the mods were doing. Lock-free and allocation-free, safe from
         * an exception filter on any thread; a raise racing the read may be missed or shown
         * twice. An event nobody listens to is not remembered: the record costs only a raise
         * that dispatches.
         */
        size_t Recent(RecentEvent* out, size_t capacity) const;

        /**
         * @brief Disables a mod after it faulted: logs what it was doing, drops its
         *        subscriptions, skips its remaining entry points, tells the disable handler.
         *
         * Off the main thread, only the subscriptions go at once; the rest follows on the main
         * thread, see the class notes.
         * @param what  what the mod was doing, for the log: "in wxl_mod_stage(early)".
         */
        void Disable(ModToken token, std::string_view what);

        /**
         * @brief Disables a mod at its own request (wxl_host.disable), as @ref Disable does
         *        after a fault.
         * @param reason  what the mod says went wrong: "OnReady threw an exception: ...".
         */
        void DisableOnRequest(ModToken token, std::string_view reason);

        /**
         * @brief Gives the host a profiler: every dispatch and every handler becomes a zone, the
         *        handler's named after its mod, while @ref SetProfiling is on.
         *
         * Before @ref LoadAll; copied. A runtime without a profiler never calls it.
         */
        void SetProfiler(const Profiler& profiler);

        /// Whether a viewer is watching. The runtime says so as it learns it, once a frame.
        void SetProfiling(bool on) { events_.SetProfiling(on); }

        /// Called after a mod is disabled. A runtime uses it to withdraw what the mod registered
        /// with its services.
        void SetDisableHandler(DisableFn handler, void* user)
        {
            disableFn_   = handler;
            disableUser_ = user;
        }

        /**
         * @brief The mod whose code is running on the calling thread, or 0 for nobody.
         *
         * Set around every entry point and every handler the host calls. A runtime that calls
         * into a mod by another route (a script function, say) brackets the call with
         * @ref SwapCurrentMod so that what the mod does inside is attributed to it.
         */
        ModToken CurrentMod() const;

        /// Sets the calling thread's current mod and returns the previous one, to put back
        /// after the call.
        ModToken SwapCurrentMod(ModToken token);

        /// Whether the caller runs on the thread that built the host.
        bool OnMainThread() const { return std::this_thread::get_id() == mainThread_; }

        /// The manifest of a loaded mod, or of the one inside its wxl_mod_query or
        /// wxl_mod_load; null for nobody. Main thread for the loading one.
        const Manifest* ManifestOf(ModToken token) const;
        /// The loaded mod a token names, or null for 0 or a token past the end.
        const LoadedMod* ModOf(ModToken token) const;

        /// The table every mod receives. Valid for the host's lifetime.
        const wxl_host* Table() const { return &table_; }

        /// The opcode numbers the manifests of the mods found declared, each with its mod and
        /// the loaded mod that claims it (wxl.lua for a script mod's). Filled by @ref LoadAll.
        const OpcodeDeclarations&      Opcodes() const { return opcodes_; }
        const std::vector<LoadedMod>&  Loaded() const { return loaded_; }
        const std::vector<RefusedMod>& Refused() const { return refused_; }

        const HostConfig&      Config() const { return config_; }
        const Logger&          Log() const { return logger_; }
        ServiceRegistry&       Services() { return services_; }
        const ServiceRegistry& Services() const { return services_; }
        EventBus&              Events() { return events_; }
        const EventBus&        Events() const { return events_; }

    private:
        static ModHost* current_;

        static void WXL_CALL                    ThunkLog(wxl_log_level level, const char* tag,
                                                         wxl_str message);
        static const wxl_runtime_info* WXL_CALL ThunkRuntime(void);
        static wxl_bool WXL_CALL                ThunkHasService(const char* name, uint32_t version);
        static const void* WXL_CALL             ThunkGetService(const char* name, uint32_t version);
        static wxl_result WXL_CALL ThunkPublishService(const char* name, uint32_t version,
                                                       const void* iface);
        static wxl_subscription WXL_CALL ThunkSubscribe(wxl_event event, wxl_event_fn handler,
                                                        void* user, int32_t priority);
        static void WXL_CALL             ThunkUnsubscribe(wxl_subscription subscription);
        static void WXL_CALL             ThunkEmit(wxl_event event, const void* args);
        static void WXL_CALL             ThunkDisable(wxl_str reason);
        static wxl_bool WXL_CALL         ThunkRaises(wxl_event event);

        /// Remembers a raise for @ref Recent when it reaches a handler.
        void NoteRaise(wxl_event event);

        static std::string NameOwner(void* user, uint32_t owner);
        static void     OnHandlerFault(void* user, uint32_t owner, wxl_event event, const Fault& fault);
        static uint32_t SwapOwner(void* user, uint32_t owner);

        /// Finishes, on the main thread, the disables a fault on another thread left pending.
        void SettleDisables();

        /// Drops the mod's handlers now and the rest on the main thread. @p headline is what the
        /// log says happened, after the mod's id: "faulted in ...", "disabled itself: ...".
        void DisableWith(ModToken token, std::string headline, std::string fault);
        void FinishDisable(ModToken token, const std::string& headline, const std::string& fault);

        /// Logs, once per mod and event, a subscription the runtime refused because it never
        /// raises that event: the handler is real code that will never run.
        void WarnUnraised(ModToken token, wxl_event event);

        /// Logs, once per mod and event, a subscription refused because the event is newer than
        /// the headers the mod's binary was built against.
        void WarnNewer(ModToken token, wxl_event event, const AbiVersion& built,
                       const AbiVersion& since);

        /// The id of a loaded mod, or of the one loading; empty for nobody.
        std::string IdOf(ModToken token) const;

        /// The abi a loaded mod's binary, or the loading one's once its query answered, was built
        /// against. False for nobody, and for a mod still inside wxl_mod_query.
        bool BuiltAbiOf(ModToken token, AbiVersion& out) const;

        void Refuse(const std::string& id, const std::string& directoryName,
                    const std::filesystem::path& directory, Verdict verdict,
                    const std::string& reason);
        void TryLoad(const ModCandidate& candidate);

        /// Calls one entry point of a mod under the guard, as that mod. False when it faulted;
        /// @p description then says how.
        bool CallEntry(ModToken token, GuardedFn fn, void* context, char* description,
                       size_t capacity);

        HostConfig       config_;
        Profiler         profiler_;
        std::string      modsDirectoryText_; ///< what info_.mods_directory points into
        Logger           logger_;
        ServiceRegistry  services_;
        EventBus         events_;
        wxl_runtime_info info_{};
        wxl_host         table_{};

        std::vector<LoadedMod>  loaded_;   ///< reserved before the first load: entries never move
        std::atomic<size_t>     loadedCount_{0}; ///< loaded_.size(), for a reader on another thread
        std::vector<ModBinary>  binaries_; ///< index-aligned with loaded_
        std::vector<RefusedMod> refused_;
        bool                    loadedAll_   = false;
        OpcodeDeclarations      opcodes_;
        std::string             loadingId_;  ///< the mod inside wxl_mod_query or wxl_mod_load
        AbiVersion              loadingAbi_;           ///< its built abi, once wxl_mod_query answered
        bool                    loadingAbiKnown_ = false;
        std::string             loadingDisabled_;      ///< the reason, when it disabled itself while loading
        bool                    loadingDisabledSet_ = false;
        const Manifest*         loadingManifest_    = nullptr; ///< the loading mod's, while it loads
        DisableFn               disableFn_   = nullptr;
        void*                   disableUser_ = nullptr;
        std::thread::id         mainThread_;

        mutable std::mutex                              stateLock_; ///< guards the three below
        struct PendingDisable
        {
            ModToken    token;
            std::string headline;
            std::string fault;
        };
        std::vector<PendingDisable>                     pendingDisables_;
        std::set<std::pair<ModToken, uint32_t>>         warnedUnraised_;
        std::set<std::pair<ModToken, uint32_t>>         warnedNewer_;
        std::atomic<bool>                               disablesPending_{false};

        /// event + 1 (0 is an empty slot), main-thread bit << 16, milliseconds since the host
        /// was built << 32. One store per raise, read without a lock.
        std::atomic<uint64_t>                           recent_[kRecentEvents] = {};
        std::atomic<uint32_t>                           recentNext_{0};
        std::chrono::steady_clock::time_point           builtAt_ = std::chrono::steady_clock::now();
    };
} // namespace wxl::host

#endif // WXL_HOST_MOD_HOST_HPP
