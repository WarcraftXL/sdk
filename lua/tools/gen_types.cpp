// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// Writes the editor definitions of the Lua API -- a lua-language-server meta file -- from the
// event table of reflect.hpp, the getter table of object_reads.def and the service tables of
// wxl/services/*.def, so that what an editor completes is what wxl.lua serves.
//
//     wxl-lua-types                  the file on stdout
//     wxl-lua-types <file>           written there, only when it differs
//     wxl-lua-types --check <file>   exit 1 when the file differs from what would be written

#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "../src/ObjectNames.hpp"
#include "wxl/objects/fields.hpp"
#include "wxl/reflect.hpp"
#include "wxl/services/rows.hpp"

namespace
{
    /// What the object methods' names leave ambiguous; the generator refuses to write then.
    std::string g_clash;
    std::string g_unknownShims; ///< shim rows ShimDefinition has no text for

    using wxl::reflect::Event;
    using wxl::reflect::Field;
    using wxl::reflect::Kind;

    const char* LuaType(Kind kind)
    {
        switch (kind)
        {
        case Kind::u32:
        case Kind::i32:
        case Kind::u64:
        case Kind::i64: return "integer";
        case Kind::f32: return "number";
        case Kind::boolean: return "boolean";
        case Kind::handle: return "wxl.Object|nil";
        case Kind::guid: return "wxl.Guid";
        case Kind::vec3: return "wxl.Vec3";
        case Kind::str: return "string";
        case Kind::ptr: return "lightuserdata|nil";
        case Kind::bytes: return "wxl.Bytes";
        case Kind::answer: return "boolean|nil";
        case Kind::buffer: return "string|nil";
        }
        return "any";
    }

    /// "wxl.ScriptContext" for wxl_script_context: an enumeration's alias.
    std::string AliasOf(const wxl::reflect::Enum& enumeration)
    {
        std::string alias = "wxl.";
        const char* name  = enumeration.type;
        if (std::strncmp(name, "wxl_", 4) == 0) name += 4;
        bool upper = true;
        for (; *name; ++name)
        {
            if (*name == '_')
            {
                upper = true;
                continue;
            }
            alias += upper ? static_cast<char>(*name - ('a' <= *name && *name <= 'z' ? 'a' - 'A' : 0))
                           : *name;
            upper = false;
        }
        return alias;
    }

    /// A member's Lua type: an enumeration's value arrives by name, or as a number it lacks.
    std::string LuaTypeOf(const Field& field)
    {
        if (field.enumeration) return AliasOf(*field.enumeration) + "|integer";
        if (field.value) return "wxl.FieldValue";
        return LuaType(field.kind);
    }

    bool Writable(const Field& field) { return field.kind == Kind::answer || field.kind == Kind::buffer; }

    bool Answerable(const Event& event)
    {
        for (uint32_t i = 0; i < event.field_count; ++i)
            if (Writable(event.fields[i])) return true;
        return false;
    }

    /// "delta: number": a handler's parameters, the members IsExtent leaves, as Lua types.
    std::string Parameters(const Event& event)
    {
        std::string text;
        for (uint32_t i = 0; i < event.field_count; ++i)
        {
            const Field& field = event.fields[i];
            if (wxl::reflect::IsExtent(event, field)) continue;
            if (!text.empty()) text += ", ";
            text += field.name;
            text += ": ";
            text += LuaTypeOf(field);
        }
        return text;
    }

    /// What an Override returns: one value per writable member, in member order.
    std::string Returns(const Event& event)
    {
        std::string text;
        for (uint32_t i = 0; i < event.field_count; ++i)
        {
            const Field& field = event.fields[i];
            if (!Writable(field)) continue;
            if (!text.empty()) text += ", ";
            text += field.kind == Kind::answer ? "boolean|nil" : "string|nil";
        }
        return text;
    }

    /// "handled", or "path, then flag": the writable members, for a description.
    std::string WritableNames(const Event& event)
    {
        std::string text;
        for (uint32_t i = 0; i < event.field_count; ++i)
        {
            const Field& field = event.fields[i];
            if (!Writable(field)) continue;
            if (!text.empty()) text += ", then ";
            text += field.name;
        }
        return text;
    }

    std::vector<std::string> Families(const Event* events, uint32_t count)
    {
        std::vector<std::string> families;
        for (uint32_t i = 0; i < count; ++i)
        {
            const std::string family = std::string(events[i].family) + "Script";
            bool              seen   = false;
            for (const std::string& known : families)
                if (known == family) seen = true;
            if (!seen) families.push_back(family);
        }
        return families;
    }

    /// What an object method answers, by its row.
    std::string ObjectAnswer(const wxl::lua::names::RowInfo& row)
    {
        if (const wxl::reflect::Enum* e = wxl::reflect::FindEnum(row.cxx)) return AliasOf(*e);
        if (std::strcmp(row.type, "wxl_bool") == 0) return "boolean";
        if (std::strcmp(row.type, "wxl_handle") == 0) return "wxl.Object";
        if (std::strcmp(row.type, "wxl_guid") == 0) return "wxl.Guid";
        if (std::strcmp(row.type, "wxl_vec3") == 0) return "wxl.Vec3";
        if (std::strcmp(row.type, "wxl_aura") == 0) return "wxl.Aura";
        if (std::strcmp(row.type, "wxl_skill") == 0) return "wxl.Skill";
        if (std::strcmp(row.type, "float") == 0) return "number";
        return "integer";
    }

    /// What an object method takes, by its row.
    std::string ObjectArgType(const wxl::lua::names::RowInfo& row)
    {
        if (const wxl::reflect::Enum* e = wxl::reflect::FindEnum(row.arg)) return AliasOf(*e) + "|integer";
        if (std::strcmp(row.arg_c, "wxl_handle") == 0) return "wxl.Object";
        if (std::strcmp(row.arg_c, "wxl_vec3") == 0) return "wxl.Vec3";
        return "integer";
    }

    std::string ObjectArgName(const wxl::lua::names::RowInfo& row)
    {
        if (std::strcmp(row.arg_c, "wxl_handle") == 0) return "other";
        if (std::strcmp(row.arg_c, "wxl_vec3") == 0) return "point";
        if (wxl::reflect::FindEnum(row.arg)) return "which";
        return "index";
    }

    // --- the service modules (wxl/services/*.def) --------------------------------------------

    /// "wxl.HookPoint" for wxl_hook_point.
    std::string ClassOf(const char* type)
    {
        std::string name = "wxl.";
        if (std::strncmp(type, "wxl_", 4) == 0) type += 4;
        bool upper = true;
        for (; *type; ++type)
        {
            if (*type == '_')
            {
                upper = true;
                continue;
            }
            name += upper && *type >= 'a' && *type <= 'z' ? static_cast<char>(*type - 'a' + 'A') : *type;
            upper = false;
        }
        return name;
    }

    /// What is between the brackets of "list<wxl_str>", or empty.
    std::string Inside(const std::string& kind, const char* outer)
    {
        const std::string prefix = std::string(outer) + "<";
        if (kind.compare(0, prefix.size(), prefix) != 0 || kind.back() != '>') return std::string();
        return kind.substr(prefix.size(), kind.size() - prefix.size() - 1);
    }

    bool IsResourceKind(const std::string& kind)
    {
        uint32_t                          count     = 0;
        const wxl::api::ResourceRow* const resources = wxl::api::Resources(&count);
        for (uint32_t i = 0; i < count; ++i)
            if (kind == resources[i].name) return true;
        return false;
    }

    /// One parameter of a row: its kind and its name, from "(str path, i32 priority)".
    struct Parameter
    {
        std::string kind;
        std::string name;
    };

    std::vector<Parameter> ParametersOf(const char* text)
    {
        std::vector<Parameter> parameters;
        std::string            inner(text);
        if (inner.size() >= 2) inner = inner.substr(1, inner.size() - 2);
        size_t at = 0;
        while (at < inner.size())
        {
            size_t end = inner.find(',', at);
            if (end == std::string::npos) end = inner.size();
            std::string piece = inner.substr(at, end - at);
            while (!piece.empty() && piece.front() == ' ') piece.erase(piece.begin());
            while (!piece.empty() && piece.back() == ' ') piece.pop_back();
            const size_t space = piece.find(' ');
            if (!piece.empty())
                parameters.push_back(space == std::string::npos ? Parameter{piece, "value"}
                                                                : Parameter{piece.substr(0, space), piece.substr(space + 1)});
            at = end + 1;
        }
        return parameters;
    }

    /// A parameter as lua-language-server reads it, or empty for one that is no argument. An
    /// opt<K> may be left out, and is K's zero then; every other one must be given.
    std::string LuaParam(const Parameter& parameter)
    {
        const std::string optional = Inside(parameter.kind, "opt");
        const std::string& k       = optional.empty() ? parameter.kind : optional;
        const std::string  name    = parameter.name + (optional.empty() ? "" : "?");
        if (k == "bytes_size") return std::string();
        if (k == "values") return "---@param ... nil|number|string|boolean the values it carries\n";
        if (k == "str" || k == "cstr") return "---@param " + name + " string\n";
        if (k == "bytes") return "---@param " + name + " string the bytes, as a string\n";
        if (k == "i32" || k == "u32" || k == "u64" || k == "uptr")
            return "---@param " + name + " integer" + (optional.empty() ? "" : " 0 when left out") + "\n";
        if (k == "f64") return "---@param " + name + " number\n";
        if (k == "boolean") return "---@param " + name + " boolean\n";
        if (k == "object")
            return "---@param " + name + " wxl.Object" + (optional.empty() ? "" : " none when left out") + "\n";
        if (k == "vec3" || k == "vec3_in") return "---@param " + name + " wxl.Vec3\n";
        if (k == "f32") return "---@param " + name + " number\n";
        if (k == "placement")
            return "---@param " + name +
                   " {position: wxl.Vec3, yaw?: number, pitch?: number, roll?: number, scale?: number}\n";
        if (k == "ptr" || k == "cptr")
            return "---@param " + name + " lightuserdata|wxl.Model an address an event or a call handed you\n";
        if (IsResourceKind(k)) return "---@param " + name + " wxl." + k + "\n";
        return "---@param " + name + " any\n";
    }

    std::string ArgumentName(const Parameter& parameter)
    {
        if (parameter.kind == "bytes_size") return std::string();
        if (parameter.kind == "values") return "...";
        return parameter.name;
    }

    /// The Lua type of a value kind an out<K> or a found<K> answers.
    std::string ValueType(const std::string& k)
    {
        if (k == "str") return "string";
        if (k == "vec3") return "wxl.Vec3";
        if (k == "f64" || k == "f32") return "number";
        if (k == "boolean") return "boolean";
        return "integer";
    }

    /// The value an answer kind gives, as a Lua type; empty for none.
    std::string LuaAnswer(const std::string& k)
    {
        if (k == "none") return std::string();
        if (k == "result") return "true|nil ok";
        if (k == "boolean") return "boolean|nil";
        if (k == "u32" || k == "uptr") return "integer|nil";
        if (k == "address") return "integer|nil address";
        if (k == "ptr") return "lightuserdata|nil address";
        if (k == "str") return "string|nil";
        if (k == "whole_bytes" || k == "read_bytes") return "string|nil bytes";
        if (IsResourceKind(k)) return "wxl." + k + "|nil";
        const std::string listed = Inside(k, "list");
        if (!listed.empty()) return (listed == "wxl_str" ? std::string("string") : ClassOf(listed.c_str())) + "[]|nil";
        const std::string out = Inside(k, "out");
        if (!out.empty()) return ValueType(out) + "|nil";
        const std::string found = Inside(k, "found");
        if (!found.empty()) return ValueType(found) + "|nil";
        const std::string named = Inside(k, "enumeration");
        if (!named.empty())
            if (const wxl::reflect::Enum* e = wxl::reflect::FindEnum(named.c_str())) return AliasOf(*e) + "|integer|nil";
        return "any";
    }

    /// What a shim row takes and gives, as the shims in sdk/lua/src/Shims.cpp serve it.
    std::string ShimDefinition(const wxl::api::Row& row, const std::string& owner)
    {
        const std::string name = row.name;
        if (name == "RegisterFunction")
            return "---Makes a Lua function callable from the client's own script, under a global name: in every\n"
                   "---script context the client makes from now on, or only in `context`. Kept until the mod\n"
                   "---unloads or reloads; its arguments and returns are numbers, strings, booleans and nil.\n"
                   "---@param name string the global's name\n---@param fn function\n"
                   "---@param context? wxl.ScriptContext\n---@return true|nil ok\n---@return string|nil reason\n"
                   "function " + owner + ".RegisterFunction(name, fn, context) end\n\n";
        if (name == "RegisterEvent")
            return "---Declares an event the client's frames can listen to, in every script context the client\n"
                   "---makes from now on, or only in `context`. FireEvent raises it.\n"
                   "---@param name string\n---@param context? wxl.ScriptContext\n"
                   "---@return true|nil ok\n---@return string|nil reason\n"
                   "function " + owner + ".RegisterEvent(name, context) end\n\n";
        if (name == "AddSearchPath")
            return "---Adds a folder inside the client's folder to the files searched, for this mod: the higher\n"
                   "---priority wins a name, the client's own data is 0. It stays until RemoveSearchPath, or until\n"
                   "---the mod is disabled, reloads or unloads; adding it again changes its priority.\n"
                   "---@param directory string\n---@param priority integer\n"
                   "---@return true|nil ok\n---@return string|nil reason\n"
                   "function " + owner + ".AddSearchPath(directory, priority) end\n\n";
        if (name == "RemoveSearchPath")
            return "---Takes a folder this mod added back out of the files searched; nil, \"not_found\" for one\n"
                   "---it did not add.\n"
                   "---@param directory string\n---@return true|nil ok\n---@return string|nil reason\n"
                   "function " + owner + ".RemoveSearchPath(directory) end\n\n";
        if (name == "Redirect")
            return "---Makes every open of `from` open `to` instead, on whichever thread the client opens it, for\n"
                   "---this mod: names compared in any case, with either slash; `to` is found as any name is. It\n"
                   "---stays until Unredirect, or until the mod is disabled, reloads or unloads; when two mods\n"
                   "---redirect one name, the later wins. An OnFileOpen handler that answers still has the last word.\n"
                   "---@param from string\n---@param to string\n"
                   "---@return true|nil ok\n---@return string|nil reason\n"
                   "function " + owner + ".Redirect(from, to) end\n\n";
        if (name == "Unredirect")
            return "---Takes this mod's redirect of a name back; nil, \"not_found\" for one it did not redirect.\n"
                   "---@param from string\n---@return true|nil ok\n---@return string|nil reason\n"
                   "function " + owner + ".Unredirect(from) end\n\n";
        if (name == "Serve")
            return "---Serves `data` under a name, for this mod, on every thread: the client opens and reads it as\n"
                   "---one of its own files, and so do wxl.storage's Read and Open. The bytes are copied. Above 0,\n"
                   "---the name wins over the client's archives; 0 or below, it only fills a name they lack. Serving\n"
                   "---a name again replaces this mod's bytes. It stays until Unserve, or until the mod is disabled,\n"
                   "---reloads or unloads; when two mods serve one name, the higher priority wins, then the later.\n"
                   "---@param path string\n---@param data string\n---@param priority integer\n"
                   "---@return true|nil ok\n---@return string|nil reason\n"
                   "function " + owner + ".Serve(path, data, priority) end\n\n";
        if (name == "Unserve")
            return "---Takes back what this mod serves under a name; nil, \"not_found\" for one it does not serve.\n"
                   "---@param path string\n---@return true|nil ok\n---@return string|nil reason\n"
                   "function " + owner + ".Unserve(path) end\n\n";
        if (name == "HashFile")
            return "---The content hash of a file as the client would read it, from whichever layer holds it: an\n"
                   "---archive, the disk, a folder of the search path, a provider, served bytes. The bytes are hashed\n"
                   "---before any transform, by the system's MD5, and the answer is kept: `fresh` reads it again.\n"
                   "---Reading a large file takes time on the main thread; HashFileAsync does not.\n"
                   "---@param path string\n---@param fresh? boolean\n"
                   "---@return wxl.FileHash|nil hash\n---@return string|nil reason\n"
                   "function " + owner + ".HashFile(path, fresh) end\n\n";
        if (name == "HashFileAsync")
            return "---HashFile on a thread of the runtime's: `fn(hash)` is called later, on the main thread, with the\n"
                   "---table HashFile answers, or nil and the reason. Answers the request's number. A request of a\n"
                   "---mod that is disabled, reloads or unloads is dropped.\n"
                   "---@param path string\n---@param fn fun(hash: wxl.FileHash|nil, reason: string|nil)\n"
                   "---@param fresh? boolean\n---@return integer|nil request\n---@return string|nil reason\n"
                   "function " + owner + ".HashFileAsync(path, fn, fresh) end\n\n";
        if (name == "OverrideFile")
            return "---Replaces a file of the client with another name, for this mod: every open of `path`, and every\n"
                   "---check that it exists, gets `with` instead, on every thread, above every layer. The way to\n"
                   "---put a file of the mod's own in place of the client's file. It stays until RestoreFile,\n"
                   "---or until the mod is disabled, reloads or unloads; when two mods override one name, the later\n"
                   "---wins. What the client already loaded stays: ReloadAsset makes it read the file again.\n"
                   "---@param path string\n---@param with string\n"
                   "---@return true|nil ok\n---@return string|nil reason\n"
                   "function " + owner + ".OverrideFile(path, with) end\n\n";
        if (name == "RestoreFile")
            return "---Takes this mod's override of a name back; nil, \"not_found\" for one it did not override.\n"
                   "---@param path string\n---@return true|nil ok\n---@return string|nil reason\n"
                   "function " + owner + ".RestoreFile(path) end\n\n";
        if (name == "ReloadAsset")
            return "---Makes the client read again a model or a texture it holds, so a file that changed shows: a\n"
                   "---texture is read again in place, a model is dropped from the model cache and the units drawn\n"
                   "---with it made again. Other kinds answer nil, \"unsupported\"; nil, \"pending\" when what the\n"
                   "---client holds is still loading.\n"
                   "---@param path string\n---@return wxl.AssetReload|nil done\n---@return string|nil reason\n"
                   "function " + owner + ".ReloadAsset(path) end\n\n";
        if (name == "Publish")
            return "---Publishes a service under a name, for this mod. `fn(request, method, ...)` runs on the main\n"
                   "---thread for each call: what it returns is the answer; when it returns nothing, it answers later\n"
                   "---with Answer or Fail. `cancelled(request)`, when given, is told of a request whose caller\n"
                   "---took it back or went. It stays until Unpublish, or until the mod is disabled, reloads or\n"
                   "---unloads; its unanswered requests then fail with \"not_found\".\n"
                   "---@param name string\n---@param fn fun(request: integer, method: string, ...): ...\n"
                   "---@param cancelled? fun(request: integer)\n"
                   "---@return true|nil ok\n---@return string|nil reason\n"
                   "function " + owner + ".Publish(name, fn, cancelled) end\n\n";
        if (name == "Unpublish")
            return "---Takes this mod's service back; nil, \"not_found\" for one it does not publish.\n"
                   "---@param name string\n---@return true|nil ok\n---@return string|nil reason\n"
                   "function " + owner + ".Unpublish(name) end\n\n";
        if (name == "Call")
            return "---Asks a service: `fn(true, ...)` is called later, on the main thread, with the answer, or\n"
                   "---`fn(false, reason)` (\"not_found\" when nobody publishes it or it went, else the reason the\n"
                   "---service gave). `args` is a list of numbers, strings and booleans, or nil. Answers the\n"
                   "---request's number. Dropped, never answered, if this mod is disabled, reloads or unloads.\n"
                   "---@param name string\n---@param method string\n---@param args? any[]\n"
                   "---@param fn fun(ok: boolean, ...)\n---@return integer|nil request\n---@return string|nil reason\n"
                   "function " + owner + ".Call(name, method, args, fn) end\n\n";
        if (name == "Answer")
            return "---Answers a request this mod's service received, with values: numbers, strings, booleans, nil.\n"
                   "---@param request integer\n---@param ... any\n"
                   "---@return true|nil ok\n---@return string|nil reason\n"
                   "function " + owner + ".Answer(request, ...) end\n\n";
        if (name == "Fail")
            return "---Fails a request this mod's service received: its caller gets false and `reason`.\n"
                   "---@param request integer\n---@param reason string\n"
                   "---@return true|nil ok\n---@return string|nil reason\n"
                   "function " + owner + ".Fail(request, reason) end\n\n";
        if (name == "Cancel")
            return "---Takes back a request this mod made: its fn is never called, and the service is told.\n"
                   "---@param request integer\n---@return true|nil ok\n---@return string|nil reason\n"
                   "function " + owner + ".Cancel(request) end\n\n";
        if (name == "Snapshot")
            return "---Takes the picture the lists read: Storm's counts, the client's consumers, the address space,\n"
                   "---and the heap of every Lua mod, each shown as its own category. On the main thread.\n"
                   "---@return true|nil ok\n---@return string|nil reason\n"
                   "function " + owner + ".Snapshot() end\n\n";
        if (name == "Report")
            return "---States one consumer of this mod's own, shown in Categories() as \"<mod id>/<name>\" until the\n"
                   "---mod reports it again or goes; 0 bytes and 0 count remove it.\n"
                   "---@param name string\n---@param bytes integer\n---@param count integer\n"
                   "---@return true|nil ok\n---@return string|nil reason\n"
                   "function " + owner + ".Report(name, bytes, count) end\n\n";
        if (name == "WatchPressure")
            return "---Raises MemoryPressure when the largest free block of the address space falls under this\n"
                   "---many bytes, and again only once it has risen back above; 0 stops. Kept for this mod until it\n"
                   "---goes; the highest threshold any mod holds is the one the runtime watches.\n"
                   "---@param bytes integer\n---@return true|nil ok\n---@return string|nil reason\n"
                   "function " + owner + ".WatchPressure(bytes) end\n\n";
        if (name == "SetView")
            return "---Places the world camera from now on, for this mod: where it is, the direction it looks, and\n"
                   "---the field of view in radians, 0 to keep the client's. Set again each frame from OnCameraUpdate\n"
                   "---to move it with the frame. It stays until ReleaseView, or until the mod is disabled, reloads\n"
                   "---or unloads; when two mods set one, the later is applied and the earlier comes back after it.\n"
                   "---@param position wxl.Vec3\n---@param forward wxl.Vec3\n---@param fov number\n"
                   "---@return true|nil ok\n---@return string|nil reason\n"
                   "function " + owner + ".SetView(position, forward, fov) end\n\n";
        if (name == "SetViewRolled")
            return "---Places the world camera as SetView does, turned around the direction it looks by `roll`\n"
                   "---radians, positive to the camera's right (the top of the screen leans right); 0 is SetView's\n"
                   "---picture. The game's picking under the pointer turns with it; the interface does not.\n"
                   "---@param position wxl.Vec3\n---@param forward wxl.Vec3\n---@param fov number\n---@param roll number\n"
                   "---@return true|nil ok\n---@return string|nil reason\n"
                   "function " + owner + ".SetViewRolled(position, forward, fov, roll) end\n\n";
        if (name == "ReleaseView")
            return "---Gives the world camera this mod placed back to the client; nil, \"not_found\" when it holds none.\n"
                   "---@return true|nil ok\n---@return string|nil reason\n"
                   "function " + owner + ".ReleaseView() end\n\n";
        if (name == "CaptureCursor")
            return "---Hides the pointer and holds it at the centre of the window (true), where the game still picks\n"
                   "---and clicks, or lets this mod's hold go (false). Held while any mod holds it, and let go when\n"
                   "---the mod is disabled, reloads or unloads. The motion arrives as OnMouseMotion.\n"
                   "---@param captured boolean\n---@return true|nil ok\n---@return string|nil reason\n"
                   "function " + owner + ".CaptureCursor(captured) end\n\n";
        if (name == "CaptureCursorAt")
            return "---Hides the pointer and holds it at a point of the window, where the game picks and clicks:\n"
                   "---x from 0 (left edge) to 1 (right edge), y from 0 (top edge) to 1 (bottom edge), (0.5, 0.5)\n"
                   "---the centre. A mod holding it already moves its hold there; of several mods, the last to\n"
                   "---capture or move it has its point. Let go as CaptureCursor(false) does.\n"
                   "---@param x number\n---@param y number\n---@return true|nil ok\n---@return string|nil reason\n"
                   "function " + owner + ".CaptureCursorAt(x, y) end\n\n";
        if (name == "BlockMouseLook")
            return "---Keeps the game out of its own mouse look (true): a held mouse button turns nothing and hides\n"
                   "---nothing, a click still selects or acts; false gives this mod's block back. Kept while any mod\n"
                   "---blocks it, and given back when the mod is disabled, reloads or unloads.\n"
                   "---@param blocked boolean\n---@return true|nil ok\n---@return string|nil reason\n"
                   "function " + owner + ".BlockMouseLook(blocked) end\n\n";
        if (name == "Head")
            return "---Turns a unit's head from where its body faces, in radians (yaw positive to its left, pitch\n"
                   "---positive up), reached over `smoothing` seconds for half the way, and holds it until\n"
                   "---ReleaseHead, or until the mod is disabled, reloads or unloads.\n"
                   "---@param unit wxl.Object\n---@param yaw number\n---@param pitch number\n---@param smoothing number\n"
                   "---@return true|nil ok\n---@return string|nil reason\n"
                   "function " + owner + ".Head(unit, yaw, pitch, smoothing) end\n\n";
        if (name == "ReleaseHead")
            return "---Gives a unit's head this mod turned back to its animation; nil, \"not_found\" for one it does not hold.\n"
                   "---@param unit wxl.Object\n---@return true|nil ok\n---@return string|nil reason\n"
                   "function " + owner + ".ReleaseHead(unit) end\n\n";
        if (std::strcmp(row.service, "highlight") == 0 && name == "Set")
            return "---Highlights an object until cleared, where it is seen and never through a wall: a sharp outline\n"
                   "---`outline` pixels wide (up to 16) and a glow fading over `glow` pixels (up to 64), `glow_strength`\n"
                   "---(0 to 1) bright, both in `color` (0xRRGGBB); 0 for none. Pixels of a 1080-line picture, drawn in\n"
                   "---proportion at any height. Called again, it changes the style.\n"
                   "---Kept until Clear, ClearAll, the object leaving view, or the mod disabled, reloaded or unloaded;\n"
                   "---nil, \"unsupported\" where this runtime does not draw highlights (see UnsupportedReason).\n"
                   "---@param target wxl.Object\n---@param color integer\n---@param outline number\n"
                   "---@param glow number\n---@param glow_strength number\n"
                   "---@return true|nil ok\n---@return string|nil reason\n"
                   "function " + owner + ".Set(target, color, outline, glow, glow_strength) end\n\n";
        if (std::strcmp(row.service, "highlight") == 0 && name == "Clear")
            return "---Takes this mod's highlight off an object; nil, \"not_found\" for one it does not hold.\n"
                   "---@param target wxl.Object\n---@return true|nil ok\n---@return string|nil reason\n"
                   "function " + owner + ".Clear(target) end\n\n";
        if (std::strcmp(row.service, "highlight") == 0 && name == "ClearAll")
            return "---Takes off every highlight of this mod.\n"
                   "function " + owner + ".ClearAll() end\n\n";
        if (std::strcmp(row.service, "texture") == 0 && name == "Make")
            return "---Makes a texture of an SVG or a PNG, which the client loads by the path answered, like its\n"
                   "---own files (Texture:SetTexture in an add-on, a model's texture). An SVG is drawn at `width` x\n"
                   "---`height` pixels, sharp, its proportions kept; a PNG is resampled to that size (nil: the\n"
                   "---source's own). The texture's edges are powers of two, the picture at its top-left corner: show\n"
                   "---it with SetTexCoord(0, right, 0, bottom) on a region of the picture's size. The same source,\n"
                   "---size, groups and tint answer the same path, made once; a change to the file makes a new one.\n"
                   "---`source`: \".\\\\assets\\\\x.svg\" in this mod's folder, \"Mods\\\\<folder>\\\\...\" in another mod's,\n"
                   "---or a name the client has (\"Interface\\\\AddOns\\\\<add-on>\\\\x.svg\").\n"
                   "---`groups` (SVG): by the id of a shape or of its nearest group with one, a colour (\"rrggbbaa\",\n"
                   "---\"rrggbb\" or 0xAARRGGBB: every paint, its alpha their opacity), false to hide it, or a table\n"
                   "---{color, width = the strokes' width times the SVG's own (2: twice, 0: none), visible}.\n"
                   "---`tint`: a colour every pixel is multiplied by.\n"
                   "---@param source string\n---@param width? integer\n---@param height? integer\n"
                   "---@param groups? table<string, string|integer|boolean|{color: string|integer?, width: number?, visible: boolean?}>\n"
                   "---@param tint? string|integer\n"
                   "---@return string|nil path\n---@return number|string right the picture's right texture coordinate, or why there is none\n"
                   "---@return number bottom\n---@return integer texture_width\n---@return integer texture_height\n"
                   "function " + owner + ".Make(source, width, height, groups, tint) end\n\n";
        if (std::strcmp(row.service, "texture") == 0 && name == "Stats")
            return "---What the service holds: textures (names known), resident (in memory), texture_limit, bytes,\n"
                   "---byte_limit, made, reused, released.\n"
                   "---@return {textures: integer, resident: integer, texture_limit: integer, bytes: integer, byte_limit: integer, made: integer, reused: integer, released: integer}|nil stats\n"
                   "---@return string|nil reason\n"
                   "function " + owner + ".Stats() end\n\n";
        if (std::strcmp(row.service, "terrain") == 0)
        {
            const std::string point = "{x: number, y: number, z: number}";
            if (name == "Trace")
                return "---The first point where the segment from `start` to `finish` meets the loaded ground, holes let\n"
                       "---through: the point, the unit normal of the triangle met (facing up), its chunk, and how far\n"
                       "---along the segment (0 to 1). nil when it meets none.\n"
                       "---@param start " + point + "\n---@param finish " + point + "\n"
                       "---@return {point: " + point + ", normal: " + point + ", chunk_x: integer, chunk_y: integer, fraction: number}|nil hit\n"
                       "function " + owner + ".Trace(start, finish) end\n\n";
            if (name == "Locate")
                return "---Where a point is on the grid: its chunk (column and row on the map, 0-1023), its cell in the\n"
                       "---chunk (0-7, the row along world x), the chunk's vertex nearest to it (0-144), and whether the\n"
                       "---chunk is loaded. nil off the map.\n"
                       "---@param x number\n---@param y number\n"
                       "---@return {chunk_x: integer, chunk_y: integer, cell_row: integer, cell_col: integer, nearest: integer, loaded: boolean}|nil place\n"
                       "function " + owner + ".Locate(x, y) end\n\n";
            if (name == "Vertices")
                return "---The world positions of a loaded chunk's 145 vertices, in index order (index i at [i + 1]):\n"
                       "---rows of 9 outer vertices, each but the last followed by a row of 8 inner ones. nil,\n"
                       "---\"not_found\" when the chunk is not loaded. `original` true: the heights before any mod's edit.\n"
                       "---@param chunk_x integer\n---@param chunk_y integer\n---@param original? boolean\n"
                       "---@return " + point + "[]|nil vertices\n---@return string|nil reason\n"
                       "function " + owner + ".Vertices(chunk_x, chunk_y, original) end\n\n";
            if (name == "VerticesIn")
                return "---Every vertex within `radius` yards (0 to 128) of a point, each once whichever chunks it is on:\n"
                       "---its chunk and index (ready for SetHeights, which writes a vertex on a chunk's edge into every\n"
                       "---chunk holding it), its place, its height now and before any edit, whether it is an inner\n"
                       "---vertex, and whether its chunk is loaded (the heights are 0 when it is not). An empty list\n"
                       "---off the map or for a radius out of range.\n"
                       "---@param x number\n---@param y number\n---@param radius number\n"
                       "---@return {chunk_x: integer, chunk_y: integer, index: integer, x: number, y: number, z: number, original_z: number, inner: boolean, loaded: boolean}[] vertices\n"
                       "function " + owner + ".VerticesIn(x, y, radius) end\n\n";
            if (name == "SetHeights")
                return "---Sets vertices to absolute world heights, all or none: the ground drawn, the walking, the\n"
                       "---falling, the camera and the client's clicks follow. A vertex on a chunk's edge is set in\n"
                       "---its neighbour too. Held by this mod until Reset, ResetAll, or until the mod is disabled,\n"
                       "---reloads or unloads; kept while the chunk is away and put back when it loads; dropped with\n"
                       "---the map. nil, \"invalid\" for an entry off the grid (logged, nothing set); nil,\n"
                       "---\"unsupported\" out of the world or on a map without terrain.\n"
                       "---@param heights {chunk_x: integer, chunk_y: integer, index: integer, height: number}[]\n"
                       "---@return true|nil ok\n---@return string|nil reason\n"
                       "function " + owner + ".SetHeights(heights) end\n\n";
            if (name == "Reset")
                return "---Takes this mod's edits off a chunk (and off its edge vertices' copies in its neighbours): the\n"
                       "---original heights come back, or another mod's edits where it has some. nil, \"not_found\" when\n"
                       "---this mod has none there.\n"
                       "---@param chunk_x integer\n---@param chunk_y integer\n"
                       "---@return true|nil ok\n---@return string|nil reason\n"
                       "function " + owner + ".Reset(chunk_x, chunk_y) end\n\n";
            if (name == "ResetAll")
                return "---Takes every edit of this mod off. nil, \"not_found\" when it has none.\n"
                       "---@return true|nil ok\n---@return string|nil reason\n"
                       "function " + owner + ".ResetAll() end\n\n";
            if (name == "Stats")
                return "---What the service holds and what its last edit cost: edited_chunks, edited_vertices, edits,\n"
                       "---reapplied (chunks given their edits back as they loaded), last_vertices, last_chunks,\n"
                       "---last_microseconds, max_microseconds.\n"
                       "---@return {edited_chunks: integer, edited_vertices: integer, edits: integer, reapplied: integer, last_vertices: integer, last_chunks: integer, last_microseconds: integer, max_microseconds: integer}|nil stats\n"
                       "---@return string|nil reason\n"
                       "function " + owner + ".Stats() end\n\n";
        }
        if (std::strcmp(row.service, "cvar") == 0 && name == "Register")
            return "---Registers a console variable for this mod: the console, the interface's GetCVar and SetCVar\n"
                   "---and every mod read and set it, and the client saves it with its own. It keeps a value the\n"
                   "---client saved for it; a new one starts at `default`. Registering it again updates its flags\n"
                   "---and the default Reset uses. Held by this mod until Release, or until the mod is disabled,\n"
                   "---reloads or unloads (the variable stays); another mod's name, or the client's own, is refused.\n"
                   "---`flags`: nil (a variable of the session), or \"saved\" (Config.wtf), \"account\", \"character\",\n"
                   "---\"read_only\" (the console and the interface cannot set it), \"latched\" (a set waits for the next\n"
                   "---start, with \"saved\"), \"protected\" (SetCVar only from secure code), \"hidden\" (the interface\n"
                   "---does not see it), a list of them, or a mask (1, 2, 4, 8, 16, 32, 64).\n"
                   "---@param name string any name without spaces or quotes, up to 63 characters; start it with your mod's prefix\n"
                   "---@param default string|number|boolean\n---@param flags? string|string[]|integer\n"
                   "---@param help? string a line for the console's help\n"
                   "---@return true|nil ok\n---@return string|nil reason\n"
                   "function " + owner + ".Register(name, default, flags, help) end\n\n";
        if (std::strcmp(row.service, "cvar") == 0 && name == "Release")
            return "---Gives up a variable this mod registered: it stays, with its value, until the client ends, and\n"
                   "---any mod may register it again; nil, \"not_found\" for one this mod does not hold.\n"
                   "---@param name string\n---@return true|nil ok\n---@return string|nil reason\n"
                   "function " + owner + ".Release(name) end\n\n";
        if (std::strcmp(row.service, "selectioncircle") == 0)
        {
            const std::string masks =
                "---`kinds`: nil or \"all\", \"target\", \"pet_target\", \"move_marker\", a list of them, or a mask (1, 2, 4).\n"
                "---`reactions`, of the unit the circle is drawn for: nil for all, \"hated\" to \"exalted\", \"none\" (no\n"
                "---unit, or no reaction read), a list of them, or a mask (bit 0 hated ... bit 7 exalted, bit 8 none).\n";
            const std::string style =
                "---`style`: a table {texture = path of a BLP added onto the ground like the game's (black is nothing),\n"
                "---color = 0xAARRGGBB, scale = size against the game's (up to 8)}; each left out keeps the game's.\n";
            const std::string kept =
                "---Kept until reset, or until the mod is disabled, reloads or unloads; an object's own look before\n"
                "---its kind's, the last set winning among mods.\n";
            const std::string answers = "---@return true|nil ok\n---@return string|nil reason\n";
            if (name == "Hide")
                return "---Hides the game's circles of these kinds drawn for units of these reactions.\n" + masks + kept +
                       "---@param kinds? string|string[]|integer\n---@param reactions? string|string[]|integer\n" + answers +
                       "function " + owner + ".Hide(kinds, reactions) end\n\n";
            if (name == "SetStyle")
                return "---Draws the game's circles of these kinds, for units of these reactions, in another look.\n" + masks +
                       style + kept +
                       "---@param kinds? string|string[]|integer\n---@param reactions? string|string[]|integer\n"
                       "---@param style {texture: string?, color: integer?, scale: number?}\n" + answers +
                       "function " + owner + ".SetStyle(kinds, reactions, style) end\n\n";
            if (name == "Reset")
                return "---Gives the circles of these kinds and reactions back the game's look, as far as this mod changed it.\n" +
                       masks + "---@param kinds? string|string[]|integer\n---@param reactions? string|string[]|integer\n" +
                       answers + "function " + owner + ".Reset(kinds, reactions) end\n\n";
            if (name == "HideObject")
                return "---Hides every circle the game draws for an object: as the target, as the pet's target, or the\n"
                       "---move marker of the unit moving.\n" + kept + "---@param target wxl.Object\n" + answers +
                       "function " + owner + ".HideObject(target) end\n\n";
            if (name == "SetObjectStyle")
                return "---Draws every circle the game draws for an object in another look.\n" + style + kept +
                       "---@param target wxl.Object\n---@param style {texture: string?, color: integer?, scale: number?}\n" +
                       answers + "function " + owner + ".SetObjectStyle(target, style) end\n\n";
            if (name == "ResetObject")
                return "---Gives an object's circles back the look of their kind; nil, \"not_found\" for one this mod did\n"
                       "---not change.\n---@param target wxl.Object\n" + answers +
                       "function " + owner + ".ResetObject(target) end\n\n";
            if (name == "ResetAll")
                return "---Gives back everything this mod changed on the game's circles.\n"
                       "function " + owner + ".ResetAll() end\n\n";
        }
        if (std::strcmp(row.service, "animation") == 0)
        {
            if (name == "Play")
                return "---Plays an AnimationData animation on a unit as the game plays an emote, seen on this client only:\n"
                       "---the whole body standing, the upper body over the legs' run while it moves. `flags`: loop 1 (again\n"
                       "---at each end until Stop), no_blend 2 (no blend from the pose it leaves), override 4 (a loop played\n"
                       "---again whenever the game replaced it, death excepted; a one-shot ignores it), exact 8 (no fallback to the model's nearest\n"
                       "---animation); 0 or nil plays it once, blended, yielding to the game. `speed`: 1 (or nil) its own.\n"
                       "---Kept until it ends (OnUnitAnimationEnded), Stop, or the mod disabled, reloaded or unloaded; nil,\n"
                       "---\"failed\" when the client did not play it (its model loading, the unit dead).\n"
                       "---@param unit wxl.Object\n---@param anim_id integer\n---@param flags? integer\n---@param speed? number\n"
                       "---@return true|nil ok\n---@return string|nil reason\n"
                       "function " + owner + ".Play(unit, anim_id, flags, speed) end\n\n";
            if (name == "Stop")
                return "---Ends this mod's animation on a unit; the game chooses the unit's animation again at once. nil,\n"
                       "---\"not_found\" when this mod plays none on it.\n"
                       "---@param unit wxl.Object\n---@return true|nil ok\n---@return string|nil reason\n"
                       "function " + owner + ".Stop(unit) end\n\n";
        }
        if (std::strcmp(row.service, "draw") == 0)
        {
            const std::string shape =
                "{kind: \"line\"|\"circle\"|\"ring\"|\"sector\"|\"rectangle\"|\"polygon\"|\"grid\"|\"box\"|\"sphere\"|\"cylinder\"|\"beam\", widths: number|number[]?, "
                "center: wxl.Vec3?, radius: number?, inner: number?, angle: number?, facing: number?, pitch: number?, roll: number?, "
                "size: wxl.Vec3?, spacing: number?, points: wxl.Vec3[]?, alphas: number[]?, flat: boolean?, on_ground: boolean?, closed: boolean?}";
            const std::string style =
                "{color: integer?, alpha: number?, fill_alpha: number?, width: number?, depth: \"tested\"|\"on_top\"?, pulse: number?, "
                "fade_in: number?, fade_out: number?, fill_color: integer?, softness: number?, alpha_start: number?, alpha_end: number?, "
                "shadow_color: integer?, shadow_alpha: number?, shadow_offset: {x: number, y: number}?, shadow_softness: number?, "
                "texture: string|table?, texture_right: number?, texture_bottom: number?, uv: \"fit\"|\"tile\"|\"world\"?, "
                "uv_size: number|{x: number, y: number}?, uv_rotation: number?, uv_offset: {x: number, y: number}?, "
                "blend: \"alpha\"|\"add\"|\"modulate\"?, feather: number?, radial_fade: number?, occluded_alpha: number?, mask: boolean?, texture_blur: number?}";
            const std::string about =
                "---`shape.kind` says which fields it reads: line `points` (`closed`, `on_ground`); circle `center`, `radius`;\n"
                "---ring and `inner`; sector and `facing`, `angle` (radians), `inner`; rectangle `center`, `size` (x along\n"
                "---`facing`, y across); polygon `points`; grid `center`, `size`, `spacing`, `facing`; box `center`, `size`,\n"
                "---`facing`, `pitch`, `roll`; sphere `center`, `radius`; cylinder `center`, `radius`, `size.z` (its height).\n"
                "---Points are offsets from `center` (nil: world coordinates). Ground shapes follow the ground; `flat` keeps\n"
                "---them at `center.z`. `style.color` 0xRRGGBB; `alpha` the lines' opacity, `fill_alpha` the inside's;\n"
                "---`width` pixels at 1080 lines; `depth` \"tested\" (hidden behind walls) or \"on_top\"; `pulse` seconds a beat.\n"
                "---Since abi 0.1.38: `softness` fades the lines' edges (pixels); `alpha_start`, `alpha_end` (and the shape's\n"
                "---`alphas`, one per point) fade a line along its length; `shadow_alpha` draws a shadow under the lines\n"
                "---(`shadow_color`, `shadow_offset` {x, y} pixels right and down, 1.5 by default, `shadow_softness`).\n"
                "---A ground shape's inside takes a `texture` (a BLP path, or {path, right, bottom} as wxl.texture.Make\n"
                "---answers), laid by `uv` \"fit\", \"tile\" or \"world\" (`uv_size`, `uv_rotation`, `uv_offset`), tinted by\n"
                "---`fill_color` (or `color`) and `fill_alpha`, put on the ground by `blend` \"alpha\", \"add\" or \"modulate\",\n"
                "---faded at its border over `feather` yards and from its centre by `radial_fade` (0 to 1).\n"
                "---Since abi 0.1.39: a box's faces and a cylinder's side and ends take the same look, each face in its own\n"
                "---frame; `occluded_alpha` (0 to 1) draws what the scene hides of the shape at that part of its opacity.\n"
                "---Since abi 0.1.40: `mask = true` takes only the texture's opacity, the colour being the style's;\n"
                "---`texture_blur` blurs the picture, a step halving its detail. A \"beam\" (its 2 `points`, `widths` at\n"
                "---each end, `alphas` its opacity at each end) faces the camera around its axis, drawn with `fill_alpha`.\n";
            if (name == "Draw")
                return "---Draws a shape in the next frame of the world only: call it each frame for what follows the\n"
                       "---frame. nil, \"invalid\" for a shape refused (logged under `draw`, saying why).\n" + about +
                       "---@param shape " + shape + "\n---@param style? " + style + "\n"
                       "---@return true|nil ok\n---@return string|nil reason\n"
                       "function " + owner + ".Draw(shape, style) end\n\n";
            if (name == "Add")
                return "---Keeps a shape until removed (shape:Remove()), or for `duration` seconds; it goes when this mod\n"
                       "---unloads. `style.fade_in` and `fade_out` bring it in and take it out over the end of its duration.\n" +
                       about + "---@param shape " + shape + "\n---@param style? " + style + "\n---@param duration? number\n"
                       "---@return wxl.Shape|nil shape\n---@return string|nil reason\n"
                       "function " + owner + ".Add(shape, style, duration) end\n\n";
            if (name == "Update")
                return "---The kept shape becomes another, from a table as Draw's; it stays attached when it is.\n"
                       "---@param self wxl.Shape\n---@param shape " + shape + "\n"
                       "---@return true|nil ok\n---@return string|nil reason\n"
                       "function " + owner + ".Update(self, shape) end\n\n";
            if (name == "SetStyle")
                return "---The kept shape's look, from a table as Draw's.\n"
                       "---@param self wxl.Shape\n---@param style " + style + "\n"
                       "---@return true|nil ok\n---@return string|nil reason\n"
                       "function " + owner + ".SetStyle(self, style) end\n\n";
            const std::string emitter =
                "{spawn: \"point\"|\"disc\"|\"box\"|\"segment\"?, center: wxl.Vec3?, extent: wxl.Vec3?, facing: number?, direction: wxl.Vec3?, "
                "spread: number?, burst: integer?, rate: number?, duration: number?, speed: number|number[]?, life: number|number[]?, "
                "size: number|number[]?, spin: number|number[]?, gravity: number?, drag: number?, ground: \"none\"|\"bounce\"|\"settle\"?, "
                "restitution: number?, friction: number?, attractor: wxl.Vec3?, attraction: number?, attract_radius: number?, seed: integer?}";
            const std::string particles =
                "{mode: \"camera\"|\"ground\"|\"stretched\"?, texture: string|table?, frames: number|{x: integer, y: integer}?, "
                "frame_rate: number?, blend: \"alpha\"|\"add\"|\"modulate\"?, depth: \"tested\"|\"on_top\"?, occluded_alpha: number?, "
                "stretch: number?, softness: number?, mask: boolean?, texture_blur: number?, keys: {t: number, size: number?, color: integer?, alpha: number?}[]?}";
            const std::string particleAbout =
                "---Since abi 0.1.40. The emitter: `spawn` (\"point\", \"disc\" of extent.x yards, \"box\" of extent, \"segment\"\n"
                "---to center + extent), a cone along `direction` (up by default) of half angle `spread`; `speed`, `life`, `size`\n"
                "---(yards across) and `spin` a number or {min, max}; `gravity` (yards/s^2 down), `drag`, `ground` \"bounce\"\n"
                "---(once, `restitution`, `friction`) or \"settle\"; `attraction` towards `attractor` (taking them out within\n"
                "---`attract_radius`). The look: `mode`, a `texture` (none: a soft round blot), sprite sheet `frames` and\n"
                "---`frame_rate`, `blend`, `softness` (yards over which they fade near the ground), up to 4 `keys` of their life.\n";
            if (name == "Emit")
                return "---Throws one burst of `emitter.burst` particles and forgets them.\n" + particleAbout +
                       "---@param emitter " + emitter + "\n---@param look? " + particles + "\n"
                       "---@return true|nil ok\n---@return string|nil reason\n"
                       "function " + owner + ".Emit(emitter, look) end\n\n";
            if (name == "AddEmitter")
                return "---Keeps an emitter: its burst now, then `rate` particles a second for `duration` seconds (nil: until\n"
                       "---stopped); it goes when this mod unloads.\n" + particleAbout +
                       "---@param emitter " + emitter + "\n---@param look? " + particles + "\n"
                       "---@return wxl.Emitter|nil emitter\n---@return string|nil reason\n"
                       "function " + owner + ".AddEmitter(emitter, look) end\n\n";
            if (name == "UpdateEmitter")
                return "---The emitter becomes another, from a table as Emit's; its particles keep how they move.\n"
                       "---@param self wxl.Emitter\n---@param emitter " + emitter + "\n"
                       "---@return true|nil ok\n---@return string|nil reason\n"
                       "function " + owner + ".UpdateEmitter(self, emitter) end\n\n";
            if (name == "SetEmitterStyle")
                return "---Its particles' look, those alive included, from a table as Emit's.\n"
                       "---@param self wxl.Emitter\n---@param look " + particles + "\n"
                       "---@return true|nil ok\n---@return string|nil reason\n"
                       "function " + owner + ".SetEmitterStyle(self, look) end\n\n";
            if (name == "Clear")
                return "---Every shape and emitter this mod keeps goes.\n"
                       "function " + owner + ".Clear() end\n\n";
            if (name == "Stats")
                return "---What the last frame of the world drew and what it cost (every mod's shapes).\n"
                       "---@return {shapes: integer, immediate: integer, drawn: integer, vertices: integer, left_out: integer, rebuilt: integer, build_microseconds: number, max_build_microseconds: number, immediate_microseconds: number, frames: integer, textured: integer, textures: integer, textures_loading: integer, shadows: integer, occluded: integer, reused: integer, particles: integer, particles_emitted: integer, particles_culled: integer, particle_draws: integer, emitters: integer, particle_microseconds: number}|nil stats\n"
                       "---@return string|nil reason\n"
                       "function " + owner + ".Stats() end\n\n";
        }
        if (std::strcmp(row.service, "render") == 0)
        {
            const std::string answers = "---@return true|nil ok\n---@return string|nil reason\n";
            const std::string push =
                "---`push`: a table of numbers (each a 32-bit float) or a string of bytes, up to 128 bytes; nil for none.\n";
            if (name == "CreatePipeline")
                return "---A pipeline of the mod's SPIR-V and the state it draws with, compiled now; nil and why when the\n"
                       "---device refuses it. `vertex`/`fragment`: SPIR-V bytes, or the path of a .spv file of the mod's\n"
                       "---folder. `kind`: \"graphics\" (vertex and fragment, vertices read from buffers), \"fullscreen\"\n"
                       "---(fragment over the pass, `uv` at location 0), \"batch\" (fragment behind a client batch),\n"
                       "---\"compute\" (`compute` alone, run by Dispatch; abi 0.1.34).\n"
                       "---`colors`: formats (\"rgba8\", \"bgra8\" the frame's, \"r8\", \"rg8\", \"r16f\", \"rg16f\",\n"
                       "---\"rgba16f\", \"r32f\", \"rgba32f\"), the frame's by default. `depth`: \"depth32f\" or nil.\n"
                       "---`samples`: 0 the frame's, 1, 2, 4, 8. `blend`: \"none\", \"alpha\", \"add\", \"premultiplied\",\n"
                       "---\"multiply\", one or one per colour. `depth_test`: \"off\", \"less\", \"less_equal\", \"equal\",\n"
                       "---\"greater\", \"greater_equal\", \"always\". `cull`: \"none\", \"back\", \"front\", \"batch\".\n"
                       "---`topology`: \"triangles\", \"triangle_strip\", \"lines\", \"points\".\n"
                       "---@param desc {kind: string?, name: string?, vertex: string?, fragment: string?, compute: string?, colors: string|string[]?, depth: string?, samples: integer?, blend: string|string[]?, depth_test: string?, depth_write: boolean?, cull: string?, topology: string?}\n"
                       "---@return wxl.Pipeline|nil pipeline\n---@return string|nil reason\n"
                       "function " + owner + ".CreatePipeline(desc) end\n\n";
            if (name == "Begin")
                return "---Opens a pass inside OnRenderPoint: what the draws until End go into. `colors`: Images,\n"
                       "---\"scene_color\" (the frame's colour), or {image, clear = {r, g, b, a}}; `depth`: an Image,\n"
                       "---\"scene_depth\" (the frame's depth), or {image, clear = z}. Every attachment has one size.\n"
                       "---@param pass {colors: (wxl.Image|string|table)[]?, depth: wxl.Image|string|table|nil}\n" + answers +
                       "function " + owner + ".Begin(pass) end\n\n";
            if (name == "Draw")
                return "---Draws `vertex_count` vertices `instance_count` times (1 when nil) with a graphics pipeline.\n" + push +
                       "---@param pipeline wxl.Pipeline\n---@param vertex_count integer\n---@param instance_count? integer\n"
                       "---@param push? number[]|string\n" + answers +
                       "function " + owner + ".Draw(pipeline, vertex_count, instance_count, push) end\n\n";
            if (name == "DrawIndexed")
                return "---Draws `index_count` 32-bit indices of a buffer from `first_index`, `instance_count` times.\n" + push +
                       "---@param pipeline wxl.Pipeline\n---@param indices wxl.Buffer\n---@param first_index integer\n"
                       "---@param index_count integer\n---@param instance_count? integer\n---@param push? number[]|string\n" + answers +
                       "function " + owner + ".DrawIndexed(pipeline, indices, first_index, index_count, instance_count, push) end\n\n";
            if (name == "Fullscreen")
                return "---Draws a fullscreen pipeline over the pass.\n" + push +
                       "---@param pipeline wxl.Pipeline\n---@param push? number[]|string\n" + answers +
                       "function " + owner + ".Fullscreen(pipeline, push) end\n\n";
            if (name == "DrawBatch")
                return "---Draws a client batch (OnModelBatchDrawn's `batch`, or one KeepBatch answered) again with a batch\n"
                       "---pipeline, into the open pass. `constants`: as Draw's push, up to 4096 bytes, read at pc.pixelRegs.\n"
                       "---@param batch lightuserdata\n---@param pipeline wxl.Pipeline\n---@param constants? number[]|string\n" +
                       answers + "function " + owner + ".DrawBatch(batch, pipeline, constants) end\n\n";
            if (name == "ReplaceShader")
                return "---Gives the client's shaders named `name` (or starting with it) the mod's SPIR-V for `stage` (0 vertex,\n"
                       "---1 pixel): bytes, or a .spv file of the mod's folder. Kept until RestoreShader, or until the mod is\n"
                       "---disabled, reloads or unloads; the last Lua mod to replace a name holds it.\n"
                       "---@param name string\n---@param stage integer\n---@param spirv string\n" + answers +
                       "function " + owner + ".ReplaceShader(name, stage, spirv) end\n\n";
            if (name == "RestoreShader")
                return "---Gives a shader this mod replaced its own program back; nil, \"not_found\" for one it did not replace.\n"
                       "---@param name string\n---@param stage integer\n" + answers +
                       "function " + owner + ".RestoreShader(name, stage) end\n\n";
            if (name == "Dispatch")
                return "---Runs a compute pipeline over x * y * z workgroups inside OnRenderPoint, outside a pass: the\n"
                       "---frame's drawing stops for it. What it writes through buffer addresses is seen after it.\n" + push +
                       "---@param pipeline wxl.Pipeline\n---@param x integer\n---@param y integer\n---@param z integer\n"
                       "---@param push? number[]|string\n" + answers +
                       "function " + owner + ".Dispatch(pipeline, x, y, z, push) end\n\n";
            if (name == "BonePalette")
                return "---Writes matrices into the open frame and answers their address on the GPU, read until the frame\n"
                       "---ends: a table of numbers, 12 a matrix (three rows of four), or a string of bytes, 48 a matrix.\n"
                       "---No bound on their count but 16 MiB at once. The address goes into a program's push constants\n"
                       "---as two 32-bit words, low first (a string of bytes).\n"
                       "---@param matrices number[]|string\n---@return number|nil address\n---@return string|nil reason\n"
                       "function " + owner + ".BonePalette(matrices) end\n\n";
        }
        if (name == "Begin")
            return "---Opens a zone named `name` on the profiler's timeline, inside the handler or callback running\n"
                   "---now; End closes it. Zones nest. Whatever this call left open is closed when it returns or\n"
                   "---errors. Nothing is recorded while no viewer is connected.\n"
                   "---@param name string\n"
                   "function " + owner + ".Begin(name) end\n\n";
        if (name == "End")
            return "---Closes the innermost zone this mod opened in the call running now; nothing when none is open.\n"
                   "function " + owner + ".End() end\n\n";
        if (name == "Text")
            return "---Adds a line of text to the innermost zone this mod opened: the item, the unit, the file.\n"
                   "---@param text string\n"
                   "function " + owner + ".Text(text) end\n\n";
        return std::string();
    }

    /// "Came with abi 0.1.12: ..." for what a mod asking for the oldest abi cannot use; empty
    /// for what was there from the start.
    std::string CameWith(const wxl::api::Version& since)
    {
        if (wxl::api::AtLeast(wxl::api::Version{0, 1, 5}, since)) return std::string();
        const std::string abi =
            std::to_string(since.major) + "." + std::to_string(since.minor) + "." + std::to_string(since.feature);
        return "Came with abi " + abi + ": a mod whose mod.json asks for an older abi cannot use it.";
    }

    std::string Services()
    {
        std::ostringstream out;
        uint32_t           serviceCount = 0, rowCount = 0, resourceCount = 0, fieldCount = 0;
        const wxl::api::ServiceRow*  services  = wxl::api::Services(&serviceCount);
        const wxl::api::Row*         rows      = wxl::api::Rows(&rowCount);
        const wxl::api::ResourceRow* resources = wxl::api::Resources(&resourceCount);
        const wxl::api::FieldRow*    fields    = wxl::api::Fields(&fieldCount);

        out << "-- The service modules. Each function answers its value, or nil and the reason: \"unsupported\"\n"
               "-- for a runtime without the service or the member, otherwise the result's name (\"invalid\",\n"
               "-- \"not_found\", \"failed\"). A number left out is 0.\n\n";

        // The structs a list answer is made of.
        std::vector<std::string> structs;
        for (uint32_t i = 0; i < fieldCount; ++i)
        {
            bool seen = false;
            for (const std::string& known : structs)
                if (known == fields[i].type) seen = true;
            if (seen) continue;
            structs.push_back(fields[i].type);
            out << "---@class " << ClassOf(fields[i].type) << "\n";
            for (uint32_t j = 0; j < fieldCount; ++j)
                if (std::strcmp(fields[j].type, fields[i].type) == 0)
                    out << "---@field " << fields[j].member << " "
                        << (std::strcmp(fields[j].kind, "str") == 0 ? "string" : "integer") << "\n";
            out << "\n";
        }

        const auto writeRow = [&](const wxl::api::Row& row, const std::string& owner, const char* separator) {
            std::vector<Parameter> parameters = ParametersOf(row.params);
            if (std::string(separator) == ":" && !parameters.empty()) parameters.erase(parameters.begin());
            if (row.doc && *row.doc) out << "---" << row.doc << "\n";
            std::string arguments;
            for (const Parameter& parameter : parameters)
            {
                out << LuaParam(parameter);
                const std::string name = ArgumentName(parameter);
                if (name.empty()) continue;
                arguments += (arguments.empty() ? "" : ", ") + name;
            }
            if (std::strcmp(row.answer, "read_bytes") == 0)
            {
                out << "---@param count integer how many bytes, at most\n";
                arguments += (arguments.empty() ? "" : ", ") + std::string("count");
            }
            const std::string answer = LuaAnswer(row.answer);
            if (!answer.empty()) out << "---@return " << answer << "\n---@return string|nil reason\n";
            out << "function " << owner << separator << row.name << "(" << arguments << ") end\n\n";
        };

        for (uint32_t r = 0; r < resourceCount; ++r)
        {
            out << "---A " << resources[r].name << " of wxl." << resources[r].service << ".\n---@class wxl."
                << resources[r].name << "\nlocal " << resources[r].name << " = {}\n\n";
            for (uint32_t i = 0; i < rowCount; ++i)
            {
                if (rows[i].lua != wxl::api::Lua::generated) continue;
                const std::vector<Parameter> parameters = ParametersOf(rows[i].params);
                if (parameters.empty() || parameters[0].kind != resources[r].name) continue;
                writeRow(rows[i], resources[r].name, ":");
            }
        }

        for (uint32_t s = 0; s < serviceCount; ++s)
        {
            std::ostringstream module;
            bool               any = false;
            for (uint32_t i = 0; i < rowCount; ++i)
            {
                const wxl::api::Row& row = rows[i];
                if (std::strcmp(row.service, services[s].id) != 0 || row.lua == wxl::api::Lua::native) continue;
                const std::vector<Parameter> parameters = ParametersOf(row.params);
                // A generated row taking a resource first is the resource's method; a shim row is the module's.
                if (!parameters.empty() && IsResourceKind(parameters[0].kind) && row.lua == wxl::api::Lua::generated) continue;
                any = true;
            }
            if (!any) continue;
            out << "---" << services[s].name << ". " << services[s].doc
                << (CameWith(services[s].since).empty() ? "" : "\n---" + CameWith(services[s].since))
                << (services[s].raw ? "\n---Raw: present only when the mod's mod.json declares it, in "
                                     "\"requires\" or \"optional\"."
                                   : "")
                << "\n---@class wxl." << services[s].id << "\nlocal " << services[s].id << " = {}\n\n";
            for (uint32_t i = 0; i < rowCount; ++i)
            {
                const wxl::api::Row& row = rows[i];
                if (std::strcmp(row.service, services[s].id) != 0 || row.lua == wxl::api::Lua::native) continue;
                const std::vector<Parameter> parameters = ParametersOf(row.params);
                // A generated row taking a resource first is the resource's method; a shim row is the module's.
                if (!parameters.empty() && IsResourceKind(parameters[0].kind) && row.lua == wxl::api::Lua::generated) continue;
                const wxl::api::Version since = wxl::api::SinceOf(row);
                if (wxl::api::AtLeast(since, services[s].since) && !wxl::api::AtLeast(services[s].since, since))
                    out << "---" << CameWith(since) << "\n";
                if (row.lua == wxl::api::Lua::shim)
                {
                    const std::string shim = ShimDefinition(row, services[s].id);
                    if (shim.empty()) g_unknownShims += std::string(g_unknownShims.empty() ? "" : ", ") + row.name;
                    out << shim;
                }
                else
                    writeRow(row, services[s].id, ".");
            }
        }
        return out.str();
    }

    /// The fields of the wxl class that hold the service modules.
    std::string ServiceFields()
    {
        std::ostringstream out;
        uint32_t           serviceCount = 0, rowCount = 0;
        const wxl::api::ServiceRow* services = wxl::api::Services(&serviceCount);
        const wxl::api::Row*        rows     = wxl::api::Rows(&rowCount);
        for (uint32_t s = 0; s < serviceCount; ++s)
        {
            bool any = false;
            for (uint32_t i = 0; i < rowCount; ++i)
                if (std::strcmp(rows[i].service, services[s].id) == 0 && rows[i].lua != wxl::api::Lua::native) any = true;
            if (any) out << "---@field " << services[s].id << " wxl." << services[s].id << "\n";
        }
        return out.str();
    }

    std::string Generate()
    {
        std::ostringstream out;
        uint32_t           count  = 0;
        const Event*       events = wxl::reflect::Events(&count);
        const std::vector<std::string> families = Families(events, count);

        out << "---@meta wxl\n"
               "-- The Lua API of wxl.lua, for lua-language-server. Generated from the SDK's event\n"
               "-- table (reflect.hpp) and object table (object_reads.def) by wxl-lua-types; do not\n"
               "-- edit by hand.\n\n";

        out << "---A guid, as \"0x\" and sixteen hex digits: nothing is lost and it keys a table.\n"
               "---@alias wxl.Guid string\n\n"
               "---An event's bytes, copied into a string.\n"
               "---@alias wxl.Bytes string\n\n"
               "---@class wxl.Vec3\n---@field x number\n---@field y number\n---@field z number\n\n"
               "---A file's content hash, as wxl.storage.HashFile answers it.\n"
               "---@class wxl.FileHash\n---@field md5 string thirty-two lower-case hex digits\n"
               "---@field size integer bytes\n"
               "---@field layer \"archive\"|\"disk\"|\"folder\"|\"provider\"|\"served\" where the bytes came from\n"
               "---@field source string the archive, the file on the disk, or the mod that serves it\n\n"
               "---What wxl.storage.ReloadAsset did.\n"
               "---@class wxl.AssetReload\n---@field textures integer textures read again in place\n"
               "---@field models integer model files dropped from the model cache\n"
               "---@field objects integer units and players whose model was made again\n"
               "---@field busy integer textures still loading, left alone\n\n"
               "---An element of an update field: a number, a guid's string for a guid or another\n"
               "---64-bit value, a table of numbers for the bytes or the two halves of a dword.\n"
               "---@alias wxl.FieldValue number|string|number[]\n\n";

        // --- the update fields (update_fields.def) ----------------------------------------------
        uint32_t                rowCount  = 0;
        const wxl::field::Row*  fieldRows = wxl::field::Rows(&rowCount);
        uint32_t                kindCount = 0;
        const wxl::field::Kind* kinds     = wxl::field::Kinds(&kindCount);
        out << "---A field of update_fields.def: the client's name, the kind's and the field's, or\n"
               "---the field's alone where the object has one of that name.\n---@alias wxl.FieldName\n";
        for (uint32_t i = 0; i < rowCount; ++i)
            out << "---| \"" << fieldRows[i].name << "\"\n---| \""
                << wxl::field::KindOf(fieldRows[i].kind)->name << "." << fieldRows[i].code << "\"\n";
        out << "\n";
        for (uint32_t k = 0; k < kindCount; ++k)
        {
            out << "---The dword index of every field of a " << kinds[k].name << ".\n---@class wxl.field."
                << kinds[k].name << "\n";
            for (uint32_t i = 0; i < rowCount; ++i)
                if (fieldRows[i].kind == kinds[k].type)
                    out << "---@field " << fieldRows[i].code << " integer " << fieldRows[i].name << "\n";
            out << "\n";
        }
        out << "---Every field's dword index, by kind: wxl.field.Unit.Health is UNIT_FIELD_HEALTH's.\n"
               "---@class wxl.field\n";
        for (uint32_t k = 0; k < kindCount; ++k)
            out << "---@field " << kinds[k].name << " wxl.field." << kinds[k].name << "\n";
        out << "\n";

        // --- the enumerations (enums.def) -------------------------------------------------------
        uint32_t                  enumCount = 0;
        const wxl::reflect::Enum* enums     = wxl::reflect::Enums(&enumCount);
        for (uint32_t e = 0; e < enumCount; ++e)
        {
            out << "---A " << enums[e].type << " value, by name; a newer value than these headers know\n"
                   "---arrives as its number.\n---@alias " << AliasOf(enums[e]) << " ";
            for (uint32_t i = 0; i < enums[e].count; ++i)
                out << (i ? "|" : "") << '"' << enums[e].values[i].name << '"';
            out << "\n\n";
        }

        // --- the families -------------------------------------------------------------------
        out << "-- The script families. A function defined on one of these tables as On<Event> is\n"
               "-- subscribed when the entry file has run and called with the event's parameters;\n"
               "-- on an event that can be answered its return values are taken, as Override\n"
               "-- takes them.\n\n";
        for (const std::string& family : families)
        {
            const std::string stem = family.substr(0, family.size() - 6);
            out << "---@class " << family << "\n";
            for (uint32_t i = 0; i < count; ++i)
            {
                const Event& event = events[i];
                if (stem != event.family) continue;
                out << "---@field On" << event.name << " fun(self: " << family;
                if (!Parameters(event).empty()) out << ", " << Parameters(event);
                out << ")";
                if (Answerable(event))
                    out << ": " << Returns(event) << " answers through " << WritableNames(event);
                out << "\n";
            }
            out << family << " = {}\n\n";
        }

        // --- Observe, Override, Unobserve ---------------------------------------------------
        out << "---@alias wxl.Family ";
        for (size_t i = 0; i < families.size(); ++i) out << (i ? "|\"" : "\"") << families[i] << "\"";
        out << "\n\n";

        out << "---Calls `handler` with the event's parameters whenever it is raised. Returns the\n"
               "---handler's id.\n";
        for (uint32_t i = 0; i < count; ++i)
        {
            const Event& event = events[i];
            out << "---@overload fun(family: \"" << event.family << "Script\", method: \"On" << event.name
                << "\", handler: fun(" << Parameters(event) << ")): integer\n";
        }
        out << "---@param family wxl.Family\n---@param method string\n---@param handler function\n"
               "---@return integer id\nfunction Observe(family, method, handler) end\n\n";

        out << "---Like Observe, on an event that can be answered. The handler returns one value per\n"
               "---member it answers through, in member order: true sets an answer, a string fills a\n"
               "---buffer, nil leaves the member as it is. Taken on the main thread only.\n";
        for (uint32_t i = 0; i < count; ++i)
        {
            const Event& event = events[i];
            if (!Answerable(event)) continue;
            out << "---@overload fun(family: \"" << event.family << "Script\", method: \"On" << event.name
                << "\", handler: fun(" << Parameters(event) << "): " << Returns(event)
                << "): integer\n";
        }
        out << "---@param family wxl.Family\n---@param method string\n---@param handler function\n"
               "---@return integer id\nfunction Override(family, method, handler) end\n\n";

        out << "---Drops a handler by the id Observe or Override returned.\n"
               "---@param id integer\n---@return boolean dropped\nfunction Unobserve(id) end\n\n";

        // --- registerForEvent ---------------------------------------------------------------
        out << "---@alias wxl.CallbackName \"onInit\"|\"onUpdate\"|\"onDraw\"|\"onShutdown\"\n\n"
               "---Registers a callback: onInit at WXL_STAGE_READY (or right after a reload),\n"
               "---onUpdate(delta) at every World.Update, onDraw at every Render.UiRenderEnd,\n"
               "---onShutdown at WXL_STAGE_SHUTDOWN and before a reload.\n"
               "---@overload fun(event: \"onInit\", fn: fun())\n"
               "---@overload fun(event: \"onUpdate\", fn: fun(delta: number))\n"
               "---@overload fun(event: \"onDraw\", fn: fun())\n"
               "---@overload fun(event: \"onShutdown\", fn: fun())\n"
               "---@param event wxl.CallbackName\n---@param fn function\n"
               "function registerForEvent(event, fn) end\n\n";

        // --- objects ------------------------------------------------------------------------
        out << "---A world object, by handle. Every getter answers the value, or nil and why:\n"
               "---\"gone\" for an object that left, \"unsupported\" for a runtime without the read, or\n"
               "---what the object is not (\"a GameObject has no Health\").\n"
               "---\n"
               "---One object is one Lua value for as long as it stays in view: the same at every event\n"
               "---and every lookup, a table key, equal only to itself. A field set on it that is not a\n"
               "---method's name (`obj.seen = true`) is the mod's own and is kept until the object leaves.\n"
               "---@class wxl.Object\n"
               "---@field [string] any what the mod stored on the object\n"
               "local Object = {}\n\n"
               "---Whether the object is still in view.\n---@return boolean|nil valid, string|nil reason\nfunction Object:Valid() end\n\n"
               "---The server's id for the object, as \"0x\" and sixteen hex digits.\n---@return string|nil guid, string|nil reason\nfunction Object:Guid() end\n\n"
               "---The kind's name, \"Unit\", \"GameObject\"...\n---@return wxl.ObjectType|nil type, string|nil reason\nfunction Object:Type() end\n\n"
               "---The kind as a number.\n---@return integer|nil type, string|nil reason\nfunction Object:TypeId() end\n\n"
               "---@return string|nil name, string|nil reason\nfunction Object:Name() end\n\n"
               "---What a unit targets; nil when nothing in view.\n---@return wxl.Object|nil target, string|nil reason\nfunction Object:Target() end\n\n"
               "---The handle itself, as \"0x\" and sixteen hex digits: for logs.\n---@return string\nfunction Object:Handle() end\n\n"
               "---The size of the object's descriptor block, in dwords.\n---@return integer|nil count, string|nil reason\nfunction Object:FieldCount() end\n\n"
               "---One element of a descriptor field: a number, a guid's string, or the four bytes or two\n"
               "---halves of a dword as that many numbers.\n"
               "---@param name wxl.FieldName|string\n---@param element? integer 0 for the first\n"
               "---@return wxl.FieldValue|nil ..., string|nil reason\nfunction Object:Field(name, element) end\n\n"
               "---Every element of a descriptor field, in order; the bytes and halves as tables.\n"
               "---@param name wxl.FieldName|string\n---@return wxl.FieldValue[]|nil values, string|nil reason\nfunction Object:Fields(name) end\n\n"
               "---Writes one element of a descriptor field in the client, until the server's next update\n"
               "---of it replaces it: a number, a guid, or a table of the bytes or the halves.\n"
               "---@param name wxl.FieldName|string\n---@param value wxl.FieldValue\n---@param element? integer\n"
               "---@return boolean|nil written, string|nil reason\nfunction Object:SetField(name, value, element) end\n\n";

        // The generated methods, from the rows: rows sharing a Lua name (Flags, DisplayId, ...)
        // become one method, its kinds joined.
        const std::vector<wxl::lua::names::RowInfo>& rows     = wxl::lua::names::Rows();
        const std::vector<std::string>&              luaNames = wxl::lua::names::RowNames();
        {
            std::vector<wxl::lua::names::Member> members;
            for (const auto& row : rows) members.push_back(wxl::lua::names::Member{row.member, row.kinds});
            g_clash = wxl::lua::names::Clash(members, luaNames);
        }

        std::vector<std::string>                                            order;
        std::map<std::string, std::vector<const wxl::lua::names::RowInfo*>> byName;
        for (size_t i = 0; i < rows.size(); ++i)
        {
            if (!byName.count(luaNames[i])) order.push_back(luaNames[i]);
            byName[luaNames[i]].push_back(&rows[i]);
        }
        for (const std::string& name : order)
        {
            const std::vector<const wxl::lua::names::RowInfo*>& group = byName[name];
            const wxl::lua::names::RowInfo&                     row   = *group[0];
            std::string                                         kinds;
            for (const auto* each : group)
            {
                if (!kinds.empty()) kinds += "; ";
                kinds += wxl::lua::names::KindsText(each->kinds);
            }
            out << "---" << row.doc << "\n---For " << kinds << " (" << group[0]->member;
            for (size_t i = 1; i < group.size(); ++i) out << ", " << group[i]->member;
            out << ").\n";
            if (row.since[0] != 0 || row.since[1] != 1 || row.since[2] != 5)
                out << "---Came with abi " << row.since[0] << "." << row.since[1] << "." << row.since[2]
                    << ": a mod whose mod.json asks for an older abi cannot use it.\n";
            const std::string arg = *row.arg_c ? ObjectArgName(row) : std::string();
            if (*row.arg_c) out << "---@param " << arg << " " << ObjectArgType(row) << "\n";
            out << "---@return " << ObjectAnswer(row) << "|nil value, string|nil reason\n";
            out << "function Object:" << name << "(" << arg << ") end\n\n";
        }
        out << "---Every aura on a unit, in slot order, as Aura(slot) answers each. Came with abi 0.1.18.\n"
               "---@return wxl.Aura[]|nil auras, string|nil reason\nfunction Object:Auras() end\n\n"
               "---The lines of the active player's skill list that hold a skill. Came with abi 0.1.18.\n"
               "---@return wxl.Skill[]|nil skills, string|nil reason\nfunction Object:Skills() end\n\n"
               "---The guids of the active player's other party members. Came with abi 0.1.18.\n"
               "---@return wxl.Guid[]|nil members, string|nil reason\nfunction Object:PartyMembers() end\n\n"
               "---The guids of the active player's raid, itself included. Came with abi 0.1.18.\n"
               "---@return wxl.Guid[]|nil members, string|nil reason\nfunction Object:RaidMembers() end\n\n"
               "---Targets the object, as a left click on it does (wxl.action). Came with abi 0.1.18.\n"
               "---@return boolean|nil done, string|nil reason\nfunction Object:Select() end\n\n"
               "---Right-clicks the object: talks, loots, opens, uses (wxl.action). Came with abi 0.1.18.\n"
               "---@return boolean|nil done, string|nil reason\nfunction Object:Interact() end\n\n"
               "---Casts a spell the player knows at this unit (wxl.action). Came with abi 0.1.18.\n"
               "---@param spell_id integer\n---@return boolean|nil done, string|nil reason\nfunction Object:CastAt(spell_id) end\n\n"
               "---Follows this unit, as /follow does (wxl.action). Came with abi 0.1.18.\n"
               "---@return boolean|nil done, string|nil reason\nfunction Object:Follow() end\n\n"
               "---Targets what this unit targets, as /assist does (wxl.action). Came with abi 0.1.18.\n"
               "---@return boolean|nil done, string|nil reason\nfunction Object:Assist() end\n\n"
               "---Uses this item, on a unit, or on what it picks itself (wxl.action). Came with abi 0.1.18.\n"
               "---@param target? wxl.Object\n---@return boolean|nil done, string|nil reason\nfunction Object:Use(target) end\n\n"
               "---An aura on a unit, as Aura(slot) answers it.\n---@class wxl.Aura\n---@field spell_id integer\n"
               "---@field caster wxl.Guid\n---@field stacks integer\n---@field level integer\n"
               "---@field flags integer 0x10 helpful, 0x20 with a duration, 0x80 harmful\n"
               "---@field duration integer milliseconds\n---@field expiration integer when it ends, on the client's clock in milliseconds; 0 for never\n\n"
               "---A line of the skill list, as Skill(line) answers it.\n---@class wxl.Skill\n---@field id integer\n"
               "---@field value integer\n---@field max integer\n---@field step integer\n"
               "---@field temporary_bonus integer\n---@field permanent_bonus integer\n\n";

        // --- the wxl table ------------------------------------------------------------------
        out << Services();
        out << "---@class wxl.ModInfo\n---@field id string\n---@field name string\n---@field version string\n"
               "---@field directory string\n\n"
               "---@class wxl.RuntimeInfo\n---@field id string\n---@field name string\n---@field version integer\n"
               "---@field abi string the contract this mod is served, \"major.minor.feature\"\n"
               "---@field abi_major integer\n---@field abi_minor integer\n---@field abi_feature integer\n"
               "---Whether the runtime raises an event at all, so that subscribing to it can work: false\n"
               "---for one it never raises; nil and \"unsupported\" on a runtime older than abi 0.1.13.\n"
               "---@field Raises fun(family: wxl.Family, event: string): boolean|nil, string|nil\n\n"
               "---@class wxl.object\n"
               "local object = {}\n\n"
               "---The object the local player controls, or nil while out of the world.\n"
               "---@return wxl.Object|nil\nfunction object.LocalPlayer() end\n\n"
               "---The object carrying a guid, or nil when it is not in view.\n"
               "---@param guid string|integer \"0x\" and up to sixteen hex digits, or a whole number\n---@return wxl.Object|nil\nfunction object.FromGuid(guid) end\n\n"
               "---The objects of a type in view, or every object.\n"
               "---@param type? wxl.ObjectType|integer\n---@return wxl.Object[]\nfunction object.InView(type) end\n\n"
               "---One element of a field of every object of a list, in its order, in one call; false\n"
               "---for an object that has none.\n"
               "---@param objects wxl.Object[]\n---@param name wxl.FieldName|string\n---@param element? integer\n"
               "---@return (wxl.FieldValue|false)[]\nfunction object.Fields(objects, name, element) end\n\n"
               "---Asks for ObjectScript's OnObjectFieldChanged whenever the field changes on an object of\n"
               "---the type (nil for every object), for this mod, until Unwatch or until the mod closes.\n"
               "---@param type wxl.ObjectType|\"Object\"|nil\n---@param name wxl.FieldName|string\n"
               "---@return boolean|nil watched, string|nil reason\nfunction object.Watch(type, name) end\n\n"
               "---Gives back what Watch(type, name) took: this mod's handlers no longer receive the\n"
               "---field's changes. True also for a field the mod did not watch.\n"
               "---@param type wxl.ObjectType|\"Object\"|nil\n---@param name wxl.FieldName|string\n"
               "---@return boolean|nil done, string|nil reason\nfunction object.Unwatch(type, name) end\n\n"
               "---@class wxl\n"
               "---@field mod wxl.ModInfo this mod, from its manifest or its folder\n"
               "---@field runtime wxl.RuntimeInfo the runtime that loaded wxl.lua\n"
               "---@field object wxl.object\n"
               "---@field field wxl.field every update field's dword index, by kind\n"
            << ServiceFields()
            << "---@field events table<wxl.Family, string[]> every family's events, as On<Event>\n"
               "wxl = {}\n\n"
               "---Writes a line to the runtime's log, tagged with the mod's id.\n"
               "---@param level \"trace\"|\"debug\"|\"info\"|\"warn\"|\"error\"\n"
               "---@param ... any\nfunction wxl.log(level, ...) end\n";

        return out.str();
    }

    std::string ReadAll(const char* path)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream) return std::string();
        std::ostringstream buffer;
        buffer << stream.rdbuf();
        std::string text = buffer.str();
        // Line endings are not a difference.
        std::string normalised;
        for (const char c : text)
            if (c != '\r') normalised += c;
        return normalised;
    }
} // namespace

int main(int argc, char** argv)
{
    const std::string text = Generate();
    if (!g_clash.empty())
    {
        std::printf("the object methods' names are ambiguous: %s\n", g_clash.c_str());
        return 1;
    }
    if (!g_unknownShims.empty())
    {
        std::printf("shim rows with no definition in ShimDefinition: %s\n", g_unknownShims.c_str());
        return 1;
    }

    if (argc == 1)
    {
        std::fwrite(text.data(), 1, text.size(), stdout);
        return 0;
    }
    if (argc == 3 && std::strcmp(argv[1], "--check") == 0)
    {
        if (ReadAll(argv[2]) == text)
        {
            std::printf("%s is what the generator writes\n", argv[2]);
            return 0;
        }
        std::printf("%s differs from what the generator writes; build wxl-lua-types-gen, or run "
                    "wxl-lua-types %s\n",
                    argv[2], argv[2]);
        return 1;
    }
    if (argc == 2)
    {
        if (ReadAll(argv[1]) == text) return 0;
        std::ofstream stream(argv[1], std::ios::binary);
        if (!stream)
        {
            std::printf("cannot write %s\n", argv[1]);
            return 1;
        }
        stream << text;
        std::printf("wrote %s\n", argv[1]);
        return 0;
    }
    std::printf("usage: wxl-lua-types [--check] [<file>]\n");
    return 2;
}
