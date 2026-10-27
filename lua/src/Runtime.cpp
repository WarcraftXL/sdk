// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL

#include "Runtime.hpp"

#include "Objects.hpp"
#include "Profile.hpp"
#include "Shims.hpp"

#include "wxl/host/admission.hpp"
#include "wxl/host/manifest.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <utility>

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
    #define NOMINMAX
#endif
#include <windows.h>

namespace wxl::lua
{
    Runtime* Runtime::instance_ = nullptr;

    namespace
    {
        constexpr int kWarnOffThread   = 0;
        constexpr int kWarnUnraised    = 1;
        constexpr int kWarnBadReturn   = 2;
        constexpr int kWarnNoAnswer    = 3;
        constexpr int kWarnExtraReturn = 4;

        /// A path a manifest may name: relative, and never leaving the folder.
        bool StaysInside(const std::string& path)
        {
            const std::filesystem::path p = std::filesystem::u8path(path);
            if (p.empty() || p.is_absolute() || p.has_root_name() || p.has_root_directory()) return false;
            for (const auto& component : p)
                if (component == "..") return false;
            return true;
        }
    } // namespace

    Runtime::Runtime(const wxl_host* host)
        : host_(host), mainThread_(std::this_thread::get_id()),
          lastPoll_(std::chrono::steady_clock::now())
    {
        instance_ = this;
        for (uint32_t i = 0; i < static_cast<uint32_t>(WXL_EV_COUNT); ++i)
        {
            slots_[i].runtime = this;
            slots_[i].event   = reflect::Find(static_cast<wxl_event>(i));
            if (slots_[i].event) slots_[i].writable = WritableOf(*slots_[i].event);
        }
        // Update drives the queue, the reload poll and onUpdate; UiRenderEnd drives onDraw.
        EnsureNative(slots_[WXL_EV_Update], nullptr);
        EnsureNative(slots_[WXL_EV_UiRenderEnd], nullptr);
        // An object's Lua identity and the fields stored on it end with it, once every handler
        // of every mod, Lua or not, has seen it leave.
        objectGone_ = wxl::Subscribe(WXL_EV_ObjectDestroy, &Runtime::OnObjectGone, this, INT32_MAX);
        // What the mods registered with the client's script is registered again in each context.
        shims::Start();
    }

    void WXL_CALL Runtime::OnObjectGone(void* user, const void* args)
    {
        Runtime* self = static_cast<Runtime*>(user);
        // Objects belong to the main thread; a runtime raising this elsewhere broke that first.
        if (!args || std::this_thread::get_id() != self->mainThread_) return;
        const wxl_handle handle = static_cast<const wxl_object_args*>(args)->object;
        for (const std::unique_ptr<ScriptMod>& mod : self->mods_)
            if (mod->Loaded()) ForgetObject(mod->State(), handle);
    }

    Runtime::~Runtime()
    {
        Stop();
        if (instance_ == this) instance_ = nullptr;
    }

    host::AbiVersion Runtime::Abi() const
    {
        const size_t     needed  = offsetof(wxl_host, abi_feature) + sizeof(host_->abi_feature);
        host::AbiVersion runtime = {host_->abi_major, host_->abi_minor,
                                    host_->struct_size >= needed ? host_->abi_feature : 0};
        const host::AbiVersion built = {WXL_ABI_VERSION_MAJOR, WXL_ABI_VERSION_MINOR,
                                        WXL_ABI_VERSION_FEATURE};
        // The host admitted this binary, so the majors agree: the lower of the two is served.
        if (runtime.major != built.major) return runtime;
        if (runtime.minor != built.minor) return runtime.minor < built.minor ? runtime : built;
        return runtime.feature < built.feature ? runtime : built;
    }

    void Runtime::Log(wxl_log_level level, std::string_view message) const
    {
        wxl::Log(level, kTag, message);
    }

    // --- finding the mods --------------------------------------------------------------------

    std::filesystem::path Runtime::ModsDirectory() const
    {
        if (const wxl_runtime_info* info = host_->runtime())
        {
            const size_t needed = offsetof(wxl_runtime_info, mods_directory) + sizeof(wxl_str);
            if (info->struct_size >= needed && info->mods_directory.data && info->mods_directory.size)
                return std::filesystem::u8path(
                    std::string(info->mods_directory.data, info->mods_directory.size));
        }

        // The runtime did not say: this binary sits at <mods>/<this mod>/bin/<arch>/.
        HMODULE module = nullptr;
        if (!::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                      GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                  reinterpret_cast<LPCWSTR>(&Runtime::OnNativeEvent), &module))
            return std::filesystem::path();
        wchar_t     buffer[MAX_PATH * 4];
        const DWORD length = ::GetModuleFileNameW(module, buffer, static_cast<DWORD>(std::size(buffer)));
        if (length == 0 || length >= std::size(buffer)) return std::filesystem::path();
        const std::filesystem::path binary(buffer, buffer + length);
        return binary.parent_path().parent_path().parent_path().parent_path();
    }

    std::string Runtime::Refusal(const host::Manifest& manifest) const
    {
        if (!StaysInside(manifest.lua)) return "entry '" + manifest.lua + "' leaves the mod folder";
        if (std::string reason = host::AbiRefusal(manifest.abi, Abi()); !reason.empty()) return reason;
        for (const host::ServiceRef& ref : manifest.required)
            if (!host_->has_service(ref.service.c_str(), ref.version))
                return "requires service " + ref.service + " version " + std::to_string(ref.version) +
                       ", which this runtime does not have";
        return std::string();
    }

    void Runtime::Admit(const host::ModCandidate& candidate)
    {
        if (const std::string reason = Refusal(candidate.manifest); !reason.empty())
        {
            Log(WXL_LOG_WARN, "refused " + candidate.manifest.id + ": " + reason);
            return;
        }
        mods_.push_back(std::make_unique<ScriptMod>(*this, candidate, static_cast<uint32_t>(mods_.size())));
    }

    namespace
    {
        /// The schema's id rule applied to a folder name: lowercase, letters and digits, runs of
        /// anything else as one '-'. Empty when nothing usable is left.
        std::string IdFromFolder(const std::string& folder)
        {
            std::string id;
            bool        separator = false;
            for (const char c : folder)
            {
                const bool keep = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
                const bool upper = c >= 'A' && c <= 'Z';
                if (keep || upper)
                {
                    if (separator && !id.empty()) id += '-';
                    separator = false;
                    id += upper ? static_cast<char>(c - 'A' + 'a') : c;
                }
                else if (c == '.' && !id.empty() && !separator)
                {
                    id += '.';
                    separator = true;
                }
                else
                {
                    separator = true;
                }
            }
            while (!id.empty() && (id.back() == '.' || id.back() == '-')) id.pop_back();
            if (id.size() > 64) id.resize(64);
            while (!id.empty() && (id.back() == '.' || id.back() == '-')) id.pop_back();
            return id;
        }
    } // namespace

    void Runtime::AddBareFolders(const std::filesystem::path& directory,
                                 std::vector<host::ModCandidate>& candidates)
    {
        namespace fs = std::filesystem;
        std::error_code       ec;
        std::vector<fs::path> folders;
        for (const fs::directory_entry& entry : fs::directory_iterator(directory, ec))
        {
            std::error_code ignored;
            if (!entry.is_directory(ignored)) continue;
            if (fs::exists(entry.path() / host::kManifestFileName, ignored)) continue;
            if (!fs::is_regular_file(entry.path() / "init.lua", ignored)) continue;
            folders.push_back(entry.path());
        }
        std::sort(folders.begin(), folders.end(), [](const fs::path& a, const fs::path& b) {
            return a.filename().u8string() < b.filename().u8string();
        });

        for (const fs::path& folder : folders)
        {
            const std::string name = folder.filename().u8string();
            const std::string id   = IdFromFolder(name);
            if (!host::IsValidModId(id))
            {
                Log(WXL_LOG_WARN, "folder '" + name + "' holds an init.lua but its name makes no mod id" +
                                      (id.empty() ? std::string() : " ('" + id + "')") +
                                      "; rename it or give it a mod.json with an id");
                continue;
            }
            const auto twin = std::find_if(candidates.begin(), candidates.end(),
                                           [&](const host::ModCandidate& c) { return c.manifest.id == id; });
            if (twin != candidates.end())
            {
                Log(WXL_LOG_WARN, "folder '" + name + "': id '" + id + "' is already provided by " +
                                      twin->directory_name);
                continue;
            }

            host::ModCandidate candidate;
            candidate.directory_name       = name;
            candidate.directory            = folder;
            candidate.manifest.id          = id;
            candidate.manifest.name        = name;
            candidate.manifest.version.text = "0.0.0";
            candidate.manifest.abi         = Abi();
            candidate.manifest.lua         = "init.lua";
            Log(WXL_LOG_INFO, "found " + id + " in folder '" + name + "' (no manifest)");
            candidates.push_back(std::move(candidate));
        }
    }

    void Runtime::Start()
    {
        const std::filesystem::path directory = ModsDirectory();
        if (directory.empty())
        {
            Log(WXL_LOG_WARN, "no mods directory is known; no script mod can be found");
            return;
        }
        Log(WXL_LOG_INFO, "scanning " + directory.u8string() + " for script mods");

        std::vector<host::ModCandidate> candidates = host::Discover(directory).mods;
        AddBareFolders(directory, candidates);
        host::LoadOrder order = host::OrderByLoadAfter(candidates);
        // The host takes out the same mods, in the same order, and logs why.
        opcodes_ = host::DeclareOpcodes(order);
        for (const host::DiscoveryProblem& problem : order.excluded)
            if (problem.message.rfind("its mod.json declares opcode", 0) == 0)
                Log(WXL_LOG_WARN, "not loading " + problem.id + ": " + problem.message);
        for (const host::ModCandidate& candidate : order.ordered)
        {
            if (candidate.manifest.lua.empty()) continue;
            if (candidate.manifest.id == "wxl.lua") continue;
            Admit(candidate);
        }

        uint32_t loaded = 0;
        for (std::unique_ptr<ScriptMod>& mod : mods_)
            if (mod->Load()) ++loaded;
        Log(WXL_LOG_INFO, std::to_string(loaded) + " script mod(s) loaded, " +
                              std::to_string(mods_.size() - loaded) + " not");
    }

    void Runtime::Stage(wxl_stage stage)
    {
        if (stage == WXL_STAGE_READY)
        {
            ready_ = true;
            for (std::unique_ptr<ScriptMod>& mod : mods_) mod->Call(ScriptMod::Callback::Init);
        }
        else if (stage == WXL_STAGE_SHUTDOWN)
        {
            for (size_t i = mods_.size(); i-- > 0;) mods_[i]->Call(ScriptMod::Callback::Shutdown);
        }
    }

    void Runtime::Stop()
    {
        objectGone_.Reset();
        shims::Stop();
        for (size_t i = mods_.size(); i-- > 0;)
        {
            DropHandlers(*mods_[i]);
            mods_[i]->Close();
        }
        mods_.clear();
        for (Slot& slot : slots_)
        {
            slot.handlers.clear();
            slot.arriving.clear();
            slot.native.Reset();
        }
        std::lock_guard<std::mutex> lock(queueLock_);
        queue_.clear();
    }

    // --- handlers ----------------------------------------------------------------------------

    void Runtime::EnsureNative(Slot& slot, const ScriptMod* requester)
    {
        if (slot.native || !slot.event) return;
        slot.native = wxl::Subscribe(slot.event->id, &Runtime::OnNativeEvent, &slot);
        if (!slot.native && requester)
            WarnOnce(*requester, *slot.event, kWarnUnraised,
                     std::string("mod ") + requester->Id() + ": this runtime does not raise On" +
                         slot.event->name + " of " + slot.event->family +
                         "Script; that handler will never run");
    }

    uint32_t Runtime::Observe(ScriptMod& mod, const reflect::Event& event, int ref, bool override, int self)
    {
        Slot& slot = slots_[event.id];
        EnsureNative(slot, &mod);
        const Handler handler{&mod, ref, self, override, ++nextId_, true,
                              profile::Location(mod, std::string("On") + event.name)};
        // A dispatch walks `handlers` by index: one subscribed meanwhile waits beside it.
        if (slot.depth == 0)
        {
            Place(slot, handler);
        }
        else
        {
            slot.arriving.push_back(handler);
            slot.dirty = true;
        }
        return nextId_;
    }

    void Runtime::Place(Slot& slot, const Handler& handler)
    {
        const uint32_t rank = handler.mod->Rank();
        const auto     at   = std::upper_bound(slot.handlers.begin(), slot.handlers.end(), rank,
                                               [](uint32_t r, const Handler& h) { return r < h.mod->Rank(); });
        slot.handlers.insert(at, handler);
    }

    bool Runtime::Unobserve(ScriptMod& mod, uint32_t id)
    {
        for (Slot& slot : slots_)
        {
            for (std::vector<Handler>* list : {&slot.handlers, &slot.arriving})
            {
                for (Handler& handler : *list)
                {
                    if (handler.id != id || handler.mod != &mod || !handler.live) continue;
                    handler.live = false;
                    slot.dirty   = true;
                    if (slot.depth == 0) Compact(slot);
                    return true;
                }
            }
        }
        return false;
    }

    void Runtime::DropHandlers(ScriptMod& mod)
    {
        for (Slot& slot : slots_)
        {
            for (std::vector<Handler>* list : {&slot.handlers, &slot.arriving})
            {
                for (Handler& handler : *list)
                {
                    if (handler.mod != &mod) continue;
                    handler.live = false;
                    slot.dirty   = true;
                }
            }
            if (slot.dirty && slot.depth == 0) Compact(slot);
        }
    }

    void Runtime::Compact(Slot& slot)
    {
        const auto dead = [](const Handler& h) { return !h.live; };
        slot.handlers.erase(std::remove_if(slot.handlers.begin(), slot.handlers.end(), dead),
                            slot.handlers.end());
        for (const Handler& handler : slot.arriving)
            if (handler.live) Place(slot, handler);
        slot.arriving.clear();
        slot.dirty = false;
        // The two slots the runtime itself works from stay subscribed.
        const bool own = slot.event && (slot.event->id == WXL_EV_Update || slot.event->id == WXL_EV_UiRenderEnd);
        if (slot.handlers.empty() && !own) slot.native.Reset();
    }

    void Runtime::Disable(ScriptMod& mod, std::string_view why)
    {
        if (mod.Disabled()) return;
        DropHandlers(mod);
        mod.Disable();
        Log(WXL_LOG_ERROR, "mod " + mod.Id() + " is disabled: " + std::string(why) +
                               "; it stays off until one of its files changes");
    }

    bool Runtime::Dispatching() const
    {
        for (const Slot& slot : slots_)
            if (slot.depth != 0) return true;
        // A script function a mod registered is running, called by the client's own script.
        return shims::Calling();
    }

    void Runtime::WarnOnce(const ScriptMod& mod, const reflect::Event& event, int what,
                           const std::string& message)
    {
        if (!warned_.insert(std::make_tuple(mod.Id(), static_cast<uint32_t>(event.id), what)).second)
            return;
        Log(WXL_LOG_WARN, message);
    }

    // --- dispatch ----------------------------------------------------------------------------

    void WXL_CALL Runtime::OnNativeEvent(void* user, const void* args)
    {
        Slot* slot = static_cast<Slot*>(user);
        if (slot && slot->runtime) slot->runtime->OnEvent(*slot, args);
    }

    void Runtime::OnEvent(Slot& slot, const void* args)
    {
        if (std::this_thread::get_id() != mainThread_)
        {
            {
                std::lock_guard<std::mutex> lock(queueLock_);
                queue_.push_back(Queued{&slot, ArgsCopy(*slot.event, args)});
            }
            queued_.store(true, std::memory_order_release);
            return;
        }

        const bool update = slot.event->id == WXL_EV_Update;
        if (update)
        {
            Housekeeping();
            DrainQueue();
        }

        Dispatch(slot, args, true);

        if (update)
        {
            const float delta = static_cast<const wxl_update_args*>(args)->delta;
            for (std::unique_ptr<ScriptMod>& mod : mods_) mod->Call(ScriptMod::Callback::Update, delta);
        }
        else if (slot.event->id == WXL_EV_UiRenderEnd)
        {
            for (std::unique_ptr<ScriptMod>& mod : mods_) mod->Call(ScriptMod::Callback::Draw);
        }
    }

    void Runtime::Dispatch(Slot& slot, const void* args, bool answerable)
    {
        ++slot.depth;

        // By index over the handlers there were: one added by a handler waits for the next
        // event, one dropped is skipped from here on. Copied, since a handler may append.
        const size_t count = slot.handlers.size();
        for (size_t i = 0; i < count; ++i)
        {
            const Handler handler = slot.handlers[i];
            if (!handler.live || !handler.mod->Runs()) continue;
            // A field's change goes to the mods that watch it, as the host does for native mods.
            if (slot.event->id == WXL_EV_ObjectFieldChanged &&
                !handler.mod->Watches().Covers(*static_cast<const wxl_object_field_args*>(args)))
                continue;

            lua_State* L    = handler.mod->State();
            const int  base = lua_gettop(L);
            // The traceback, the function, self, a value per member: never more than this.
            if (!lua_checkstack(L, static_cast<int>(slot.event->field_count) + 3))
            {
                handler.mod->ReportError(std::string("in its handler for On") + slot.event->name,
                                         "the Lua stack cannot take the event's parameters");
                continue;
            }
            lua_rawgeti(L, LUA_REGISTRYINDEX, handler.mod->TracebackRef());
            lua_rawgeti(L, LUA_REGISTRYINDEX, handler.ref);
            int nargs = 0;
            if (handler.self != LUA_NOREF)
            {
                lua_rawgeti(L, LUA_REGISTRYINDEX, handler.self);
                nargs = 1;
            }
            nargs += PushParameters(L, *slot.event, args);
            int status = 0;
            {
                const profile::Call zone(handler.zone);
                status = lua_pcall(L, nargs, LUA_MULTRET, base + 1);
            }

            if (status != 0)
            {
                handler.mod->ReportError(std::string("in its ") + (handler.override ? "Override" : "Observe") +
                                             " handler for On" + slot.event->name + " of " +
                                             slot.event->family + "Script",
                                         lua_tostring(L, -1) ? lua_tostring(L, -1) : "?");
            }
            else if (handler.override)
            {
                if (answerable)
                    TakeAnswers(L, *handler.mod, slot, args, base + 2, lua_gettop(L) - (base + 1));
                else
                    WarnOnce(*handler.mod, *slot.event, kWarnOffThread,
                             "mod " + handler.mod->Id() + ": On" + slot.event->name +
                                 " was raised off the main thread; its Override handler ran on a "
                                 "copy at the next Update and its answer is not taken");
            }
            lua_settop(L, base);
        }

        --slot.depth;
        if (slot.depth == 0 && slot.dirty) Compact(slot);
    }

    void Runtime::TakeAnswers(lua_State* L, ScriptMod& mod, const Slot& slot, const void* args,
                              int first, int count)
    {
        const reflect::Event& event = *slot.event;
        const std::string     where = "mod " + mod.Id() + ": the Override of On" + event.name;

        if (count > static_cast<int>(slot.writable.size()))
        {
            std::string members;
            for (const reflect::Field* field : slot.writable)
                members += (members.empty() ? "'" : ", '") + std::string(field->name) + "'";
            WarnOnce(mod, event, kWarnExtraReturn,
                     where + " returned " + std::to_string(count) + " values; it answers through " +
                         members + ", one value each, and the rest are not taken");
        }

        for (int i = 0; i < count && i < static_cast<int>(slot.writable.size()); ++i)
        {
            const reflect::Field& field = *slot.writable[i];
            const int             index = first + i;
            const int             type  = lua_type(L, index);
            if (type == LUA_TNIL) continue;

            if (field.kind == reflect::Kind::answer)
            {
                if (type != LUA_TBOOLEAN)
                {
                    WarnOnce(mod, event, kWarnBadReturn,
                             where + " returned a " + lua_typename(L, type) + " for '" + field.name +
                                 "'; it takes true to answer, or false or nil to leave it");
                    continue;
                }
                // WXL_TRUE is a request, honoured unless another handler already decided;
                // false is never written back.
                if (lua_toboolean(L, index) && !WriteAnswer(field, args))
                    WarnOnce(mod, event, kWarnNoAnswer,
                             "mod " + mod.Id() + ": the runtime passed no '" + field.name +
                                 "' flag with On" + event.name + "; the answer is not taken");
                continue;
            }

            if (type != LUA_TSTRING)
            {
                WarnOnce(mod, event, kWarnBadReturn,
                         where + " returned a " + lua_typename(L, type) + " for '" + field.name +
                             "'; it takes a string, or nil to leave it");
                continue;
            }
            size_t      size     = 0;
            const char* text     = lua_tolstring(L, index, &size);
            uint32_t    capacity = 0;
            if (!WriteBuffer(field, args, std::string_view(text, size), capacity))
                Log(WXL_LOG_WARN, where + " returned " + std::to_string(size) + " bytes for '" +
                                      field.name + "', which takes at most " +
                                      std::to_string(capacity ? capacity - 1 : 0) +
                                      "; it is not taken");
        }
    }

    // --- the main thread's housekeeping ------------------------------------------------------

    void Runtime::DrainQueue()
    {
        if (!queued_.load(std::memory_order_acquire)) return;
        std::vector<Queued> pending;
        {
            std::lock_guard<std::mutex> lock(queueLock_);
            pending.swap(queue_);
            queued_.store(false, std::memory_order_release);
        }
        for (Queued& queued : pending) Dispatch(*queued.slot, queued.copy.Args(), false);
    }

    void Runtime::Housekeeping()
    {
        if (Dispatching()) return;

        SettleReloads();

        // A disabled mod's state is closed here, never from inside one of its own calls.
        for (std::unique_ptr<ScriptMod>& mod : mods_)
            if (mod->Disabled() && mod->Loaded()) mod->Close();

        const auto now = std::chrono::steady_clock::now();
        if (now - lastPoll_ >= kPollInterval)
        {
            lastPoll_ = now;
            CheckReloads();
        }
    }

    void Runtime::ReloadMod(ScriptMod& mod)
    {
        Log(WXL_LOG_INFO, "reloading " + mod.Id());
        if (mod.Runs()) mod.Call(ScriptMod::Callback::Shutdown);
        DropHandlers(mod);
        mod.Close();

        // A changed mod.json is read again and checked as at the start.
        std::string problem;
        if (mod.Reread(problem)) problem = Refusal(mod.Manifest());
        if (!problem.empty())
        {
            // Watched as they are now, so that the next change to either tries again.
            mod.Rewatch();
            Log(WXL_LOG_ERROR, "mod " + mod.Id() + " stays unloaded: " + problem +
                                   "; it loads again when its files change");
            return;
        }
        if (mod.Load() && ready_) mod.Call(ScriptMod::Callback::Init);
    }

    uint32_t Runtime::CheckReloads()
    {
        uint32_t reloaded = 0;
        for (std::unique_ptr<ScriptMod>& mod : mods_)
        {
            const std::filesystem::path changed = mod->Changed();
            if (changed.empty()) continue;
            Log(WXL_LOG_INFO, "mod " + mod->Id() + ": " + changed.generic_u8string() + " changed");
            ReloadMod(*mod);
            ++reloaded;
        }
        return reloaded;
    }

    void Runtime::SettleReloads()
    {
        std::vector<ScriptMod*> pending;
        pending.swap(pendingReloads_);
        for (ScriptMod* mod : pending) ReloadMod(*mod);
    }

    // --- the service -------------------------------------------------------------------------

    uint32_t Runtime::Poll()
    {
        if (std::this_thread::get_id() != mainThread_ || Dispatching()) return 0;
        lastPoll_ = std::chrono::steady_clock::now();
        return CheckReloads();
    }

    wxl_result Runtime::Reload(const char* id)
    {
        if (!id) return WXL_E_INVALID;
        for (std::unique_ptr<ScriptMod>& mod : mods_)
        {
            if (mod->Id() != id) continue;
            if (std::this_thread::get_id() != mainThread_) return WXL_E_FAILED;
            // From inside a handler the reload waits for the next Update: the state in use
            // cannot be closed under its own call.
            if (Dispatching())
            {
                if (std::find(pendingReloads_.begin(), pendingReloads_.end(), mod.get()) ==
                    pendingReloads_.end())
                    pendingReloads_.push_back(mod.get());
                return WXL_OK;
            }
            ReloadMod(*mod);
            return WXL_OK;
        }
        return WXL_E_NOT_FOUND;
    }
} // namespace wxl::lua
