// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL

#include "Services.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "Objects.hpp"
#include "ScriptMod.hpp"
#include "Shims.hpp"
#include "wxl/reflect.hpp"
#include "wxl/services/rows.hpp"

namespace wxl::lua
{
    const char* ReasonOf(wxl_result result)
    {
        switch (result)
        {
        case WXL_OK: return "ok";
        case WXL_E_UNSUPPORTED: return "unsupported";
        case WXL_E_PENDING: return "pending";
        case WXL_E_VERSION: return "version";
        case WXL_E_INVALID: return "invalid";
        case WXL_E_NOT_FOUND: return "not_found";
        case WXL_E_FAILED: return "failed";
        }
        return "failed";
    }

    int PushResultFailure(lua_State* L, wxl_result result)
    {
        lua_pushnil(L);
        lua_pushstring(L, ReasonOf(result));
        return 2;
    }

    namespace
    {
        namespace kind = wxl::api::kind;
        using wxl::api::IsResource;
        using wxl::api::When;

        /// The largest whole number a Lua number holds exactly.
        constexpr double kExact = 9007199254740992.0;

        // --- the resources -----------------------------------------------------------------
        //
        // A resource reaches Lua as a full userdata of its own metatable, "wxl.<Name>", whose
        // methods are the rows taking it first. The box remembers whether it was given back, and
        // how, so that what gives it back at the end -- the collector, or the mod's unload --
        // needs no row of its own.

        struct ResourceBox
        {
            uint64_t value;              ///< the resource's C value, widened
            void (*give_back)(uint64_t); ///< its close row, called with the value
            bool open;
        };

        /// The close row of each resource kind (WXL_API_CLOSE), and how to call it.
        template <class R>
        struct Closer;

        namespace closers
        {
            using namespace ::wxl::api::kind;
#define WXL_API_SERVICE(svc, name, version, since, access, doc)
#define WXL_API(svc, member, Name, answer, params, lua, cxx, doc)
#define WXL_API_RESOURCE(svc, Name, ctype, none_value)
#define WXL_API_SINCE(svc, member, since)
#define WXL_API_FIELD(type, member, field_kind)
#define WXL_API_CLOSE(svc, member, Name, answer_kind, Resource, when_kind, doc)                  \
    struct Resource##Closer                                                             \
    {                                                                                   \
        using Api    = wxl_##svc##_api;                                                 \
        using answer = answer_kind;                                                     \
        static constexpr auto        kMember = &wxl_##svc##_api::member;                \
        static constexpr When        kWhen   = When::when_kind;                         \
        static constexpr const char* kMethod = #Name;                                   \
    };
#include "wxl/services/tables.inc"
#undef WXL_API_CLOSE
#undef WXL_API_FIELD
#undef WXL_API_SINCE
#undef WXL_API_RESOURCE
#undef WXL_API
#undef WXL_API_SERVICE
        } // namespace closers

#define WXL_API_SERVICE(svc, name, version, since, access, doc)
#define WXL_API(svc, member, Name, answer, params, lua, cxx, doc)
#define WXL_API_RESOURCE(svc, Name, ctype, none_value)
#define WXL_API_SINCE(svc, member, since)
#define WXL_API_FIELD(type, member, field_kind)
#define WXL_API_CLOSE(svc, member, Name, answer_kind, Resource, when_kind, doc) \
    template <>                                                        \
    struct Closer<kind::Resource> : closers::Resource##Closer          \
    {                                                                  \
    };
#include "wxl/services/tables.inc"
#undef WXL_API_CLOSE
#undef WXL_API_FIELD
#undef WXL_API_SINCE
#undef WXL_API_RESOURCE
#undef WXL_API
#undef WXL_API_SERVICE

        /// A resource's C value as the box keeps it, and back: a number, or an address.
        template <class T>
        uint64_t Widen(T value)
        {
            if constexpr (std::is_pointer_v<T>)
                return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(value));
            else
                return static_cast<uint64_t>(value);
        }

        template <class T>
        T Narrow(uint64_t value)
        {
            if constexpr (std::is_pointer_v<T>)
                return reinterpret_cast<T>(static_cast<uintptr_t>(value));
            else
                return static_cast<T>(value);
        }

        /// Calls a resource's close row; its answer, WXL_OK for a row that answers nothing.
        template <class R>
        wxl_result GiveBack(uint64_t value)
        {
            using C          = Closer<R>;
            const auto* api  = wxl::api::Table<typename C::Api>();
            if (!wxl::detail::Carries(api, C::kMember)) return WXL_E_UNSUPPORTED;
            const auto handle = Narrow<typename R::type>(value);
            if constexpr (std::is_same_v<typename C::answer, kind::none>)
            {
                (api->*C::kMember)(handle);
                return WXL_OK;
            }
            else
            {
                return (api->*C::kMember)(handle);
            }
        }

        template <class R>
        void GiveBackSilently(uint64_t value)
        {
            (void)GiveBack<R>(value);
        }

        std::string MetaName(const char* resource) { return std::string("wxl.") + resource; }

        /// The list of what a state gives back when it unloads, in the order it was handed out.
        char kKeptKey;

        void PushKept(lua_State* L)
        {
            lua_pushlightuserdata(L, &kKeptKey);
            lua_rawget(L, LUA_REGISTRYINDEX);
        }

        template <class R>
        void PushResource(lua_State* L, typename R::type value)
        {
            ResourceBox* box = static_cast<ResourceBox*>(lua_newuserdata(L, sizeof(ResourceBox)));
            box->value       = Widen(value);
            box->give_back   = &GiveBackSilently<R>;
            box->open        = true;
            luaL_getmetatable(L, MetaName(R::kName).c_str());
            lua_setmetatable(L, -2);
            if (Closer<R>::kWhen == When::unloaded)
            {
                PushKept(L);
                lua_pushvalue(L, -2);
                lua_rawseti(L, -2, static_cast<int>(lua_objlen(L, -2)) + 1);
                lua_pop(L, 1);
            }
        }

        template <class R>
        ResourceBox* CheckResource(lua_State* L, int index)
        {
            return static_cast<ResourceBox*>(luaL_checkudata(L, index, MetaName(R::kName).c_str()));
        }

        // --- reading the arguments ---------------------------------------------------------

        struct LuaRow;

        /// Where the next Lua argument is, the row's next parameter, and the length the bytes so
        /// far share.
        struct Reader
        {
            lua_State*    L;
            int           next;
            const LuaRow* row;
            int           declared = 0; ///< the row's parameters read so far, bytes_size included
            int64_t       bytes    = -1;
        };

        /// Raises the error of a parameter left out: the function, the parameter, what it takes.
        [[noreturn]] void MissingArgument(const Reader& r, int declared, const char* what);

        /// Counts the row's next parameter, and raises MissingArgument when the script left it
        /// out or passed nil: only an opt<K> parameter may be left out.
        void Require(Reader& r, const char* what)
        {
            const int declared = r.declared++;
            if (lua_isnoneornil(r.L, r.next)) MissingArgument(r, declared, what);
        }

        /// A whole number in [low, high].
        double CheckWhole(lua_State* L, int index, double low, double high)
        {
            const double value = luaL_checknumber(L, index);
            if (std::floor(value) != value || value < low || value > high)
            {
                char message[96];
                std::snprintf(message, sizeof message, "a whole number from %.0f to %.0f", low, high);
                luaL_argerror(L, index, message);
            }
            return value;
        }

        template <class K, class = void>
        struct Arg;

        template <>
        struct Arg<kind::str>
        {
            using Held = wxl_str;
            static Held Read(Reader& r)
            {
                Require(r, "a string");
                size_t      size = 0;
                const char* text = luaL_checklstring(r.L, r.next++, &size);
                return wxl_str{text, static_cast<uint32_t>(size)};
            }
            static std::tuple<wxl_str> C(const Held& held) { return {held}; }
        };

        template <>
        struct Arg<kind::cstr>
        {
            using Held = const char*;
            static Held Read(Reader& r)
            {
                Require(r, "a string");
                return luaL_checkstring(r.L, r.next++);
            }
            static std::tuple<const char*> C(Held held) { return {held}; }
        };

        template <class T, int64_t Low, uint64_t High>
        struct WholeArg
        {
            using Held = T;
            static Held Read(Reader& r)
            {
                Require(r, "a whole number");
                const double high = static_cast<double>(High) < kExact ? static_cast<double>(High) : kExact;
                return static_cast<T>(CheckWhole(r.L, r.next++, static_cast<double>(Low), high));
            }
            static std::tuple<T> C(Held held) { return {held}; }
        };

        template <> struct Arg<kind::i32> : WholeArg<int32_t, INT32_MIN, INT32_MAX> {};
        template <> struct Arg<kind::u32> : WholeArg<uint32_t, 0, UINT32_MAX> {};
        template <> struct Arg<kind::u64> : WholeArg<uint64_t, 0, UINT64_MAX> {};
        template <> struct Arg<kind::uptr> : WholeArg<uintptr_t, 0, UINTPTR_MAX> {};

        template <>
        struct Arg<kind::f64>
        {
            using Held = double;
            static Held Read(Reader& r)
            {
                Require(r, "a number");
                return luaL_checknumber(r.L, r.next++);
            }
            static std::tuple<double> C(Held held) { return {held}; }
        };

        template <>
        struct Arg<kind::boolean>
        {
            using Held = wxl_bool;
            static Held Read(Reader& r)
            {
                Require(r, "true or false");
                luaL_checktype(r.L, r.next, LUA_TBOOLEAN);
                return lua_toboolean(r.L, r.next++) ? WXL_TRUE : WXL_FALSE;
            }
            static std::tuple<wxl_bool> C(Held held) { return {held}; }
        };

        /// Every argument from here on, each a value a script event can carry.
        template <>
        struct Arg<kind::values>
        {
            using Held = std::vector<wxl_value>;
            static Held Read(Reader& r)
            {
                ++r.declared;
                const int top = lua_gettop(r.L);
                for (int i = r.next; i <= top; ++i)
                {
                    const int type = lua_type(r.L, i);
                    if (type != LUA_TNIL && type != LUA_TNUMBER && type != LUA_TSTRING && type != LUA_TBOOLEAN)
                        luaL_argerror(r.L, i, "a value the client's script can take: nil, a number, a string "
                                              "or a boolean");
                }
                Held values;
                values.reserve(top >= r.next ? static_cast<size_t>(top - r.next + 1) : 0);
                for (int i = r.next; i <= top; ++i)
                {
                    wxl_value value{};
                    switch (lua_type(r.L, i))
                    {
                    case LUA_TNUMBER:
                        value.type      = WXL_VALUE_NUMBER;
                        value.as.number = lua_tonumber(r.L, i);
                        break;
                    case LUA_TSTRING:
                    {
                        size_t      size = 0;
                        const char* text = lua_tolstring(r.L, i, &size);
                        value.type       = WXL_VALUE_STRING;
                        value.as.string  = wxl_str{text, static_cast<uint32_t>(size)};
                        break;
                    }
                    case LUA_TBOOLEAN:
                        value.type       = WXL_VALUE_BOOL;
                        value.as.boolean = lua_toboolean(r.L, i) ? WXL_TRUE : WXL_FALSE;
                        break;
                    default: value.type = WXL_VALUE_NIL; break;
                    }
                    values.push_back(value);
                }
                r.next = top + 1;
                return values;
            }
            static std::tuple<const wxl_value*, uint32_t> C(const Held& held)
            {
                return {held.empty() ? nullptr : held.data(), static_cast<uint32_t>(held.size())};
            }
        };

        /// A string of bytes; every one a call takes must be as long as the first.
        template <>
        struct Arg<kind::bytes>
        {
            using Held = const void*;
            static Held Read(Reader& r)
            {
                Require(r, "a string of bytes");
                size_t      size  = 0;
                const int   index = r.next++;
                const char* data  = luaL_checklstring(r.L, index, &size);
                if (r.bytes >= 0 && static_cast<int64_t>(size) != r.bytes)
                    luaL_argerror(r.L, index, "as many bytes as the string before it");
                r.bytes = static_cast<int64_t>(size);
                return data;
            }
            static std::tuple<const void*> C(Held held) { return {held}; }
        };

        /// The length the bytes before it share: not an argument of its own.
        template <>
        struct Arg<kind::bytes_size>
        {
            using Held = uint32_t;
            static Held Read(Reader& r)
            {
                ++r.declared;
                return r.bytes < 0 ? 0u : static_cast<uint32_t>(r.bytes);
            }
            static std::tuple<uint32_t> C(Held held) { return {held}; }
        };

        /// A resource the mod holds, still open.
        template <class R>
        struct Arg<R, std::enable_if_t<IsResource<R>::value>>
        {
            using Held = typename R::type;
            static Held Read(Reader& r)
            {
                Require(r, (std::string("a wxl.") + R::kName).c_str());
                const int    index = r.next++;
                ResourceBox* box   = CheckResource<R>(r.L, index);
                if (!box->open)
                {
                    const std::string why = std::string("the ") + R::kName + " was given back already";
                    luaL_argerror(r.L, index, why.c_str());
                }
                return Narrow<Held>(box->value);
            }
            static std::tuple<Held> C(Held held) { return {held}; }
        };

        /// A world object: the mod's object, as a wxl.object method hands it.
        template <>
        struct Arg<kind::object>
        {
            using Held = wxl_handle;
            static Held Read(Reader& r)
            {
                Require(r, "an object");
                return CheckObject(r.L, r.next++);
            }
            static std::tuple<wxl_handle> C(Held held) { return {held}; }
        };

        /// A point: a table with x, y and z, as Position() answers one.
        template <>
        struct Arg<kind::vec3>
        {
            using Held = wxl_vec3;
            static Held Read(Reader& r)
            {
                Require(r, "a point");
                const int at = r.next++;
                luaL_checktype(r.L, at, LUA_TTABLE);
                lua_getfield(r.L, at, "x");
                lua_getfield(r.L, at, "y");
                lua_getfield(r.L, at, "z");
                if (!lua_isnumber(r.L, -3) || !lua_isnumber(r.L, -2) || !lua_isnumber(r.L, -1))
                    luaL_argerror(r.L, at, "a point: a table with numbers x, y and z");
                const wxl_vec3 point{static_cast<float>(lua_tonumber(r.L, -3)), static_cast<float>(lua_tonumber(r.L, -2)),
                                     static_cast<float>(lua_tonumber(r.L, -1))};
                lua_pop(r.L, 3);
                return point;
            }
            static std::tuple<wxl_vec3> C(Held held) { return {held}; }
        };

        /// A point the C member takes by address: read as a point, handed over as the address of
        /// the copy the call holds.
        template <>
        struct Arg<kind::vec3_in>
        {
            using Held = wxl_vec3;
            static Held Read(Reader& r) { return Arg<kind::vec3>::Read(r); }
            static std::tuple<const wxl_vec3*> C(const Held& held) { return {&held}; }
        };

        template <>
        struct Arg<kind::f32>
        {
            using Held = float;
            static Held Read(Reader& r)
            {
                Require(r, "a number");
                return static_cast<float>(luaL_checknumber(r.L, r.next++));
            }
            static std::tuple<float> C(Held held) { return {held}; }
        };

        /// A number field of a table, or `absent` when the table leaves it out.
        float NumberField(lua_State* L, int table, const char* name, float absent)
        {
            lua_getfield(L, table, name);
            float value = absent;
            if (!lua_isnil(L, -1))
            {
                if (!lua_isnumber(L, -1))
                {
                    lua_pop(L, 1);
                    const std::string why = std::string("a placement whose ") + name + " is a number";
                    luaL_argerror(L, table, why.c_str());
                }
                value = static_cast<float>(lua_tonumber(L, -1));
            }
            lua_pop(L, 1);
            return value;
        }

        /// Where wxl.m2 puts a model: {position = {x, y, z}, yaw, pitch, roll, scale}, the turn 0
        /// and the scale 1 when left out.
        template <>
        struct Arg<kind::placement>
        {
            using Held = wxl_m2_placement;
            static Held Read(Reader& r)
            {
                Require(r, "a placement");
                const int at = r.next++;
                luaL_checktype(r.L, at, LUA_TTABLE);
                wxl_m2_placement placement{};
                placement.struct_size = sizeof placement;
                lua_getfield(r.L, at, "position");
                if (!lua_istable(r.L, -1)) luaL_argerror(r.L, at, "a placement: a table with a position {x, y, z}");
                lua_getfield(r.L, -1, "x");
                lua_getfield(r.L, -2, "y");
                lua_getfield(r.L, -3, "z");
                if (!lua_isnumber(r.L, -3) || !lua_isnumber(r.L, -2) || !lua_isnumber(r.L, -1))
                    luaL_argerror(r.L, at, "a placement whose position is a table with numbers x, y and z");
                placement.position = wxl_vec3{static_cast<float>(lua_tonumber(r.L, -3)),
                                              static_cast<float>(lua_tonumber(r.L, -2)),
                                              static_cast<float>(lua_tonumber(r.L, -1))};
                lua_pop(r.L, 4);
                placement.yaw   = NumberField(r.L, at, "yaw", 0.0f);
                placement.pitch = NumberField(r.L, at, "pitch", 0.0f);
                placement.roll  = NumberField(r.L, at, "roll", 0.0f);
                placement.scale = NumberField(r.L, at, "scale", 1.0f);
                return placement;
            }
            static std::tuple<const wxl_m2_placement*> C(const Held& held) { return {&held}; }
        };

        /// An address of the client's: what an event or a call handed the script (a light
        /// userdata), or a resource of the script's own that is one, still open (a wxl.Model).
        void* CheckAddress(Reader& r, const char* what)
        {
            Require(r, what);
            const int index = r.next++;
            if (lua_type(r.L, index) == LUA_TLIGHTUSERDATA) return lua_touserdata(r.L, index);
            if (lua_type(r.L, index) == LUA_TUSERDATA && lua_getmetatable(r.L, index))
            {
                lua_getfield(r.L, -1, "__wxl_address");
                const bool resource = lua_toboolean(r.L, -1) != 0;
                lua_pop(r.L, 2);
                if (resource)
                {
                    const ResourceBox* box = static_cast<const ResourceBox*>(lua_touserdata(r.L, index));
                    if (!box->open) luaL_argerror(r.L, index, "it was given back already");
                    return reinterpret_cast<void*>(static_cast<uintptr_t>(box->value));
                }
            }
            luaL_argerror(r.L, index, what);
            return nullptr;
        }

        template <>
        struct Arg<kind::ptr>
        {
            using Held = void*;
            static Held Read(Reader& r) { return CheckAddress(r, "an address an event or a call handed you"); }
            static std::tuple<void*> C(Held held) { return {held}; }
        };

        template <>
        struct Arg<kind::cptr>
        {
            using Held = const void*;
            static Held Read(Reader& r) { return CheckAddress(r, "an address an event or a call handed you"); }
            static std::tuple<const void*> C(Held held) { return {held}; }
        };

        /// A parameter a script may leave out: K's zero when it does, which the row's doc says.
        template <class K>
        struct Arg<kind::opt<K>>
        {
            using Held = typename Arg<K>::Held;
            static Held Read(Reader& r)
            {
                if (!lua_isnoneornil(r.L, r.next)) return Arg<K>::Read(r);
                ++r.declared;
                ++r.next;
                return Held{};
            }
            static auto C(const Held& held) { return Arg<K>::C(held); }
        };

        // --- pushing the answer ------------------------------------------------------------

        /// The C type name of an enumeration of enums.def, for its value names.
        template <class E>
        struct EnumOf;

#define WXL_ENUM(type)                                    \
    template <>                                           \
    struct EnumOf<type>                                   \
    {                                                     \
        static constexpr const char* kType = #type;       \
    };
#define WXL_ENUM_VALUE(type, constant, value, name)
#define WXL_ENUM_END(type)
#include "wxl/enums.def"
#undef WXL_ENUM_END
#undef WXL_ENUM_VALUE
#undef WXL_ENUM

        void PushText(lua_State* L, wxl_str text)
        {
            lua_pushlstring(L, text.data ? text.data : "", text.data ? text.size : 0);
        }

        template <class K>
        void PushValue(lua_State* L, const typename wxl::api::Value<K>::type& value)
        {
            if constexpr (std::is_same_v<K, kind::str>)
                PushText(L, value);
            else if constexpr (std::is_same_v<K, kind::vec3>)
            {
                lua_createtable(L, 0, 3);
                lua_pushnumber(L, value.x);
                lua_setfield(L, -2, "x");
                lua_pushnumber(L, value.y);
                lua_setfield(L, -2, "y");
                lua_pushnumber(L, value.z);
                lua_setfield(L, -2, "z");
            }
            else if constexpr (std::is_same_v<K, kind::boolean>)
                lua_pushboolean(L, value == WXL_TRUE);
            else
                lua_pushnumber(L, static_cast<lua_Number>(value));
        }

        /// Whether a struct carries struct_size, which its reader sets before the call.
        template <class T, class = void>
        struct Sized : std::false_type
        {
        };
        template <class T>
        struct Sized<T, std::void_t<decltype(T::struct_size)>> : std::true_type
        {
        };

        /// One element of a list<struct> answer, as a table of the fields its rows name.
        template <class E>
        void PushStruct(lua_State* L, const E& element)
        {
            uint32_t                        count  = 0;
            const wxl::api::FieldRow* const fields = wxl::api::Fields(&count);
            lua_newtable(L);
            for (uint32_t i = 0; i < count; ++i)
            {
                if (fields[i].of != &wxl::api::TypeTag<E>) continue;
                const char* at = reinterpret_cast<const char*>(&element) + fields[i].offset;
                if (std::strcmp(fields[i].kind, "str") == 0)
                {
                    wxl_str text;
                    std::memcpy(&text, at, sizeof text);
                    PushText(L, text);
                }
                else if (std::strcmp(fields[i].kind, "u64") == 0)
                {
                    uint64_t number;
                    std::memcpy(&number, at, sizeof number);
                    lua_pushnumber(L, static_cast<lua_Number>(number));
                }
                else if (std::strcmp(fields[i].kind, "i32") == 0)
                {
                    int32_t number;
                    std::memcpy(&number, at, sizeof number);
                    lua_pushnumber(L, number);
                }
                else
                {
                    uint32_t number;
                    std::memcpy(&number, at, sizeof number);
                    lua_pushnumber(L, number);
                }
                lua_setfield(L, -2, fields[i].member);
            }
        }

        template <class A, class = void>
        struct Reply;

        template <>
        struct Reply<kind::none>
        {
            template <class F>
            static int Call(lua_State*, Reader&, F&& f)
            {
                f();
                return 0;
            }
        };

        template <>
        struct Reply<kind::result>
        {
            template <class F>
            static int Call(lua_State* L, Reader&, F&& f)
            {
                const wxl_result result = f();
                if (result != WXL_OK) return PushResultFailure(L, result);
                lua_pushboolean(L, 1);
                return 1;
            }
        };

        template <>
        struct Reply<kind::boolean>
        {
            template <class F>
            static int Call(lua_State* L, Reader&, F&& f)
            {
                lua_pushboolean(L, f() == WXL_TRUE);
                return 1;
            }
        };

        template <>
        struct Reply<kind::u32>
        {
            template <class F>
            static int Call(lua_State* L, Reader&, F&& f)
            {
                lua_pushnumber(L, f());
                return 1;
            }
        };

        template <>
        struct Reply<kind::uptr>
        {
            template <class F>
            static int Call(lua_State* L, Reader&, F&& f)
            {
                lua_pushnumber(L, static_cast<lua_Number>(f()));
                return 1;
            }
        };

        /// An address of the client's, as a light userdata; nil, "not_found" for none.
        template <>
        struct Reply<kind::ptr>
        {
            template <class F>
            static int Call(lua_State* L, Reader&, F&& f)
            {
                void* const address = f();
                if (!address) return PushResultFailure(L, WXL_E_NOT_FOUND);
                lua_pushlightuserdata(L, address);
                return 1;
            }
        };

        template <>
        struct Reply<kind::address>
        {
            template <class F>
            static int Call(lua_State* L, Reader&, F&& f)
            {
                const uintptr_t address = f();
                if (!address) return PushResultFailure(L, WXL_E_NOT_FOUND);
                lua_pushnumber(L, static_cast<lua_Number>(address));
                return 1;
            }
        };

        template <>
        struct Reply<kind::str>
        {
            template <class F>
            static int Call(lua_State* L, Reader&, F&& f)
            {
                PushText(L, f());
                return 1;
            }
        };

        template <class E>
        struct Reply<kind::enumeration<E>>
        {
            template <class F>
            static int Call(lua_State* L, Reader&, F&& f)
            {
                const uint32_t                  value       = f();
                const wxl::reflect::Enum* const enumeration = wxl::reflect::FindEnum(EnumOf<E>::kType);
                const char* const name = enumeration ? wxl::reflect::NameOf(*enumeration, value) : nullptr;
                if (name)
                    lua_pushstring(L, name);
                else
                    lua_pushnumber(L, value);
                return 1;
            }
        };

        template <class K>
        struct Reply<kind::out<K>>
        {
            template <class F>
            static int Call(lua_State* L, Reader&, F&& f)
            {
                typename wxl::api::Value<K>::type value{};
                const wxl_result                  result = f(&value);
                if (result != WXL_OK) return PushResultFailure(L, result);
                PushValue<K>(L, value);
                return 1;
            }
        };

        /// A value that may not be there: the value, or nil when the call found none.
        template <class K>
        struct Reply<kind::found<K>>
        {
            template <class F>
            static int Call(lua_State* L, Reader&, F&& f)
            {
                typename wxl::api::Value<K>::type value{};
                if (f(&value) != WXL_TRUE)
                {
                    lua_pushnil(L);
                    return 1;
                }
                PushValue<K>(L, value);
                return 1;
            }
        };

        /// The whole of something whose size the call says: asked, then read, once more if it
        /// grew in between.
        template <>
        struct Reply<kind::whole_bytes>
        {
            template <class F>
            static int Call(lua_State* L, Reader&, F&& f)
            {
                // A buffer of no capacity, not a null one: an empty file fits it.
                char        none   = 0;
                std::string bytes;
                uint64_t    got    = 0;
                wxl_result  result = f(static_cast<void*>(&none), uint64_t(0), &got);
                for (int attempt = 0; attempt < 3 && result == WXL_E_INVALID && got > bytes.size(); ++attempt)
                {
                    if (got > 0x7FFFFFFFu) return PushResultFailure(L, WXL_E_INVALID);
                    bytes.resize(static_cast<size_t>(got));
                    got    = 0;
                    result = f(&bytes[0], static_cast<uint64_t>(bytes.size()), &got);
                }
                if (result != WXL_OK) return PushResultFailure(L, result);
                lua_pushlstring(L, bytes.data(), static_cast<size_t>(got < bytes.size() ? got : bytes.size()));
                return 1;
            }
        };

        /// As many bytes as the next argument says, or fewer at the end of what is read.
        template <>
        struct Reply<kind::read_bytes>
        {
            template <class F>
            static int Call(lua_State* L, Reader& r, F&& f)
            {
                if (lua_isnoneornil(L, r.next)) MissingArgument(r, -1, "how many bytes to read, a whole number");
                const uint64_t count = static_cast<uint64_t>(CheckWhole(L, r.next++, 0, 0x7FFFFFFF));
                std::string    bytes(static_cast<size_t>(count) + 1, '\0');
                uint64_t       got    = 0;
                const wxl_result result = f(static_cast<void*>(&bytes[0]), count, &got);
                if (result != WXL_OK) return PushResultFailure(L, result);
                lua_pushlstring(L, bytes.data(), static_cast<size_t>(got < count ? got : count));
                return 1;
            }
        };

        /// Counted, then filled: an array.
        template <class E>
        struct Reply<kind::list<E>>
        {
            template <class F>
            static int Call(lua_State* L, Reader&, F&& f)
            {
                const uint32_t count = f(static_cast<E*>(nullptr), uint32_t(0));
                std::vector<E> items(count);
                if constexpr (Sized<E>::value)
                    for (E& item : items) item.struct_size = sizeof(E);
                const uint32_t written = count ? f(items.data(), count) : 0;
                const uint32_t n       = written < count ? written : count;
                lua_createtable(L, static_cast<int>(n), 0);
                for (uint32_t i = 0; i < n; ++i)
                {
                    if constexpr (std::is_same_v<E, wxl_str>)
                        PushText(L, items[i]);
                    else
                        PushStruct(L, items[i]);
                    lua_rawseti(L, -2, static_cast<int>(i) + 1);
                }
                return 1;
            }
        };

        /// A resource handed out, or nil, "not_found".
        template <class R>
        struct Reply<R, std::enable_if_t<IsResource<R>::value>>
        {
            template <class F>
            static int Call(lua_State* L, Reader&, F&& f)
            {
                const typename R::type value = f();
                if (value == R::kNone) return PushResultFailure(L, WXL_E_NOT_FOUND);
                PushResource<R>(L, value);
                return 1;
            }
        };

        // --- a row, called from Lua --------------------------------------------------------

        template <auto Member, class Sig>
        struct Call;
        template <auto Member, class A, class... P>
        struct Call<Member, A(P...)>
        {
            using Api = typename wxl::api::MemberOf<decltype(Member)>::api;

            template <class Held, size_t... I>
            static auto Gather(const Held& held, std::index_sequence<I...>)
            {
                return std::tuple_cat(Arg<P>::C(std::get<I>(held))...);
            }

            static int Invoke(lua_State* L)
            {
                const Api* api = wxl::api::Table<Api>();
                if (!wxl::detail::Carries(api, Member)) return PushResultFailure(L, WXL_E_UNSUPPORTED);
                Reader reader{L, 1, static_cast<const LuaRow*>(lua_touserdata(L, lua_upvalueindex(1)))};
                // Braced, so that the arguments are read left to right.
                const std::tuple<typename Arg<P>::Held...> held{Arg<P>::Read(reader)...};
                const auto c = Gather(held, std::index_sequence_for<P...>{});
                return std::apply(
                    [&](auto... args) {
                        return Reply<A>::Call(L, reader,
                                              [&](auto... tail) { return (api->*Member)(args..., tail...); });
                    },
                    c);
            }
        };

        /// A close row, called on its resource: given back once; a second call answers as the
        /// runtime does for something no longer live.
        template <class R>
        int CloseCall(lua_State* L)
        {
            ResourceBox* box = CheckResource<R>(L, 1);
            using answer     = typename Closer<R>::answer;
            if (!box->open)
            {
                if constexpr (std::is_same_v<answer, kind::none>) return 0;
                else return PushResultFailure(L, WXL_E_NOT_FOUND);
            }
            box->open               = false;
            const wxl_result result = GiveBack<R>(box->value);
            if constexpr (std::is_same_v<answer, kind::none>)
            {
                (void)result;
                return 0;
            }
            else
            {
                if (result != WXL_OK) return PushResultFailure(L, result);
                lua_pushboolean(L, 1);
                return 1;
            }
        }

        template <class R>
        int Collect(lua_State* L)
        {
            ResourceBox* box = static_cast<ResourceBox*>(lua_touserdata(L, 1));
            if (box && box->open && Closer<R>::kWhen == When::collected)
            {
                box->open = false;
                box->give_back(box->value);
            }
            return 0;
        }

        template <class R>
        int Describe(lua_State* L)
        {
            const ResourceBox* box = CheckResource<R>(L, 1);
            char               text[64];
            std::snprintf(text, sizeof text, "%s%s(0x%llX)", box->open ? "" : "given back ", R::kName,
                          static_cast<unsigned long long>(box->value));
            lua_pushstring(L, text);
            return 1;
        }

        /// The resource a row's first parameter is, whose method the row is; null for none.
        template <class Sig>
        struct MethodOf
        {
            static constexpr const char* kName = nullptr;
        };
        template <class A, class P0, class... P>
        struct MethodOf<A(P0, P...)>
        {
            static constexpr const char* Name()
            {
                if constexpr (IsResource<P0>::value) return P0::kName;
                else return nullptr;
            }
            static constexpr const char* kName = Name();
        };

        // --- the rows a script reaches -----------------------------------------------------

        struct LuaRow
        {
            const char*   service;
            const char*   name;
            lua_CFunction call;      ///< null for a shim row: Shims.hpp serves it
            const char*   method_of; ///< the resource it is a method of, or null for a module function
            const char*   params;    ///< as the row writes them, "(str directory, i32 priority)"
            wxl::api::Version since; ///< the version of the contract the member came with
        };

        /// The name of a row's parameter by its place, from its params text: "priority".
        std::string ParameterName(const char* params, int place)
        {
            std::string inner(params ? params : "");
            if (inner.size() >= 2) inner = inner.substr(1, inner.size() - 2);
            int    at    = 0;
            size_t start = 0;
            while (start <= inner.size())
            {
                size_t end = inner.find(',', start);
                if (end == std::string::npos) end = inner.size();
                if (at == place)
                {
                    std::string piece = inner.substr(start, end - start);
                    while (!piece.empty() && piece.back() == ' ') piece.pop_back();
                    const size_t space = piece.rfind(' ');
                    return space == std::string::npos ? piece : piece.substr(space + 1);
                }
                ++at;
                start = end + 1;
            }
            return "?";
        }

        void MissingArgument(const Reader& r, int declared, const char* what)
        {
            const std::string function =
                r.row ? (r.row->method_of ? std::string(r.row->method_of) + ":" + r.row->name
                                          : std::string("wxl.") + r.row->service + "." + r.row->name)
                      : std::string("?");
            const std::string parameter = declared < 0 ? std::string("count")
                                                       : ParameterName(r.row ? r.row->params : nullptr, declared);
            luaL_error(r.L, "%s: '%s' (argument %d) is missing; it takes %s", function.c_str(), parameter.c_str(),
                       r.next, what);
            std::abort(); // not reached: luaL_error does not return
        }

        struct LuaResource
        {
            const char*   name;
            lua_CFunction collect;
            lua_CFunction describe;
            bool          address; ///< an address of the client's, which an address parameter takes
        };

        namespace rows
        {
            using namespace ::wxl::api::kind;

#define WXL_LUA_SINCE(svc, member) ::wxl::api::SinceOf(#svc, uint32_t(offsetof(wxl_##svc##_api, member)))
#define WXL_LUA_generated(svc, member, Name, answer, params)                                 \
    LuaRow{#svc, #Name, &Call<&wxl_##svc##_api::member, answer params>::Invoke,             \
           MethodOf<answer params>::kName, #params, WXL_LUA_SINCE(svc, member)},
#define WXL_LUA_shim(svc, member, Name, answer, params) \
    LuaRow{#svc, #Name, nullptr, nullptr, #params, WXL_LUA_SINCE(svc, member)},
#define WXL_LUA_native(svc, member, Name, answer, params)
#define WXL_API_SERVICE(svc, name, version, since, access, doc)
#define WXL_API(svc, member, Name, answer, params, lua, cxx, doc) \
    WXL_LUA_##lua(svc, member, Name, answer, params)
#define WXL_API_RESOURCE(svc, Name, ctype, none_value)
#define WXL_API_SINCE(svc, member, since)
#define WXL_API_FIELD(type, member, field_kind)
#define WXL_API_CLOSE(svc, member, Name, answer, Resource, when, doc) \
    LuaRow{#svc, #Name, &CloseCall<kind::Resource>, kind::Resource::kName, "(" #Resource " self)", \
           WXL_LUA_SINCE(svc, member)},
            const LuaRow kRows[] = {
#include "wxl/services/tables.inc"
            };
#undef WXL_API_CLOSE
#undef WXL_API_FIELD
#undef WXL_API_SINCE
#undef WXL_API_RESOURCE
#undef WXL_API
#undef WXL_API_SERVICE
#undef WXL_LUA_native
#undef WXL_LUA_shim
#undef WXL_LUA_generated
#undef WXL_LUA_SINCE

#define WXL_API_SERVICE(svc, name, version, since, access, doc)
#define WXL_API(svc, member, Name, answer, params, lua, cxx, doc)
#define WXL_API_RESOURCE(svc, Name, ctype, none_value) \
    LuaResource{#Name, &Collect<kind::Name>, &Describe<kind::Name>, std::is_pointer_v<kind::Name::type>},
#define WXL_API_SINCE(svc, member, since)
#define WXL_API_FIELD(type, member, field_kind)
#define WXL_API_CLOSE(svc, member, Name, answer, Resource, when, doc)
            const LuaResource kResources[] = {
#include "wxl/services/tables.inc"
            };
#undef WXL_API_CLOSE
#undef WXL_API_FIELD
#undef WXL_API_SINCE
#undef WXL_API_RESOURCE
#undef WXL_API
#undef WXL_API_SERVICE
        } // namespace rows

        /// Whether a mod's manifest names a service, in "requires" or in "optional".
        bool Declares(const host::Manifest& manifest, const char* service)
        {
            for (const host::ServiceRef& ref : manifest.required)
                if (ref.service == service) return true;
            for (const host::ServiceRef& ref : manifest.optional)
                if (ref.service == service) return true;
            return false;
        }

        /// Any use of a module the mod may not use: upvalue 1 is why.
        int Refused(lua_State* L)
        {
            return luaL_error(L, "%s", lua_tostring(L, lua_upvalueindex(1)));
        }

        /// A module whose every use raises @p why.
        void PushRefusedModule(lua_State* L, const std::string& why)
        {
            lua_newtable(L);
            lua_createtable(L, 0, 2);
            lua_pushlstring(L, why.data(), why.size());
            lua_pushcclosure(L, &Refused, 1);
            lua_pushvalue(L, -1);
            lua_setfield(L, -3, "__index");
            lua_setfield(L, -2, "__newindex");
            lua_setmetatable(L, -2);
        }

        void PushUndeclared(lua_State* L, const ScriptMod& mod, const wxl::api::ServiceRow& service)
        {
            PushRefusedModule(
                L, std::string("wxl.") + service.id + " reaches the client's memory, so a mod uses it only when " +
                       (mod.HasManifest() ? "its mod.json declares it: add { \"service\": \"" +
                                                std::string(service.name) + "\", \"version\": " +
                                                std::to_string(service.version) + " } to \"requires\" (or \"optional\")"
                                          : std::string("it has a mod.json that declares it in \"requires\"")));
        }

        /// Empty when @p mod may use what came with @p since, otherwise why not, led by @p what.
        std::string TooNew(const ScriptMod& mod, const std::string& what, const wxl::api::Version& since)
        {
            const std::string newer = mod.TooNew(host::AbiVersion{since.major, since.minor, since.feature});
            return newer.empty() ? newer : what + " " + newer;
        }

        /// The __index of a table some of whose names the mod may not use: upvalue 1 maps each
        /// such name to why; any other name is nil.
        int RefusedName(lua_State* L)
        {
            lua_pushvalue(L, 2);
            lua_rawget(L, lua_upvalueindex(1));
            if (lua_type(L, -1) == LUA_TSTRING) return luaL_error(L, "%s", lua_tostring(L, -1));
            lua_pushnil(L);
            return 1;
        }

        /// Gives the table at the top of the stack a metatable refusing the names in @p refused
        /// (name, why); nothing when there are none.
        void RefuseNames(lua_State* L, const std::vector<std::pair<std::string, std::string>>& refused)
        {
            if (refused.empty()) return;
            lua_createtable(L, 0, 1);
            lua_createtable(L, 0, static_cast<int>(refused.size()));
            for (const auto& [name, why] : refused)
            {
                lua_pushlstring(L, why.data(), why.size());
                lua_setfield(L, -2, name.c_str());
            }
            lua_pushcclosure(L, &RefusedName, 1);
            lua_setfield(L, -2, "__index");
            lua_setmetatable(L, -2);
        }

        /// Registers each resource's metatable in a state: its methods (those the mod's abi
        /// has), its collector, its text.
        void OpenResources(lua_State* L, const ScriptMod& mod)
        {
            for (const LuaResource& resource : rows::kResources)
            {
                luaL_newmetatable(L, MetaName(resource.name).c_str());
                lua_newtable(L); // the methods
                std::vector<std::pair<std::string, std::string>> refused;
                for (const LuaRow& row : rows::kRows)
                {
                    if (!row.method_of || std::strcmp(row.method_of, resource.name) != 0) continue;
                    if (std::string why = TooNew(mod, std::string(resource.name) + ":" + row.name, row.since);
                        !why.empty())
                    {
                        refused.emplace_back(row.name, std::move(why));
                        continue;
                    }
                    lua_pushlightuserdata(L, const_cast<LuaRow*>(&row));
                    lua_pushcclosure(L, row.call, 1);
                    lua_setfield(L, -2, row.name);
                }
                RefuseNames(L, refused);
                lua_setfield(L, -2, "__index");
                lua_pushcfunction(L, resource.collect);
                lua_setfield(L, -2, "__gc");
                lua_pushcfunction(L, resource.describe);
                lua_setfield(L, -2, "__tostring");
                lua_pushboolean(L, resource.address ? 1 : 0);
                lua_setfield(L, -2, "__wxl_address");
                lua_pushboolean(L, 0);
                lua_setfield(L, -2, "__metatable");
                lua_pop(L, 1);
            }
            lua_pushlightuserdata(L, &kKeptKey);
            lua_newtable(L);
            lua_rawset(L, LUA_REGISTRYINDEX);
        }
    } // namespace

    void PushServiceModules(lua_State* L, ScriptMod& mod)
    {
        OpenResources(L, mod);

        uint32_t                          count    = 0;
        const wxl::api::ServiceRow* const services = wxl::api::Services(&count);
        for (uint32_t s = 0; s < count; ++s)
        {
            const wxl::api::ServiceRow& service = services[s];
            bool                        any     = false;
            for (const LuaRow& row : rows::kRows)
                if (std::strcmp(row.service, service.id) == 0 && !row.method_of) any = true;
            if (!any) continue;

            const std::string module = std::string("wxl.") + service.id;
            if (const std::string why = TooNew(mod, module, service.since); !why.empty())
            {
                PushRefusedModule(L, why);
                lua_setfield(L, -2, service.id);
                continue;
            }
            if (service.raw && !Declares(mod.Manifest(), service.name))
            {
                PushUndeclared(L, mod, service);
                lua_setfield(L, -2, service.id);
                continue;
            }

            lua_newtable(L);
            std::vector<std::pair<std::string, std::string>> refused;
            for (const LuaRow& row : rows::kRows)
            {
                if (std::strcmp(row.service, service.id) != 0 || row.method_of) continue;
                if (std::string why = TooNew(mod, module + "." + row.name, row.since); !why.empty())
                {
                    refused.emplace_back(row.name, std::move(why));
                    continue;
                }
                if (row.call)
                {
                    lua_pushlightuserdata(L, const_cast<LuaRow*>(&row));
                    lua_pushcclosure(L, row.call, 1);
                }
                else
                {
                    const lua_CFunction shim = shims::Find(row.service, row.name);
                    if (!shim) continue;
                    lua_pushlightuserdata(L, &mod);
                    lua_pushcclosure(L, shim, 1);
                }
                lua_setfield(L, -2, row.name);
            }
            RefuseNames(L, refused);
            lua_setfield(L, -2, service.id);
        }
    }

    void CloseServiceResources(lua_State* L)
    {
        PushKept(L);
        if (!lua_istable(L, -1))
        {
            lua_pop(L, 1);
            return;
        }
        for (int i = static_cast<int>(lua_objlen(L, -1)); i >= 1; --i)
        {
            lua_rawgeti(L, -1, i);
            ResourceBox* box = static_cast<ResourceBox*>(lua_touserdata(L, -1));
            if (box && box->open)
            {
                box->open = false;
                box->give_back(box->value);
            }
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }
    void PushRenderResource(lua_State* L, RenderResource which, wxl_handle value)
    {
        switch (which)
        {
        case RenderResource::image: PushResource<kind::Image>(L, value); break;
        case RenderResource::buffer: PushResource<kind::Buffer>(L, value); break;
        case RenderResource::pipeline: PushResource<kind::Pipeline>(L, value); break;
        }
    }

    wxl_handle ToRenderResource(lua_State* L, int index, RenderResource which)
    {
        const char* name = which == RenderResource::image ? kind::Image::kName
                           : which == RenderResource::buffer ? kind::Buffer::kName
                                                            : kind::Pipeline::kName;
        const ResourceBox* box = static_cast<const ResourceBox*>(luaL_testudata(L, index, MetaName(name).c_str()));
        return box && box->open ? static_cast<wxl_handle>(box->value) : WXL_HANDLE_NONE;
    }

    void PushDrawShape(lua_State* L, wxl_handle value) { PushResource<kind::Shape>(L, value); }

    wxl_handle ToDrawShape(lua_State* L, int index)
    {
        const ResourceBox* box = static_cast<const ResourceBox*>(luaL_testudata(L, index, MetaName(kind::Shape::kName).c_str()));
        return box && box->open ? static_cast<wxl_handle>(box->value) : WXL_HANDLE_NONE;
    }

    void CloseDrawShapes(lua_State* L)
    {
        PushKept(L);
        if (!lua_istable(L, -1))
        {
            lua_pop(L, 1);
            return;
        }
        const std::string meta = MetaName(kind::Shape::kName);
        for (int i = static_cast<int>(lua_objlen(L, -1)); i >= 1; --i)
        {
            lua_rawgeti(L, -1, i);
            ResourceBox* box = static_cast<ResourceBox*>(luaL_testudata(L, -1, meta.c_str()));
            if (box && box->open)
            {
                box->open = false;
                box->give_back(box->value);
            }
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }
} // namespace wxl::lua
