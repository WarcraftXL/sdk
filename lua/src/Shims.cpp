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
#include <fstream>
#include <iterator>
#include <functional>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include "Objects.hpp"
#include "Profile.hpp"
#include "Runtime.hpp"
#include "ScriptMod.hpp"
#include "Services.hpp"
#include "wxl/reflect.hpp"
#include "wxl/sdk.hpp"
#include "wxl/services/rows.hpp"
#include "wxl/services/script.h"
#include "wxl/services/camera.h"
#include "wxl/services/footprint.h"
#include "wxl/services/input.h"
#include "wxl/services/pose.h"
#include "wxl/services/highlight.h"
#include "wxl/services/selectioncircle.h"
#include "wxl/services/texture.h"
#include "wxl/services/cvar.h"
#include "wxl/services/render.h"
#include "wxl/services/relay.h"
#include "wxl/services/storage.h"
#include "wxl/services/terrain.h"
#include "wxl/services/draw.h"

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

        // --- wxl.storage: hashes, reloads -------------------------------------------------------

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
        // --- wxl.camera and wxl.input: the view and the pointer each mod holds ---------------

        const wxl_camera_api* Camera() { return wxl::api::Table<wxl_camera_api>(); }
        const wxl_input_api*  Input() { return wxl::api::Table<wxl_input_api>(); }

        /// A view a Lua mod holds, as it last set it.
        struct View
        {
            ScriptMod* mod;
            wxl_vec3   position;
            wxl_vec3   forward;
            double     fov;
            double     roll; ///< 0 for a view set by SetView
        };

        /// The Lua mods holding a view, the last set first: the runtime sees them all as
        /// wxl.lua, which holds one view, the first mod's here.
        std::vector<View> g_viewers;

        bool HoldsView(const View& view, const ScriptMod& mod) { return view.mod == &mod; }

        /// A Lua mod holding the pointer, and the point of the window it holds it at.
        struct Capture
        {
            ScriptMod* mod;
            double     x;
            double     y;
        };

        /// The Lua mods holding the pointer, the last to capture or move it last: wxl.lua holds it
        /// while one does, at the last one's point.
        std::vector<Capture> g_capturers;

        /// The point wxl.lua holds the pointer at in the runtime, while it holds it.
        double g_heldX = 0.5;
        double g_heldY = 0.5;

        /// A point argument: a table with numbers x, y and z.
        wxl_vec3 CheckPoint(lua_State* L, int index, const char* function, const char* name)
        {
            if (lua_isnoneornil(L, index))
                luaL_error(L, "wxl.camera.%s: '%s' (argument %d) is missing; it takes a point, a table with x, y and z",
                           function, name, index);
            luaL_checktype(L, index, LUA_TTABLE);
            lua_getfield(L, index, "x");
            lua_getfield(L, index, "y");
            lua_getfield(L, index, "z");
            if (!lua_isnumber(L, -3) || !lua_isnumber(L, -2) || !lua_isnumber(L, -1))
                luaL_argerror(L, index, "a point: a table with numbers x, y and z");
            const wxl_vec3 point{static_cast<float>(lua_tonumber(L, -3)), static_cast<float>(lua_tonumber(L, -2)),
                                 static_cast<float>(lua_tonumber(L, -1))};
            lua_pop(L, 3);
            return point;
        }

        /// A number argument the function cannot do without.
        lua_Number CheckNumber(lua_State* L, int index, const char* function, const char* name, const char* takes)
        {
            if (lua_isnoneornil(L, index))
                luaL_error(L, "%s: '%s' (argument %d) is missing; it takes %s", function, name, index, takes);
            return luaL_checknumber(L, index);
        }

        /// Sets a view in the runtime: through set_view_rolled when it is rolled, so that an
        /// unrolled one reaches a runtime older than the roll too.
        wxl_result Place(const wxl_camera_api* api, const View& view)
        {
            if (view.roll != 0.0)
            {
                if (!wxl::detail::Carries(api, &wxl_camera_api::set_view_rolled)) return WXL_E_UNSUPPORTED;
                return api->set_view_rolled(view.position, view.forward, view.fov, view.roll);
            }
            if (!wxl::detail::Carries(api, &wxl_camera_api::set_view)) return WXL_E_UNSUPPORTED;
            return api->set_view(view.position, view.forward, view.fov);
        }

        /// SetView and SetViewRolled: the view is the mod's, the last set applied.
        int SetViewFor(lua_State* L, const char* function, bool rolled)
        {
            ScriptMod&     mod      = Self(L);
            const char*    name     = rolled ? "SetViewRolled" : "SetView";
            const wxl_vec3 position = CheckPoint(L, 1, name, "position");
            const wxl_vec3 forward  = CheckPoint(L, 2, name, "forward");
            const lua_Number fov    = CheckNumber(L, 3, function, "fov", "a number of radians, 0 to keep the client's");
            const lua_Number roll   = rolled ? CheckNumber(L, 4, function, "roll",
                                                           "a number of radians, positive to the camera's right")
                                             : 0.0;

            const wxl_camera_api* api = Camera();
            if (!api) return PushResultFailure(L, WXL_E_UNSUPPORTED);
            const View       view{&mod, position, forward, static_cast<double>(fov), static_cast<double>(roll)};
            const wxl_result result =
                rolled ? (wxl::detail::Carries(api, &wxl_camera_api::set_view_rolled)
                              ? api->set_view_rolled(position, forward, view.fov, view.roll)
                              : WXL_E_UNSUPPORTED)
                       : Place(api, view);
            if (result != WXL_OK) return PushResultFailure(L, result);
            g_viewers.erase(std::remove_if(g_viewers.begin(), g_viewers.end(),
                                           [&](const View& held) { return HoldsView(held, mod); }),
                            g_viewers.end());
            g_viewers.insert(g_viewers.begin(), view);
            lua_pushboolean(L, 1);
            return 1;
        }

        int L_SetView(lua_State* L) { return SetViewFor(L, "wxl.camera.SetView", false); }
        int L_SetViewRolled(lua_State* L) { return SetViewFor(L, "wxl.camera.SetViewRolled", true); }

        /// A mod lets its view go: the view set before it, by a Lua mod still holding one,
        /// becomes the runtime's again; with none left, the runtime's goes.
        wxl_result LetGoView(ScriptMod& mod)
        {
            const auto held = std::find_if(g_viewers.begin(), g_viewers.end(),
                                           [&](const View& view) { return HoldsView(view, mod); });
            if (held == g_viewers.end()) return WXL_E_NOT_FOUND;
            const bool applied = held == g_viewers.begin();
            g_viewers.erase(held);
            if (!applied) return WXL_OK;
            const wxl_camera_api* api = Camera();
            if (!api) return WXL_E_UNSUPPORTED;
            if (!g_viewers.empty()) return Place(api, g_viewers.front());
            if (!wxl::detail::Carries(api, &wxl_camera_api::release_view)) return WXL_E_UNSUPPORTED;
            return api->release_view();
        }

        int L_ReleaseView(lua_State* L)
        {
            const wxl_result result = LetGoView(Self(L));
            if (result != WXL_OK) return PushResultFailure(L, result);
            lua_pushboolean(L, 1);
            return 1;
        }

        /// Holds the pointer in the runtime at a point, for wxl.lua: at the centre through
        /// capture_cursor, so that a runtime older than the point serves it too.
        wxl_result HoldAt(const wxl_input_api* api, double x, double y)
        {
            if (x == 0.5 && y == 0.5)
            {
                if (!wxl::detail::Carries(api, &wxl_input_api::capture_cursor)) return WXL_E_UNSUPPORTED;
                return api->capture_cursor(WXL_TRUE);
            }
            if (!wxl::detail::Carries(api, &wxl_input_api::capture_cursor_at)) return WXL_E_UNSUPPORTED;
            return api->capture_cursor_at(x, y);
        }

        /// The runtime holds the pointer for wxl.lua while one Lua mod does, at the point of the
        /// last to capture or move it.
        wxl_result CaptureFor(ScriptMod& mod, bool captured, double x = 0.5, double y = 0.5)
        {
            const wxl_input_api* api = Input();
            if (!api || !wxl::detail::Carries(api, &wxl_input_api::capture_cursor)) return WXL_E_UNSUPPORTED;
            const auto held = std::find_if(g_capturers.begin(), g_capturers.end(),
                                           [&](const Capture& capture) { return capture.mod == &mod; });
            if (captured)
            {
                const bool moves = g_capturers.empty() || x != g_heldX || y != g_heldY;
                if (moves)
                {
                    const wxl_result result = HoldAt(api, x, y);
                    if (result != WXL_OK) return result;
                    g_heldX = x;
                    g_heldY = y;
                }
                if (held != g_capturers.end()) g_capturers.erase(held);
                g_capturers.push_back(Capture{&mod, x, y});
                return WXL_OK;
            }
            if (held == g_capturers.end()) return WXL_OK;
            g_capturers.erase(held);
            if (g_capturers.empty()) return api->capture_cursor(WXL_FALSE);
            const Capture& last = g_capturers.back();
            if (last.x == g_heldX && last.y == g_heldY) return WXL_OK;
            g_heldX = last.x;
            g_heldY = last.y;
            return HoldAt(api, last.x, last.y);
        }

        int L_CaptureCursor(lua_State* L)
        {
            if (lua_isnone(L, 1))
                return luaL_error(L, "wxl.input.CaptureCursor: 'captured' (argument 1) is missing; it takes a boolean");
            luaL_checktype(L, 1, LUA_TBOOLEAN);
            const wxl_result result = CaptureFor(Self(L), lua_toboolean(L, 1) != 0);
            if (result != WXL_OK) return PushResultFailure(L, result);
            lua_pushboolean(L, 1);
            return 1;
        }

        int L_CaptureCursorAt(lua_State* L)
        {
            const char*      function = "wxl.input.CaptureCursorAt";
            const lua_Number x = CheckNumber(L, 1, function, "x", "a number from 0 (left) to 1 (right)");
            const lua_Number y = CheckNumber(L, 2, function, "y", "a number from 0 (top) to 1 (bottom)");
            if (!(x >= 0.0 && x <= 1.0 && y >= 0.0 && y <= 1.0)) return PushResultFailure(L, WXL_E_INVALID);
            const wxl_result result = CaptureFor(Self(L), true, static_cast<double>(x), static_cast<double>(y));
            if (result != WXL_OK) return PushResultFailure(L, result);
            lua_pushboolean(L, 1);
            return 1;
        }

        /// The Lua mods keeping the game out of its mouse look: wxl.lua blocks it while one does.
        std::vector<ScriptMod*> g_blockers;

        wxl_result BlockFor(ScriptMod& mod, bool blocked)
        {
            const wxl_input_api* api = Input();
            if (!api || !wxl::detail::Carries(api, &wxl_input_api::block_mouse_look)) return WXL_E_UNSUPPORTED;
            const auto held = std::find(g_blockers.begin(), g_blockers.end(), &mod);
            if (blocked)
            {
                if (held != g_blockers.end()) return WXL_OK;
                if (g_blockers.empty())
                {
                    const wxl_result result = api->block_mouse_look(WXL_TRUE);
                    if (result != WXL_OK) return result;
                }
                g_blockers.push_back(&mod);
                return WXL_OK;
            }
            if (held == g_blockers.end()) return WXL_OK;
            g_blockers.erase(held);
            return g_blockers.empty() ? api->block_mouse_look(WXL_FALSE) : WXL_OK;
        }

        int L_BlockMouseLook(lua_State* L)
        {
            if (lua_isnone(L, 1))
                return luaL_error(L, "wxl.input.BlockMouseLook: 'blocked' (argument 1) is missing; it takes a boolean");
            luaL_checktype(L, 1, LUA_TBOOLEAN);
            const wxl_result result = BlockFor(Self(L), lua_toboolean(L, 1) != 0);
            if (result != WXL_OK) return PushResultFailure(L, result);
            lua_pushboolean(L, 1);
            return 1;
        }

        // --- wxl.pose: the heads each Lua mod turned ------------------------------------------

        const wxl_pose_api* Pose() { return wxl::api::Table<wxl_pose_api>(); }

        /// The heads a Lua mod holds: the runtime sees them as wxl.lua's.
        struct Head
        {
            ScriptMod* mod;
            wxl_handle unit;
        };
        std::vector<Head> g_heads;

        double CheckAngle(lua_State* L, int index, const char* name)
        {
            if (lua_isnoneornil(L, index))
                luaL_error(L, "wxl.pose.Head: '%s' (argument %d) is missing; it takes a number", name, index);
            return static_cast<double>(luaL_checknumber(L, index));
        }

        int L_Head(lua_State* L)
        {
            ScriptMod&       mod       = Self(L);
            const wxl_handle unit      = CheckObject(L, 1);
            const double     yaw       = CheckAngle(L, 2, "yaw");
            const double     pitch     = CheckAngle(L, 3, "pitch");
            const double     smoothing = CheckAngle(L, 4, "smoothing");
            const wxl_pose_api* api = Pose();
            if (!api || !wxl::detail::Carries(api, &wxl_pose_api::head)) return PushResultFailure(L, WXL_E_UNSUPPORTED);
            const wxl_result result = api->head(unit, yaw, pitch, smoothing);
            if (result != WXL_OK) return PushResultFailure(L, result);
            for (Head& head : g_heads)
                if (head.unit == unit)
                {
                    head.mod = &mod;
                    lua_pushboolean(L, 1);
                    return 1;
                }
            g_heads.push_back(Head{&mod, unit});
            lua_pushboolean(L, 1);
            return 1;
        }

        int L_ReleaseHead(lua_State* L)
        {
            ScriptMod&       mod  = Self(L);
            const wxl_handle unit = CheckObject(L, 1);
            const auto held = std::find_if(g_heads.begin(), g_heads.end(),
                                           [&](const Head& head) { return head.mod == &mod && head.unit == unit; });
            if (held == g_heads.end()) return PushResultFailure(L, WXL_E_NOT_FOUND);
            g_heads.erase(held);
            const wxl_pose_api* api = Pose();
            if (!api || !wxl::detail::Carries(api, &wxl_pose_api::release_head)) return PushResultFailure(L, WXL_E_UNSUPPORTED);
            const wxl_result result = api->release_head(unit);
            if (result != WXL_OK) return PushResultFailure(L, result);
            lua_pushboolean(L, 1);
            return 1;
        }

        // --- wxl.highlight: the objects each Lua mod highlighted -------------------------------

        const wxl_highlight_api* Highlight() { return wxl::api::Table<wxl_highlight_api>(); }

        /// The highlights a Lua mod holds: the runtime sees them as wxl.lua's, one per object.
        struct Lit
        {
            ScriptMod* mod;
            wxl_handle target;
        };
        std::vector<Lit> g_lit;

        float CheckWidth(lua_State* L, int index, const char* name)
        {
            if (lua_isnoneornil(L, index))
                luaL_error(L, "wxl.highlight.Set: '%s' (argument %d) is missing; it takes a number", name, index);
            return static_cast<float>(luaL_checknumber(L, index));
        }

        int L_HighlightSet(lua_State* L)
        {
            ScriptMod&       mod    = Self(L);
            const wxl_handle target = CheckObject(L, 1);
            if (lua_isnoneornil(L, 2)) luaL_error(L, "wxl.highlight.Set: 'color' (argument 2) is missing; it takes 0xAARRGGBB");
            const double color = luaL_checknumber(L, 2);
            if (!(color >= 0.0 && color <= 4294967295.0) || color != static_cast<double>(static_cast<uint32_t>(color)))
                luaL_error(L, "wxl.highlight.Set: 'color' (argument 2) must be a whole number from 0 to 0xFFFFFFFF");
            const float outline  = CheckWidth(L, 3, "outline");
            const float glow     = CheckWidth(L, 4, "glow");
            const float strength = CheckWidth(L, 5, "glow_strength");
            const wxl_highlight_api* api = Highlight();
            if (!api || !wxl::detail::Carries(api, &wxl_highlight_api::set)) return PushResultFailure(L, WXL_E_UNSUPPORTED);
            const wxl_result result = api->set(target, static_cast<uint32_t>(color), outline, glow, strength);
            if (result != WXL_OK) return PushResultFailure(L, result);
            for (Lit& lit : g_lit)
                if (lit.target == target)
                {
                    lit.mod = &mod; // the last Lua mod to set it holds it, as among native mods
                    lua_pushboolean(L, 1);
                    return 1;
                }
            g_lit.push_back(Lit{&mod, target});
            lua_pushboolean(L, 1);
            return 1;
        }

        int L_HighlightClear(lua_State* L)
        {
            ScriptMod&       mod    = Self(L);
            const wxl_handle target = CheckObject(L, 1);
            const auto held = std::find_if(g_lit.begin(), g_lit.end(),
                                           [&](const Lit& lit) { return lit.mod == &mod && lit.target == target; });
            if (held == g_lit.end()) return PushResultFailure(L, WXL_E_NOT_FOUND);
            g_lit.erase(held);
            const wxl_highlight_api* api = Highlight();
            if (!api || !wxl::detail::Carries(api, &wxl_highlight_api::clear)) return PushResultFailure(L, WXL_E_UNSUPPORTED);
            const wxl_result result = api->clear(target);
            if (result != WXL_OK) return PushResultFailure(L, result);
            lua_pushboolean(L, 1);
            return 1;
        }

        /// A Lua mod's highlights are taken off.
        void ClearHighlights(ScriptMod& mod)
        {
            const wxl_highlight_api* api = Highlight();
            for (size_t i = g_lit.size(); i-- > 0;)
            {
                if (g_lit[i].mod != &mod) continue;
                if (api && wxl::detail::Carries(api, &wxl_highlight_api::clear)) api->clear(g_lit[i].target);
                g_lit.erase(g_lit.begin() + static_cast<std::ptrdiff_t>(i));
            }
        }

        int L_HighlightClearAll(lua_State* L)
        {
            ClearHighlights(Self(L));
            return 0;
        }

        // --- wxl.selectioncircle: the circles each Lua mod changed ---------------------------

        const wxl_selectioncircle_api* Circles() { return wxl::api::Table<wxl_selectioncircle_api>(); }

        constexpr uint32_t kCircleKinds     = 3;
        constexpr uint32_t kCircleReactions = 9; ///< the eight wxl_reaction values, then none

        /// The Lua mod that last changed each kind and reaction: the runtime sees them all as
        /// wxl.lua's, so a mod's reset gives back only what it holds.
        ScriptMod* g_circleCells[kCircleKinds][kCircleReactions] = {};

        struct CircleObject
        {
            ScriptMod* mod;
            wxl_handle target;
        };
        std::vector<CircleObject> g_circleObjects;

        constexpr const char* kCircleKindNames[kCircleKinds] = {"target", "pet_target", "move_marker"};
        constexpr const char* kCircleReactionNames[kCircleReactions] = {
            "hated", "hostile", "unfriendly", "neutral", "friendly", "honored", "revered", "exalted", "none"};

        /// One name of `names`, as its bit; 0 for a name not there.
        uint32_t CircleBit(const char* name, const char* const* names, uint32_t count)
        {
            for (uint32_t i = 0; i < count; ++i)
                if (std::strcmp(names[i], name) == 0) return 1u << i;
            return 0;
        }

        /// A mask of kinds or reactions as a Lua mod gives it: nil (all), a whole number (the
        /// mask), a name, or a list of names. `all` is the mask of every bit.
        uint32_t CheckCircleMask(lua_State* L, int index, const char* call, const char* what, const char* const* names,
                                 uint32_t count, uint32_t all)
        {
            if (lua_isnoneornil(L, index)) return 0;
            if (lua_type(L, index) == LUA_TNUMBER)
            {
                const double mask = lua_tonumber(L, index);
                if (!(mask >= 0.0 && mask <= static_cast<double>(all)) || mask != static_cast<double>(static_cast<uint32_t>(mask)))
                    luaL_error(L, "wxl.selectioncircle.%s: '%s' (argument %d) must be a mask from 0 to %u", call, what, index, all);
                return static_cast<uint32_t>(mask);
            }
            if (lua_type(L, index) == LUA_TSTRING)
            {
                const char* name = lua_tostring(L, index);
                if (std::strcmp(name, "all") == 0) return 0;
                if (const uint32_t bit = CircleBit(name, names, count)) return bit;
                luaL_error(L, "wxl.selectioncircle.%s: '%s' (argument %d) has no %s named \"%s\"", call, what, index,
                           what, name);
            }
            if (lua_type(L, index) == LUA_TTABLE)
            {
                uint32_t mask = 0;
                for (int i = 1;; ++i)
                {
                    lua_rawgeti(L, index, i);
                    if (lua_isnil(L, -1))
                    {
                        lua_pop(L, 1);
                        break;
                    }
                    const char*    name = lua_tostring(L, -1);
                    const uint32_t bit  = name ? CircleBit(name, names, count) : 0;
                    lua_pop(L, 1);
                    if (!bit)
                        luaL_error(L, "wxl.selectioncircle.%s: '%s' (argument %d) has an entry %d that names no %s", call,
                                   what, index, i, what);
                    mask |= bit;
                }
                return mask;
            }
            luaL_error(L, "wxl.selectioncircle.%s: '%s' (argument %d) takes nil, a name, a list of names or a mask", call,
                       what, index);
            return 0;
        }

        uint32_t CheckCircleKinds(lua_State* L, int index, const char* call)
        {
            return CheckCircleMask(L, index, call, "kinds", kCircleKindNames, kCircleKinds, WXL_SELECTIONCIRCLE_ALL_KINDS);
        }

        uint32_t CheckCircleReactions(lua_State* L, int index, const char* call)
        {
            return CheckCircleMask(L, index, call, "reactions", kCircleReactionNames, kCircleReactions,
                                   WXL_SELECTIONCIRCLE_ALL_REACTIONS);
        }

        struct CircleStyle
        {
            std::string texture;
            uint32_t    color = 0;
            float       scale = 0.0f;
        };

        /// A style table {texture, color, scale}, each left out keeping the game's.
        CircleStyle CheckCircleStyle(lua_State* L, int index, const char* call)
        {
            if (!lua_istable(L, index))
                luaL_error(L, "wxl.selectioncircle.%s: 'style' (argument %d) is missing; it takes a table {texture, color, scale}",
                           call, index);
            CircleStyle style;
            lua_getfield(L, index, "texture");
            if (!lua_isnil(L, -1))
            {
                if (lua_type(L, -1) != LUA_TSTRING)
                    luaL_error(L, "wxl.selectioncircle.%s: the style's 'texture' must be a file's path", call);
                size_t      size = 0;
                const char* text = lua_tolstring(L, -1, &size);
                style.texture.assign(text, size);
            }
            lua_pop(L, 1);
            lua_getfield(L, index, "color");
            if (!lua_isnil(L, -1))
            {
                const double color = lua_type(L, -1) == LUA_TNUMBER ? lua_tonumber(L, -1) : -1.0;
                if (!(color >= 0.0 && color <= 4294967295.0) || color != static_cast<double>(static_cast<uint32_t>(color)))
                    luaL_error(L, "wxl.selectioncircle.%s: the style's 'color' must be a whole number from 0 to 0xFFFFFFFF", call);
                style.color = static_cast<uint32_t>(color);
            }
            lua_pop(L, 1);
            lua_getfield(L, index, "scale");
            if (!lua_isnil(L, -1))
            {
                if (lua_type(L, -1) != LUA_TNUMBER) luaL_error(L, "wxl.selectioncircle.%s: the style's 'scale' must be a number", call);
                style.scale = static_cast<float>(lua_tonumber(L, -1));
            }
            lua_pop(L, 1);
            return style;
        }

        /// The cells of a mask of kinds and reactions, 0 meaning all of either.
        template <class Fn>
        void ForCircleCells(uint32_t kinds, uint32_t reactions, Fn&& fn)
        {
            if (!kinds) kinds = WXL_SELECTIONCIRCLE_ALL_KINDS;
            if (!reactions) reactions = WXL_SELECTIONCIRCLE_ALL_REACTIONS;
            for (uint32_t k = 0; k < kCircleKinds; ++k)
                for (uint32_t r = 0; r < kCircleReactions; ++r)
                    if ((kinds & (1u << k)) && (reactions & (1u << r))) fn(k, r);
        }

        void HoldCircleCells(ScriptMod& mod, uint32_t kinds, uint32_t reactions)
        {
            ForCircleCells(kinds, reactions, [&](uint32_t k, uint32_t r) { g_circleCells[k][r] = &mod; });
        }

        void HoldCircleObject(ScriptMod& mod, wxl_handle target)
        {
            for (CircleObject& held : g_circleObjects)
                if (held.target == target)
                {
                    held.mod = &mod; // the last Lua mod to change it holds it, as among native mods
                    return;
                }
            g_circleObjects.push_back(CircleObject{&mod, target});
        }

        int PushCircleResult(lua_State* L, wxl_result result)
        {
            if (result != WXL_OK) return PushResultFailure(L, result);
            lua_pushboolean(L, 1);
            return 1;
        }

        int L_CircleHide(lua_State* L)
        {
            ScriptMod&     mod       = Self(L);
            const uint32_t kinds     = CheckCircleKinds(L, 1, "Hide");
            const uint32_t reactions = CheckCircleReactions(L, 2, "Hide");
            const wxl_selectioncircle_api* api = Circles();
            if (!api || !wxl::detail::Carries(api, &wxl_selectioncircle_api::hide)) return PushResultFailure(L, WXL_E_UNSUPPORTED);
            const wxl_result result = api->hide(kinds, reactions);
            if (result == WXL_OK) HoldCircleCells(mod, kinds, reactions);
            return PushCircleResult(L, result);
        }

        int L_CircleSetStyle(lua_State* L)
        {
            ScriptMod&        mod       = Self(L);
            const uint32_t    kinds     = CheckCircleKinds(L, 1, "SetStyle");
            const uint32_t    reactions = CheckCircleReactions(L, 2, "SetStyle");
            const CircleStyle style     = CheckCircleStyle(L, 3, "SetStyle");
            const wxl_selectioncircle_api* api = Circles();
            if (!api || !wxl::detail::Carries(api, &wxl_selectioncircle_api::set_style))
                return PushResultFailure(L, WXL_E_UNSUPPORTED);
            const wxl_result result = api->set_style(kinds, reactions, Str(style.texture), style.color, style.scale);
            if (result == WXL_OK) HoldCircleCells(mod, kinds, reactions);
            return PushCircleResult(L, result);
        }

        /// Gives back the cells of a mask a Lua mod holds; the others stay as their holder left them.
        void ResetCircleCells(ScriptMod& mod, uint32_t kinds, uint32_t reactions)
        {
            const wxl_selectioncircle_api* api = Circles();
            ForCircleCells(kinds, reactions, [&](uint32_t k, uint32_t r) {
                if (g_circleCells[k][r] != &mod) return;
                g_circleCells[k][r] = nullptr;
                if (api && wxl::detail::Carries(api, &wxl_selectioncircle_api::reset)) api->reset(1u << k, 1u << r);
            });
        }

        int L_CircleReset(lua_State* L)
        {
            ScriptMod&     mod       = Self(L);
            const uint32_t kinds     = CheckCircleKinds(L, 1, "Reset");
            const uint32_t reactions = CheckCircleReactions(L, 2, "Reset");
            const wxl_selectioncircle_api* api = Circles();
            if (!api || !wxl::detail::Carries(api, &wxl_selectioncircle_api::reset)) return PushResultFailure(L, WXL_E_UNSUPPORTED);
            ResetCircleCells(mod, kinds, reactions);
            lua_pushboolean(L, 1);
            return 1;
        }

        int L_CircleHideObject(lua_State* L)
        {
            ScriptMod&       mod    = Self(L);
            const wxl_handle target = CheckObject(L, 1);
            const wxl_selectioncircle_api* api = Circles();
            if (!api || !wxl::detail::Carries(api, &wxl_selectioncircle_api::hide_object))
                return PushResultFailure(L, WXL_E_UNSUPPORTED);
            const wxl_result result = api->hide_object(target);
            if (result == WXL_OK) HoldCircleObject(mod, target);
            return PushCircleResult(L, result);
        }

        int L_CircleSetObjectStyle(lua_State* L)
        {
            ScriptMod&        mod    = Self(L);
            const wxl_handle  target = CheckObject(L, 1);
            const CircleStyle style  = CheckCircleStyle(L, 2, "SetObjectStyle");
            const wxl_selectioncircle_api* api = Circles();
            if (!api || !wxl::detail::Carries(api, &wxl_selectioncircle_api::set_object_style))
                return PushResultFailure(L, WXL_E_UNSUPPORTED);
            const wxl_result result = api->set_object_style(target, Str(style.texture), style.color, style.scale);
            if (result == WXL_OK) HoldCircleObject(mod, target);
            return PushCircleResult(L, result);
        }

        int L_CircleResetObject(lua_State* L)
        {
            ScriptMod&       mod    = Self(L);
            const wxl_handle target = CheckObject(L, 1);
            const auto held = std::find_if(g_circleObjects.begin(), g_circleObjects.end(),
                                           [&](const CircleObject& o) { return o.mod == &mod && o.target == target; });
            if (held == g_circleObjects.end()) return PushResultFailure(L, WXL_E_NOT_FOUND);
            g_circleObjects.erase(held);
            const wxl_selectioncircle_api* api = Circles();
            if (!api || !wxl::detail::Carries(api, &wxl_selectioncircle_api::reset_object))
                return PushResultFailure(L, WXL_E_UNSUPPORTED);
            return PushCircleResult(L, api->reset_object(target));
        }

        /// Everything a Lua mod changed on the circles is given back.
        void ResetCircles(ScriptMod& mod)
        {
            ResetCircleCells(mod, 0, 0);
            const wxl_selectioncircle_api* api = Circles();
            for (size_t i = g_circleObjects.size(); i-- > 0;)
            {
                if (g_circleObjects[i].mod != &mod) continue;
                if (api && wxl::detail::Carries(api, &wxl_selectioncircle_api::reset_object))
                    api->reset_object(g_circleObjects[i].target);
                g_circleObjects.erase(g_circleObjects.begin() + static_cast<std::ptrdiff_t>(i));
            }
        }

        int L_CircleResetAll(lua_State* L)
        {
            ResetCircles(Self(L));
            return 0;
        }

        // --- wxl.cvar: the names each Lua mod registered -------------------------------------

        const wxl_cvar_api* CVars() { return wxl::api::Table<wxl_cvar_api>(); }

        /// A variable a Lua mod registered: the runtime sees every Lua mod as wxl.lua, so which
        /// of them holds a name is kept here.
        struct CVarName
        {
            std::string key; ///< the name without case, as the client compares names
            std::string name;
            ScriptMod*  mod;
        };
        std::vector<CVarName> g_cvarNames;

        constexpr uint32_t    kCVarFlagCount                 = 7;
        constexpr const char* kCVarFlagNames[kCVarFlagCount] = {"saved",   "account",   "character", "read_only",
                                                                "latched", "protected", "hidden"};

        std::string CVarKey(std::string_view name)
        {
            std::string key(name);
            for (char& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return key;
        }

        CVarName* HeldCVar(std::string_view name)
        {
            const std::string key = CVarKey(name);
            for (CVarName& held : g_cvarNames)
                if (held.key == key) return &held;
            return nullptr;
        }

        /// A variable's flags as a Lua mod gives them: nil (none), a whole number (the mask), a
        /// flag's name, or a list of names.
        uint32_t CheckCVarFlags(lua_State* L, int index)
        {
            if (lua_isnoneornil(L, index)) return 0;
            if (lua_type(L, index) == LUA_TNUMBER)
            {
                const double mask = lua_tonumber(L, index);
                if (!(mask >= 0.0 && mask <= static_cast<double>(WXL_CVAR_ALL_FLAGS)) ||
                    mask != static_cast<double>(static_cast<uint32_t>(mask)))
                    luaL_error(L, "wxl.cvar.Register: 'flags' (argument %d) must be a mask from 0 to %u", index,
                               WXL_CVAR_ALL_FLAGS);
                return static_cast<uint32_t>(mask);
            }
            const auto bitOf = [](const char* name) -> uint32_t {
                for (uint32_t i = 0; i < kCVarFlagCount; ++i)
                    if (std::strcmp(kCVarFlagNames[i], name) == 0) return 1u << i;
                return 0;
            };
            if (lua_type(L, index) == LUA_TSTRING)
            {
                const char* name = lua_tostring(L, index);
                if (const uint32_t bit = bitOf(name)) return bit;
                luaL_error(L,
                           "wxl.cvar.Register: 'flags' (argument %d) has no flag named \"%s\"; the flags are saved, "
                           "account, character, read_only, latched, protected and hidden",
                           index, name);
            }
            if (lua_type(L, index) == LUA_TTABLE)
            {
                uint32_t mask = 0;
                for (int i = 1;; ++i)
                {
                    lua_rawgeti(L, index, i);
                    if (lua_isnil(L, -1))
                    {
                        lua_pop(L, 1);
                        break;
                    }
                    const char*    name = lua_tostring(L, -1);
                    const uint32_t bit  = name ? bitOf(name) : 0;
                    lua_pop(L, 1);
                    if (!bit)
                        luaL_error(L, "wxl.cvar.Register: 'flags' (argument %d) has an entry %d that names no flag", index, i);
                    mask |= bit;
                }
                return mask;
            }
            luaL_error(L, "wxl.cvar.Register: 'flags' (argument %d) takes nil, a flag's name, a list of names or a mask",
                       index);
            return 0;
        }

        /// A value as a Lua mod gives it: a string, a number (as Lua writes it) or a boolean
        /// ("1" or "0").
        std::string CheckCVarValue(lua_State* L, int index, const char* call, const char* what)
        {
            switch (lua_type(L, index))
            {
            case LUA_TSTRING:
            case LUA_TNUMBER:
            {
                size_t      size = 0;
                const char* text = lua_tolstring(L, index, &size);
                return std::string(text, size);
            }
            case LUA_TBOOLEAN: return lua_toboolean(L, index) ? "1" : "0";
            default:
                luaL_error(L, "wxl.cvar.%s: '%s' (argument %d) is missing; it takes a string, a number or a boolean", call,
                           what, index);
                return {};
            }
        }

        int L_CVarRegister(lua_State* L)
        {
            ScriptMod&        mod = Self(L);
            size_t            size = 0;
            const char*       text = luaL_checklstring(L, 1, &size);
            const std::string name(text, size);
            const std::string value = CheckCVarValue(L, 2, "Register", "default");
            const uint32_t    flags = CheckCVarFlags(L, 3);
            std::string       help;
            if (!lua_isnoneornil(L, 4))
            {
                const char* line = luaL_checklstring(L, 4, &size);
                help.assign(line, size);
            }
            CVarName* held = HeldCVar(name);
            if (held && held->mod != &mod)
            {
                wxl::Log(WXL_LOG_WARN, kTag,
                         "mod " + mod.Id() + ": wxl.cvar.Register refused '" + name + "': Lua mod " + held->mod->Id() +
                             " holds it; choose a name of your own (start it with a prefix of your mod)");
                return PushResultFailure(L, WXL_E_INVALID);
            }
            const wxl_cvar_api* api = CVars();
            if (!api || !wxl::detail::Carries(api, &wxl_cvar_api::register_cvar)) return PushResultFailure(L, WXL_E_UNSUPPORTED);
            const wxl_result result = api->register_cvar(Str(name), Str(value), flags, Str(help));
            if (result != WXL_OK) return PushResultFailure(L, result);
            if (!held) g_cvarNames.push_back(CVarName{CVarKey(name), name, &mod});
            lua_pushboolean(L, 1);
            return 1;
        }

        int L_CVarRelease(lua_State* L)
        {
            ScriptMod&        mod  = Self(L);
            size_t            size = 0;
            const char*       text = luaL_checklstring(L, 1, &size);
            const std::string name(text, size);
            CVarName*         held = HeldCVar(name);
            if (!held || held->mod != &mod) return PushResultFailure(L, WXL_E_NOT_FOUND);
            g_cvarNames.erase(g_cvarNames.begin() + (held - g_cvarNames.data()));
            const wxl_cvar_api* api = CVars();
            if (!api || !wxl::detail::Carries(api, &wxl_cvar_api::release)) return PushResultFailure(L, WXL_E_UNSUPPORTED);
            const wxl_result result = api->release(Str(name));
            if (result != WXL_OK) return PushResultFailure(L, result);
            lua_pushboolean(L, 1);
            return 1;
        }

        /// A Lua mod's variables are given up: they stay, with their values.
        void ReleaseCVars(ScriptMod& mod)
        {
            const wxl_cvar_api* api = CVars();
            for (size_t i = g_cvarNames.size(); i-- > 0;)
            {
                if (g_cvarNames[i].mod != &mod) continue;
                if (api && wxl::detail::Carries(api, &wxl_cvar_api::release)) api->release(Str(g_cvarNames[i].name));
                g_cvarNames.erase(g_cvarNames.begin() + static_cast<std::ptrdiff_t>(i));
            }
        }

        /// A mod's view, its hold on the pointer, its block of the mouse look, its heads, its
        /// highlights, its circles and its console variables go with it.
        void ReleaseViewAndPointer(ScriptMod& mod)
        {
            ReleaseCVars(mod);
            ResetCircles(mod);
            ClearHighlights(mod);
            LetGoView(mod);
            CaptureFor(mod, false);
            BlockFor(mod, false);
            const wxl_pose_api* api = Pose();
            for (size_t i = g_heads.size(); i-- > 0;)
            {
                if (g_heads[i].mod != &mod) continue;
                if (api && wxl::detail::Carries(api, &wxl_pose_api::release_head)) api->release_head(g_heads[i].unit);
                g_heads.erase(g_heads.begin() + static_cast<std::ptrdiff_t>(i));
            }
        }

        // --- wxl.texture: a table of groups and colours as Lua writes them ---------------------

        const wxl_texture_api* Textures() { return wxl::api::Table<wxl_texture_api>(); }

        int HexDigit(char c)
        {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        }

        /// A colour as a Lua mod writes it: "rrggbbaa" or "rrggbb" (an optional '#' first, the
        /// order CSS writes), or a whole number 0xAARRGGBB. Answers it as 0xAARRGGBB.
        uint32_t CheckTextureColor(lua_State* L, int index, const std::string& what)
        {
            if (lua_type(L, index) == LUA_TNUMBER)
            {
                const double value = lua_tonumber(L, index);
                if (!(value >= 0.0 && value <= 4294967295.0) || value != static_cast<double>(static_cast<uint32_t>(value)))
                    luaL_error(L, "wxl.texture.Make: %s must be a whole number from 0 to 0xFFFFFFFF (0xAARRGGBB)", what.c_str());
                return static_cast<uint32_t>(value);
            }
            if (lua_type(L, index) == LUA_TSTRING)
            {
                size_t      size = 0;
                const char* text = lua_tolstring(L, index, &size);
                if (size && text[0] == '#')
                {
                    ++text;
                    --size;
                }
                uint32_t rgba = 0;
                bool     ok   = size == 6 || size == 8;
                for (size_t i = 0; ok && i < size; ++i)
                {
                    const int digit = HexDigit(text[i]);
                    ok              = digit >= 0;
                    rgba            = (rgba << 4) | static_cast<uint32_t>(digit < 0 ? 0 : digit);
                }
                if (ok) return size == 6 ? (0xFF000000u | rgba) : ((rgba & 0xFFu) << 24) | (rgba >> 8);
                luaL_error(L, "wxl.texture.Make: %s \"%s\" is not a colour: write \"rrggbb\" or \"rrggbbaa\" in hex",
                           what.c_str(), lua_tostring(L, index));
            }
            luaL_error(L, "wxl.texture.Make: %s must be a colour, \"rrggbbaa\" or 0xAARRGGBB", what.c_str());
            return 0;
        }

        /// The groups table: {id = colour}, {id = false} (hidden), or {id = {color, width, visible}}.
        std::vector<wxl_texture_group> CheckTextureGroups(lua_State* L, int index, std::deque<std::string>& ids)
        {
            std::vector<wxl_texture_group> groups;
            if (lua_isnoneornil(L, index)) return groups;
            if (!lua_istable(L, index))
                luaL_error(L, "wxl.texture.Make: 'groups' (argument %d) takes a table {id = colour or {color, width, visible}}",
                           index);
            lua_pushnil(L);
            while (lua_next(L, index) != 0)
            {
                if (lua_type(L, -2) != LUA_TSTRING)
                    luaL_error(L, "wxl.texture.Make: the groups are named by the SVG's ids, strings; one key is a %s",
                               luaL_typename(L, -2));
                ids.emplace_back(lua_tostring(L, -2));
                const std::string& id = ids.back();
                wxl_texture_group  group{};
                group.struct_size = sizeof group;
                group.id          = Str(id);
                const int value   = lua_gettop(L);
                switch (lua_type(L, value))
                {
                case LUA_TSTRING:
                case LUA_TNUMBER:
                    group.set |= WXL_TEXTURE_SET_COLOR;
                    group.color = CheckTextureColor(L, value, "group '" + id + "'");
                    break;
                case LUA_TBOOLEAN:
                    group.set |= WXL_TEXTURE_SET_VISIBLE;
                    group.visible = lua_toboolean(L, value) ? WXL_TRUE : WXL_FALSE;
                    break;
                case LUA_TTABLE:
                    lua_pushnil(L);
                    while (lua_next(L, value) != 0)
                    {
                        const char* field = lua_type(L, -2) == LUA_TSTRING ? lua_tostring(L, -2) : "";
                        if (std::strcmp(field, "color") == 0)
                        {
                            group.set |= WXL_TEXTURE_SET_COLOR;
                            group.color = CheckTextureColor(L, lua_gettop(L), "group '" + id + "' color");
                        }
                        else if (std::strcmp(field, "width") == 0)
                        {
                            if (lua_type(L, -1) != LUA_TNUMBER || !(lua_tonumber(L, -1) >= 0.0))
                                luaL_error(L, "wxl.texture.Make: group '%s' width must be a number, 0 or more", id.c_str());
                            group.set |= WXL_TEXTURE_SET_WIDTH;
                            group.width = static_cast<float>(lua_tonumber(L, -1));
                        }
                        else if (std::strcmp(field, "visible") == 0)
                        {
                            if (lua_type(L, -1) != LUA_TBOOLEAN)
                                luaL_error(L, "wxl.texture.Make: group '%s' visible must be true or false", id.c_str());
                            group.set |= WXL_TEXTURE_SET_VISIBLE;
                            group.visible = lua_toboolean(L, -1) ? WXL_TRUE : WXL_FALSE;
                        }
                        else
                            luaL_error(L, "wxl.texture.Make: group '%s' has a field '%s'; it takes color, width and visible",
                                       id.c_str(), field);
                        lua_pop(L, 1);
                    }
                    break;
                default:
                    luaL_error(L, "wxl.texture.Make: group '%s' takes a colour, false, or a table {color, width, visible}",
                               id.c_str());
                }
                groups.push_back(group);
                lua_pop(L, 1);
            }
            return groups;
        }

        uint32_t CheckTextureEdge(lua_State* L, int index, const char* what)
        {
            if (lua_isnoneornil(L, index)) return 0;
            const double edge = lua_type(L, index) == LUA_TNUMBER ? lua_tonumber(L, index) : -1.0;
            if (!(edge >= 0.0 && edge <= 4294967295.0) || edge != static_cast<double>(static_cast<uint32_t>(edge)))
                luaL_error(L, "wxl.texture.Make: '%s' (argument %d) must be a whole number of pixels, or nil for the source's own",
                           what, index);
            return static_cast<uint32_t>(edge);
        }

        /// Make(source, width, height [, groups [, tint]]): path, right, bottom, texture width and
        /// height, or nil and why. ".\\" names this Lua mod's own folder.
        int L_TextureMake(lua_State* L)
        {
            ScriptMod&  mod  = Self(L);
            size_t      size = 0;
            const char* text = luaL_checklstring(L, 1, &size);
            std::string source(text, size);
            if (source.size() >= 2 && source[0] == '.' && (source[1] == '\\' || source[1] == '/'))
                source = "Mods\\" + mod.Directory().filename().string() + "\\" + source.substr(2);
            const uint32_t                       width  = CheckTextureEdge(L, 2, "width");
            const uint32_t                       height = CheckTextureEdge(L, 3, "height");
            std::deque<std::string>              ids;
            const std::vector<wxl_texture_group> groups = CheckTextureGroups(L, 4, ids);
            const uint32_t tint = lua_isnoneornil(L, 5) ? 0u : CheckTextureColor(L, 5, "'tint' (argument 5)");

            const wxl_texture_api* api = Textures();
            if (!api || !wxl::detail::Carries(api, &wxl_texture_api::make)) return PushResultFailure(L, WXL_E_UNSUPPORTED);
            wxl_texture_request request{};
            request.struct_size = sizeof request;
            request.source      = Str(source);
            request.width       = width;
            request.height      = height;
            request.groups      = groups.empty() ? nullptr : groups.data();
            request.group_count = static_cast<uint32_t>(groups.size());
            request.tint        = tint;
            wxl_texture made{};
            made.struct_size        = sizeof made;
            const wxl_result result = api->make(&request, &made);
            if (result != WXL_OK)
            {
                if (!made.reason[0]) return PushResultFailure(L, result);
                lua_pushnil(L);
                lua_pushstring(L, made.reason);
                return 2;
            }
            lua_pushstring(L, made.path);
            lua_pushnumber(L, made.right);
            lua_pushnumber(L, made.bottom);
            lua_pushnumber(L, made.texture_width);
            lua_pushnumber(L, made.texture_height);
            return 5;
        }

        int L_TextureStats(lua_State* L)
        {
            const wxl_texture_api* api = Textures();
            if (!api || !wxl::detail::Carries(api, &wxl_texture_api::stats)) return PushResultFailure(L, WXL_E_UNSUPPORTED);
            wxl_texture_stats stats{};
            stats.struct_size       = sizeof stats;
            const wxl_result result = api->stats(&stats);
            if (result != WXL_OK) return PushResultFailure(L, result);
            lua_createtable(L, 0, 8);
            const auto field = [&](const char* name, double value) {
                lua_pushnumber(L, value);
                lua_setfield(L, -2, name);
            };
            field("textures", stats.textures);
            field("resident", stats.resident);
            field("texture_limit", stats.texture_limit);
            field("bytes", static_cast<double>(stats.bytes));
            field("byte_limit", static_cast<double>(stats.byte_limit));
            field("made", static_cast<double>(stats.made));
            field("reused", static_cast<double>(stats.reused));
            field("released", static_cast<double>(stats.released));
            return 1;
        }

        // --- wxl.terrain: each Lua mod's edits in a layer of its own ------------------------

        const wxl_terrain_api* Terrain() { return wxl::api::Table<wxl_terrain_api>(); }

        /// The layer of wxl.lua's edits each Lua mod writes in: the runtime sees every Lua mod as
        /// wxl.lua, so a mod's reset and its unload take off its own edits only.
        std::vector<std::pair<ScriptMod*, uint32_t>> g_terrainLayers;
        uint32_t                                     g_nextTerrainLayer = 1;

        uint32_t TerrainLayer(ScriptMod& mod)
        {
            for (const auto& held : g_terrainLayers)
                if (held.first == &mod) return held.second;
            g_terrainLayers.emplace_back(&mod, g_nextTerrainLayer);
            return g_nextTerrainLayer++;
        }

        void PushVec3(lua_State* L, const wxl_vec3& v)
        {
            lua_createtable(L, 0, 3);
            lua_pushnumber(L, v.x);
            lua_setfield(L, -2, "x");
            lua_pushnumber(L, v.y);
            lua_setfield(L, -2, "y");
            lua_pushnumber(L, v.z);
            lua_setfield(L, -2, "z");
        }

        wxl_vec3 CheckTerrainPoint(lua_State* L, int index, const char* call, const char* what)
        {
            if (!lua_istable(L, index))
                luaL_error(L, "wxl.terrain.%s: '%s' (argument %d) must be a point {x, y, z}", call, what, index);
            lua_getfield(L, index, "x");
            lua_getfield(L, index, "y");
            lua_getfield(L, index, "z");
            if (!lua_isnumber(L, -3) || !lua_isnumber(L, -2) || !lua_isnumber(L, -1))
                luaL_error(L, "wxl.terrain.%s: '%s' (argument %d) must be a point with numbers x, y and z", call, what, index);
            const wxl_vec3 v{static_cast<float>(lua_tonumber(L, -3)), static_cast<float>(lua_tonumber(L, -2)),
                             static_cast<float>(lua_tonumber(L, -1))};
            lua_pop(L, 3);
            return v;
        }

        int32_t CheckChunk(lua_State* L, int index, const char* call, const char* what)
        {
            if (!lua_isnumber(L, index))
                luaL_error(L, "wxl.terrain.%s: '%s' (argument %d) must be a chunk's column or row, 0 to 1023", call, what, index);
            return static_cast<int32_t>(lua_tointeger(L, index));
        }

        int L_TerrainTrace(lua_State* L)
        {
            const wxl_vec3         start  = CheckTerrainPoint(L, 1, "Trace", "start");
            const wxl_vec3         finish = CheckTerrainPoint(L, 2, "Trace", "finish");
            const wxl_terrain_api* api    = Terrain();
            if (!api || !wxl::detail::Carries(api, &wxl_terrain_api::trace)) return PushResultFailure(L, WXL_E_UNSUPPORTED);
            wxl_terrain_hit hit{};
            hit.struct_size = sizeof hit;
            if (api->trace(start, finish, &hit) != WXL_TRUE)
            {
                lua_pushnil(L);
                return 1;
            }
            lua_createtable(L, 0, 5);
            PushVec3(L, hit.point);
            lua_setfield(L, -2, "point");
            PushVec3(L, hit.normal);
            lua_setfield(L, -2, "normal");
            lua_pushinteger(L, hit.chunk_x);
            lua_setfield(L, -2, "chunk_x");
            lua_pushinteger(L, hit.chunk_y);
            lua_setfield(L, -2, "chunk_y");
            lua_pushnumber(L, hit.fraction);
            lua_setfield(L, -2, "fraction");
            return 1;
        }

        int L_TerrainLocate(lua_State* L)
        {
            const double           x   = luaL_checknumber(L, 1);
            const double           y   = luaL_checknumber(L, 2);
            const wxl_terrain_api* api = Terrain();
            if (!api || !wxl::detail::Carries(api, &wxl_terrain_api::locate)) return PushResultFailure(L, WXL_E_UNSUPPORTED);
            wxl_terrain_place place{};
            place.struct_size = sizeof place;
            if (api->locate(x, y, &place) != WXL_TRUE)
            {
                lua_pushnil(L);
                return 1;
            }
            lua_createtable(L, 0, 6);
            lua_pushinteger(L, place.chunk_x);
            lua_setfield(L, -2, "chunk_x");
            lua_pushinteger(L, place.chunk_y);
            lua_setfield(L, -2, "chunk_y");
            lua_pushinteger(L, place.cell_row);
            lua_setfield(L, -2, "cell_row");
            lua_pushinteger(L, place.cell_col);
            lua_setfield(L, -2, "cell_col");
            lua_pushinteger(L, place.nearest);
            lua_setfield(L, -2, "nearest");
            lua_pushboolean(L, place.loaded == WXL_TRUE);
            lua_setfield(L, -2, "loaded");
            return 1;
        }

        int L_TerrainVertices(lua_State* L)
        {
            const int32_t          cx       = CheckChunk(L, 1, "Vertices", "chunk_x");
            const int32_t          cy       = CheckChunk(L, 2, "Vertices", "chunk_y");
            const bool             original = lua_toboolean(L, 3) != 0;
            const wxl_terrain_api* api      = Terrain();
            if (!api || !wxl::detail::Carries(api, &wxl_terrain_api::vertices)) return PushResultFailure(L, WXL_E_UNSUPPORTED);
            wxl_vec3       out[WXL_TERRAIN_CHUNK_VERTICES];
            const uint32_t count = api->vertices(cx, cy, original ? WXL_TRUE : WXL_FALSE, out, WXL_TERRAIN_CHUNK_VERTICES);
            if (!count) return PushResultFailure(L, WXL_E_NOT_FOUND);
            lua_createtable(L, static_cast<int>(count), 0);
            for (uint32_t i = 0; i < count && i < WXL_TERRAIN_CHUNK_VERTICES; ++i)
            {
                PushVec3(L, out[i]);
                lua_rawseti(L, -2, static_cast<int>(i) + 1);
            }
            return 1;
        }

        int L_TerrainVerticesIn(lua_State* L)
        {
            const double           x      = luaL_checknumber(L, 1);
            const double           y      = luaL_checknumber(L, 2);
            const double           radius = luaL_checknumber(L, 3);
            const wxl_terrain_api* api    = Terrain();
            if (!api || !wxl::detail::Carries(api, &wxl_terrain_api::vertices_in)) return PushResultFailure(L, WXL_E_UNSUPPORTED);
            const uint32_t                  count = api->vertices_in(x, y, radius, nullptr, 0);
            std::vector<wxl_terrain_vertex> out(count);
            const uint32_t written = count ? api->vertices_in(x, y, radius, out.data(), count) : 0;
            const uint32_t n       = written < count ? written : count;
            lua_createtable(L, static_cast<int>(n), 0);
            for (uint32_t i = 0; i < n; ++i)
            {
                const wxl_terrain_vertex& v = out[i];
                lua_createtable(L, 0, 9);
                lua_pushinteger(L, v.chunk_x);
                lua_setfield(L, -2, "chunk_x");
                lua_pushinteger(L, v.chunk_y);
                lua_setfield(L, -2, "chunk_y");
                lua_pushinteger(L, v.index);
                lua_setfield(L, -2, "index");
                lua_pushnumber(L, v.x);
                lua_setfield(L, -2, "x");
                lua_pushnumber(L, v.y);
                lua_setfield(L, -2, "y");
                lua_pushnumber(L, v.z);
                lua_setfield(L, -2, "z");
                lua_pushnumber(L, v.original_z);
                lua_setfield(L, -2, "original_z");
                lua_pushboolean(L, v.inner == WXL_TRUE);
                lua_setfield(L, -2, "inner");
                lua_pushboolean(L, v.loaded == WXL_TRUE);
                lua_setfield(L, -2, "loaded");
                lua_rawseti(L, -2, static_cast<int>(i) + 1);
            }
            return 1;
        }

        /// A number field of an edit's entry; a Lua error naming the entry when it is not one.
        double EditField(lua_State* L, int position, const char* name)
        {
            lua_getfield(L, -1, name);
            if (!lua_isnumber(L, -1))
                luaL_error(L, "wxl.terrain.SetHeights: entry %d must be a table {chunk_x, chunk_y, index, height} of numbers; its %s is %s",
                           position, name, luaL_typename(L, -1));
            const double value = lua_tonumber(L, -1);
            lua_pop(L, 1);
            return value;
        }

        int L_TerrainSetHeights(lua_State* L)
        {
            ScriptMod& mod = Self(L);
            if (!lua_istable(L, 1))
                luaL_error(L, "wxl.terrain.SetHeights: 'heights' (argument 1) must be a list of {chunk_x, chunk_y, index, height}");
            const int                       count = static_cast<int>(lua_objlen(L, 1));
            std::vector<wxl_terrain_height> heights;
            heights.reserve(static_cast<size_t>(count));
            for (int n = 1; n <= count; ++n)
            {
                lua_rawgeti(L, 1, n);
                if (!lua_istable(L, -1))
                    luaL_error(L, "wxl.terrain.SetHeights: entry %d must be a table {chunk_x, chunk_y, index, height}", n);
                wxl_terrain_height h{};
                h.chunk_x = static_cast<int32_t>(EditField(L, n, "chunk_x"));
                h.chunk_y = static_cast<int32_t>(EditField(L, n, "chunk_y"));
                const double index = EditField(L, n, "index");
                h.index  = index < 0 ? 0xFFFFFFFFu : static_cast<uint32_t>(index);
                h.height = static_cast<float>(EditField(L, n, "height"));
                lua_pop(L, 1);
                heights.push_back(h);
            }
            const wxl_terrain_api* api = Terrain();
            if (!api || !wxl::detail::Carries(api, &wxl_terrain_api::set_heights)) return PushResultFailure(L, WXL_E_UNSUPPORTED);
            const wxl_result result = api->set_heights(heights.data(), static_cast<uint32_t>(heights.size()), TerrainLayer(mod));
            if (result != WXL_OK) return PushResultFailure(L, result);
            lua_pushboolean(L, 1);
            return 1;
        }

        int L_TerrainReset(lua_State* L)
        {
            ScriptMod&             mod = Self(L);
            const int32_t          cx  = CheckChunk(L, 1, "Reset", "chunk_x");
            const int32_t          cy  = CheckChunk(L, 2, "Reset", "chunk_y");
            const wxl_terrain_api* api = Terrain();
            if (!api || !wxl::detail::Carries(api, &wxl_terrain_api::reset_chunk)) return PushResultFailure(L, WXL_E_UNSUPPORTED);
            const wxl_result result = api->reset_chunk(cx, cy, TerrainLayer(mod));
            if (result != WXL_OK) return PushResultFailure(L, result);
            lua_pushboolean(L, 1);
            return 1;
        }

        int L_TerrainResetAll(lua_State* L)
        {
            ScriptMod&             mod = Self(L);
            const wxl_terrain_api* api = Terrain();
            if (!api || !wxl::detail::Carries(api, &wxl_terrain_api::reset_all)) return PushResultFailure(L, WXL_E_UNSUPPORTED);
            const wxl_result result = api->reset_all(TerrainLayer(mod));
            if (result != WXL_OK) return PushResultFailure(L, result);
            lua_pushboolean(L, 1);
            return 1;
        }

        int L_TerrainStats(lua_State* L)
        {
            const wxl_terrain_api* api = Terrain();
            if (!api || !wxl::detail::Carries(api, &wxl_terrain_api::stats)) return PushResultFailure(L, WXL_E_UNSUPPORTED);
            wxl_terrain_stats stats{};
            stats.struct_size       = sizeof stats;
            const wxl_result result = api->stats(&stats);
            if (result != WXL_OK) return PushResultFailure(L, result);
            lua_createtable(L, 0, 8);
            const auto field = [&](const char* name, double value) {
                lua_pushnumber(L, value);
                lua_setfield(L, -2, name);
            };
            field("edited_chunks", stats.edited_chunks);
            field("edited_vertices", stats.edited_vertices);
            field("edits", static_cast<double>(stats.edits));
            field("reapplied", static_cast<double>(stats.reapplied));
            field("last_vertices", stats.last_vertices);
            field("last_chunks", stats.last_chunks);
            field("last_microseconds", stats.last_microseconds);
            field("max_microseconds", stats.max_microseconds);
            return 1;
        }

        /// A Lua mod's edits come off when it goes.
        void ReleaseTerrain(ScriptMod& mod)
        {
            for (size_t i = 0; i < g_terrainLayers.size(); ++i)
            {
                if (g_terrainLayers[i].first != &mod) continue;
                const wxl_terrain_api* api = Terrain();
                if (api && wxl::detail::Carries(api, &wxl_terrain_api::reset_all)) api->reset_all(g_terrainLayers[i].second);
                g_terrainLayers.erase(g_terrainLayers.begin() + static_cast<std::ptrdiff_t>(i));
                return;
            }
        }

        // --- wxl.render: tables into descriptions, push constants, the shaders each Lua mod replaced -

        const wxl_render_api* Render() { return wxl::api::Table<wxl_render_api>(); }

        /// A name of a list, or a whole number; @p where names the function and parameter for an error.
        uint32_t CheckNamed(lua_State* L, int index, const char* const* names, uint32_t count, uint32_t first, const char* where)
        {
            if (lua_type(L, index) == LUA_TNUMBER)
            {
                const double value = lua_tonumber(L, index);
                if (value < 0.0 || value > 4294967295.0 || value != static_cast<double>(static_cast<uint32_t>(value)))
                    luaL_error(L, "%s must be a whole number from 0, or a name", where);
                return static_cast<uint32_t>(value);
            }
            if (lua_type(L, index) == LUA_TSTRING)
            {
                const char* text = lua_tostring(L, index);
                for (uint32_t i = 0; i < count; ++i)
                    if (std::strcmp(text, names[i]) == 0) return first + i;
            }
            std::string choices;
            for (uint32_t i = 0; i < count; ++i) choices += std::string(i ? ", \"" : "\"") + names[i] + "\"";
            luaL_error(L, "%s must be one of %s, or its number", where, choices.c_str());
            return 0;
        }

        constexpr const char* kRenderFormats[] = {"rgba8", "bgra8", "r8", "rg8", "r16f", "rg16f", "rgba16f", "r32f", "rgba32f", "depth32f"};
        constexpr const char* kRenderKinds[]   = {"graphics", "fullscreen", "batch", "compute"};
        constexpr const char* kRenderBlends[]  = {"none", "alpha", "add", "premultiplied", "multiply"};
        constexpr const char* kRenderDepths[]  = {"off", "less", "less_equal", "equal", "greater", "greater_equal", "always"};
        constexpr const char* kRenderCulls[]   = {"none", "back", "front", "batch"};
        constexpr const char* kRenderShapes[]  = {"triangles", "triangle_strip", "lines", "points"};
        constexpr uint32_t    kSpirvMagic      = 0x07230203u;

        /// SPIR-V from a Lua string: the bytes themselves when they start with SPIR-V's magic,
        /// otherwise the path of a file of the mod's own folder, read into @p bytes.
        bool ReadSpirv(lua_State* L, ScriptMod& mod, int index, std::string& bytes, const char* where)
        {
            size_t      size = 0;
            const char* text = lua_tolstring(L, index, &size);
            if (!text) luaL_error(L, "%s must be SPIR-V: a string of its bytes, or the path of a .spv file of the mod's folder", where);
            uint32_t magic = 0;
            if (size >= 4) std::memcpy(&magic, text, 4);
            if (magic == kSpirvMagic)
            {
                bytes.assign(text, size);
                return true;
            }
            const std::filesystem::path relative = std::filesystem::path(std::string(text, size)).lexically_normal();
            const std::string           first    = relative.empty() ? std::string() : relative.begin()->string();
            if (relative.empty() || relative.is_absolute() || relative.has_root_name() || first == "..")
                luaL_error(L, "%s: \"%s\" is not inside the mod's folder; give a path relative to it", where, text);
            std::ifstream file(mod.Directory() / relative, std::ios::binary);
            if (!file) luaL_error(L, "%s: \"%s\" cannot be read from the mod's folder", where, text);
            bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
            std::memcpy(&magic, bytes.size() >= 4 ? bytes.data() : "\0\0\0\0", 4);
            if (magic != kSpirvMagic) luaL_error(L, "%s: \"%s\" is not SPIR-V (no magic number at its start)", where, text);
            return true;
        }

        /// Push constants or a batch's constants: a string of bytes, a table of numbers (each a
        /// 32-bit float), or nothing.
        void ReadPush(lua_State* L, int index, std::string& bytes, const char* where)
        {
            bytes.clear();
            if (lua_isnoneornil(L, index)) return;
            if (lua_type(L, index) == LUA_TSTRING)
            {
                size_t      size = 0;
                const char* text = lua_tolstring(L, index, &size);
                bytes.assign(text, size);
                return;
            }
            if (lua_type(L, index) != LUA_TTABLE) luaL_error(L, "%s must be a table of numbers or a string of bytes", where);
            const size_t count = lua_objlen(L, index);
            if (count > WXL_RENDER_BATCH_BYTES / 4) luaL_error(L, "%s holds more than %d numbers", where, WXL_RENDER_BATCH_BYTES / 4);
            bytes.resize(count * 4);
            for (size_t i = 0; i < count; ++i)
            {
                lua_rawgeti(L, index, static_cast<int>(i) + 1);
                if (lua_type(L, -1) != LUA_TNUMBER) luaL_error(L, "%s: entry %d is not a number", where, static_cast<int>(i) + 1);
                const float value = static_cast<float>(lua_tonumber(L, -1));
                std::memcpy(&bytes[i * 4], &value, 4);
                lua_pop(L, 1);
            }
        }

        /// A table field as a named value (absent: @p absent).
        uint32_t FieldNamed(lua_State* L, int table, const char* field, const char* const* names, uint32_t count, uint32_t first,
                            uint32_t absent, const char* function)
        {
            lua_getfield(L, table, field);
            uint32_t value = absent;
            if (!lua_isnil(L, -1))
            {
                const std::string where = std::string(function) + ": '" + field + "'";
                value                   = CheckNamed(L, lua_gettop(L), names, count, first, where.c_str());
            }
            lua_pop(L, 1);
            return value;
        }

        /// CreatePipeline(desc): a Pipeline, or nil and why.
        int L_RenderCreatePipeline(lua_State* L)
        {
            ScriptMod& mod = Self(L);
            const char* fn = "wxl.render.CreatePipeline";
            if (!lua_istable(L, 1)) luaL_error(L, "%s: 'desc' (argument 1) must be a table", fn);
            wxl_render_pipeline_desc desc{};
            desc.struct_size = sizeof desc;
            desc.kind        = FieldNamed(L, 1, "kind", kRenderKinds, 4, 0, WXL_RENDER_PIPELINE_GRAPHICS, fn);

            std::string vertex, fragment, compute, name;
            lua_getfield(L, 1, "name");
            if (lua_type(L, -1) == LUA_TSTRING) name = lua_tostring(L, -1);
            lua_pop(L, 1);
            desc.name = name.empty() ? nullptr : name.c_str();
            if (desc.kind == WXL_RENDER_PIPELINE_COMPUTE)
            {
                // A compute program alone: it draws into nothing.
                lua_getfield(L, 1, "compute");
                if (lua_isnil(L, -1)) luaL_error(L, "%s: 'compute' is missing; a compute pipeline takes SPIR-V", fn);
                ReadSpirv(L, mod, lua_gettop(L), compute, "wxl.render.CreatePipeline: 'compute'");
                lua_pop(L, 1);
                desc.compute      = compute.data();
                desc.compute_size = static_cast<uint32_t>(compute.size());
                const wxl_render_api* api = Render();
                if (!api || !wxl::detail::Carries(api, &wxl_render_api::create_pipeline)) return PushResultFailure(L, WXL_E_UNSUPPORTED);
                const wxl_handle made = api->create_pipeline(&desc);
                if (made == WXL_HANDLE_NONE)
                    return PushResultFailure(L, api->supported && api->supported() ? WXL_E_INVALID : WXL_E_UNSUPPORTED);
                PushRenderResource(L, RenderResource::pipeline, made);
                return 1;
            }
            lua_getfield(L, 1, "vertex");
            if (!lua_isnil(L, -1)) ReadSpirv(L, mod, lua_gettop(L), vertex, "wxl.render.CreatePipeline: 'vertex'");
            lua_pop(L, 1);
            lua_getfield(L, 1, "fragment");
            if (lua_isnil(L, -1)) luaL_error(L, "%s: 'fragment' is missing; it takes SPIR-V", fn);
            ReadSpirv(L, mod, lua_gettop(L), fragment, "wxl.render.CreatePipeline: 'fragment'");
            lua_pop(L, 1);
            if (desc.kind == WXL_RENDER_PIPELINE_GRAPHICS && vertex.empty())
                luaL_error(L, "%s: 'vertex' is missing; a graphics pipeline takes a vertex program", fn);
            desc.vertex        = vertex.empty() ? nullptr : vertex.data();
            desc.vertex_size   = static_cast<uint32_t>(vertex.size());
            desc.fragment      = fragment.data();
            desc.fragment_size = static_cast<uint32_t>(fragment.size());

            // The colour formats: a list, one name, or the frame's format when left out.
            lua_getfield(L, 1, "colors");
            const int colors = lua_gettop(L);
            if (lua_isnil(L, colors))
            {
                desc.color_count      = 1;
                desc.color_formats[0] = WXL_RENDER_FORMAT_BGRA8;
            }
            else if (lua_istable(L, colors))
            {
                const size_t count = lua_objlen(L, colors);
                if (count > WXL_RENDER_MAX_COLORS) luaL_error(L, "%s: 'colors' holds more than %d formats", fn, WXL_RENDER_MAX_COLORS);
                for (size_t i = 0; i < count; ++i)
                {
                    lua_rawgeti(L, colors, static_cast<int>(i) + 1);
                    desc.color_formats[i] = CheckNamed(L, lua_gettop(L), kRenderFormats, 9, 1, "wxl.render.CreatePipeline: an entry of 'colors'");
                    lua_pop(L, 1);
                }
                desc.color_count = static_cast<uint32_t>(count);
            }
            else
            {
                desc.color_formats[0] = CheckNamed(L, colors, kRenderFormats, 9, 1, "wxl.render.CreatePipeline: 'colors'");
                desc.color_count      = 1;
            }
            lua_pop(L, 1);

            lua_getfield(L, 1, "depth");
            if (!lua_isnil(L, -1)) desc.depth_format = CheckNamed(L, lua_gettop(L), kRenderFormats + 9, 1, WXL_RENDER_FORMAT_DEPTH32F,
                                                                  "wxl.render.CreatePipeline: 'depth'");
            lua_pop(L, 1);
            lua_getfield(L, 1, "samples");
            if (!lua_isnil(L, -1))
            {
                const double samples = luaL_checknumber(L, -1);
                if (samples != 0 && samples != 1 && samples != 2 && samples != 4 && samples != 8)
                    luaL_error(L, "%s: 'samples' must be 0 (the frame's), 1, 2, 4 or 8", fn);
                desc.samples = static_cast<uint32_t>(samples);
            }
            lua_pop(L, 1);

            // Blending: one mode for every attachment, or one per attachment.
            lua_getfield(L, 1, "blend");
            const int blend = lua_gettop(L);
            if (lua_istable(L, blend))
            {
                const size_t count = lua_objlen(L, blend);
                for (size_t i = 0; i < count && i < WXL_RENDER_MAX_COLORS; ++i)
                {
                    lua_rawgeti(L, blend, static_cast<int>(i) + 1);
                    desc.blend[i] = CheckNamed(L, lua_gettop(L), kRenderBlends, 5, 0, "wxl.render.CreatePipeline: an entry of 'blend'");
                    lua_pop(L, 1);
                }
            }
            else if (!lua_isnil(L, blend))
            {
                const uint32_t mode = CheckNamed(L, blend, kRenderBlends, 5, 0, "wxl.render.CreatePipeline: 'blend'");
                for (uint32_t& b : desc.blend) b = mode;
            }
            lua_pop(L, 1);

            desc.depth_test = FieldNamed(L, 1, "depth_test", kRenderDepths, 7, 0, WXL_RENDER_DEPTH_OFF, fn);
            lua_getfield(L, 1, "depth_write");
            desc.depth_write = lua_toboolean(L, -1) ? WXL_TRUE : WXL_FALSE;
            lua_pop(L, 1);
            desc.cull     = FieldNamed(L, 1, "cull", kRenderCulls, 4, 0,
                                       desc.kind == WXL_RENDER_PIPELINE_BATCH ? WXL_RENDER_CULL_BATCH : WXL_RENDER_CULL_NONE, fn);
            desc.topology = FieldNamed(L, 1, "topology", kRenderShapes, 4, 0, WXL_RENDER_TOPOLOGY_TRIANGLES, fn);

            const wxl_render_api* api = Render();
            if (!api || !wxl::detail::Carries(api, &wxl_render_api::create_pipeline)) return PushResultFailure(L, WXL_E_UNSUPPORTED);
            const wxl_handle made = api->create_pipeline(&desc);
            if (made == WXL_HANDLE_NONE)
                return PushResultFailure(L, api->supported && api->supported() ? WXL_E_INVALID : WXL_E_UNSUPPORTED);
            PushRenderResource(L, RenderResource::pipeline, made);
            return 1;
        }

        /// An attachment of a pass: an Image, the frame's "scene_color" or "scene_depth", or a
        /// table {image, clear = ...}; @p cleared says whether the table asked for a clear.
        wxl_handle CheckAttachment(lua_State* L, int index, bool depth, bool& cleared, float* clear, const char* where)
        {
            cleared = false;
            int at  = index;
            if (lua_istable(L, index))
            {
                lua_rawgeti(L, index, 1);
                at = lua_gettop(L);
                lua_getfield(L, index, "clear");
                if (!lua_isnil(L, -1))
                {
                    cleared = true;
                    if (depth)
                    {
                        if (lua_type(L, -1) != LUA_TNUMBER) luaL_error(L, "%s: 'clear' must be a depth, a number from 0 to 1", where);
                        clear[0] = static_cast<float>(lua_tonumber(L, -1));
                    }
                    else
                    {
                        if (!lua_istable(L, -1)) luaL_error(L, "%s: 'clear' must be a colour, a table {r, g, b, a}", where);
                        for (int c = 0; c < 4; ++c)
                        {
                            lua_rawgeti(L, -1, c + 1);
                            clear[c] = lua_isnumber(L, -1) ? static_cast<float>(lua_tonumber(L, -1)) : (c == 3 ? 1.0f : 0.0f);
                            lua_pop(L, 1);
                        }
                    }
                }
                lua_pop(L, 1);
            }
            wxl_handle handle = WXL_HANDLE_NONE;
            if (lua_type(L, at) == LUA_TSTRING)
            {
                const char* text = lua_tostring(L, at);
                if (!depth && std::strcmp(text, "scene_color") == 0) handle = WXL_RENDER_SCENE_COLOR;
                if (depth && std::strcmp(text, "scene_depth") == 0) handle = WXL_RENDER_SCENE_DEPTH;
            }
            else
                handle = ToRenderResource(L, at, RenderResource::image);
            if (handle == WXL_HANDLE_NONE)
                luaL_error(L, "%s must be an image of wxl.render (not given back), \"%s\", or a table {image, clear = ...}", where,
                           depth ? "scene_depth" : "scene_color");
            if (at != index) lua_pop(L, 1);
            return handle;
        }

        /// Begin(pass): true, or nil and why.
        int L_RenderBegin(lua_State* L)
        {
            if (!lua_istable(L, 1)) luaL_error(L, "wxl.render.Begin: 'pass' (argument 1) must be a table {colors = {...}, depth = ...}");
            wxl_render_pass pass{};
            pass.struct_size = sizeof pass;
            lua_getfield(L, 1, "colors");
            const int colors = lua_gettop(L);
            if (lua_istable(L, colors))
            {
                const size_t count = lua_objlen(L, colors);
                if (count > WXL_RENDER_MAX_COLORS) luaL_error(L, "wxl.render.Begin: 'colors' holds more than %d attachments", WXL_RENDER_MAX_COLORS);
                for (size_t i = 0; i < count; ++i)
                {
                    lua_rawgeti(L, colors, static_cast<int>(i) + 1);
                    bool cleared = false;
                    pass.colors[i] = CheckAttachment(L, lua_gettop(L), false, cleared, pass.clear_colors[i],
                                                     "wxl.render.Begin: an entry of 'colors'");
                    if (cleared) pass.clear |= 1u << i;
                    lua_pop(L, 1);
                }
                pass.color_count = static_cast<uint32_t>(count);
            }
            else if (!lua_isnil(L, colors))
                luaL_error(L, "wxl.render.Begin: 'colors' must be a list of attachments");
            lua_pop(L, 1);
            lua_getfield(L, 1, "depth");
            if (!lua_isnil(L, -1))
            {
                bool  cleared = false;
                float value[4] = {1.0f, 0.0f, 0.0f, 0.0f};
                pass.depth     = CheckAttachment(L, lua_gettop(L), true, cleared, value, "wxl.render.Begin: 'depth'");
                if (cleared)
                {
                    pass.clear |= WXL_RENDER_CLEAR_DEPTH;
                    pass.clear_depth = value[0];
                }
            }
            lua_pop(L, 1);
            const wxl_render_api* api = Render();
            if (!api || !wxl::detail::Carries(api, &wxl_render_api::begin)) return PushResultFailure(L, WXL_E_UNSUPPORTED);
            const wxl_result result = api->begin(&pass);
            if (result != WXL_OK) return PushResultFailure(L, result);
            lua_pushboolean(L, 1);
            return 1;
        }

        wxl_handle CheckPipeline(lua_State* L, int index, const char* fn)
        {
            const wxl_handle pipeline = ToRenderResource(L, index, RenderResource::pipeline);
            if (pipeline == WXL_HANDLE_NONE)
                luaL_error(L, "%s: 'pipeline' (argument %d) must be a pipeline of wxl.render, not given back", fn, index);
            return pipeline;
        }

        uint32_t CheckRenderCount(lua_State* L, int index, const char* fn, const char* name)
        {
            if (lua_isnoneornil(L, index)) luaL_error(L, "%s: '%s' (argument %d) is missing; it takes a whole number", fn, name, index);
            const double value = luaL_checknumber(L, index);
            if (value < 0.0 || value > 4294967295.0 || value != static_cast<double>(static_cast<uint32_t>(value)))
                luaL_error(L, "%s: '%s' (argument %d) must be a whole number from 0", fn, name, index);
            return static_cast<uint32_t>(value);
        }

        int PushOk(lua_State* L, wxl_result result)
        {
            if (result != WXL_OK) return PushResultFailure(L, result);
            lua_pushboolean(L, 1);
            return 1;
        }

        /// Draw(pipeline, vertex_count, instance_count [, push])
        int L_RenderDraw(lua_State* L)
        {
            const char*      fn       = "wxl.render.Draw";
            const wxl_handle pipeline = CheckPipeline(L, 1, fn);
            const uint32_t   vertices = CheckRenderCount(L, 2, fn, "vertex_count");
            const uint32_t   copies   = lua_isnoneornil(L, 3) ? 1u : CheckRenderCount(L, 3, fn, "instance_count");
            std::string      push;
            ReadPush(L, 4, push, "wxl.render.Draw: 'push' (argument 4)");
            const wxl_render_api* api = Render();
            if (!api || !wxl::detail::Carries(api, &wxl_render_api::draw)) return PushResultFailure(L, WXL_E_UNSUPPORTED);
            return PushOk(L, api->draw(pipeline, vertices, copies, push.empty() ? nullptr : push.data(), static_cast<uint32_t>(push.size())));
        }

        /// DrawIndexed(pipeline, indices, first_index, index_count, instance_count [, push])
        int L_RenderDrawIndexed(lua_State* L)
        {
            const char*      fn       = "wxl.render.DrawIndexed";
            const wxl_handle pipeline = CheckPipeline(L, 1, fn);
            const wxl_handle indices  = ToRenderResource(L, 2, RenderResource::buffer);
            if (indices == WXL_HANDLE_NONE) luaL_error(L, "%s: 'indices' (argument 2) must be a buffer of wxl.render, not given back", fn);
            const uint32_t first  = CheckRenderCount(L, 3, fn, "first_index");
            const uint32_t count  = CheckRenderCount(L, 4, fn, "index_count");
            const uint32_t copies = lua_isnoneornil(L, 5) ? 1u : CheckRenderCount(L, 5, fn, "instance_count");
            std::string    push;
            ReadPush(L, 6, push, "wxl.render.DrawIndexed: 'push' (argument 6)");
            const wxl_render_api* api = Render();
            if (!api || !wxl::detail::Carries(api, &wxl_render_api::draw_indexed)) return PushResultFailure(L, WXL_E_UNSUPPORTED);
            return PushOk(L, api->draw_indexed(pipeline, indices, first, count, copies, push.empty() ? nullptr : push.data(),
                                               static_cast<uint32_t>(push.size())));
        }

        /// Fullscreen(pipeline [, push])
        int L_RenderFullscreen(lua_State* L)
        {
            const wxl_handle pipeline = CheckPipeline(L, 1, "wxl.render.Fullscreen");
            std::string      push;
            ReadPush(L, 2, push, "wxl.render.Fullscreen: 'push' (argument 2)");
            const wxl_render_api* api = Render();
            if (!api || !wxl::detail::Carries(api, &wxl_render_api::fullscreen)) return PushResultFailure(L, WXL_E_UNSUPPORTED);
            return PushOk(L, api->fullscreen(pipeline, push.empty() ? nullptr : push.data(), static_cast<uint32_t>(push.size())));
        }

        /// DrawBatch(batch, pipeline [, constants])
        int L_RenderDrawBatch(lua_State* L)
        {
            const char* fn = "wxl.render.DrawBatch";
            if (lua_type(L, 1) != LUA_TLIGHTUSERDATA)
                luaL_error(L, "%s: 'batch' (argument 1) must be a batch: OnModelBatchDrawn's, or one KeepBatch answered", fn);
            const void*      batch    = lua_touserdata(L, 1);
            const wxl_handle pipeline = CheckPipeline(L, 2, fn);
            std::string      constants;
            ReadPush(L, 3, constants, "wxl.render.DrawBatch: 'constants' (argument 3)");
            const wxl_render_api* api = Render();
            if (!api || !wxl::detail::Carries(api, &wxl_render_api::draw_batch)) return PushResultFailure(L, WXL_E_UNSUPPORTED);
            return PushOk(L, api->draw_batch(batch, pipeline, constants.empty() ? nullptr : constants.data(),
                                             static_cast<uint32_t>(constants.size())));
        }

        /// Dispatch(pipeline, x, y, z [, push])
        int L_RenderDispatch(lua_State* L)
        {
            const char*      fn       = "wxl.render.Dispatch";
            const wxl_handle pipeline = CheckPipeline(L, 1, fn);
            const uint32_t   x        = CheckRenderCount(L, 2, fn, "x");
            const uint32_t   y        = CheckRenderCount(L, 3, fn, "y");
            const uint32_t   z        = CheckRenderCount(L, 4, fn, "z");
            std::string      push;
            ReadPush(L, 5, push, "wxl.render.Dispatch: 'push' (argument 5)");
            const wxl_render_api* api = Render();
            if (!api || !wxl::detail::Carries(api, &wxl_render_api::dispatch)) return PushResultFailure(L, WXL_E_UNSUPPORTED);
            return PushOk(L, api->dispatch(pipeline, x, y, z, push.empty() ? nullptr : push.data(), static_cast<uint32_t>(push.size())));
        }

        /// BonePalette(matrices): their address on the GPU, or nil and why. A string of bytes, or a
        /// table of numbers (32-bit floats), 12 a matrix.
        int L_RenderBonePalette(lua_State* L)
        {
            const char* fn = "wxl.render.BonePalette";
            std::string bytes;
            if (lua_type(L, 1) == LUA_TSTRING)
            {
                size_t      size = 0;
                const char* text = lua_tolstring(L, 1, &size);
                bytes.assign(text, size);
            }
            else if (lua_type(L, 1) == LUA_TTABLE)
            {
                const size_t count = lua_objlen(L, 1);
                if (count > (16u << 20) / 4) luaL_error(L, "%s: 'matrices' (argument 1) holds more than 16 MiB of numbers", fn);
                bytes.resize(count * 4);
                for (size_t i = 0; i < count; ++i)
                {
                    lua_rawgeti(L, 1, static_cast<int>(i) + 1);
                    if (lua_type(L, -1) != LUA_TNUMBER) luaL_error(L, "%s: entry %d of 'matrices' is not a number", fn, static_cast<int>(i) + 1);
                    const float value = static_cast<float>(lua_tonumber(L, -1));
                    std::memcpy(&bytes[i * 4], &value, 4);
                    lua_pop(L, 1);
                }
            }
            else luaL_error(L, "%s: 'matrices' (argument 1) must be a table of numbers (12 a matrix) or a string of bytes", fn);
            if (bytes.empty() || bytes.size() % WXL_RENDER_PALETTE_MATRIX_BYTES)
                luaL_error(L, "%s: 'matrices' (argument 1) must hold whole matrices: 12 numbers, or 48 bytes, each", fn);
            const wxl_render_api* api = Render();
            if (!api || !wxl::detail::Carries(api, &wxl_render_api::bone_palette)) return PushResultFailure(L, WXL_E_UNSUPPORTED);
            uint64_t         address = 0;
            const wxl_result result  = api->bone_palette(bytes.data(), static_cast<uint32_t>(bytes.size()), &address);
            if (result != WXL_OK) return PushResultFailure(L, result);
            lua_pushnumber(L, static_cast<lua_Number>(address));
            return 1;
        }

        /// The client shaders a Lua mod replaced: the runtime sees them as wxl.lua's, by name and stage.
        struct Replaced
        {
            ScriptMod*  mod;
            std::string name;
            uint32_t    stage;
        };
        std::vector<Replaced> g_replaced;

        uint32_t CheckStage(lua_State* L, int index, const char* fn)
        {
            const uint32_t stage = CheckRenderCount(L, index, fn, "stage");
            if (stage > 1) luaL_error(L, "%s: 'stage' (argument %d) must be 0 (vertex) or 1 (pixel)", fn, index);
            return stage;
        }

        /// ReplaceShader(name, stage, spirv): spirv the bytes, or a .spv file of the mod's folder.
        int L_RenderReplaceShader(lua_State* L)
        {
            ScriptMod&  mod  = Self(L);
            const char* fn   = "wxl.render.ReplaceShader";
            size_t      size = 0;
            if (lua_type(L, 1) != LUA_TSTRING) luaL_error(L, "%s: 'name' (argument 1) must be a shader's name, or the start of names", fn);
            const char*    text  = lua_tolstring(L, 1, &size);
            const uint32_t stage = CheckStage(L, 2, fn);
            if (lua_isnoneornil(L, 3)) luaL_error(L, "%s: 'spirv' (argument 3) is missing; it takes SPIR-V", fn);
            std::string spirv;
            ReadSpirv(L, mod, 3, spirv, "wxl.render.ReplaceShader: 'spirv' (argument 3)");
            const wxl_render_api* api = Render();
            if (!api || !wxl::detail::Carries(api, &wxl_render_api::replace_shader)) return PushResultFailure(L, WXL_E_UNSUPPORTED);
            const std::string name(text, size);
            const wxl_result  result = api->replace_shader(Str(name), stage, spirv.data(), static_cast<uint32_t>(spirv.size()));
            if (result != WXL_OK) return PushResultFailure(L, result);
            for (Replaced& held : g_replaced)
                if (held.name == name && held.stage == stage)
                {
                    held.mod = &mod; // the last Lua mod to replace it holds it
                    lua_pushboolean(L, 1);
                    return 1;
                }
            g_replaced.push_back(Replaced{&mod, name, stage});
            lua_pushboolean(L, 1);
            return 1;
        }

        /// RestoreShader(name, stage)
        int L_RenderRestoreShader(lua_State* L)
        {
            ScriptMod&  mod  = Self(L);
            const char* fn   = "wxl.render.RestoreShader";
            size_t      size = 0;
            if (lua_type(L, 1) != LUA_TSTRING) luaL_error(L, "%s: 'name' (argument 1) must be the name ReplaceShader was given", fn);
            const std::string name(lua_tolstring(L, 1, &size), size);
            const uint32_t    stage = CheckStage(L, 2, fn);
            const auto held = std::find_if(g_replaced.begin(), g_replaced.end(), [&](const Replaced& r) {
                return r.mod == &mod && r.name == name && r.stage == stage;
            });
            if (held == g_replaced.end()) return PushResultFailure(L, WXL_E_NOT_FOUND);
            g_replaced.erase(held);
            const wxl_render_api* api = Render();
            if (!api || !wxl::detail::Carries(api, &wxl_render_api::restore_shader)) return PushResultFailure(L, WXL_E_UNSUPPORTED);
            return PushOk(L, api->restore_shader(Str(name), stage));
        }

        /// A Lua mod's replaced shaders get the client's programs back.
        void RestoreShaders(ScriptMod& mod)
        {
            const wxl_render_api* api = Render();
            for (size_t i = g_replaced.size(); i-- > 0;)
            {
                if (g_replaced[i].mod != &mod) continue;
                if (api && wxl::detail::Carries(api, &wxl_render_api::restore_shader))
                    api->restore_shader(Str(g_replaced[i].name), g_replaced[i].stage);
                g_replaced.erase(g_replaced.begin() + static_cast<std::ptrdiff_t>(i));
            }
        }
        // --- wxl.draw: tables into shapes and styles ---------------------------------------------

        const wxl_draw_api* Draw() { return wxl::api::Table<wxl_draw_api>(); }

        constexpr const char* kDrawKinds[]  = {"line", "circle", "ring", "sector", "rectangle", "polygon", "grid", "box", "sphere", "cylinder"};
        constexpr const char* kDrawDepths[] = {"tested", "on_top"};

        /// A point of a table {x, y, z} or {x, y, z} by position; false for anything else.
        bool ReadPoint(lua_State* L, int index, wxl_vec3& out)
        {
            if (!lua_istable(L, index)) return false;
            index = index < 0 ? lua_gettop(L) + index + 1 : index;
            lua_getfield(L, index, "x");
            if (lua_isnil(L, -1))
            {
                lua_pop(L, 1);
                lua_rawgeti(L, index, 1);
                lua_rawgeti(L, index, 2);
                lua_rawgeti(L, index, 3);
            }
            else
            {
                lua_getfield(L, index, "y");
                lua_getfield(L, index, "z");
            }
            const bool ok = lua_isnumber(L, -3) && lua_isnumber(L, -2) && (lua_isnumber(L, -1) || lua_isnil(L, -1));
            if (ok)
                out = wxl_vec3{static_cast<float>(lua_tonumber(L, -3)), static_cast<float>(lua_tonumber(L, -2)),
                               static_cast<float>(lua_tonumber(L, -1))};
            lua_pop(L, 3);
            return ok;
        }

        /// The number in field @p name of the table at @p index, or @p otherwise when it is nil.
        float FieldNumber(lua_State* L, int index, const char* name, float otherwise, const char* where)
        {
            lua_getfield(L, index, name);
            float value = otherwise;
            if (lua_isnumber(L, -1)) value = static_cast<float>(lua_tonumber(L, -1));
            else if (!lua_isnil(L, -1)) luaL_error(L, "%s: '%s' must be a number", where, name);
            lua_pop(L, 1);
            return value;
        }

        bool FieldFlag(lua_State* L, int index, const char* name, const char* where)
        {
            lua_getfield(L, index, name);
            const int type = lua_type(L, -1);
            if (type != LUA_TNIL && type != LUA_TBOOLEAN) luaL_error(L, "%s: '%s' must be true or false", where, name);
            const bool value = lua_toboolean(L, -1) != 0;
            lua_pop(L, 1);
            return value;
        }

        /// A shape table into @p shape, its points into @p points (which the shape points into).
        void ReadDrawShape(lua_State* L, int index, const char* where, wxl_draw_shape& shape, std::vector<wxl_vec3>& points)
        {
            if (!lua_istable(L, index))
                luaL_error(L, "%s: the shape (argument %d) must be a table {kind = \"circle\", center = {x, y, z}, radius = 5, ...}", where,
                           index);
            shape             = wxl_draw_shape{};
            shape.struct_size = sizeof shape;
            lua_getfield(L, index, "kind");
            if (lua_isnil(L, -1)) luaL_error(L, "%s: the shape has no 'kind'; it takes \"line\", \"circle\", \"ring\", \"sector\", "
                                               "\"rectangle\", \"polygon\", \"grid\", \"box\", \"sphere\" or \"cylinder\"", where);
            const std::string kindWhere = std::string(where) + ": the shape's 'kind'";
            shape.kind = CheckNamed(L, lua_gettop(L), kDrawKinds, 10, 0, kindWhere.c_str());
            lua_pop(L, 1);
            lua_getfield(L, index, "center");
            if (!lua_isnil(L, -1) && !ReadPoint(L, -1, shape.center))
                luaL_error(L, "%s: the shape's 'center' must be a point {x, y, z}", where);
            lua_pop(L, 1);
            lua_getfield(L, index, "size");
            if (!lua_isnil(L, -1) && !ReadPoint(L, -1, shape.size))
                luaL_error(L, "%s: the shape's 'size' must be {x, y, z}", where);
            lua_pop(L, 1);
            shape.radius  = FieldNumber(L, index, "radius", 0.0f, where);
            shape.inner   = FieldNumber(L, index, "inner", 0.0f, where);
            shape.angle   = FieldNumber(L, index, "angle", 0.0f, where);
            shape.facing  = FieldNumber(L, index, "facing", 0.0f, where);
            shape.pitch   = FieldNumber(L, index, "pitch", 0.0f, where);
            shape.roll    = FieldNumber(L, index, "roll", 0.0f, where);
            shape.spacing = FieldNumber(L, index, "spacing", 0.0f, where);
            // A rectangle's and a grid's sides may be given by name.
            shape.size.x = FieldNumber(L, index, "length", shape.size.x, where);
            shape.size.y = FieldNumber(L, index, "width", shape.size.y, where);
            shape.size.z = FieldNumber(L, index, "height", shape.size.z, where);
            if (FieldFlag(L, index, "flat", where)) shape.flags |= WXL_DRAW_FLAT;
            if (FieldFlag(L, index, "on_ground", where)) shape.flags |= WXL_DRAW_ON_GROUND;
            if (FieldFlag(L, index, "closed", where)) shape.flags |= WXL_DRAW_CLOSED;
            lua_getfield(L, index, "points");
            points.clear();
            if (!lua_isnil(L, -1))
            {
                if (!lua_istable(L, -1)) luaL_error(L, "%s: the shape's 'points' must be a list of points {x, y, z}", where);
                const int list  = lua_gettop(L);
                const size_t n  = lua_objlen(L, list);
                if (n > WXL_DRAW_MAX_POINTS)
                    luaL_error(L, "%s: the shape has %d points, more than a shape may (%d)", where, static_cast<int>(n),
                               static_cast<int>(WXL_DRAW_MAX_POINTS));
                points.resize(n);
                for (size_t i = 0; i < n; ++i)
                {
                    lua_rawgeti(L, list, static_cast<int>(i + 1));
                    if (!ReadPoint(L, -1, points[i]))
                        luaL_error(L, "%s: point %d of the shape is not a point {x, y, z}", where, static_cast<int>(i + 1));
                    lua_pop(L, 1);
                }
                shape.points      = points.data();
                shape.point_count = static_cast<uint32_t>(n);
            }
            lua_pop(L, 1);
        }

        /// A style table (nil: the service's default) into @p style; false for nil.
        bool ReadDrawStyle(lua_State* L, int index, const char* where, wxl_draw_style& style)
        {
            style             = wxl_draw_style{};
            style.struct_size = sizeof style;
            style.color       = 0xFFFFFFu;
            style.alpha       = 1.0f;
            style.width       = 2.0f;
            if (lua_isnoneornil(L, index)) return false;
            if (!lua_istable(L, index))
                luaL_error(L, "%s: the style (argument %d) must be a table {color = 0xRRGGBB, alpha, fill_alpha, width, depth, pulse}",
                           where, index);
            lua_getfield(L, index, "color");
            if (lua_isnumber(L, -1))
            {
                const double value = lua_tonumber(L, -1);
                if (value < 0.0 || value > 16777215.0) luaL_error(L, "%s: the style's 'color' must be 0xRRGGBB", where);
                style.color = static_cast<uint32_t>(value);
            }
            else if (!lua_isnil(L, -1)) luaL_error(L, "%s: the style's 'color' must be a number 0xRRGGBB", where);
            lua_pop(L, 1);
            style.alpha      = FieldNumber(L, index, "alpha", 1.0f, where);
            style.fill_alpha = FieldNumber(L, index, "fill_alpha", 0.0f, where);
            style.width      = FieldNumber(L, index, "width", 2.0f, where);
            style.pulse      = FieldNumber(L, index, "pulse", 0.0f, where);
            style.fade_in    = FieldNumber(L, index, "fade_in", 0.0f, where);
            style.fade_out   = FieldNumber(L, index, "fade_out", 0.0f, where);
            lua_getfield(L, index, "depth");
            if (!lua_isnil(L, -1))
            {
                const std::string depthWhere = std::string(where) + ": the style's 'depth'";
                style.depth = CheckNamed(L, lua_gettop(L), kDrawDepths, 2, 0, depthWhere.c_str());
            }
            lua_pop(L, 1);
            return true;
        }

        int L_DrawDraw(lua_State* L)
        {
            wxl_draw_shape        shape{};
            wxl_draw_style        style{};
            std::vector<wxl_vec3> points;
            ReadDrawShape(L, 1, "wxl.draw.Draw", shape, points);
            const bool          styled = ReadDrawStyle(L, 2, "wxl.draw.Draw", style);
            const wxl_draw_api* api    = Draw();
            if (!api || !wxl::detail::Carries(api, &wxl_draw_api::draw)) return PushResultFailure(L, WXL_E_UNSUPPORTED);
            const wxl_result result = api->draw(&shape, styled ? &style : nullptr);
            if (result != WXL_OK) return PushResultFailure(L, result);
            lua_pushboolean(L, 1);
            return 1;
        }

        int L_DrawAdd(lua_State* L)
        {
            wxl_draw_shape        shape{};
            wxl_draw_style        style{};
            std::vector<wxl_vec3> points;
            ReadDrawShape(L, 1, "wxl.draw.Add", shape, points);
            const bool styled = ReadDrawStyle(L, 2, "wxl.draw.Add", style);
            float      duration = 0.0f;
            if (!lua_isnoneornil(L, 3))
            {
                if (!lua_isnumber(L, 3)) luaL_error(L, "wxl.draw.Add: 'duration' (argument 3) must be seconds, or nil to keep it until removed");
                duration = static_cast<float>(lua_tonumber(L, 3));
            }
            const wxl_draw_api* api = Draw();
            if (!api || !wxl::detail::Carries(api, &wxl_draw_api::add)) return PushResultFailure(L, WXL_E_UNSUPPORTED);
            const wxl_handle made = api->add(&shape, styled ? &style : nullptr, duration);
            if (made == WXL_HANDLE_NONE) return PushResultFailure(L, WXL_E_INVALID);
            PushDrawShape(L, made);
            return 1;
        }

        wxl_handle CheckDrawShape(lua_State* L, const char* where)
        {
            const wxl_handle handle = ToDrawShape(L, 1);
            if (handle == WXL_HANDLE_NONE) luaL_error(L, "%s: 'self' (argument 1) must be a Shape wxl.draw.Add answered, not removed", where);
            return handle;
        }

        int L_DrawUpdate(lua_State* L)
        {
            const wxl_handle      handle = CheckDrawShape(L, "wxl.draw.Update");
            wxl_draw_shape        shape{};
            std::vector<wxl_vec3> points;
            ReadDrawShape(L, 2, "wxl.draw.Update", shape, points);
            const wxl_draw_api* api = Draw();
            if (!api || !wxl::detail::Carries(api, &wxl_draw_api::update)) return PushResultFailure(L, WXL_E_UNSUPPORTED);
            const wxl_result result = api->update(handle, &shape);
            if (result != WXL_OK) return PushResultFailure(L, result);
            lua_pushboolean(L, 1);
            return 1;
        }

        int L_DrawSetStyle(lua_State* L)
        {
            const wxl_handle handle = CheckDrawShape(L, "wxl.draw.SetStyle");
            wxl_draw_style   style{};
            const bool       styled = ReadDrawStyle(L, 2, "wxl.draw.SetStyle", style);
            const wxl_draw_api* api = Draw();
            if (!api || !wxl::detail::Carries(api, &wxl_draw_api::set_style)) return PushResultFailure(L, WXL_E_UNSUPPORTED);
            const wxl_result result = api->set_style(handle, styled ? &style : nullptr);
            if (result != WXL_OK) return PushResultFailure(L, result);
            lua_pushboolean(L, 1);
            return 1;
        }

        int L_DrawClear(lua_State* L)
        {
            CloseDrawShapes(L);
            return 0;
        }

        int L_DrawStats(lua_State* L)
        {
            const wxl_draw_api* api = Draw();
            if (!api || !wxl::detail::Carries(api, &wxl_draw_api::stats)) return PushResultFailure(L, WXL_E_UNSUPPORTED);
            wxl_draw_stats stats{};
            stats.struct_size       = sizeof stats;
            const wxl_result result = api->stats(&stats);
            if (result != WXL_OK) return PushResultFailure(L, result);
            lua_createtable(L, 0, 10);
            const auto field = [&](const char* name, double value) {
                lua_pushnumber(L, value);
                lua_setfield(L, -2, name);
            };
            field("shapes", stats.shapes);
            field("immediate", stats.immediate);
            field("drawn", stats.drawn);
            field("vertices", stats.vertices);
            field("left_out", stats.left_out);
            field("rebuilt", stats.rebuilt);
            field("build_microseconds", stats.build_microseconds);
            field("max_build_microseconds", stats.max_build_microseconds);
            field("immediate_microseconds", stats.immediate_microseconds);
            field("frames", stats.frames);
            return 1;
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
        if (service == "camera" && name == "SetView") return &L_SetView;
        if (service == "camera" && name == "ReleaseView") return &L_ReleaseView;
        if (service == "camera" && name == "SetViewRolled") return &L_SetViewRolled;
        if (service == "input" && name == "CaptureCursor") return &L_CaptureCursor;
        if (service == "input" && name == "CaptureCursorAt") return &L_CaptureCursorAt;
        if (service == "input" && name == "BlockMouseLook") return &L_BlockMouseLook;
        if (service == "pose" && name == "Head") return &L_Head;
        if (service == "pose" && name == "ReleaseHead") return &L_ReleaseHead;
        if (service == "highlight" && name == "Set") return &L_HighlightSet;
        if (service == "highlight" && name == "Clear") return &L_HighlightClear;
        if (service == "highlight" && name == "ClearAll") return &L_HighlightClearAll;
        if (service == "selectioncircle" && name == "Hide") return &L_CircleHide;
        if (service == "selectioncircle" && name == "SetStyle") return &L_CircleSetStyle;
        if (service == "selectioncircle" && name == "Reset") return &L_CircleReset;
        if (service == "selectioncircle" && name == "HideObject") return &L_CircleHideObject;
        if (service == "selectioncircle" && name == "SetObjectStyle") return &L_CircleSetObjectStyle;
        if (service == "selectioncircle" && name == "ResetObject") return &L_CircleResetObject;
        if (service == "selectioncircle" && name == "ResetAll") return &L_CircleResetAll;
        if (service == "texture" && name == "Make") return &L_TextureMake;
        if (service == "texture" && name == "Stats") return &L_TextureStats;
        if (service == "cvar" && name == "Register") return &L_CVarRegister;
        if (service == "terrain" && name == "Trace") return &L_TerrainTrace;
        if (service == "terrain" && name == "Locate") return &L_TerrainLocate;
        if (service == "terrain" && name == "Vertices") return &L_TerrainVertices;
        if (service == "terrain" && name == "VerticesIn") return &L_TerrainVerticesIn;
        if (service == "terrain" && name == "SetHeights") return &L_TerrainSetHeights;
        if (service == "terrain" && name == "Reset") return &L_TerrainReset;
        if (service == "terrain" && name == "ResetAll") return &L_TerrainResetAll;
        if (service == "terrain" && name == "Stats") return &L_TerrainStats;
        if (service == "cvar" && name == "Release") return &L_CVarRelease;
        if (service == "draw" && name == "Draw") return &L_DrawDraw;
        if (service == "draw" && name == "Add") return &L_DrawAdd;
        if (service == "draw" && name == "Update") return &L_DrawUpdate;
        if (service == "draw" && name == "SetStyle") return &L_DrawSetStyle;
        if (service == "draw" && name == "Clear") return &L_DrawClear;
        if (service == "draw" && name == "Stats") return &L_DrawStats;
        if (service == "render" && name == "CreatePipeline") return &L_RenderCreatePipeline;
        if (service == "render" && name == "Begin") return &L_RenderBegin;
        if (service == "render" && name == "Draw") return &L_RenderDraw;
        if (service == "render" && name == "DrawIndexed") return &L_RenderDrawIndexed;
        if (service == "render" && name == "Fullscreen") return &L_RenderFullscreen;
        if (service == "render" && name == "DrawBatch") return &L_RenderDrawBatch;
        if (service == "render" && name == "ReplaceShader") return &L_RenderReplaceShader;
        if (service == "render" && name == "RestoreShader") return &L_RenderRestoreShader;
        if (service == "render" && name == "Dispatch") return &L_RenderDispatch;
        if (service == "render" && name == "BonePalette") return &L_RenderBonePalette;
        if (service == "profile" && name == "Begin") return &profile::L_Begin;
        if (service == "profile" && name == "End") return &profile::L_End;
        if (service == "profile" && name == "Text") return &profile::L_Text;
        return nullptr;
    }

    void ReleaseHeld(ScriptMod& mod)
    {
        ReleaseFootprint(mod);
        ReleaseTerrain(mod);
        ReleaseViewAndPointer(mod);
        ReleaseStorage(mod);
        ReleaseRelay(mod);
        RestoreShaders(mod);
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
