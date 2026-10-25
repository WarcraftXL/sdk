// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The services as data: every member of every service table, with what it takes and what it
// answers, read from the table beside each header (wxl/services/<service>.def). A runtime that is
// not C++ reaches the services through these rows -- wxl.lua builds its modules from them --, the
// C++ facades take their plain methods from them, and sdk/tests/reflect.cpp checks each row
// against its struct, so that a header and its table cannot drift apart.
//
// --- The tables ----------------------------------------------------------------------------
//
// One .def per service table, one row per member, in member order:
//
//   WXL_API_SERVICE(svc, name, version, since, access, doc)
//       opens the table of wxl_<svc>_api: the macros of its name and version, the version of
//       the contract it came with as `(major, minor, feature)`, and `open` or `raw`. A script
//       mod whose mod.json asks for an older abi cannot use it; a raw service reaches a
//       script mod only when its mod.json declares it. The services there before the contract
//       dated what it brought say (0, 1, 5), the oldest abi a mod can ask for.
//   WXL_API_SINCE(svc, member, (major, minor, feature))
//       right above a member appended to a table after the table came: that member and the
//       ones below it came with that version, until the next such row. A script mod whose
//       mod.json asks for an older abi cannot use them.
//   WXL_API(svc, member, Name, answer, (params), lua, cxx, doc)
//       one member. `answer (params)` reads as a C++ function type over the kinds below, its
//       parameters named for the reader: `out<u64> (str path)`. `lua` is `generated` (wxl.lua
//       marshals it from the kinds), `shim` (a hand-written shim in sdk/lua/src/Shims.cpp
//       serves it: it takes a callback, or what it does is kept for each script mod and undone
//       when the mod goes) or `native` (no script reaches it). `cxx` is
//       `generated` (the facade's method is this row) or `by_hand` (the facade has an idiomatic
//       one: RAII, a container, a template). `doc` says what it does in a line, for a script's
//       editor definitions; empty for a native row.
//   WXL_API_RESOURCE(svc, Name, ctype, none)
//       something the runtime owns and hands out: a ctype, `none` meaning there is none. A row
//       answering Name hands one out; a row whose first parameter is Name is a method of it.
//   WXL_API_CLOSE(svc, member, Name, answer, Resource, when, doc)
//       the member that gives a resource back, a method of it. `collected`: a script mod's is
//       given back when the mod lets go of it, and when it unloads; `unloaded`: only when the mod
//       unloads, newest first.
//   WXL_API_FIELD(struct, member, kind)
//       a member of the struct a list<struct> answer is made of.
//
// A consumer defines every shape, then includes the tables it reads. A facade takes its own
// table's generated rows through wxl/services/methods.inc.
//
// --- The kinds -----------------------------------------------------------------------------
//
// Parameters, and the C parameters each one is:
//   str wxl_str  cstr const char*  i32  u32  u64  uptr uintptr_t  f64 double  boolean wxl_bool
//   ptr void*  cptr const void*: an address of the client's (a model); a script passes what an
//       event or a call handed it, or one of its own resources
//   ptr_out void**  call wxl_script_call*  script_fn wxl_script_fn
//   point_fn wxl_point_fn  point_call wxl_point_call*
//   values (const wxl_value*, uint32_t count)  bytes const void*
//   bytes_size uint32_t: the length of the bytes before it, which must all be as long
//   object wxl_handle: a world object; a script passes the object, C++ a wxl::Object
//   vec3 wxl_vec3: a point; a script passes a table with x, y and z
//   provider const wxl_asset_provider*  transform const wxl_asset_transform*: what a native
//       mod hands wxl.storage; no script reaches a row taking one
//   a resource: its ctype
//   opt<K> the C parameters of K, a parameter a script may leave out: it is then K's zero (0
//       for a number), which the row's doc says. Every other parameter a script must give, and
//       one left out is an error naming the function and the parameter.
//
// Answers: the C return, and the C parameters the answer appends after the row's own:
//   none void  result wxl_result  boolean wxl_bool  u32  uptr  str wxl_str
//   address uintptr_t, 0 meaning none
//   ptr void*, null meaning none: an address of the client's, which a script holds as it is
//   enumeration<E> uint32_t holding a value of E, an enumeration of enums.def
//   out<K> wxl_result, K*            found<K> wxl_bool, K*: K a value kind (u64 u32 i32 f64 str
//       boolean vec3) or a struct kind (m2_item_display: wxl_m2_item_display, written no longer
//       than its struct_size)
//   whole_bytes wxl_result, (void* buffer, uint64_t capacity, uint64_t* out_read): a buffer
//       too small is WXL_E_INVALID with out_read set to what it needs
//   read_bytes wxl_result, (void* buffer, uint64_t size, uint64_t* out_read): the caller says
//       how many
//   list<E> uint32_t count, (E* out, uint32_t capacity): out may be null to count only
//   a resource: its ctype

#ifndef WXL_SERVICE_ROWS_HPP
#define WXL_SERVICE_ROWS_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

#include "wxl/base.hpp"
#include "wxl/events.h"
#include "wxl/expected.hpp"
#include "wxl/services/data.h"
#include "wxl/services/hook.h"
#include "wxl/services/patch.h"
#include "wxl/services/point.h"
#include "wxl/services/footprint.h"
#include "wxl/services/profile.h"
#include "wxl/services/action.h"
#include "wxl/services/gx.h"
#include "wxl/services/engine.h"
#include "wxl/services/m2.h"
#include "wxl/services/m2file.h"
#include "wxl/objects/object.hpp"
#include "wxl/services/script.h"
#include "wxl/services/storage.h"

namespace wxl::api
{
    /// A list of types.
    template <class... T>
    struct List
    {
    };

    namespace kind
    {
        // Parameters.
        struct str {};
        struct cstr {};
        struct i32 {};
        struct u32 {};
        struct u64 {};
        struct uptr {};
        struct f64 {};
        struct boolean {};
        struct ptr {};
        struct cptr {};
        struct ptr_out {};
        struct call {};
        struct script_fn {};
        struct point_fn {};
        struct point_call {};
        struct values {};
        struct bytes {};
        struct bytes_size {};
        struct object {};
        struct vec3 {};
        struct provider {};
        struct transform {};
        struct m2_item_display {};
        template <class K>
        struct opt {};

        // Answers.
        struct none {};
        struct result {};
        struct address {};
        struct whole_bytes {};
        struct read_bytes {};
        template <class E>
        struct enumeration {};
        template <class K>
        struct out {};
        template <class K>
        struct found {};
        template <class E>
        struct list {};

        // The resources of the tables: a Name with its C type and the value meaning none.
#define WXL_API_SERVICE(svc, name, version, since, access, doc)
#define WXL_API(svc, member, Name, answer, params, lua, cxx, doc)
#define WXL_API_CLOSE(svc, member, Name, answer, Resource, when, doc)
#define WXL_API_SINCE(svc, member, since)
#define WXL_API_FIELD(type, member, kind)
#define WXL_API_RESOURCE(svc, Name, ctype, none_value)              \
    struct Name                                                     \
    {                                                               \
        using type                           = ctype;               \
        static constexpr ctype       kNone   = none_value;          \
        static constexpr const char* kName   = #Name;               \
        static constexpr const char* kService = #svc;               \
    };
#include "wxl/services/tables.inc"
#undef WXL_API_RESOURCE
#undef WXL_API_FIELD
#undef WXL_API_SINCE
#undef WXL_API_CLOSE
#undef WXL_API
#undef WXL_API_SERVICE
    } // namespace kind

    /// Whether a kind is a resource of a table.
    template <class K, class = void>
    struct IsResource : std::false_type
    {
    };

    template <class K>
    struct IsResource<K, std::void_t<typename K::type, decltype(K::kNone)>> : std::true_type
    {
    };

    // --- what each kind is in C ------------------------------------------------------------

    /// The C value one kind holds, for an out<K> or a found<K>.
    template <class K>
    struct Value;
    template <> struct Value<kind::u64> { using type = uint64_t; };
    template <> struct Value<kind::u32> { using type = uint32_t; };
    template <> struct Value<kind::i32> { using type = int32_t; };
    template <> struct Value<kind::f64> { using type = double; };
    template <> struct Value<kind::str> { using type = wxl_str; };
    template <> struct Value<kind::boolean> { using type = wxl_bool; };
    template <> struct Value<kind::vec3> { using type = wxl_vec3; };
    template <> struct Value<kind::m2_item_display> { using type = wxl_m2_item_display; };

    /// The C parameters a parameter kind stands for.
    template <class K, class = void>
    struct Param;
    template <> struct Param<kind::str> { using C = List<wxl_str>; };
    template <> struct Param<kind::cstr> { using C = List<const char*>; };
    template <> struct Param<kind::i32> { using C = List<int32_t>; };
    template <> struct Param<kind::u32> { using C = List<uint32_t>; };
    template <> struct Param<kind::u64> { using C = List<uint64_t>; };
    template <> struct Param<kind::uptr> { using C = List<uintptr_t>; };
    template <> struct Param<kind::f64> { using C = List<double>; };
    template <> struct Param<kind::boolean> { using C = List<wxl_bool>; };
    template <> struct Param<kind::ptr> { using C = List<void*>; };
    template <> struct Param<kind::cptr> { using C = List<const void*>; };
    template <> struct Param<kind::ptr_out> { using C = List<void**>; };
    template <> struct Param<kind::call> { using C = List<wxl_script_call*>; };
    template <> struct Param<kind::script_fn> { using C = List<wxl_script_fn>; };
    template <> struct Param<kind::point_fn> { using C = List<wxl_point_fn>; };
    template <> struct Param<kind::point_call> { using C = List<wxl_point_call*>; };
    template <> struct Param<kind::values> { using C = List<const wxl_value*, uint32_t>; };
    template <> struct Param<kind::bytes> { using C = List<const void*>; };
    template <> struct Param<kind::bytes_size> { using C = List<uint32_t>; };
    template <> struct Param<kind::object> { using C = List<wxl_handle>; };
    template <> struct Param<kind::vec3> { using C = List<wxl_vec3>; };
    template <> struct Param<kind::provider> { using C = List<const wxl_asset_provider*>; };
    template <> struct Param<kind::transform> { using C = List<const wxl_asset_transform*>; };
    template <class R>
    struct Param<R, std::enable_if_t<IsResource<R>::value>>
    {
        using C = List<typename R::type>;
    };
    template <class K>
    struct Param<kind::opt<K>>
    {
        using C = typename Param<K>::C;
    };

    /// The C return of an answer kind, and the C parameters it appends.
    template <class K, class = void>
    struct Answer;
    template <> struct Answer<kind::none> { using Return = void; using Tail = List<>; };
    template <> struct Answer<kind::result> { using Return = wxl_result; using Tail = List<>; };
    template <> struct Answer<kind::boolean> { using Return = wxl_bool; using Tail = List<>; };
    template <> struct Answer<kind::u32> { using Return = uint32_t; using Tail = List<>; };
    template <> struct Answer<kind::uptr> { using Return = uintptr_t; using Tail = List<>; };
    template <> struct Answer<kind::str> { using Return = wxl_str; using Tail = List<>; };
    template <> struct Answer<kind::address> { using Return = uintptr_t; using Tail = List<>; };
    template <> struct Answer<kind::ptr> { using Return = void*; using Tail = List<>; };
    template <class E>
    struct Answer<kind::enumeration<E>>
    {
        using Return = uint32_t;
        using Tail   = List<>;
    };
    template <class K>
    struct Answer<kind::out<K>>
    {
        using Return = wxl_result;
        using Tail   = List<typename Value<K>::type*>;
    };
    template <class K>
    struct Answer<kind::found<K>>
    {
        using Return = wxl_bool;
        using Tail   = List<typename Value<K>::type*>;
    };
    template <> struct Answer<kind::whole_bytes> { using Return = wxl_result; using Tail = List<void*, uint64_t, uint64_t*>; };
    template <> struct Answer<kind::read_bytes> { using Return = wxl_result; using Tail = List<void*, uint64_t, uint64_t*>; };
    template <class E>
    struct Answer<kind::list<E>>
    {
        using Return = uint32_t;
        using Tail   = List<E*, uint32_t>;
    };
    template <class R>
    struct Answer<R, std::enable_if_t<IsResource<R>::value>>
    {
        using Return = typename R::type;
        using Tail   = List<>;
    };

    namespace detail
    {
        template <class... L>
        struct Concat;
        template <>
        struct Concat<>
        {
            using type = List<>;
        };
        template <class... A>
        struct Concat<List<A...>>
        {
            using type = List<A...>;
        };
        template <class... A, class... B, class... Rest>
        struct Concat<List<A...>, List<B...>, Rest...>
        {
            using type = typename Concat<List<A..., B...>, Rest...>::type;
        };

        template <class R, class L>
        struct Pointer;
        template <class R, class... A>
        struct Pointer<R, List<A...>>
        {
            using type = R(WXL_CALL*)(A...);
        };
    } // namespace detail

    /**
     * @brief The C member a row describes: `Signature<out<u64>(str)>` is
     * `wxl_result(WXL_CALL*)(wxl_str, uint64_t*)`.
     */
    template <class Sig>
    struct Signature;
    template <class A, class... P>
    struct Signature<A(P...)>
    {
        using type = typename detail::Pointer<
            typename Answer<A>::Return,
            typename detail::Concat<typename Param<P>::C..., typename Answer<A>::Tail>::type>::type;
    };

    template <class Sig>
    using SignatureOf = typename Signature<Sig>::type;

    // --- the services --------------------------------------------------------------------

    /// What WXL_API_SERVICE says of a table.
    template <class Api>
    struct ServiceOf;

#define WXL_API_RAW_open false
#define WXL_API_RAW_raw  true
#define WXL_API_SERVICE(svc, name, version, since, access, doc)                     \
    template <>                                                         \
    struct ServiceOf<wxl_##svc##_api>                                   \
    {                                                                   \
        static constexpr const char* kId      = #svc;                   \
        static constexpr const char* kName    = name;                   \
        static constexpr uint32_t    kVersion = version;                \
        static constexpr bool        kRaw     = WXL_API_RAW_##access;    \
    };
#define WXL_API(svc, member, Name, answer, params, lua, cxx, doc)
#define WXL_API_RESOURCE(svc, Name, ctype, none_value)
#define WXL_API_CLOSE(svc, member, Name, answer, Resource, when, doc)
#define WXL_API_SINCE(svc, member, since)
#define WXL_API_FIELD(type, member, kind)
#include "wxl/services/tables.inc"
#undef WXL_API_FIELD
#undef WXL_API_SINCE
#undef WXL_API_CLOSE
#undef WXL_API_RESOURCE
#undef WXL_API
#undef WXL_API_SERVICE

    /// A service's table, resolved on first use and retried while nobody publishes it.
    template <class Api>
    const Api* Table()
    {
        static Service<Api> service(ServiceOf<Api>::kName, ServiceOf<Api>::kVersion);
        return service.Get();
    }

    /// The struct a member pointer belongs to, and the member's type.
    template <class M>
    struct MemberOf;
    template <class C, class M>
    struct MemberOf<M C::*>
    {
        using api  = C;
        using type = M;
    };

    // --- the rows as data ------------------------------------------------------------------

    /// How a script reaches a member: the `lua` column.
    enum class Lua : uint8_t
    {
        generated,
        shim,
        native,
    };

    /// How the C++ facade reaches it: the `cxx` column.
    enum class Cxx : uint8_t
    {
        generated,
        by_hand,
    };

    /// How a resource is given back: the `when` column of a WXL_API_CLOSE row.
    enum class When : uint8_t
    {
        never,     ///< the row does not give one back
        collected, ///< when the mod lets go of it, and when it unloads
        unloaded,  ///< when the mod unloads, newest first
    };

    /// A version of the contract, major.minor.feature, as abi.h numbers it.
    struct Version
    {
        uint32_t major;
        uint32_t minor;
        uint32_t feature;
    };

    /// Whether @p version is @p floor or later.
    constexpr bool AtLeast(const Version& version, const Version& floor)
    {
        if (version.major != floor.major) return version.major > floor.major;
        if (version.minor != floor.minor) return version.minor > floor.minor;
        return version.feature >= floor.feature;
    }

    /// One WXL_API_SERVICE row.
    struct ServiceRow
    {
        const char* id;       ///< "storage": the module is wxl.storage
        const char* name;     ///< what the runtime publishes it as, "wxl.storage"
        uint32_t    version;
        Version     since;    ///< the version of the contract the table came with
        bool        raw;      ///< reached by a script mod only when its mod.json declares it
        uint32_t    api_size; ///< sizeof the table's struct
        const char* doc;      ///< what it is, in a line
    };

    /// One WXL_API or WXL_API_CLOSE row.
    struct Row
    {
        const char* service; ///< the table's id, "storage"
        const char* member;  ///< the C member, "add_search_path"
        const char* name;    ///< what a script and the facade call it, "AddSearchPath"
        const char* answer;  ///< the answer kind as written, "out<u64>"
        const char* params;  ///< the parameters as written, "(str directory, i32 priority)"
        Lua         lua;
        Cxx         cxx;
        When        closes;  ///< what a WXL_API_CLOSE row closes on; never for a WXL_API row
        uint32_t    offset;  ///< offsetof the member
        const char* doc;     ///< what it does, in a line; empty for a native row
    };

    /// One WXL_API_SINCE row.
    struct SinceRow
    {
        const char* service; ///< the table's id, "storage"
        const char* member;  ///< the first member it dates, "remove_search_path"
        uint32_t    offset;  ///< offsetof that member: it and every member after it
        Version     since;
    };

    /// One WXL_API_RESOURCE row.
    struct ResourceRow
    {
        const char* service;
        const char* name; ///< "File": a script sees a wxl.File
    };

    /// One WXL_API_FIELD row.
    struct FieldRow
    {
        const void* of;     ///< the struct, as &TypeTag<struct>: what a list<struct> answer looks up
        const char* type;   ///< the struct's name
        const char* member;
        const char* kind;   ///< as written: "str", "u32"
        uint32_t    offset;
    };

    /// One address per type, which is how a field row names its struct. A variable, and a
    /// writable one: a linker folds identical functions and constants into one address, never
    /// two writable variables.
    template <class T>
    inline char TypeTag = 0;

    namespace detail
    {
#define WXL_API_VERSION_(major, minor, feature) Version{major, minor, feature}
#define WXL_API_SERVICE(svc, name, version, since, access, doc)                                 \
    ServiceRow{#svc, name, version, WXL_API_VERSION_ since, WXL_API_RAW_##access,                \
               uint32_t(sizeof(wxl_##svc##_api)), doc},
#define WXL_API(svc, member, Name, answer, params, lua, cxx, doc)
#define WXL_API_RESOURCE(svc, Name, ctype, none_value)
#define WXL_API_CLOSE(svc, member, Name, answer, Resource, when, doc)
#define WXL_API_SINCE(svc, member, since)
#define WXL_API_FIELD(type, member, kind)
        inline constexpr ServiceRow kServices[] = {
#include "wxl/services/tables.inc"
        };
#undef WXL_API_FIELD
#undef WXL_API_SINCE
#undef WXL_API_CLOSE
#undef WXL_API_RESOURCE
#undef WXL_API
#undef WXL_API_SERVICE

#define WXL_API_SERVICE(svc, name, version, since, access, doc)
#define WXL_API(svc, member, Name, answer, params, lua, cxx, doc)                              \
    Row{#svc, #member, #Name, #answer, #params, Lua::lua, Cxx::cxx, When::never,          \
        uint32_t(offsetof(wxl_##svc##_api, member)), doc},
#define WXL_API_RESOURCE(svc, Name, ctype, none_value)
#define WXL_API_CLOSE(svc, member, Name, answer, Resource, when, doc)                           \
    Row{#svc, #member, #Name, #answer, "(" #Resource " self)", Lua::generated,    \
        Cxx::by_hand, When::when, uint32_t(offsetof(wxl_##svc##_api, member)), doc},
#define WXL_API_SINCE(svc, member, since)
#define WXL_API_FIELD(type, member, kind)
        inline constexpr Row kRows[] = {
#include "wxl/services/tables.inc"
        };
#undef WXL_API_FIELD
#undef WXL_API_SINCE
#undef WXL_API_CLOSE
#undef WXL_API_RESOURCE
#undef WXL_API
#undef WXL_API_SERVICE

#define WXL_API_SERVICE(svc, name, version, since, access, doc)
#define WXL_API(svc, member, Name, answer, params, lua, cxx, doc)
#define WXL_API_RESOURCE(svc, Name, ctype, none_value) ResourceRow{#svc, #Name},
#define WXL_API_CLOSE(svc, member, Name, answer, Resource, when, doc)
#define WXL_API_SINCE(svc, member, since)
#define WXL_API_FIELD(type, member, kind)
        inline constexpr ResourceRow kResources[] = {
#include "wxl/services/tables.inc"
        };
#undef WXL_API_FIELD
#undef WXL_API_SINCE
#undef WXL_API_CLOSE
#undef WXL_API_RESOURCE
#undef WXL_API
#undef WXL_API_SERVICE

#define WXL_API_SERVICE(svc, name, version, since, access, doc)
#define WXL_API(svc, member, Name, answer, params, lua, cxx, doc)
#define WXL_API_RESOURCE(svc, Name, ctype, none_value)
#define WXL_API_CLOSE(svc, member, Name, answer, Resource, when, doc)
#define WXL_API_SINCE(svc, member, since)
#define WXL_API_FIELD(type, member, kind) \
    FieldRow{&TypeTag<type>, #type, #member, #kind, uint32_t(offsetof(type, member))},
        inline constexpr FieldRow kFields[] = {
#include "wxl/services/tables.inc"
        };
#undef WXL_API_FIELD
#undef WXL_API_SINCE
#undef WXL_API_CLOSE
#undef WXL_API_RESOURCE
#undef WXL_API
#undef WXL_API_SERVICE

#define WXL_API_SERVICE(svc, name, version, since, access, doc)
#define WXL_API(svc, member, Name, answer, params, lua, cxx, doc)
#define WXL_API_RESOURCE(svc, Name, ctype, none_value)
#define WXL_API_CLOSE(svc, member, Name, answer, Resource, when, doc)
#define WXL_API_FIELD(type, member, kind)
#define WXL_API_SINCE(svc, member, since) \
    SinceRow{#svc, #member, uint32_t(offsetof(wxl_##svc##_api, member)), WXL_API_VERSION_ since},
        // Led by a row no table has, so that the array is never empty.
        inline constexpr SinceRow kSinces[] = {
            SinceRow{"", "", 0, Version{0, 0, 0}},
#include "wxl/services/tables.inc"
        };
#undef WXL_API_SINCE
#undef WXL_API_FIELD
#undef WXL_API_CLOSE
#undef WXL_API_RESOURCE
#undef WXL_API
#undef WXL_API_SERVICE
#undef WXL_API_VERSION_

        constexpr bool Same(const char* a, const char* b)
        {
            while (*a && *a == *b) ++a, ++b;
            return *a == *b;
        }
    } // namespace detail

    /// Every WXL_API_SERVICE row, in table order.
    inline const ServiceRow* Services(uint32_t* count)
    {
        *count = uint32_t(sizeof detail::kServices / sizeof detail::kServices[0]);
        return detail::kServices;
    }

    /// Every WXL_API and WXL_API_CLOSE row, in table order: a service's rows follow each other.
    inline const Row* Rows(uint32_t* count)
    {
        *count = uint32_t(sizeof detail::kRows / sizeof detail::kRows[0]);
        return detail::kRows;
    }

    /// Every WXL_API_RESOURCE row.
    inline const ResourceRow* Resources(uint32_t* count)
    {
        *count = uint32_t(sizeof detail::kResources / sizeof detail::kResources[0]);
        return detail::kResources;
    }

    /// Every WXL_API_FIELD row.
    inline const FieldRow* Fields(uint32_t* count)
    {
        *count = uint32_t(sizeof detail::kFields / sizeof detail::kFields[0]);
        return detail::kFields;
    }

    /// Every WXL_API_SINCE row, in table order.
    inline const SinceRow* Sinces(uint32_t* count)
    {
        *count = uint32_t(sizeof detail::kSinces / sizeof detail::kSinces[0]) - 1;
        return detail::kSinces + 1;
    }

    /// The WXL_API_SERVICE row of a table's id ("storage"), or null.
    constexpr const ServiceRow* FindService(const char* id)
    {
        for (const ServiceRow& service : detail::kServices)
            if (detail::Same(service.id, id)) return &service;
        return nullptr;
    }

    /// The version of the contract that brought the member at @p offset of a table ("storage"):
    /// the table's, or that of the last WXL_API_SINCE row of the table at or above the member.
    constexpr Version SinceOf(const char* service, uint32_t offset)
    {
        const ServiceRow* table   = FindService(service);
        Version           version = table ? table->since : Version{0, 0, 0};
        for (const SinceRow& since : detail::kSinces)
            if (detail::Same(since.service, service) && since.offset <= offset) version = since.since;
        return version;
    }

    /// The version of the contract that brought a row's member.
    constexpr Version SinceOf(const Row& row) { return SinceOf(row.service, row.offset); }

    // --- a generated facade method -----------------------------------------------------------
    //
    // A row with `cxx` generated is a static member of its facade, an object called like a
    // method: `wxl::DataService::ServeTable("Spell.dbc", path)`. It resolves the table, checks
    // that the runtime's table reaches the member and fills it, and answers as the facades
    // always have: a wxl_result, a bool that is false, an empty string, or an Expected that says
    // WXL_E_UNSUPPORTED when it does not.

    /// The C++ parameter of a kind, and how it crosses.
    template <class K, class = void>
    struct CxxParam;
    template <>
    struct CxxParam<kind::str>
    {
        using type = std::string_view;
        static wxl_str ToC(std::string_view value) { return Str(value); }
    };
    template <>
    struct CxxParam<kind::cstr>
    {
        using type = const char*;
        static const char* ToC(const char* value) { return value; }
    };
    template <>
    struct CxxParam<kind::i32>
    {
        using type = int32_t;
        static int32_t ToC(int32_t value) { return value; }
    };
    template <>
    struct CxxParam<kind::u32>
    {
        using type = uint32_t;
        static uint32_t ToC(uint32_t value) { return value; }
    };
    template <>
    struct CxxParam<kind::u64>
    {
        using type = uint64_t;
        static uint64_t ToC(uint64_t value) { return value; }
    };
    template <>
    struct CxxParam<kind::f64>
    {
        using type = double;
        static double ToC(double value) { return value; }
    };
    template <>
    struct CxxParam<kind::boolean>
    {
        using type = bool;
        static wxl_bool ToC(bool value) { return value ? WXL_TRUE : WXL_FALSE; }
    };
    template <>
    struct CxxParam<kind::object>
    {
        using type = Object;
        static wxl_handle ToC(Object value) { return value.Handle(); }
    };
    template <>
    struct CxxParam<kind::vec3>
    {
        using type = wxl_vec3;
        static wxl_vec3 ToC(wxl_vec3 value) { return value; }
    };
    template <>
    struct CxxParam<kind::ptr>
    {
        using type = void*;
        static void* ToC(void* value) { return value; }
    };
    template <>
    struct CxxParam<kind::cptr>
    {
        using type = const void*;
        static const void* ToC(const void* value) { return value; }
    };
    /// A resource is its C value in C++: what the row that handed it out answered.
    template <class R>
    struct CxxParam<R, std::enable_if_t<IsResource<R>::value>>
    {
        using type = typename R::type;
        static type ToC(type value) { return value; }
    };
    /// A parameter a script may leave out is an ordinary one in C++.
    template <class K>
    struct CxxParam<kind::opt<K>> : CxxParam<K>
    {
    };

    /// The C++ answer of a kind: what a missing member answers, and what a call does.
    template <class K, class = void>
    struct CxxAnswer;
    template <>
    struct CxxAnswer<kind::none>
    {
        using type = void;
        static void Missing() {}
        template <class F>
        static void Call(F&& f)
        {
            f();
        }
    };
    template <>
    struct CxxAnswer<kind::result>
    {
        using type = wxl_result;
        static wxl_result Missing() { return WXL_E_UNSUPPORTED; }
        template <class F>
        static wxl_result Call(F&& f)
        {
            return f();
        }
    };
    template <>
    struct CxxAnswer<kind::boolean>
    {
        using type = bool;
        static bool Missing() { return false; }
        template <class F>
        static bool Call(F&& f)
        {
            return f() == WXL_TRUE;
        }
    };
    template <>
    struct CxxAnswer<kind::str>
    {
        using type = std::string;
        static std::string Missing() { return std::string(); }
        template <class F>
        static std::string Call(F&& f)
        {
            return std::string(View(f()));
        }
    };
    template <>
    struct CxxAnswer<kind::u32>
    {
        using type = Expected<uint32_t>;
        static type Missing() { return Unexpected{WXL_E_UNSUPPORTED}; }
        template <class F>
        static type Call(F&& f)
        {
            return f();
        }
    };
    template <>
    struct CxxAnswer<kind::uptr>
    {
        using type = Expected<uintptr_t>;
        static type Missing() { return Unexpected{WXL_E_UNSUPPORTED}; }
        template <class F>
        static type Call(F&& f)
        {
            return f();
        }
    };
    template <>
    struct CxxAnswer<kind::ptr>
    {
        using type = void*;
        static void* Missing() { return nullptr; }
        template <class F>
        static void* Call(F&& f)
        {
            return f();
        }
    };
    template <class R>
    struct CxxAnswer<R, std::enable_if_t<IsResource<R>::value>>
    {
        using type = typename R::type;
        static type Missing() { return R::kNone; }
        template <class F>
        static type Call(F&& f)
        {
            return f();
        }
    };
    template <class E>
    struct CxxAnswer<kind::enumeration<E>>
    {
        using type = Expected<E>;
        static type Missing() { return Unexpected{WXL_E_UNSUPPORTED}; }
        template <class F>
        static type Call(F&& f)
        {
            return static_cast<E>(f());
        }
    };
    template <class K>
    struct CxxAnswer<kind::out<K>>
    {
        using value = typename Value<K>::type;
        using type  = Expected<value>;
        static type Missing() { return Unexpected{WXL_E_UNSUPPORTED}; }
        template <class F>
        static type Call(F&& f)
        {
            value            out{};
            const wxl_result result = f(&out);
            if (result != WXL_OK) return Unexpected{result};
            return out;
        }
    };

    /// A string answered through an out parameter is a view: the runtime's text, which the row
    /// says how long it lives.
    template <>
    struct CxxAnswer<kind::out<kind::str>>
    {
        using type = Expected<std::string_view>;
        static type Missing() { return Unexpected{WXL_E_UNSUPPORTED}; }
        template <class F>
        static type Call(F&& f)
        {
            wxl_str          out{};
            const wxl_result result = f(&out);
            if (result != WXL_OK) return Unexpected{result};
            return View(out);
        }
    };

    /// A row's method: `Member` is the C member, `Sig` the row's `answer (params)`.
    template <auto Member, class Sig>
    struct Method;
    template <auto Member, class A, class... P>
    struct Method<Member, A(P...)>
    {
        using Api = typename MemberOf<decltype(Member)>::api;

        typename CxxAnswer<A>::type operator()(typename CxxParam<P>::type... args) const
        {
            const Api* api = Table<Api>();
            if (!::wxl::detail::Carries(api, Member)) return CxxAnswer<A>::Missing();
            return CxxAnswer<A>::Call(
                [&](auto... tail) { return (api->*Member)(CxxParam<P>::ToC(args)..., tail...); });
        }
    };

    namespace facade
    {
        using namespace ::wxl::api::kind;

        /// The methods a facade takes from its table, as a base it derives from: each header
        /// specializes it over its own table with wxl/services/methods.inc.
        template <class Api>
        struct Methods
        {
        };
    } // namespace facade
} // namespace wxl::api

/// A row whose C++ method is generated, inside a Methods<Api> specialization.
#define WXL_API_CXX_generated(svc, member, Name, answer, params) \
    static constexpr ::wxl::api::Method<&wxl_##svc##_api::member, answer params> Name{};
#define WXL_API_CXX_by_hand(svc, member, Name, answer, params)

#undef WXL_API_RAW_raw
#undef WXL_API_RAW_open

#endif // WXL_SERVICE_ROWS_HPP
