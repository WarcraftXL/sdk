// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL

#include "Shims.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include "Profile.hpp"
#include "Runtime.hpp"
#include "ScriptMod.hpp"
#include "Services.hpp"
#include "wxl/reflect.hpp"
#include "wxl/sdk.hpp"
#include "wxl/services/rows.hpp"
#include "wxl/services/script.h"
#include "wxl/services/footprint.h"
#include "wxl/services/relay.h"
#include "wxl/services/storage.h"

namespace wxl::lua::shims
{
    namespace
    {
        constexpr const char* kTag = "wxl.lua";

        /// A context as a bit: the glue's is 1, the game's 2.
        uint32_t Bit(uint32_t context) { return context < 32 ? (1u << context) : 0u; }

        constexpr uint32_t kEveryContext = ~0u;

        /**
         * @brief One global name a Lua mod registered, and the native function behind it.
         *
         * Never freed: the client keeps calling the function it was handed as long as its
         * context lives, whoever holds the name now. A mod that reloads finds its name here,
         * already bound, and only points it at its new function.
         */
        struct Function
        {
            std::string name;
            ScriptMod*  mod      = nullptr; ///< who answers it; null while nobody does
            int         ref      = LUA_NOREF;
            uint32_t    contexts = kEveryContext;
            uint32_t    bound    = 0;       ///< the contexts in which the client holds it
            bool        warned   = false;   ///< a return value that cannot cross was logged
            uintptr_t   zone     = 0;       ///< its zone's location, made for the mod answering it
        };

        struct Event
        {
            std::string name;
            ScriptMod*  mod;
            uint32_t    contexts;
        };

        std::deque<Function> g_functions;
        std::vector<Event>   g_events;
        wxl::Subscription    g_ready;
        wxl::Subscription    g_closing;
        int                  g_calling = 0;

        const wxl_script_api* Api() { return wxl::api::Table<wxl_script_api>(); }

        ScriptMod& Self(lua_State* L)
        {
            return *static_cast<ScriptMod*>(lua_touserdata(L, lua_upvalueindex(1)));
        }

        /// The optional context argument: every context, or the one it names.
        uint32_t CheckContexts(lua_State* L, int index)
        {
            if (lua_isnoneornil(L, index)) return kEveryContext;
            const char*                     name = luaL_checkstring(L, index);
            const wxl::reflect::Enum* const contexts = wxl::reflect::FindEnum("wxl_script_context");
            for (uint32_t i = 0; contexts && i < contexts->count; ++i)
                if (std::strcmp(contexts->values[i].name, name) == 0) return Bit(contexts->values[i].value);
            luaL_argerror(L, index, "a context: \"glue\" or \"game\", or nil for both");
            return 0;
        }

        // --- the function the client calls --------------------------------------------------

        /// The first line of an error, without the traceback the log keeps.
        std::string FirstLine(const char* message)
        {
            const char* end = std::strchr(message, '\n');
            return end ? std::string(message, end) : std::string(message);
        }

        /// Runs a registered Lua function: the call's arguments as Lua values, its returns as
        /// the call's. A Lua error fails the call with the error, and counts against the mod.
        void WXL_CALL Invoke(wxl_script_call* call, void* user)
        {
            Function&             function = *static_cast<Function*>(user);
            const wxl_script_api* api      = Api();
            if (!api) return;

            if (!function.mod || !function.mod->Runs())
            {
                const std::string why = function.name + ": the Lua mod that registered it is not running";
                api->fail(call, Str(why));
                return;
            }

            ScriptMod& mod = *function.mod;
            lua_State* L   = mod.State();
            const int  base = lua_gettop(L);
            const uint32_t count = api->arg_count(call);
            if (!lua_checkstack(L, static_cast<int>(count) + 2))
            {
                api->fail(call, Str(function.name + ": too many arguments for the Lua stack"));
                return;
            }

            ++g_calling;
            const profile::Call zone(function.zone);
            lua_rawgeti(L, LUA_REGISTRYINDEX, mod.TracebackRef());
            lua_rawgeti(L, LUA_REGISTRYINDEX, function.ref);
            for (uint32_t i = 0; i < count; ++i)
            {
                switch (api->arg_type(call, i))
                {
                case WXL_VALUE_NUMBER:
                {
                    double number = 0;
                    api->arg_number(call, i, &number);
                    lua_pushnumber(L, number);
                    break;
                }
                case WXL_VALUE_STRING:
                {
                    wxl_str text{nullptr, 0};
                    api->arg_string(call, i, &text);
                    lua_pushlstring(L, text.data ? text.data : "", text.data ? text.size : 0);
                    break;
                }
                case WXL_VALUE_BOOL:
                {
                    wxl_bool flag = WXL_FALSE;
                    api->arg_bool(call, i, &flag);
                    lua_pushboolean(L, flag == WXL_TRUE);
                    break;
                }
                default: lua_pushnil(L); break;
                }
            }

            if (lua_pcall(L, static_cast<int>(count), LUA_MULTRET, base + 1) != 0)
            {
                const char* message = lua_tostring(L, -1) ? lua_tostring(L, -1) : "?";
                api->fail(call, Str(function.name + ": " + FirstLine(message)));
                mod.ReportError("in its script function " + function.name, message);
                lua_settop(L, base);
                --g_calling;
                return;
            }

            const int top = lua_gettop(L);
            for (int i = base + 2; i <= top; ++i)
            {
                switch (lua_type(L, i))
                {
                case LUA_TNUMBER: api->push_number(call, lua_tonumber(L, i)); break;
                case LUA_TBOOLEAN: api->push_bool(call, lua_toboolean(L, i) ? WXL_TRUE : WXL_FALSE); break;
                case LUA_TSTRING:
                {
                    size_t      size = 0;
                    const char* text = lua_tolstring(L, i, &size);
                    api->push_string(call, wxl_str{text, static_cast<uint32_t>(size)});
                    break;
                }
                case LUA_TNIL: api->push_nil(call); break;
                default:
                    api->push_nil(call);
                    if (!function.warned)
                    {
                        function.warned = true;
                        wxl::Log(WXL_LOG_WARN, kTag,
                                 "mod " + mod.Id() + ": script function " + function.name + " returned a " +
                                     lua_typename(L, lua_type(L, i)) +
                                     ", which the client's script cannot take; it receives nil (a function "
                                     "returns numbers, strings, booleans or nil)");
                    }
                    break;
                }
            }
            lua_settop(L, base);
            --g_calling;
        }

        /// Binds a function in the live context when it should be there and is not.
        wxl_result Bind(Function& function, uint32_t context)
        {
            const wxl_script_api* api = Api();
            if (!api || !wxl::detail::Carries(api, &wxl_script_api::register_function)) return WXL_E_UNSUPPORTED;
            if (!(function.contexts & Bit(context)) || (function.bound & Bit(context))) return WXL_OK;
            const wxl_result result = api->register_function(function.name.c_str(), &Invoke, &function);
            if (result == WXL_OK) function.bound |= Bit(context);
            return result;
        }

        wxl_result Declare(const Event& event, uint32_t context)
        {
            const wxl_script_api* api = Api();
            if (!api || !wxl::detail::Carries(api, &wxl_script_api::register_event)) return WXL_E_UNSUPPORTED;
            if (!(event.contexts & Bit(context))) return WXL_OK;
            return api->register_event(event.name.c_str());
        }

        /// Whether a context is live to register on now. A runtime older than live() is asked
        /// anyway, as it always was.
        bool Live()
        {
            const wxl_script_api* api = Api();
            if (!api) return false;
            return !wxl::detail::Carries(api, &wxl_script_api::live) || api->live() == WXL_TRUE;
        }

        /// The live context, or kEveryContext when the service cannot say.
        uint32_t Context()
        {
            const wxl_script_api* api = Api();
            return api && wxl::detail::Carries(api, &wxl_script_api::context) ? api->context() : kEveryContext;
        }

        // --- what a new context and a closing one do -----------------------------------------

        void WXL_CALL OnReady(void*, const void* args)
        {
            if (!args) return;
            const uint32_t context = static_cast<const wxl_script_vm_args*>(args)->context;
            for (Function& function : g_functions)
            {
                if (!function.mod || !function.mod->Runs()) continue;
                const wxl_result result = Bind(function, context);
                if (result != WXL_OK && result != WXL_E_FAILED)
                    wxl::Log(WXL_LOG_WARN, kTag,
                             "mod " + function.mod->Id() + ": script function " + function.name +
                                 " was not registered in the new context (" + ReasonOf(result) +
                                 "): the name is taken there");
            }
            for (const Event& event : g_events)
                if (event.mod && event.mod->Runs()) Declare(event, context);
        }

        void WXL_CALL OnClosing(void*, const void* args)
        {
            if (!args) return;
            const uint32_t context = static_cast<const wxl_script_vm_args*>(args)->context;
            for (Function& function : g_functions) function.bound &= ~Bit(context);
        }

        // --- what a Lua mod calls --------------------------------------------------------------

        int L_RegisterFunction(lua_State* L)
        {
            ScriptMod&     mod      = Self(L);
            const char*    name     = luaL_checkstring(L, 1);
            luaL_checktype(L, 2, LUA_TFUNCTION);
            const uint32_t contexts = CheckContexts(L, 3);
            if (!*name) return luaL_argerror(L, 1, "a name");

            const wxl_script_api* api = Api();
            if (!api || !wxl::detail::Carries(api, &wxl_script_api::register_function))
                return PushResultFailure(L, WXL_E_UNSUPPORTED);

            Function* function = nullptr;
            for (Function& known : g_functions)
                if (known.name == name) function = &known;
            if (function && function->mod && function->mod != &mod)
            {
                wxl::Log(WXL_LOG_WARN, kTag,
                         "mod " + mod.Id() + ": RegisterFunction '" + name + "': mod " + function->mod->Id() +
                             " registered that name already");
                return PushResultFailure(L, WXL_E_INVALID);
            }
            if (!function)
            {
                g_functions.emplace_back();
                function       = &g_functions.back();
                function->name = name;
            }
            if (function->mod == &mod && function->ref != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, function->ref);
            lua_pushvalue(L, 2);
            function->ref      = luaL_ref(L, LUA_REGISTRYINDEX);
            function->mod      = &mod;
            function->contexts = contexts;
            function->warned   = false;
            function->zone     = profile::Location(mod, function->name);

            // Bound now when a context it belongs to is live; otherwise at the next one, by the
            // ScriptVmReady replay, without asking the service now.
            const wxl_result result = Live() ? Bind(*function, Context()) : WXL_OK;
            if (result != WXL_OK && result != WXL_E_FAILED)
            {
                luaL_unref(L, LUA_REGISTRYINDEX, function->ref);
                function->ref = LUA_NOREF;
                function->mod = nullptr;
                wxl::Log(WXL_LOG_WARN, kTag,
                         "mod " + mod.Id() + ": RegisterFunction '" + name + "': " +
                             (result == WXL_E_INVALID ? "the name is taken in the client's script"
                                                      : std::string("refused (") + ReasonOf(result) + ")"));
                return PushResultFailure(L, result);
            }
            lua_pushboolean(L, 1);
            return 1;
        }

        int L_RegisterEvent(lua_State* L)
        {
            ScriptMod&     mod      = Self(L);
            const char*    name     = luaL_checkstring(L, 1);
            const uint32_t contexts = CheckContexts(L, 2);
            if (!*name) return luaL_argerror(L, 1, "a name");

            const wxl_script_api* api = Api();
            if (!api || !wxl::detail::Carries(api, &wxl_script_api::register_event))
                return PushResultFailure(L, WXL_E_UNSUPPORTED);

            bool known = false;
            for (const Event& event : g_events)
                if (event.mod == &mod && event.name == name) known = true;
            if (!known) g_events.push_back(Event{name, &mod, contexts});

            const wxl_result result = Live() && (contexts & Bit(Context())) ? api->register_event(name) : WXL_OK;
            if (result != WXL_OK && result != WXL_E_FAILED) return PushResultFailure(L, result);
            lua_pushboolean(L, 1);
            return 1;
        }

        // --- wxl.storage: the folders each mod adds -----------------------------------------
        //
        // The host sees every folder a Lua mod adds as wxl.lua's. Which mod added it is kept
        // here, so that a mod removes only its own and its folders go when it does; a folder two
        // mods add stays until the last of them lets it go.

        struct Folder
        {
            std::string             key;       ///< the folder, absolute, lower case, backslashes
            std::string             directory; ///< as the first mod gave it
            std::vector<ScriptMod*> holders;
        };

        std::vector<Folder> g_folders;

        const wxl_storage_api* Storage() { return wxl::api::Table<wxl_storage_api>(); }

        /// One folder however it is spelled: absolute against the current directory, which is
        /// the client's folder, as the runtime reads it.
        std::string FolderKey(const std::string& directory)
        {
            std::error_code       ignored;
            std::filesystem::path path = std::filesystem::absolute(std::filesystem::u8path(directory), ignored);
            std::string           key  = path.lexically_normal().u8string();
            for (char& c : key) c = c == '/' ? '\\' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            while (key.size() > 3 && key.back() == '\\') key.pop_back();
            return key;
        }

        /// The string at an argument the script may not leave out.
        std::string CheckDirectory(lua_State* L, const char* function)
        {
            if (lua_isnoneornil(L, 1))
                luaL_error(L, "wxl.storage.%s: 'directory' (argument 1) is missing; it takes a string", function);
            size_t      size = 0;
            const char* text = luaL_checklstring(L, 1, &size);
            return std::string(text, size);
        }

        /// Takes a mod off a folder; the runtime lets the folder go with its last holder.
        void LetGo(size_t index, ScriptMod& mod)
        {
            Folder& folder = g_folders[index];
            folder.holders.erase(std::remove(folder.holders.begin(), folder.holders.end(), &mod), folder.holders.end());
            if (!folder.holders.empty()) return;
            const wxl_storage_api* api = Storage();
            if (api && wxl::detail::Carries(api, &wxl_storage_api::remove_search_path))
                api->remove_search_path(Str(folder.directory));
            g_folders.erase(g_folders.begin() + static_cast<std::ptrdiff_t>(index));
        }

        int L_AddSearchPath(lua_State* L)
        {
            ScriptMod&        mod       = Self(L);
            const std::string directory = CheckDirectory(L, "AddSearchPath");
            if (lua_isnoneornil(L, 2))
                return luaL_error(L, "wxl.storage.AddSearchPath: 'priority' (argument 2) is missing; it takes a "
                                     "whole number");
            const lua_Number priority = luaL_checknumber(L, 2);
            if (std::floor(priority) != priority || priority < INT32_MIN || priority > INT32_MAX)
                return luaL_argerror(L, 2, "a whole number from -2147483648 to 2147483647");

            const wxl_storage_api* api = Storage();
            if (!api || !wxl::detail::Carries(api, &wxl_storage_api::add_search_path))
                return PushResultFailure(L, WXL_E_UNSUPPORTED);
            const wxl_result result = api->add_search_path(Str(directory), static_cast<int32_t>(priority));
            if (result != WXL_OK) return PushResultFailure(L, result);

            const std::string key   = FolderKey(directory);
            const auto        known = std::find_if(g_folders.begin(), g_folders.end(),
                                                   [&](const Folder& folder) { return folder.key == key; });
            if (known == g_folders.end())
                g_folders.push_back(Folder{key, directory, {&mod}});
            else if (std::find(known->holders.begin(), known->holders.end(), &mod) == known->holders.end())
                known->holders.push_back(&mod);
            lua_pushboolean(L, 1);
            return 1;
        }

        int L_RemoveSearchPath(lua_State* L)
        {
            ScriptMod&        mod       = Self(L);
            const std::string directory = CheckDirectory(L, "RemoveSearchPath");
            const wxl_storage_api* api = Storage();
            if (!api || !wxl::detail::Carries(api, &wxl_storage_api::remove_search_path))
                return PushResultFailure(L, WXL_E_UNSUPPORTED);

            const std::string key = FolderKey(directory);
            for (size_t i = 0; i < g_folders.size(); ++i)
            {
                const Folder& folder = g_folders[i];
                if (folder.key != key ||
                    std::find(folder.holders.begin(), folder.holders.end(), &mod) == folder.holders.end())
                    continue;
                LetGo(i, mod);
                lua_pushboolean(L, 1);
                return 1;
            }
            return PushResultFailure(L, WXL_E_NOT_FOUND);
        }

        // --- wxl.storage: the names each mod redirects ----------------------------------------
        //
        // The host sees every redirect of a Lua mod as wxl.lua's, one per name. Each mod's are kept
        // here in the order they were made, so that the runtime holds the latest, taking one back
        // uncovers the mod's before it, and a mod's go when it does.

        struct Redirected
        {
            std::string key;  ///< the name, lower case, backslashes
            std::string from; ///< as the first mod gave it
            std::vector<std::pair<ScriptMod*, std::string>> holders; ///< oldest first
        };

        std::vector<Redirected> g_redirects;

        std::string NameKey(const std::string& name)
        {
            std::string key = name;
            for (char& c : key) c = c == '/' ? '\\' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return key;
        }

        /// A string argument the script may not leave out.
        std::string CheckName(lua_State* L, int index, const char* function, const char* parameter)
        {
            if (lua_isnoneornil(L, index))
                luaL_error(L, "wxl.storage.%s: '%s' (argument %d) is missing; it takes a string", function, parameter,
                           index);
            size_t      size = 0;
            const char* text = luaL_checklstring(L, index, &size);
            return std::string(text, size);
        }

        /// Hands the runtime what a name leads to now: the latest holder's, or nothing.
        wxl_result Settle(const Redirected& entry)
        {
            const wxl_storage_api* api = Storage();
            if (!api || !wxl::detail::Carries(api, &wxl_storage_api::unredirect)) return WXL_E_UNSUPPORTED;
            if (entry.holders.empty()) return api->unredirect(Str(entry.from));
            return api->redirect(Str(entry.from), Str(entry.holders.back().second));
        }

        /// Takes a mod off a redirected name; the runtime follows the holder left on top.
        void LetGoName(size_t index, ScriptMod& mod)
        {
            Redirected& entry  = g_redirects[index];
            const bool  latest = !entry.holders.empty() && entry.holders.back().first == &mod;
            entry.holders.erase(std::remove_if(entry.holders.begin(), entry.holders.end(),
                                               [&](const auto& holder) { return holder.first == &mod; }),
                                entry.holders.end());
            if (latest) Settle(entry);
            if (entry.holders.empty()) g_redirects.erase(g_redirects.begin() + static_cast<std::ptrdiff_t>(index));
        }

        int L_Redirect(lua_State* L)
        {
            ScriptMod&        mod  = Self(L);
            const std::string from = CheckName(L, 1, "Redirect", "from");
            const std::string to   = CheckName(L, 2, "Redirect", "to");
            const wxl_storage_api* api = Storage();
            if (!api || !wxl::detail::Carries(api, &wxl_storage_api::redirect))
                return PushResultFailure(L, WXL_E_UNSUPPORTED);
            const wxl_result result = api->redirect(Str(from), Str(to));
            if (result != WXL_OK) return PushResultFailure(L, result);

            const std::string key   = NameKey(from);
            auto              known = std::find_if(g_redirects.begin(), g_redirects.end(),
                                                   [&](const Redirected& entry) { return entry.key == key; });
            if (known == g_redirects.end())
            {
                g_redirects.push_back(Redirected{key, from, {}});
                known = g_redirects.end() - 1;
            }
            known->holders.erase(std::remove_if(known->holders.begin(), known->holders.end(),
                                                [&](const auto& holder) { return holder.first == &mod; }),
                                 known->holders.end());
            known->holders.emplace_back(&mod, to);
            lua_pushboolean(L, 1);
            return 1;
        }

        int L_Unredirect(lua_State* L)
        {
            ScriptMod&        mod  = Self(L);
            const std::string from = CheckName(L, 1, "Unredirect", "from");
            const wxl_storage_api* api = Storage();
            if (!api || !wxl::detail::Carries(api, &wxl_storage_api::unredirect))
                return PushResultFailure(L, WXL_E_UNSUPPORTED);

            const std::string key = NameKey(from);
            for (size_t i = 0; i < g_redirects.size(); ++i)
            {
                const Redirected& entry = g_redirects[i];
                if (entry.key != key ||
                    std::none_of(entry.holders.begin(), entry.holders.end(),
                                 [&](const auto& holder) { return holder.first == &mod; }))
                    continue;
                LetGoName(i, mod);
                lua_pushboolean(L, 1);
                return 1;
            }
            return PushResultFailure(L, WXL_E_NOT_FOUND);
        }


        // --- wxl.storage: the names each mod serves ------------------------------------------
        //
        // The host sees what every Lua mod serves as wxl.lua's, one set of bytes per name. Each
        // mod's are kept here, with their priority, so that the runtime serves the one that should
        // win -- the highest priority, then the latest --, and a mod's go when it does.

        struct ServedName
        {
            ScriptMod*  mod;
            std::string bytes;
            int32_t     priority;
        };

        struct Served
        {
            std::string             key;  ///< the name, lower case, backslashes
            std::string             path; ///< as the first mod gave it
            std::vector<ServedName> holders; ///< oldest first
        };

        std::vector<Served> g_served;

        /// The holder that wins a name: the highest priority, the latest of equals.
        const ServedName* Winner(const Served& entry)
        {
            const ServedName* best = nullptr;
            for (const ServedName& holder : entry.holders)
                if (!best || holder.priority >= best->priority) best = &holder;
            return best;
        }

        wxl_result SettleServed(const Served& entry)
        {
            const wxl_storage_api* api = Storage();
            if (!api || !wxl::detail::Carries(api, &wxl_storage_api::unserve)) return WXL_E_UNSUPPORTED;
            const ServedName* best = Winner(entry);
            if (!best) return api->unserve(Str(entry.path));
            return api->serve(Str(entry.path), best->bytes.data(), static_cast<uint32_t>(best->bytes.size()),
                              best->priority);
        }

        void LetGoServed(size_t index, ScriptMod& mod)
        {
            Served& entry = g_served[index];
            entry.holders.erase(std::remove_if(entry.holders.begin(), entry.holders.end(),
                                               [&](const ServedName& holder) { return holder.mod == &mod; }),
                                entry.holders.end());
            SettleServed(entry);
            if (entry.holders.empty()) g_served.erase(g_served.begin() + static_cast<std::ptrdiff_t>(index));
        }

        int L_Serve(lua_State* L)
        {
            ScriptMod&        mod  = Self(L);
            const std::string path = CheckName(L, 1, "Serve", "path");
            if (lua_isnoneornil(L, 2))
                luaL_error(L, "wxl.storage.Serve: 'data' (argument 2) is missing; it takes a string");
            size_t      size = 0;
            const char* data = luaL_checklstring(L, 2, &size);
            if (lua_isnoneornil(L, 3))
                luaL_error(L, "wxl.storage.Serve: 'priority' (argument 3) is missing; it takes a whole number");
            const lua_Integer priority = luaL_checkinteger(L, 3);
            if (size > UINT32_MAX) return PushResultFailure(L, WXL_E_INVALID);
            const wxl_storage_api* api = Storage();
            if (!api || !wxl::detail::Carries(api, &wxl_storage_api::serve)) return PushResultFailure(L, WXL_E_UNSUPPORTED);

            const std::string key   = NameKey(path);
            auto              known = std::find_if(g_served.begin(), g_served.end(),
                                                   [&](const Served& entry) { return entry.key == key; });
            Served candidate{key, path, {}};
            Served& entry = known == g_served.end() ? candidate : *known;
            std::vector<ServedName> before = entry.holders;
            entry.holders.erase(std::remove_if(entry.holders.begin(), entry.holders.end(),
                                               [&](const ServedName& holder) { return holder.mod == &mod; }),
                                entry.holders.end());
            entry.holders.push_back(ServedName{&mod, std::string(data, size), static_cast<int32_t>(priority)});
            const wxl_result result = SettleServed(entry);
            if (result != WXL_OK)
            {
                entry.holders = std::move(before);
                return PushResultFailure(L, result);
            }
            if (known == g_served.end()) g_served.push_back(std::move(candidate));
            lua_pushboolean(L, 1);
            return 1;
        }

        int L_Unserve(lua_State* L)
        {
            ScriptMod&        mod  = Self(L);
            const std::string path = CheckName(L, 1, "Unserve", "path");
            const wxl_storage_api* api = Storage();
            if (!api || !wxl::detail::Carries(api, &wxl_storage_api::unserve)) return PushResultFailure(L, WXL_E_UNSUPPORTED);

            const std::string key = NameKey(path);
            for (size_t i = 0; i < g_served.size(); ++i)
            {
                const Served& entry = g_served[i];
                if (entry.key != key || std::none_of(entry.holders.begin(), entry.holders.end(),
                                                     [&](const ServedName& holder) { return holder.mod == &mod; }))
                    continue;
                LetGoServed(i, mod);
                lua_pushboolean(L, 1);
                return 1;
            }
            return PushResultFailure(L, WXL_E_NOT_FOUND);
        }

        // --- wxl.footprint: what each mod states about memory -------------------------------
        //
        // The host sees every report and threshold of a Lua mod as wxl.lua's. Each mod's are kept
        // here, named after the mod, so that they go with it.

        struct Reported
        {
            ScriptMod*  mod;
            std::string name; ///< "<mod id>/<name>", as the runtime shows it
        };

        struct Threshold
        {
            ScriptMod* mod;
            uint64_t   bytes;
        };

        std::vector<Reported>  g_reported;
        std::vector<Threshold> g_thresholds;

        const wxl_footprint_api* Footprint() { return wxl::api::Table<wxl_footprint_api>(); }

        /// A whole number argument the script may not leave out, from 0 up.
        uint64_t CheckCount(lua_State* L, int index, const char* function, const char* parameter)
        {
            if (lua_isnoneornil(L, index))
                luaL_error(L, "wxl.footprint.%s: '%s' (argument %d) is missing; it takes a whole number", function,
                           parameter, index);
            const lua_Number value = luaL_checknumber(L, index);
            if (std::floor(value) != value || value < 0 || value > 9007199254740992.0)
                luaL_argerror(L, index, "a whole number from 0");
            return static_cast<uint64_t>(value);
        }

        /// Reports one name for a mod, and keeps it while it holds anything.
        wxl_result ReportAs(ScriptMod& mod, const std::string& name, uint64_t bytes, uint64_t count)
        {
            const wxl_footprint_api* api = Footprint();
            if (!api || !wxl::detail::Carries(api, &wxl_footprint_api::report)) return WXL_E_UNSUPPORTED;
            const wxl_result result = api->report(Str(name), bytes, count);
            if (result != WXL_OK) return result;
            const auto known = std::find_if(g_reported.begin(), g_reported.end(),
                                            [&](const Reported& r) { return r.mod == &mod && r.name == name; });
            if (bytes == 0 && count == 0)
            {
                if (known != g_reported.end()) g_reported.erase(known);
            }
            else if (known == g_reported.end())
                g_reported.push_back(Reported{&mod, name});
            return WXL_OK;
        }

        /// Hands the runtime the highest threshold any Lua mod holds, 0 when none does.
        wxl_result WatchHighest()
        {
            const wxl_footprint_api* api = Footprint();
            if (!api || !wxl::detail::Carries(api, &wxl_footprint_api::watch_pressure)) return WXL_E_UNSUPPORTED;
            uint64_t highest = 0;
            for (const Threshold& threshold : g_thresholds) highest = std::max(highest, threshold.bytes);
            return api->watch_pressure(highest);
        }

        int L_Snapshot(lua_State* L)
        {
            const wxl_footprint_api* api = Footprint();
            if (!api || !wxl::detail::Carries(api, &wxl_footprint_api::snapshot))
                return PushResultFailure(L, WXL_E_UNSUPPORTED);
            if (const Runtime* runtime = Runtime::Instance())
                for (const std::unique_ptr<ScriptMod>& mod : runtime->Mods())
                {
                    lua_State* state = mod->State();
                    if (!state || !mod->Runs()) continue;
                    const uint64_t bytes = static_cast<uint64_t>(lua_gc(state, LUA_GCCOUNT, 0)) * 1024u +
                                           static_cast<uint64_t>(lua_gc(state, LUA_GCCOUNTB, 0));
                    ReportAs(*mod, mod->Id() + "/heap", bytes, 0);
                }
            const wxl_result result = api->snapshot();
            if (result != WXL_OK) return PushResultFailure(L, result);
            lua_pushboolean(L, 1);
            return 1;
        }

        int L_Report(lua_State* L)
        {
            ScriptMod& mod = Self(L);
            if (lua_isnoneornil(L, 1))
                return luaL_error(L, "wxl.footprint.Report: 'name' (argument 1) is missing; it takes a string");
            size_t            size  = 0;
            const char*       text  = luaL_checklstring(L, 1, &size);
            const uint64_t    bytes = CheckCount(L, 2, "Report", "bytes");
            const uint64_t    count = CheckCount(L, 3, "Report", "count");
            if (size == 0) return PushResultFailure(L, WXL_E_INVALID);
            const wxl_result result = ReportAs(mod, mod.Id() + "/" + std::string(text, size), bytes, count);
            if (result != WXL_OK) return PushResultFailure(L, result);
            lua_pushboolean(L, 1);
            return 1;
        }

        int L_WatchPressure(lua_State* L)
        {
            ScriptMod&     mod   = Self(L);
            const uint64_t bytes = CheckCount(L, 1, "WatchPressure", "bytes");
            g_thresholds.erase(std::remove_if(g_thresholds.begin(), g_thresholds.end(),
                                              [&](const Threshold& t) { return t.mod == &mod; }),
                               g_thresholds.end());
            if (bytes) g_thresholds.push_back(Threshold{&mod, bytes});
            const wxl_result result = WatchHighest();
            if (result != WXL_OK) return PushResultFailure(L, result);
            lua_pushboolean(L, 1);
            return 1;
        }

        /// A mod's reports and threshold go with it.
        void ReleaseFootprint(ScriptMod& mod)
        {
            const wxl_footprint_api* api = Footprint();
            for (size_t i = g_reported.size(); i-- > 0;)
            {
                if (g_reported[i].mod != &mod) continue;
                if (api && wxl::detail::Carries(api, &wxl_footprint_api::report))
                    api->report(Str(g_reported[i].name), 0, 0);
                g_reported.erase(g_reported.begin() + static_cast<std::ptrdiff_t>(i));
            }
            const size_t before = g_thresholds.size();
            g_thresholds.erase(std::remove_if(g_thresholds.begin(), g_thresholds.end(),
                                              [&](const Threshold& t) { return t.mod == &mod; }),
                               g_thresholds.end());
            if (g_thresholds.size() != before) WatchHighest();
        }

        // --- calling back into a mod -----------------------------------------------------------

        /// Calls the function a mod keeps at @p ref with what @p push leaves on the stack (it
        /// answers how many), under the mod's traceback. @p after reads the returns, from the
        /// index it is given to the top, before the stack is put back. False on a Lua error,
        /// which counts against the mod.
        template <class Push, class After>
        bool CallBack(ScriptMod& mod, int ref, const char* what, Push&& push, After&& after)
        {
            if (!mod.Runs() || ref == LUA_NOREF) return false;
            lua_State* L    = mod.State();
            const int  base = lua_gettop(L);
            if (!lua_checkstack(L, 8)) return false;
            lua_rawgeti(L, LUA_REGISTRYINDEX, mod.TracebackRef());
            lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
            const int count = push(L);
            ++g_calling;
            const bool ok = lua_pcall(L, count, LUA_MULTRET, base + 1) == 0;
            --g_calling;
            if (!ok)
            {
                const char* message = lua_tostring(L, -1) ? lua_tostring(L, -1) : "?";
                mod.ReportError(what, message);
            }
            else
            {
                after(L, base + 2);
            }
            lua_settop(L, base);
            return ok;
        }

        bool CallBack(ScriptMod& mod, int ref, const char* what, const std::function<int(lua_State*)>& push)
        {
            return CallBack(mod, ref, what, push, [](lua_State*, int) {});
        }

        /// Pushes values that cross the boundary as Lua values; how many.
        int PushValues(lua_State* L, const wxl_value* values, uint32_t count)
        {
            if (!values || !lua_checkstack(L, static_cast<int>(count) + 4)) return 0;
            for (uint32_t i = 0; i < count; ++i)
            {
                const wxl_value& value = values[i];
                switch (value.type)
                {
                case WXL_VALUE_NUMBER: lua_pushnumber(L, value.as.number); break;
                case WXL_VALUE_STRING:
                    lua_pushlstring(L, value.as.string.data ? value.as.string.data : "",
                                    value.as.string.data ? value.as.string.size : 0);
                    break;
                case WXL_VALUE_BOOL: lua_pushboolean(L, value.as.boolean == WXL_TRUE); break;
                default: lua_pushnil(L); break;
                }
            }
            return static_cast<int>(count);
        }

        /// The Lua values from @p first to @p last as values that cross the boundary: numbers,
        /// strings, booleans; anything else, nil. Strings point into the state, valid while the
        /// values stay on the stack.
        std::vector<wxl_value> ValuesOf(lua_State* L, int first, int last)
        {
            std::vector<wxl_value> values;
            for (int i = first; i <= last; ++i)
            {
                wxl_value value{};
                switch (lua_type(L, i))
                {
                case LUA_TNUMBER:
                    value.type      = WXL_VALUE_NUMBER;
                    value.as.number = lua_tonumber(L, i);
                    break;
                case LUA_TSTRING:
                {
                    size_t      size = 0;
                    const char* text = lua_tolstring(L, i, &size);
                    value.type       = WXL_VALUE_STRING;
                    value.as.string  = wxl_str{text, static_cast<uint32_t>(size)};
                    break;
                }
                case LUA_TBOOLEAN:
                    value.type       = WXL_VALUE_BOOL;
                    value.as.boolean = lua_toboolean(L, i) ? WXL_TRUE : WXL_FALSE;
                    break;
                default: value.type = WXL_VALUE_NIL; break;
                }
                values.push_back(value);
            }
            return values;
        }

        /// A request number argument: a whole number from 1.
        uint64_t CheckRequest(lua_State* L, int index, const char* function)
        {
            if (lua_isnoneornil(L, index))
                luaL_error(L, "wxl.relay.%s: 'request' (argument %d) is missing; it takes a whole number", function,
                           index);
            const lua_Number value = luaL_checknumber(L, index);
            if (std::floor(value) != value || value < 1 || value > 9007199254740992.0)
                luaL_argerror(L, index, "a request number");
            return static_cast<uint64_t>(value);
        }

        // --- wxl.storage: the files each mod overrides ----------------------------------------
        //
        // The host sees every override of a Lua mod as wxl.lua's, one per name. Each mod's are kept
        // here in the order they were made, as the redirects are.

        std::vector<Redirected> g_overrides;

        wxl_result SettleOverride(const Redirected& entry)
        {
            const wxl_storage_api* api = Storage();
            if (!api || !wxl::detail::Carries(api, &wxl_storage_api::restore_file)) return WXL_E_UNSUPPORTED;
            if (entry.holders.empty()) return api->restore_file(Str(entry.from));
            return api->override_file(Str(entry.from), Str(entry.holders.back().second));
        }

        void LetGoOverride(size_t index, ScriptMod& mod)
        {
            Redirected& entry  = g_overrides[index];
            const bool  latest = !entry.holders.empty() && entry.holders.back().first == &mod;
            entry.holders.erase(std::remove_if(entry.holders.begin(), entry.holders.end(),
                                               [&](const auto& holder) { return holder.first == &mod; }),
                                entry.holders.end());
            if (latest) SettleOverride(entry);
            if (entry.holders.empty()) g_overrides.erase(g_overrides.begin() + static_cast<std::ptrdiff_t>(index));
        }

        int L_OverrideFile(lua_State* L)
        {
            ScriptMod&        mod  = Self(L);
            const std::string path = CheckName(L, 1, "OverrideFile", "path");
            const std::string with = CheckName(L, 2, "OverrideFile", "with");
            const wxl_storage_api* api = Storage();
            if (!api || !wxl::detail::Carries(api, &wxl_storage_api::override_file))
                return PushResultFailure(L, WXL_E_UNSUPPORTED);
            const wxl_result result = api->override_file(Str(path), Str(with));
            if (result != WXL_OK) return PushResultFailure(L, result);

            const std::string key   = NameKey(path);
            auto              known = std::find_if(g_overrides.begin(), g_overrides.end(),
                                                   [&](const Redirected& entry) { return entry.key == key; });
            if (known == g_overrides.end())
            {
                g_overrides.push_back(Redirected{key, path, {}});
                known = g_overrides.end() - 1;
            }
            known->holders.erase(std::remove_if(known->holders.begin(), known->holders.end(),
                                                [&](const auto& holder) { return holder.first == &mod; }),
                                 known->holders.end());
            known->holders.emplace_back(&mod, with);
            lua_pushboolean(L, 1);
            return 1;
        }

        int L_RestoreFile(lua_State* L)
        {
            ScriptMod&        mod  = Self(L);
            const std::string path = CheckName(L, 1, "RestoreFile", "path");
            const wxl_storage_api* api = Storage();
            if (!api || !wxl::detail::Carries(api, &wxl_storage_api::restore_file))
                return PushResultFailure(L, WXL_E_UNSUPPORTED);
            const std::string key = NameKey(path);
            for (size_t i = 0; i < g_overrides.size(); ++i)
            {
                const Redirected& entry = g_overrides[i];
                if (entry.key != key || std::none_of(entry.holders.begin(), entry.holders.end(),
                                                     [&](const auto& holder) { return holder.first == &mod; }))
                    continue;
                LetGoOverride(i, mod);
                lua_pushboolean(L, 1);
                return 1;
            }
            return PushResultFailure(L, WXL_E_NOT_FOUND);
        }

        // --- wxl.storage: scoped names, hashes, reloads -----------------------------------------

        int L_ScopedName(lua_State* L)
        {
            ScriptMod&        mod  = Self(L);
            const std::string path = CheckName(L, 1, "ScopedName", "path");
            std::string       owner = mod.Id();
            if (!lua_isnoneornil(L, 2))
            {
                size_t      size = 0;
                const char* text = luaL_checklstring(L, 2, &size);
                owner.assign(text, size);
            }
            const wxl_storage_api* api = Storage();
            if (!api || !wxl::detail::Carries(api, &wxl_storage_api::scoped_name))
                return PushResultFailure(L, WXL_E_UNSUPPORTED);
            const wxl_str scoped = api->scoped_name(Str(owner), Str(path));
            if (!scoped.data || !scoped.size) return PushResultFailure(L, WXL_E_INVALID);
            lua_pushlstring(L, scoped.data, scoped.size);
            return 1;
        }

        const char* LayerName(uint32_t layer)
        {
            switch (layer)
            {
            case WXL_FILE_LAYER_ARCHIVE: return "archive";
            case WXL_FILE_LAYER_DISK: return "disk";
            case WXL_FILE_LAYER_FOLDER: return "folder";
            case WXL_FILE_LAYER_PROVIDER: return "provider";
            case WXL_FILE_LAYER_SERVED: return "served";
            default: return "none";
            }
        }

        /// A file hash as the table a script reads.
        void PushHash(lua_State* L, const wxl_file_hash& hash)
        {
            static const char kDigits[] = "0123456789abcdef";
            char              hex[32];
            for (int i = 0; i < 16; ++i)
            {
                hex[2 * i]     = kDigits[hash.md5[i] >> 4];
                hex[2 * i + 1] = kDigits[hash.md5[i] & 0xF];
            }
            lua_createtable(L, 0, 4);
            lua_pushlstring(L, hex, sizeof hex);
            lua_setfield(L, -2, "md5");
            lua_pushnumber(L, static_cast<lua_Number>(hash.size));
            lua_setfield(L, -2, "size");
            lua_pushstring(L, LayerName(hash.layer));
            lua_setfield(L, -2, "layer");
            lua_pushlstring(L, hash.source.data ? hash.source.data : "", hash.source.data ? hash.source.size : 0);
            lua_setfield(L, -2, "source");
        }

        int L_HashFile(lua_State* L)
        {
            const std::string path  = CheckName(L, 1, "HashFile", "path");
            const bool        fresh = lua_toboolean(L, 2) != 0;
            const wxl_storage_api* api = Storage();
            if (!api || !wxl::detail::Carries(api, &wxl_storage_api::hash_file))
                return PushResultFailure(L, WXL_E_UNSUPPORTED);
            wxl_file_hash hash{};
            hash.struct_size        = sizeof hash;
            const wxl_result result = api->hash_file(Str(path), fresh ? WXL_HASH_FRESH : 0u, &hash);
            if (result != WXL_OK) return PushResultFailure(L, result);
            PushHash(L, hash);
            return 1;
        }

        /// A hash a Lua mod waits for. Kept until the runtime answers it, which it always does
        /// while wxl.lua runs: a mod that went in between is forgotten, not the wait.
        struct HashWait
        {
            ScriptMod* mod;
            int        ref;
        };

        std::vector<std::unique_ptr<HashWait>> g_hashWaits;

        void WXL_CALL HashAnswered(void* user, uint64_t, wxl_result result, const wxl_file_hash* hash)
        {
            const auto found = std::find_if(g_hashWaits.begin(), g_hashWaits.end(),
                                            [&](const std::unique_ptr<HashWait>& wait) { return wait.get() == user; });
            if (found == g_hashWaits.end()) return;
            const std::unique_ptr<HashWait> wait = std::move(*found);
            g_hashWaits.erase(found);
            if (!wait->mod) return;
            ScriptMod& mod = *wait->mod;
            CallBack(mod, wait->ref, "in its HashFileAsync function", [&](lua_State* L) {
                if (result == WXL_OK && hash)
                {
                    PushHash(L, *hash);
                    return 1;
                }
                lua_pushnil(L);
                lua_pushstring(L, ReasonOf(result == WXL_OK ? WXL_E_FAILED : result));
                return 2;
            });
            if (mod.State()) luaL_unref(mod.State(), LUA_REGISTRYINDEX, wait->ref);
        }

        int L_HashFileAsync(lua_State* L)
        {
            ScriptMod&        mod  = Self(L);
            const std::string path = CheckName(L, 1, "HashFileAsync", "path");
            if (lua_isnoneornil(L, 2))
                return luaL_error(L, "wxl.storage.HashFileAsync: 'fn' (argument 2) is missing; it takes a function");
            luaL_checktype(L, 2, LUA_TFUNCTION);
            const bool fresh = lua_toboolean(L, 3) != 0;
            const wxl_storage_api* api = Storage();
            if (!api || !wxl::detail::Carries(api, &wxl_storage_api::hash_file_async))
                return PushResultFailure(L, WXL_E_UNSUPPORTED);
            lua_pushvalue(L, 2);
            auto wait  = std::make_unique<HashWait>();
            wait->mod  = &mod;
            wait->ref  = luaL_ref(L, LUA_REGISTRYINDEX);
            HashWait* kept = wait.get();
            g_hashWaits.push_back(std::move(wait));
            uint64_t         request = 0;
            const wxl_result result  = api->hash_file_async(Str(path), fresh ? WXL_HASH_FRESH : 0u, &HashAnswered, kept,
                                                            &request);
            if (result != WXL_OK)
            {
                luaL_unref(L, LUA_REGISTRYINDEX, kept->ref);
                g_hashWaits.erase(std::find_if(g_hashWaits.begin(), g_hashWaits.end(),
                                               [&](const std::unique_ptr<HashWait>& w) { return w.get() == kept; }));
                return PushResultFailure(L, result);
            }
            lua_pushnumber(L, static_cast<lua_Number>(request));
            return 1;
        }

        int L_ReloadAsset(lua_State* L)
        {
            const std::string path = CheckName(L, 1, "ReloadAsset", "path");
            const wxl_storage_api* api = Storage();
            if (!api || !wxl::detail::Carries(api, &wxl_storage_api::reload_asset))
                return PushResultFailure(L, WXL_E_UNSUPPORTED);
            wxl_asset_reload done{};
            done.struct_size        = sizeof done;
            const wxl_result result = api->reload_asset(Str(path), &done);
            if (result != WXL_OK) return PushResultFailure(L, result);
            lua_createtable(L, 0, 4);
            lua_pushnumber(L, done.textures);
            lua_setfield(L, -2, "textures");
            lua_pushnumber(L, done.models);
            lua_setfield(L, -2, "models");
            lua_pushnumber(L, done.objects);
            lua_setfield(L, -2, "objects");
            lua_pushnumber(L, done.busy);
            lua_setfield(L, -2, "busy");
            return 1;
        }

        /// What a mod held of hashes and overrides goes with it.
        void ReleaseStorage(ScriptMod& mod)
        {
            for (size_t i = g_overrides.size(); i-- > 0;)
                if (std::any_of(g_overrides[i].holders.begin(), g_overrides[i].holders.end(),
                                [&](const auto& holder) { return holder.first == &mod; }))
                    LetGoOverride(i, mod);
            for (const std::unique_ptr<HashWait>& wait : g_hashWaits)
            {
                if (wait->mod != &mod) continue;
                if (mod.State()) luaL_unref(mod.State(), LUA_REGISTRYINDEX, wait->ref);
                wait->mod = nullptr;
            }
        }

        // --- wxl.relay: the services each mod publishes and the requests it waits on ----------
        //
        // The host sees every Lua mod's services and requests as wxl.lua's. Each mod's are kept
        // here: a mod that goes takes its services back (their requests fail for their callers)
        // and cancels its requests (their services are told).

        const wxl_relay_api* Relay() { return wxl::api::Table<wxl_relay_api>(); }

        struct Published
        {
            ScriptMod*         mod;
            std::string        name;
            int                fn;
            int                cancelled; ///< LUA_NOREF for none
            wxl_relay_endpoint table{};
        };

        std::vector<std::unique_ptr<Published>> g_published;

        /// A request a Lua mod made and waits on. Kept until answered, or cancelled.
        struct Waiting
        {
            ScriptMod*  mod;
            int         fn;
            wxl_request request = 0;
        };

        std::vector<std::unique_ptr<Waiting>> g_waiting;

        void WXL_CALL RelayRequested(void* user, wxl_request request, wxl_str method, const wxl_value* args,
                                     uint32_t count)
        {
            const auto found = std::find_if(g_published.begin(), g_published.end(),
                                            [&](const std::unique_ptr<Published>& p) { return p.get() == user; });
            if (found == g_published.end()) return;
            Published& published = **found;
            ScriptMod& mod       = *published.mod;
            const std::string what = "in its wxl.relay service " + published.name;
            const bool ok = CallBack(
                mod, published.fn, what.c_str(),
                [&](lua_State* L) {
                    lua_pushnumber(L, static_cast<lua_Number>(request));
                    lua_pushlstring(L, method.data ? method.data : "", method.data ? method.size : 0);
                    return 2 + PushValues(L, args, count);
                },
                [&](lua_State* L, int first) {
                    // Values returned are the answer; none, an answer to come.
                    const int last = lua_gettop(L);
                    if (last < first) return;
                    const std::vector<wxl_value> values = ValuesOf(L, first, last);
                    if (const wxl_relay_api* api = Relay())
                        api->answer(request, values.data(), static_cast<uint32_t>(values.size()));
                });
            if (!ok)
                if (const wxl_relay_api* api = Relay()) api->fail(request, Str("the service's Lua function errored"));
        }

        void WXL_CALL RelayCancelled(void* user, wxl_request request)
        {
            const auto found = std::find_if(g_published.begin(), g_published.end(),
                                            [&](const std::unique_ptr<Published>& p) { return p.get() == user; });
            if (found == g_published.end() || (*found)->cancelled == LUA_NOREF) return;
            Published& published = **found;
            const std::string what = "in its wxl.relay cancel function of " + published.name;
            CallBack(*published.mod, published.cancelled, what.c_str(), [&](lua_State* L) {
                lua_pushnumber(L, static_cast<lua_Number>(request));
                return 1;
            });
        }

        void WXL_CALL RelayReplied(void* user, wxl_request, wxl_result result, const wxl_value* values, uint32_t count)
        {
            const auto found = std::find_if(g_waiting.begin(), g_waiting.end(),
                                            [&](const std::unique_ptr<Waiting>& w) { return w.get() == user; });
            if (found == g_waiting.end()) return;
            const std::unique_ptr<Waiting> waiting = std::move(*found);
            g_waiting.erase(found);
            if (!waiting->mod) return;
            ScriptMod& mod = *waiting->mod;
            CallBack(mod, waiting->fn, "in its wxl.relay answer function", [&](lua_State* L) {
                if (result == WXL_OK)
                {
                    lua_pushboolean(L, 1);
                    return 1 + PushValues(L, values, count);
                }
                lua_pushboolean(L, 0);
                if (result == WXL_E_FAILED && values && count && values[0].type == WXL_VALUE_STRING)
                    PushValues(L, values, 1);
                else
                    lua_pushstring(L, ReasonOf(result));
                return 2;
            });
            if (mod.State()) luaL_unref(mod.State(), LUA_REGISTRYINDEX, waiting->fn);
        }

        std::string CheckRelayName(lua_State* L, int index, const char* function, const char* parameter)
        {
            if (lua_isnoneornil(L, index))
                luaL_error(L, "wxl.relay.%s: '%s' (argument %d) is missing; it takes a string", function, parameter,
                           index);
            size_t      size = 0;
            const char* text = luaL_checklstring(L, index, &size);
            return std::string(text, size);
        }

        /// Takes one service of a Lua mod back from the runtime and forgets it.
        void Withdraw(size_t index)
        {
            const std::unique_ptr<Published> published = std::move(g_published[index]);
            g_published.erase(g_published.begin() + static_cast<std::ptrdiff_t>(index));
            if (const wxl_relay_api* api = Relay()) api->unpublish(Str(published->name));
            if (lua_State* L = published->mod->State())
            {
                luaL_unref(L, LUA_REGISTRYINDEX, published->fn);
                luaL_unref(L, LUA_REGISTRYINDEX, published->cancelled);
            }
        }

        int L_Publish(lua_State* L)
        {
            ScriptMod&        mod  = Self(L);
            const std::string name = CheckRelayName(L, 1, "Publish", "name");
            if (lua_isnoneornil(L, 2))
                return luaL_error(L, "wxl.relay.Publish: 'fn' (argument 2) is missing; it takes a function");
            luaL_checktype(L, 2, LUA_TFUNCTION);
            if (!lua_isnoneornil(L, 3)) luaL_checktype(L, 3, LUA_TFUNCTION);
            const wxl_relay_api* api = Relay();
            if (!api || !wxl::detail::Carries(api, &wxl_relay_api::publish)) return PushResultFailure(L, WXL_E_UNSUPPORTED);

            // The runtime sees every Lua mod as one: a name another Lua mod publishes is refused
            // here, and the mod's own is published again, its old functions let go.
            const std::string          key = NameKey(name);
            std::unique_ptr<Published> replaced; // kept until the runtime holds the new entry
            for (size_t i = 0; i < g_published.size(); ++i)
            {
                if (NameKey(g_published[i]->name) != key) continue;
                if (g_published[i]->mod != &mod) return PushResultFailure(L, WXL_E_INVALID);
                // Published again: the runtime's entry is replaced below, its requests kept.
                luaL_unref(L, LUA_REGISTRYINDEX, g_published[i]->fn);
                luaL_unref(L, LUA_REGISTRYINDEX, g_published[i]->cancelled);
                replaced = std::move(g_published[i]);
                g_published.erase(g_published.begin() + static_cast<std::ptrdiff_t>(i));
                break;
            }

            auto published  = std::make_unique<Published>();
            published->mod  = &mod;
            published->name = name;
            lua_pushvalue(L, 2);
            published->fn = luaL_ref(L, LUA_REGISTRYINDEX);
            if (!lua_isnoneornil(L, 3))
            {
                lua_pushvalue(L, 3);
                published->cancelled = luaL_ref(L, LUA_REGISTRYINDEX);
            }
            else
            {
                published->cancelled = LUA_NOREF;
            }
            published->table.struct_size = sizeof published->table;
            published->table.user        = published.get();
            published->table.request     = &RelayRequested;
            published->table.cancelled   = &RelayCancelled;
            const wxl_result result      = api->publish(Str(name), &published->table);
            if (result != WXL_OK)
            {
                if (replaced) api->unpublish(Str(name)); // the runtime must not keep the old entry
                luaL_unref(L, LUA_REGISTRYINDEX, published->fn);
                luaL_unref(L, LUA_REGISTRYINDEX, published->cancelled);
                return PushResultFailure(L, result);
            }
            g_published.push_back(std::move(published));
            lua_pushboolean(L, 1);
            return 1;
        }

        int L_Unpublish(lua_State* L)
        {
            ScriptMod&        mod  = Self(L);
            const std::string name = CheckRelayName(L, 1, "Unpublish", "name");
            const std::string key  = NameKey(name);
            for (size_t i = 0; i < g_published.size(); ++i)
            {
                if (g_published[i]->mod != &mod || NameKey(g_published[i]->name) != key) continue;
                Withdraw(i);
                lua_pushboolean(L, 1);
                return 1;
            }
            return PushResultFailure(L, WXL_E_NOT_FOUND);
        }

        int L_Call(lua_State* L)
        {
            ScriptMod&        mod    = Self(L);
            const std::string name   = CheckRelayName(L, 1, "Call", "name");
            const std::string method = CheckRelayName(L, 2, "Call", "method");
            if (!lua_isnoneornil(L, 3)) luaL_checktype(L, 3, LUA_TTABLE);
            if (lua_isnoneornil(L, 4))
                return luaL_error(L, "wxl.relay.Call: 'fn' (argument 4) is missing; it takes a function");
            luaL_checktype(L, 4, LUA_TFUNCTION);
            const wxl_relay_api* api = Relay();
            if (!api || !wxl::detail::Carries(api, &wxl_relay_api::call)) return PushResultFailure(L, WXL_E_UNSUPPORTED);

            // The list's values, pushed in order so that their strings live through the call.
            const int base  = lua_gettop(L);
            const int count = lua_istable(L, 3) ? static_cast<int>(lua_objlen(L, 3)) : 0;
            luaL_checkstack(L, count + 2, "too many arguments");
            for (int i = 1; i <= count; ++i) lua_rawgeti(L, 3, i);
            const std::vector<wxl_value> args = ValuesOf(L, base + 1, base + count);

            lua_pushvalue(L, 4);
            auto waiting  = std::make_unique<Waiting>();
            waiting->mod  = &mod;
            waiting->fn   = luaL_ref(L, LUA_REGISTRYINDEX);
            Waiting* kept = waiting.get();
            g_waiting.push_back(std::move(waiting));
            wxl_request      request = 0;
            const wxl_result result  = api->call(Str(name), Str(method), args.data(), static_cast<uint32_t>(args.size()),
                                                 &RelayReplied, kept, &request);
            lua_settop(L, base);
            if (result != WXL_OK)
            {
                luaL_unref(L, LUA_REGISTRYINDEX, kept->fn);
                g_waiting.erase(std::find_if(g_waiting.begin(), g_waiting.end(),
                                             [&](const std::unique_ptr<Waiting>& w) { return w.get() == kept; }));
                return PushResultFailure(L, result);
            }
            kept->request = request;
            lua_pushnumber(L, static_cast<lua_Number>(request));
            return 1;
        }

        int L_Answer(lua_State* L)
        {
            const uint64_t       request = CheckRequest(L, 1, "Answer");
            const wxl_relay_api* api     = Relay();
            if (!api || !wxl::detail::Carries(api, &wxl_relay_api::answer)) return PushResultFailure(L, WXL_E_UNSUPPORTED);
            const std::vector<wxl_value> values = ValuesOf(L, 2, lua_gettop(L));
            const wxl_result result = api->answer(request, values.data(), static_cast<uint32_t>(values.size()));
            if (result != WXL_OK) return PushResultFailure(L, result);
            lua_pushboolean(L, 1);
            return 1;
        }

        int L_Fail(lua_State* L)
        {
            const uint64_t    request = CheckRequest(L, 1, "Fail");
            const std::string reason  = CheckRelayName(L, 2, "Fail", "reason");
            const wxl_relay_api* api  = Relay();
            if (!api || !wxl::detail::Carries(api, &wxl_relay_api::fail)) return PushResultFailure(L, WXL_E_UNSUPPORTED);
            const wxl_result result = api->fail(request, Str(reason));
            if (result != WXL_OK) return PushResultFailure(L, result);
            lua_pushboolean(L, 1);
            return 1;
        }

        int L_Cancel(lua_State* L)
        {
            ScriptMod&     mod     = Self(L);
            const uint64_t request = CheckRequest(L, 1, "Cancel");
            const auto     found   = std::find_if(g_waiting.begin(), g_waiting.end(), [&](const std::unique_ptr<Waiting>& w) {
                return w->mod == &mod && w->request == request;
            });
            if (found == g_waiting.end()) return PushResultFailure(L, WXL_E_NOT_FOUND);
            const wxl_relay_api* api = Relay();
            if (!api || !wxl::detail::Carries(api, &wxl_relay_api::cancel)) return PushResultFailure(L, WXL_E_UNSUPPORTED);
            const wxl_result result = api->cancel(request);
            if (result != WXL_OK) return PushResultFailure(L, result);
            luaL_unref(L, LUA_REGISTRYINDEX, (*found)->fn);
            g_waiting.erase(found);
            lua_pushboolean(L, 1);
            return 1;
        }

        /// A mod's services go and its requests are cancelled.
        void ReleaseRelay(ScriptMod& mod)
        {
            for (size_t i = g_published.size(); i-- > 0;)
                if (g_published[i]->mod == &mod) Withdraw(i);
            const wxl_relay_api* api = Relay();
            for (size_t i = g_waiting.size(); i-- > 0;)
            {
                Waiting& waiting = *g_waiting[i];
                if (waiting.mod != &mod) continue;
                if (mod.State()) luaL_unref(mod.State(), LUA_REGISTRYINDEX, waiting.fn);
                waiting.mod = nullptr;
                // Cancelled, the runtime never answers it: forgotten now. Otherwise its answer is
                // on its way, and finds nobody.
                if (api && wxl::detail::Carries(api, &wxl_relay_api::cancel) && waiting.request &&
                    api->cancel(waiting.request) == WXL_OK)
                    g_waiting.erase(g_waiting.begin() + static_cast<std::ptrdiff_t>(i));
            }
        }
    } // namespace

    lua_CFunction Find(std::string_view service, std::string_view name)
    {
        if (service == "script" && name == "RegisterFunction") return &L_RegisterFunction;
        if (service == "script" && name == "RegisterEvent") return &L_RegisterEvent;
        if (service == "storage" && name == "AddSearchPath") return &L_AddSearchPath;
        if (service == "storage" && name == "RemoveSearchPath") return &L_RemoveSearchPath;
        if (service == "storage" && name == "Redirect") return &L_Redirect;
        if (service == "storage" && name == "Unredirect") return &L_Unredirect;
        if (service == "storage" && name == "Serve") return &L_Serve;
        if (service == "storage" && name == "Unserve") return &L_Unserve;
        if (service == "storage" && name == "HashFile") return &L_HashFile;
        if (service == "storage" && name == "HashFileAsync") return &L_HashFileAsync;
        if (service == "storage" && name == "OverrideFile") return &L_OverrideFile;
        if (service == "storage" && name == "RestoreFile") return &L_RestoreFile;
        if (service == "storage" && name == "ScopedName") return &L_ScopedName;
        if (service == "storage" && name == "ReloadAsset") return &L_ReloadAsset;
        if (service == "relay" && name == "Publish") return &L_Publish;
        if (service == "relay" && name == "Unpublish") return &L_Unpublish;
        if (service == "relay" && name == "Call") return &L_Call;
        if (service == "relay" && name == "Answer") return &L_Answer;
        if (service == "relay" && name == "Fail") return &L_Fail;
        if (service == "relay" && name == "Cancel") return &L_Cancel;
        if (service == "footprint" && name == "Snapshot") return &L_Snapshot;
        if (service == "footprint" && name == "Report") return &L_Report;
        if (service == "footprint" && name == "WatchPressure") return &L_WatchPressure;
        if (service == "profile" && name == "Begin") return &profile::L_Begin;
        if (service == "profile" && name == "End") return &profile::L_End;
        if (service == "profile" && name == "Text") return &profile::L_Text;
        return nullptr;
    }

    void ReleaseHeld(ScriptMod& mod)
    {
        ReleaseFootprint(mod);
        ReleaseStorage(mod);
        ReleaseRelay(mod);
        for (size_t i = g_folders.size(); i-- > 0;)
            if (std::find(g_folders[i].holders.begin(), g_folders[i].holders.end(), &mod) != g_folders[i].holders.end())
                LetGo(i, mod);
        for (size_t i = g_redirects.size(); i-- > 0;)
            if (std::any_of(g_redirects[i].holders.begin(), g_redirects[i].holders.end(),
                            [&](const auto& holder) { return holder.first == &mod; }))
                LetGoName(i, mod);
        for (size_t i = g_served.size(); i-- > 0;)
            if (std::any_of(g_served[i].holders.begin(), g_served[i].holders.end(),
                            [&](const ServedName& holder) { return holder.mod == &mod; }))
                LetGoServed(i, mod);
    }

    void Start()
    {
        g_ready   = wxl::Subscribe(WXL_EV_ScriptVmReady, &OnReady, nullptr, INT32_MIN);
        g_closing = wxl::Subscribe(WXL_EV_ScriptVmClosing, &OnClosing, nullptr, INT32_MAX);
    }

    void Stop()
    {
        g_ready.Reset();
        g_closing.Reset();
        for (Function& function : g_functions)
        {
            function.mod = nullptr;
            function.ref = LUA_NOREF;
        }
        g_events.clear();
    }

    void Forget(ScriptMod& mod)
    {
        ReleaseHeld(mod);
        // The state closes right after: its references go with it, nothing to release here.
        for (Function& function : g_functions)
        {
            if (function.mod != &mod) continue;
            function.mod = nullptr;
            function.ref = LUA_NOREF;
        }
        for (size_t i = g_events.size(); i-- > 0;)
            if (g_events[i].mod == &mod) g_events.erase(g_events.begin() + static_cast<std::ptrdiff_t>(i));
    }

    bool Calling() { return g_calling != 0; }
} // namespace wxl::lua::shims
