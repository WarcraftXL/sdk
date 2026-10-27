// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL

#include "wxl/host/mod_host.hpp"

#include <chrono>
#include <cstdio>
#include <string_view>
#include <utility>

namespace wxl::host
{
    ModHost* ModHost::current_ = nullptr;

    namespace
    {
        /// The mod whose code runs on this thread. Per thread, because events are raised on
        /// more than one and each handler is its own mod's code wherever it runs.
        thread_local ModHost::ModToken t_currentMod = 0;

        /// How long an unload waits for another thread to leave a handler before it gives up
        /// and leaves the binary mapped.
        constexpr std::chrono::milliseconds kUnloadWait{2000};

        std::string_view View(wxl_str str)
        {
            return str.data ? std::string_view(str.data, str.size) : std::string_view();
        }

        const char* StageName(wxl_stage stage)
        {
            switch (stage)
            {
            case WXL_STAGE_EARLY: return "early";
            case WXL_STAGE_STORAGE: return "storage";
            case WXL_STAGE_GRAPHICS: return "graphics";
            case WXL_STAGE_SCRIPT: return "script";
            case WXL_STAGE_READY: return "ready";
            case WXL_STAGE_SHUTDOWN: return "shutdown";
            }
            return "?";
        }

        // The entry points as the guard calls them: one plain function per entry, reading its
        // arguments and writing its result through a context block.

        struct QueryCall
        {
            wxl_mod_query_fn    fn;
            const wxl_mod_info* result;
        };

        void InvokeQuery(void* context)
        {
            QueryCall* call = static_cast<QueryCall*>(context);
            call->result    = call->fn();
        }

        struct LoadCall
        {
            wxl_mod_load_fn fn;
            const wxl_host* table;
            wxl_result      result;
        };

        void InvokeLoad(void* context)
        {
            LoadCall* call = static_cast<LoadCall*>(context);
            call->result   = call->fn(call->table);
        }

        struct StageCall
        {
            wxl_mod_stage_fn fn;
            wxl_stage        stage;
        };

        void InvokeStage(void* context)
        {
            StageCall* call = static_cast<StageCall*>(context);
            call->fn(call->stage);
        }

        void InvokeUnload(void* context)
        {
            static_cast<wxl_mod_unload_fn>(context)();
        }
    } // namespace

    ModHost::ModHost(HostConfig config)
        : config_(std::move(config)), logger_(config_.log_sink, config_.log_minimum),
          events_(config_.events, &logger_)
    {
        current_ = this;

        mainThread_ = std::this_thread::get_id();
        events_.SetOwnerTracker(&ModHost::SwapOwner, this);
        events_.SetFaultHandler(&ModHost::OnHandlerFault, this);

        info_.struct_size = sizeof(info_);
        info_.id          = wxl_str{config_.runtime_id.data(),
                           static_cast<uint32_t>(config_.runtime_id.size())};
        info_.name        = wxl_str{config_.runtime_name.data(),
                             static_cast<uint32_t>(config_.runtime_name.size())};
        info_.version     = config_.runtime_version;
        info_.abi_major   = WXL_ABI_VERSION_MAJOR;
        info_.abi_minor   = WXL_ABI_VERSION_MINOR;
        info_.abi_feature = WXL_ABI_VERSION_FEATURE;
        // Copied byte for byte: u8string() is std::string in C++17 and std::u8string in C++20,
        // and the host is built at both levels.
        const auto modsDirectory = config_.mods_directory.u8string();
        modsDirectoryText_.assign(modsDirectory.begin(), modsDirectory.end());
        info_.mods_directory = wxl_str{modsDirectoryText_.data(),
                                       static_cast<uint32_t>(modsDirectoryText_.size())};

        table_.struct_size     = sizeof(table_);
        table_.abi_major       = WXL_ABI_VERSION_MAJOR;
        table_.abi_minor       = WXL_ABI_VERSION_MINOR;
        table_.log             = &ModHost::ThunkLog;
        table_.runtime         = &ModHost::ThunkRuntime;
        table_.has_service     = &ModHost::ThunkHasService;
        table_.get_service     = &ModHost::ThunkGetService;
        table_.publish_service = &ModHost::ThunkPublishService;
        table_.subscribe       = &ModHost::ThunkSubscribe;
        table_.unsubscribe     = &ModHost::ThunkUnsubscribe;
        table_.emit            = &ModHost::ThunkEmit;
        table_.abi_feature     = WXL_ABI_VERSION_FEATURE;
        table_.disable         = &ModHost::ThunkDisable;
        table_.raises          = &ModHost::ThunkRaises;

        for (const PublishedService& service : config_.services)
        {
            if (services_.Publish(service.name, service.version, service.iface) != WXL_OK)
                logger_.Warn(kHostTag, "runtime service " + service.name + " version " +
                                           std::to_string(service.version) +
                                           " was not published: null or already taken");
        }
    }

    ModHost::~ModHost()
    {
        if (!loaded_.empty())
        {
            logger_.Warn(kHostTag, "host destroyed with mods loaded; unloading without a "
                                   "shutdown stage");
            UnloadAll();
        }
        if (current_ == this) current_ = nullptr;
    }

    // --- The table -------------------------------------------------------------------------

    void WXL_CALL ModHost::ThunkLog(wxl_log_level level, const char* tag, wxl_str message)
    {
        if (current_) current_->logger_.Write(level, tag ? tag : "?", View(message));
    }

    const wxl_runtime_info* WXL_CALL ModHost::ThunkRuntime(void)
    {
        return current_ ? &current_->info_ : nullptr;
    }

    wxl_bool WXL_CALL ModHost::ThunkHasService(const char* name, uint32_t version)
    {
        if (!current_ || !name) return WXL_FALSE;
        return current_->services_.Has(name, version) ? WXL_TRUE : WXL_FALSE;
    }

    const void* WXL_CALL ModHost::ThunkGetService(const char* name, uint32_t version)
    {
        if (!current_ || !name) return nullptr;
        return current_->services_.Get(name, version);
    }

    wxl_result WXL_CALL ModHost::ThunkPublishService(const char* name, uint32_t version,
                                                     const void* iface)
    {
        if (!current_) return WXL_E_FAILED;
        if (!name) return WXL_E_INVALID;
        return current_->services_.Publish(name, version, iface);
    }

    wxl_subscription WXL_CALL ModHost::ThunkSubscribe(wxl_event event, wxl_event_fn handler,
                                                      void* user, int32_t priority)
    {
        if (!current_) return WXL_SUBSCRIPTION_NONE;

        // A binary cannot know an event its headers did not carry: refused with a reason rather
        // than handed args it would read with a layout it never had.
        AbiVersion built;
        if (handler && static_cast<uint32_t>(event) < static_cast<uint32_t>(WXL_EV_COUNT) &&
            current_->BuiltAbiOf(t_currentMod, built))
        {
            const AbiVersion since = EventSince(event);
            if (!AtLeast(built, since))
            {
                current_->WarnNewer(t_currentMod, event, built, since);
                return WXL_SUBSCRIPTION_NONE;
            }
        }

        const wxl_subscription token =
            current_->events_.Subscribe(event, handler, user, priority, t_currentMod);
        if (token == WXL_SUBSCRIPTION_NONE && handler &&
            static_cast<uint32_t>(event) < static_cast<uint32_t>(WXL_EV_COUNT))
            current_->WarnUnraised(t_currentMod, event);
        return token;
    }

    wxl_bool WXL_CALL ModHost::ThunkRaises(wxl_event event)
    {
        if (!current_ || static_cast<uint32_t>(event) >= static_cast<uint32_t>(WXL_EV_COUNT)) return 0;
        return current_->events_.CanRaise(event) ? 1 : 0;
    }

    void WXL_CALL ModHost::ThunkUnsubscribe(wxl_subscription subscription)
    {
        if (current_) current_->events_.Unsubscribe(subscription);
    }

    void WXL_CALL ModHost::ThunkEmit(wxl_event event, const void* args)
    {
        if (current_) current_->events_.Emit(event, args);
    }

    void WXL_CALL ModHost::ThunkDisable(wxl_str reason)
    {
        if (current_) current_->DisableOnRequest(t_currentMod, View(reason));
    }

    // --- The current mod ---------------------------------------------------------------------

    ModHost::ModToken ModHost::CurrentMod() const { return t_currentMod; }

    ModHost::ModToken ModHost::SwapCurrentMod(ModToken token)
    {
        const ModToken previous = t_currentMod;
        t_currentMod            = token;
        return previous;
    }

    uint32_t ModHost::SwapOwner(void* user, uint32_t owner)
    {
        return static_cast<ModHost*>(user)->SwapCurrentMod(owner);
    }

    void ModHost::SetProfiler(const Profiler& profiler)
    {
        profiler_ = profiler;
        events_.SetProfiler(&profiler_, &ModHost::NameOwner, this);
    }

    std::string ModHost::NameOwner(void* user, uint32_t owner)
    {
        return owner ? static_cast<const ModHost*>(user)->IdOf(owner) : std::string("WarcraftXL");
    }

    std::string ModHost::IdOf(ModToken token) const
    {
        if (const LoadedMod* mod = ModOf(token)) return mod->id;
        if (token == loaded_.size() + 1) return loadingId_;
        return std::string();
    }

    const Manifest* ModHost::ManifestOf(ModToken token) const
    {
        if (const LoadedMod* mod = ModOf(token)) return &mod->manifest;
        if (token != 0 && token == loaded_.size() + 1) return loadingManifest_;
        return nullptr;
    }

    bool ModHost::BuiltAbiOf(ModToken token, AbiVersion& out) const
    {
        if (const LoadedMod* mod = ModOf(token))
        {
            out = mod->built_abi;
            return true;
        }
        if (token == loaded_.size() + 1 && loadingAbiKnown_)
        {
            out = loadingAbi_;
            return true;
        }
        return false;
    }

    void ModHost::WarnNewer(ModToken token, wxl_event event, const AbiVersion& built,
                            const AbiVersion& since)
    {
        {
            std::lock_guard<std::mutex> lock(stateLock_);
            if (!warnedNewer_.insert({token, static_cast<uint32_t>(event)}).second) return;
        }
        logger_.Warn(kHostTag, "mod " + IdOf(token) + " subscribed to " + EventName(event) +
                                   ", which came with abi " + AbiText(since) +
                                   "; its binary was built against abi " + AbiText(built) +
                                   ", whose headers do not have that event: the subscription is "
                                   "refused. Rebuild it against headers of " + AbiText(since) +
                                   " or later");
    }

    void ModHost::WarnUnraised(ModToken token, wxl_event event)
    {
        {
            std::lock_guard<std::mutex> lock(stateLock_);
            if (!warnedUnraised_.insert({token, static_cast<uint32_t>(event)}).second) return;
        }
        const std::string id = IdOf(token);
        logger_.Warn(kHostTag, (id.empty() ? std::string("a handler") : "mod " + id) +
                                   " subscribed to " + EventName(event) +
                                   ", which this runtime does not raise: that handler will "
                                   "never run");
    }

    // --- Faults ----------------------------------------------------------------------------

    void ModHost::OnHandlerFault(void* user, uint32_t owner, wxl_event event, const Fault& fault)
    {
        ModHost* host = static_cast<ModHost*>(user);
        if (!host) return;
        if (owner == 0)
        {
            host->logger_.Error(kHostTag, std::string("the faulting handler for ") + EventName(event) +
                                              " belongs to no mod; nothing is disabled");
            return;
        }
        char description[192];
        host->Disable(owner, std::string("in its handler for ") + EventName(event) + ": " +
                                 DescribeFault(fault, description, sizeof description));
    }

    const LoadedMod* ModHost::ModOf(ModToken token) const
    {
        // The count is published after the entry is built, and loaded_ never reallocates while
        // mods load, so a handler on another thread reads a whole entry or none.
        if (token == 0 || token > loadedCount_.load(std::memory_order_acquire)) return nullptr;
        return &loaded_[token - 1];
    }

    void ModHost::Disable(ModToken token, std::string_view what)
    {
        DisableWith(token, "faulted " + std::string(what), std::string(what));
    }

    void ModHost::DisableOnRequest(ModToken token, std::string_view reason)
    {
        if (token == 0)
        {
            logger_.Warn(kHostTag, "disable was called from code that is no mod's; nothing is "
                                   "disabled. The reason given: " + std::string(reason));
            return;
        }
        const std::string text = reason.empty() ? std::string("no reason given") : std::string(reason);
        // A mod still inside wxl_mod_load is not in loaded_ yet: its load is refused instead.
        if (OnMainThread() && !loadingId_.empty() && token == loaded_.size() + 1)
        {
            events_.DropOwner(token);
            loadingDisabled_    = text;
            loadingDisabledSet_ = true;
            return;
        }
        DisableWith(token, "disabled itself: " + text, text);
    }

    void ModHost::DisableWith(ModToken token, std::string headline, std::string fault)
    {
        if (token == 0) return;

        // Whatever thread this is, the mod's handlers stop now.
        events_.DropOwner(token);

        if (!OnMainThread())
        {
            std::lock_guard<std::mutex> lock(stateLock_);
            pendingDisables_.push_back(PendingDisable{token, std::move(headline), std::move(fault)});
            disablesPending_.store(true, std::memory_order_release);
            return;
        }
        FinishDisable(token, headline, fault);
    }

    void ModHost::FinishDisable(ModToken token, const std::string& headline, const std::string& fault)
    {
        if (token == 0 || token > loaded_.size()) return;
        LoadedMod& mod = loaded_[token - 1];
        if (mod.disabled) return;

        mod.disabled = true;
        mod.fault    = fault;
        logger_.Error(kHostTag, "mod " + mod.id + " " + headline +
                                    "; it is disabled: its handlers are dropped, its remaining "
                                    "stages and its unload are skipped, its binary stays mapped, "
                                    "and what it published or wrote stays as it was");
        if (disableFn_) disableFn_(disableUser_, token, mod.id.c_str());
    }

    void ModHost::SettleDisables()
    {
        if (!disablesPending_.load(std::memory_order_acquire) || !OnMainThread()) return;

        std::vector<PendingDisable> pending;
        {
            std::lock_guard<std::mutex> lock(stateLock_);
            pending.swap(pendingDisables_);
            disablesPending_.store(false, std::memory_order_release);
        }
        for (const PendingDisable& one : pending) FinishDisable(one.token, one.headline, one.fault);
    }

    bool ModHost::CallEntry(ModToken token, GuardedFn fn, void* context, char* description,
                            size_t capacity)
    {
        const ModToken previous = SwapCurrentMod(token);
        Fault          fault;
        const bool     returned = CallGuarded(fn, context, &fault);
        SwapCurrentMod(previous);
        if (!returned) DescribeFault(fault, description, capacity);
        return returned;
    }

    // --- Loading ---------------------------------------------------------------------------

    void ModHost::Refuse(const std::string& id, const std::string& directoryName,
                        const std::filesystem::path& directory, Verdict verdict,
                        const std::string& reason)
    {
        const std::string who = id.empty() ? directoryName : id;
        if (verdict == Verdict::Skip) logger_.Info(kHostTag, "skipped " + who + ": " + reason);
        else logger_.Warn(kHostTag, "refused " + who + ": " + reason);
        refused_.push_back(RefusedMod{id, directoryName, directory, verdict, reason});
    }

    void ModHost::TryLoad(const ModCandidate& candidate)
    {
        const Manifest& manifest = candidate.manifest;

        const Decision admitted = AdmitManifest(manifest, config_.target, services_);
        if (!admitted)
        {
            Refuse(manifest.id, candidate.directory_name, candidate.directory, admitted.verdict,
                   admitted.reason);
            return;
        }

        LibraryLoader&              loader = config_.loader ? *config_.loader : PlatformLoader();
        const std::filesystem::path path   = candidate.directory / admitted.binary;
        ModBinary                   binary;
        std::string                 error;
        if (!ModBinary::Open(loader, path, binary, error))
        {
            Refuse(manifest.id, candidate.directory_name, candidate.directory, Verdict::Refuse,
                   error);
            return;
        }

        // The token this mod gets if it loads: its place at the end of loaded_.
        const ModToken token = static_cast<ModToken>(loaded_.size() + 1);
        char           description[192];

        // Names the mod in what its query and load do, before it is in loaded_.
        struct Loading
        {
            std::string&     id;
            bool&            abiKnown;
            bool&            disabled;
            const Manifest*& manifest;
            ~Loading()
            {
                id.clear();
                abiKnown = false;
                disabled = false;
                manifest = nullptr;
            }
        } loading{loadingId_, loadingAbiKnown_, loadingDisabledSet_, loadingManifest_};
        loadingId_       = manifest.id;
        loadingManifest_ = &manifest;

        // The first mod code to run, and by contract side-effect free.
        QueryCall query{binary.Entries().query, nullptr};
        if (!CallEntry(token, &InvokeQuery, &query, description, sizeof description))
        {
            events_.DropOwner(token);
            binary.Abandon();
            Refuse(manifest.id, candidate.directory_name, candidate.directory, Verdict::Refuse,
                   std::string("wxl_mod_query faulted: ") + description +
                       "; the binary stays mapped and nothing of it is called again");
            return;
        }

        const Decision accepted = AdmitInfo(manifest, query.result);
        if (!accepted)
        {
            Refuse(manifest.id, candidate.directory_name, candidate.directory, Verdict::Refuse,
                   accepted.reason);
            return;
        }

        const AbiVersion built{query.result->abi_major, query.result->abi_minor,
                               query.result->abi_feature};
        loadingAbi_      = built;
        loadingAbiKnown_ = true;

        LoadedMod loaded;
        loaded.id             = manifest.id;
        loaded.built_abi      = built;
        loaded.name           = query.result->name ? query.result->name : manifest.name;
        loaded.directory_name = candidate.directory_name;
        loaded.directory      = candidate.directory;
        loaded.binary         = path;
        loaded.manifest       = manifest;
        loaded.has_stage      = binary.Entries().stage != nullptr;
        loaded.has_unload     = binary.Entries().unload != nullptr;

        LoadCall load{binary.Entries().load, &table_, WXL_E_FAILED};
        if (!CallEntry(token, &InvokeLoad, &load, description, sizeof description))
        {
            events_.DropOwner(token);
            binary.Abandon();
            Refuse(manifest.id, candidate.directory_name, candidate.directory, Verdict::Refuse,
                   std::string("wxl_mod_load faulted: ") + description +
                       "; the binary stays mapped and nothing of it is called again");
            return;
        }
        if (loadingDisabledSet_)
        {
            events_.DropOwner(token);
            binary.Abandon();
            Refuse(manifest.id, candidate.directory_name, candidate.directory, Verdict::Refuse,
                   "it disabled itself in wxl_mod_load: " + loadingDisabled_ +
                       "; the binary stays mapped and nothing of it is called again");
            return;
        }
        if (load.result != WXL_OK)
        {
            events_.DropOwner(token);
            Refuse(manifest.id, candidate.directory_name, candidate.directory, Verdict::Refuse,
                   std::string("wxl_mod_load returned ") + ResultName(load.result));
            return;
        }

        logger_.Info(kHostTag, "loaded " + loaded.id + " " + manifest.version.text + " (" +
                                   loaded.name + ") from " + admitted.binary);
        loaded_.push_back(std::move(loaded));
        binaries_.push_back(std::move(binary));
        loadedCount_.store(loaded_.size(), std::memory_order_release);
    }

    void ModHost::LoadAll()
    {
        if (loadedAll_) return;
        loadedAll_ = true;

        logger_.Info(kHostTag, "scanning " + config_.mods_directory.string());
        const Discovery found = Discover(config_.mods_directory);
        for (const DiscoveryProblem& problem : found.problems)
            Refuse(problem.id, problem.directory_name, problem.directory, Verdict::Refuse,
                   problem.message);

        LoadOrder order = OrderByLoadAfter(found.mods);
        opcodes_        = DeclareOpcodes(order);
        for (const DiscoveryProblem& problem : order.excluded)
            Refuse(problem.id, problem.directory_name, problem.directory, Verdict::Refuse,
                   problem.message);

        loaded_.reserve(order.ordered.size());
        binaries_.reserve(order.ordered.size());
        for (const ModCandidate& candidate : order.ordered) TryLoad(candidate);

        logger_.Info(kHostTag, std::to_string(loaded_.size()) + " mod(s) loaded, " +
                                   std::to_string(refused_.size()) + " not");
    }

    // --- Running ---------------------------------------------------------------------------

    void ModHost::Stage(wxl_stage stage)
    {
        SettleDisables();
        logger_.Debug(kHostTag, std::string("stage ") + StageName(stage));
        const size_t count = binaries_.size();

        const auto one = [&](size_t i) {
            if (loaded_[i].disabled || !binaries_[i].Entries().stage) return;
            StageCall call{binaries_[i].Entries().stage, stage};
            char      description[192];
            if (!CallEntry(static_cast<ModToken>(i + 1), &InvokeStage, &call, description,
                           sizeof description))
                Disable(static_cast<ModToken>(i + 1), std::string("in wxl_mod_stage(") +
                                                         StageName(stage) + "): " + description);
        };

        if (stage == WXL_STAGE_SHUTDOWN)
        {
            for (size_t i = count; i-- > 0;) one(i);
        }
        else
        {
            for (size_t i = 0; i < count; ++i) one(i);
        }
    }

    void ModHost::UnloadAll()
    {
        SettleDisables();

        // Once a wait has run out, a thread is stuck in some handler; the remaining mods are
        // not waited for again, and none of them is freed.
        bool stuck = false;
        for (size_t i = binaries_.size(); i-- > 0;)
        {
            // Before anything frees the code a handler points at. The client keeps raising
            // events while it takes itself apart -- window messages do not stop because the
            // mods are gone -- so a subscription left behind is a call into a freed binary,
            // and so is a handler another thread entered just before the drop.
            events_.DropOwner(static_cast<uint32_t>(i + 1));
            if (!stuck && !events_.WaitForOtherDispatches(kUnloadWait))
            {
                stuck = true;
                logger_.Warn(kHostTag, "another thread is still inside a handler; the remaining "
                                       "mods are unloaded but their binaries stay mapped");
            }

            if (loaded_[i].disabled)
            {
                logger_.Info(kHostTag, "left " + loaded_[i].id + " mapped: it is disabled");
                binaries_[i].Abandon();
                continue;
            }
            if (binaries_[i].Entries().unload)
            {
                char description[192];
                if (!CallEntry(static_cast<ModToken>(i + 1), &InvokeUnload,
                               reinterpret_cast<void*>(binaries_[i].Entries().unload), description,
                               sizeof description))
                {
                    Disable(static_cast<ModToken>(i + 1),
                            std::string("in wxl_mod_unload: ") + description);
                    binaries_[i].Abandon();
                    continue;
                }
                // It disabled itself inside its unload: its code is not trusted with a close.
                if (loaded_[i].disabled)
                {
                    binaries_[i].Abandon();
                    continue;
                }
            }
            logger_.Info(kHostTag, "unloaded " + loaded_[i].id);
            if (stuck) binaries_[i].Abandon();
            else binaries_[i].Close();
        }
        loadedCount_.store(0, std::memory_order_release);
        binaries_.clear();
        loaded_.clear();
    }

    void ModHost::Shutdown()
    {
        Stage(WXL_STAGE_SHUTDOWN);
        UnloadAll();
    }

    void ModHost::Raise(wxl_event event, const void* args)
    {
        if (disablesPending_.load(std::memory_order_relaxed)) SettleDisables();
        if (events_.Listening(event)) NoteRaise(event);
        events_.Emit(event, args);
    }

    void ModHost::RaiseTo(wxl_event event, const void* args, EventBus::AcceptFn accept, void* user)
    {
        if (disablesPending_.load(std::memory_order_relaxed)) SettleDisables();
        if (events_.Listening(event)) NoteRaise(event);
        events_.EmitTo(event, args, accept, user);
    }

    void ModHost::NoteRaise(wxl_event event)
    {
        const uint64_t ms = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                      std::chrono::steady_clock::now() - builtAt_)
                                                      .count());
        const uint64_t packed = (static_cast<uint64_t>(static_cast<uint32_t>(event) + 1u) & 0xFFFFu) |
                                (OnMainThread() ? 0x10000u : 0u) | (ms << 32);
        const uint32_t slot = recentNext_.fetch_add(1, std::memory_order_relaxed) % kRecentEvents;
        recent_[slot].store(packed, std::memory_order_relaxed);
    }

    size_t ModHost::Recent(RecentEvent* out, size_t capacity) const
    {
        if (!out || capacity == 0) return 0;
        const uint64_t now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                       std::chrono::steady_clock::now() - builtAt_)
                                                       .count());
        const uint32_t next    = recentNext_.load(std::memory_order_relaxed);
        const size_t   skipped = kRecentEvents > capacity ? kRecentEvents - capacity : 0;
        size_t         written = 0;
        for (size_t i = skipped; i < kRecentEvents; ++i)
        {
            const uint64_t packed = recent_[(next + i) % kRecentEvents].load(std::memory_order_relaxed);
            if ((packed & 0xFFFFu) == 0) continue;
            const uint64_t at = packed >> 32;
            out[written++]    = RecentEvent{static_cast<wxl_event>((packed & 0xFFFFu) - 1u),
                                            (packed & 0x10000u) != 0,
                                            static_cast<uint32_t>(now > at ? now - at : 0)};
        }
        return written;
    }
} // namespace wxl::host
