// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The field table against the structs it describes: every member listed, in order, of its kind.
// The service tables against the service structs: every member listed, in order, with the
// signature its kinds make.

#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>

#include "wxl/objects/fields.hpp"
#include "wxl/reflect.hpp"
#include "wxl/sdk.hpp"
#include "wxl/services/rows.hpp"

namespace
{
    int g_failures = 0;

    void Check(bool condition, const char* what)
    {
        if (condition) return;
        std::printf("  FAIL  %s\n", what);
        ++g_failures;
    }

    using wxl::reflect::Kind;

    /// Whether a member's C type is what its kind says.
    template <Kind K, class T>
    constexpr bool Fits()
    {
        switch (K)
        {
            case Kind::u32: return std::is_same_v<T, uint32_t>;
            case Kind::i32: return std::is_same_v<T, int32_t>;
            case Kind::u64: return std::is_same_v<T, uint64_t>;
            case Kind::i64: return std::is_same_v<T, int64_t>;
            case Kind::f32: return std::is_same_v<T, float>;
            case Kind::boolean: return std::is_same_v<T, wxl_bool>;
            case Kind::handle: return std::is_same_v<T, wxl_handle>;
            case Kind::guid: return std::is_same_v<T, wxl_guid>;
            case Kind::vec3: return std::is_same_v<T, wxl_vec3>;
            case Kind::str: return std::is_same_v<T, wxl_str>;
            case Kind::ptr: return std::is_pointer_v<T>;
            case Kind::bytes: return std::is_same_v<T, const void*>;
            case Kind::answer: return std::is_same_v<T, wxl_bool*>;
            case Kind::buffer: return std::is_same_v<T, char*>;
        }
        return false;
    }

#define WXL_ARGS_FIELD(args, member, kind)                                                       \
    static_assert(Fits<wxl::reflect::detail::kind_##kind, decltype(args::member)>(),        \
                  #args "." #member " is not of kind " #kind);
#define WXL_ARGS_BYTES(args, member, size)                                                 \
    static_assert(Fits<Kind::bytes, decltype(args::member)>(), #args "." #member);          \
    static_assert(std::is_same_v<decltype(args::size), uint32_t>, #args "." #size);
#define WXL_ARGS_BUFFER(args, member, capacity)                                            \
    static_assert(Fits<Kind::buffer, decltype(args::member)>(), #args "." #member);         \
    static_assert(std::is_same_v<decltype(args::capacity), uint32_t>, #args "." #capacity);
#define WXL_ARGS_ENUM(args, member, type)                                                  \
    static_assert(std::is_same_v<decltype(args::member), uint32_t>,                         \
                  #args "." #member " holds an enumeration's values in a uint32_t");        \
    static_assert(std::is_enum_v<type>, #args "." #member ": " #type " is an enum of enums.def");
#include "wxl/fields.def"
#undef WXL_ARGS_ENUM
#undef WXL_ARGS_BUFFER
#undef WXL_ARGS_BYTES
#undef WXL_ARGS_FIELD

    /// The size and alignment of each row's member, in the order of the field table.
    struct FieldShape
    {
        uint32_t size;
        uint32_t align;
    };

    constexpr FieldShape kShapes[] = {
#define WXL_ARGS_FIELD(args, member, kind) \
    { uint32_t(sizeof(decltype(args::member))), uint32_t(alignof(decltype(args::member))) },
#include "wxl/fields.def"
#undef WXL_ARGS_FIELD
    };

    uint32_t AlignUp(uint32_t value, uint32_t align) { return (value + align - 1) / align * align; }

    // A handler's parameters, as the C++ layer derives them from the rows and as a runtime reading
    // the field table does, one letter per parameter: a view or a value of the kind it is.
    template <class T>
    constexpr char Letter()
    {
        if constexpr (std::is_same_v<T, wxl::Bytes>) return 'B';
        else if constexpr (std::is_same_v<T, wxl::Answer>) return 'A';
        else if constexpr (std::is_same_v<T, wxl::Buffer>) return 'F';
        else if constexpr (std::is_same_v<T, std::string_view>) return 's';
        else if constexpr (std::is_same_v<T, bool>) return 'b';
        else if constexpr (std::is_same_v<T, wxl::Object>) return 'o';
        else return 'v';
    }

    template <class Signature>
    struct Letters;
    template <class... P>
    struct Letters<void(P...)>
    {
        static constexpr char text[] = {Letter<P>()..., '\0'};
    };

    /// Per event, in table order: the parameters of its C++ handler.
    constexpr const char* kHandlerLetters[] = {
#define WXL_EVENT(family, name, id, args) Letters<wxl::HandlerOf<WXL_EV_##name>>::text,
#include "wxl/events.def"
#undef WXL_EVENT
    };

    /// events.def in table order, both shapes: a WXL_EVENTS_SINCE row as its first id, an event
    /// as its id, told apart by `since`.
    struct Row
    {
        bool     since;
        uint32_t id;
    };

    constexpr Row kRows[] = {
#define WXL_EVENT(family, name, id, args)              { false, id },
#define WXL_EVENTS_SINCE(major, minor, feature, first) { true, first },
#include "wxl/events.def"
#undef WXL_EVENTS_SINCE
#undef WXL_EVENT
    };

    /// a < b, in the order versions follow each other.
    bool Before(const wxl::reflect::Version& a, const wxl::reflect::Version& b)
    {
        if (a.major != b.major) return a.major < b.major;
        if (a.minor != b.minor) return a.minor < b.minor;
        return a.feature < b.feature;
    }

    char LetterOf(Kind kind)
    {
        switch (kind)
        {
            case Kind::bytes: return 'B';
            case Kind::answer: return 'A';
            case Kind::buffer: return 'F';
            case Kind::str: return 's';
            case Kind::boolean: return 'b';
            case Kind::handle: return 'o';
            default: return 'v';
        }
    }
}

// Every row's kinds make the member's C type: a header and its table cannot drift.
namespace service_signatures
{
    using namespace wxl::api::kind;

#define WXL_API_SERVICE(svc, name, version, since, access, doc)
#define WXL_API_RESOURCE(svc, Name, ctype, none_value)
#define WXL_API_SINCE(svc, member, since)
#define WXL_API_FIELD(Struct, member, field_kind)                                         \
    static_assert(std::is_same_v<decltype(Struct::member),                                \
                                 wxl::api::Value<wxl::api::kind::field_kind>::type>,      \
                  #Struct "." #member " is not of kind " #field_kind);
#define WXL_API(svc, member, Name, answer, params, lua, cxx, doc)                              \
    static_assert(std::is_same_v<decltype(wxl_##svc##_api::member),                       \
                                 wxl::api::SignatureOf<answer params>>,                   \
                  "wxl_" #svc "_api::" #member " is not " #answer " " #params);
#define WXL_API_CLOSE(svc, member, Name, answer, Resource, when, doc)                          \
    static_assert(std::is_same_v<decltype(wxl_##svc##_api::member),                       \
                                 wxl::api::SignatureOf<answer(Resource)>>,                \
                  "wxl_" #svc "_api::" #member " is not " #answer " (" #Resource ")");
#include "wxl/services/tables.inc"
#undef WXL_API_CLOSE
#undef WXL_API
#undef WXL_API_FIELD
#undef WXL_API_SINCE
#undef WXL_API_RESOURCE
#undef WXL_API_SERVICE
} // namespace service_signatures

// The service tables in table order, a WXL_API_SINCE row as a mark: what sits right below it.
namespace service_order
{
    struct Entry
    {
        bool        since;
        const char* service;
        const char* member;
    };

#define WXL_API_SERVICE(svc, name, version, since, access, doc)
#define WXL_API_RESOURCE(svc, Name, ctype, none_value)
#define WXL_API_SINCE(svc, member, since) Entry{true, #svc, #member},
#define WXL_API_FIELD(Struct, member, field_kind)
#define WXL_API(svc, member, Name, answer, params, lua, cxx, doc) Entry{false, #svc, #member},
#define WXL_API_CLOSE(svc, member, Name, answer, Resource, when, doc) Entry{false, #svc, #member},
    const Entry kEntries[] = {
#include "wxl/services/tables.inc"
    };
#undef WXL_API_CLOSE
#undef WXL_API
#undef WXL_API_FIELD
#undef WXL_API_SINCE
#undef WXL_API_RESOURCE
#undef WXL_API_SERVICE
} // namespace service_order

int main()
{
    // Unbuffered, so the last line printed before a crash is never held back in a buffer.
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    uint32_t fieldCount = 0, eventCount = 0;
    const wxl::reflect::Field* fields = wxl::reflect::Fields(&fieldCount);
    const wxl::reflect::Event* events = wxl::reflect::Events(&eventCount);
    Check(fieldCount == sizeof kShapes / sizeof kShapes[0], "one shape per field row");

    std::printf("every args struct is fully described\n");
    for (uint32_t e = 0; e < eventCount; ++e)
    {
        const wxl::reflect::Event& event = events[e];
        char what[200];

        std::snprintf(what, sizeof what, "%s has rows in fields.def", event.args);
        Check(event.field_count > 0, what);
        if (event.field_count == 0) continue;

        // Rebuilt from the rows with C's own rule: each member at the next offset its alignment
        // allows. A member left out, swapped or mistyped puts the next one somewhere else.
        const uint32_t first = uint32_t(event.fields - fields);
        uint32_t       end = 0, align = 1;
        bool           laidOut = true;
        for (uint32_t i = 0; i < event.field_count; ++i)
        {
            const FieldShape& shape = kShapes[first + i];
            if (event.fields[i].offset != AlignUp(end, shape.align)) laidOut = false;
            end   = event.fields[i].offset + shape.size;
            align = shape.align > align ? shape.align : align;
        }
        std::snprintf(what, sizeof what, "%s: the rows are its members, in order", event.args);
        Check(laidOut && AlignUp(end, align) == event.args_size, what);

        for (uint32_t i = 0; i < event.field_count; ++i)
        {
            const wxl::reflect::Field& field = event.fields[i];
            const bool sized = field.kind == Kind::bytes || field.kind == Kind::buffer;
            std::snprintf(what, sizeof what, "%s.%s names its extent exactly when it needs one",
                          event.args, field.name);
            Check(sized == (field.extent != nullptr), what);
        }
    }

    std::printf("the C++ handlers take the parameters the rows give\n");
    Check(eventCount == sizeof kHandlerLetters / sizeof kHandlerLetters[0], "one handler per event");
    for (uint32_t e = 0; e < eventCount && e < sizeof kHandlerLetters / sizeof kHandlerLetters[0]; ++e)
    {
        const wxl::reflect::Event& event = events[e];
        std::string                fromRows;
        for (uint32_t i = 0; i < event.field_count; ++i)
            if (!wxl::reflect::IsExtent(event, event.fields[i])) fromRows += LetterOf(event.fields[i].kind);
        char what[200];
        std::snprintf(what, sizeof what, "On%s takes %s, the rows give %s", event.name,
                      kHandlerLetters[e], fromRows.c_str());
        Check(fromRows == kHandlerLetters[e], what);
    }
    const wxl::reflect::Event* cast = wxl::reflect::Find(WXL_EV_SpellCastGo);
    const wxl::reflect::Field* size = cast ? wxl::reflect::FieldOf(*cast, "hits_size") : nullptr;
    const wxl::reflect::Field* hits = cast ? wxl::reflect::FieldOf(*cast, "hit_count") : nullptr;
    Check(size && hits && wxl::reflect::IsExtent(*cast, *size) && !wxl::reflect::IsExtent(*cast, *hits),
          "a member another one measures is an extent, a count nobody measures is not");

    std::printf("the rows of a struct are contiguous\n");
    for (uint32_t i = 0; i < fieldCount; ++i)
        for (uint32_t j = i + 2; j < fieldCount; ++j)
            if (std::strcmp(fields[i].args, fields[j].args) == 0 &&
                std::strcmp(fields[i].args, fields[j - 1].args) != 0)
            {
                char what[200];
                std::snprintf(what, sizeof what, "%s's rows are split", fields[i].args);
                Check(false, what);
            }

    std::printf("each event knows the version that brought it\n");
    {
        uint32_t                   sinceCount = 0;
        const wxl::reflect::Since* sinces     = wxl::reflect::Sinces(&sinceCount);
        Check(sinceCount > 0 && sinces[0].first == 0, "a WXL_EVENTS_SINCE row covers id 0");
        for (uint32_t i = 1; i < sinceCount; ++i)
        {
            char what[200];
            std::snprintf(what, sizeof what, "WXL_EVENTS_SINCE(%u, %u, %u, %u) comes after the row before it, "
                          "in version and in id", sinces[i].version.major, sinces[i].version.minor,
                          sinces[i].version.feature, sinces[i].first);
            Check(Before(sinces[i - 1].version, sinces[i].version) && sinces[i - 1].first < sinces[i].first, what);
        }
        const wxl::reflect::Version abi{WXL_ABI_VERSION_MAJOR, WXL_ABI_VERSION_MINOR, WXL_ABI_VERSION_FEATURE};
        Check(sinceCount > 0 && !Before(abi, sinces[sinceCount - 1].version),
              "no WXL_EVENTS_SINCE row is newer than abi.h");

        constexpr size_t rowCount = sizeof kRows / sizeof kRows[0];
        uint32_t         seen     = 0;
        for (size_t i = 0; i < rowCount; ++i)
        {
            if (!kRows[i].since) continue;
            char what[200];
            std::snprintf(what, sizeof what, "WXL_EVENTS_SINCE(..., %u) sits right above the event of id %u",
                          kRows[i].id, kRows[i].id);
            Check(i + 1 < rowCount && !kRows[i + 1].since && kRows[i + 1].id == kRows[i].id, what);
            ++seen;
        }
        Check(seen == sinceCount, "one Since per WXL_EVENTS_SINCE row");

        // Below each row, every event has an id at or past its first: an event appended above
        // the row of its version would be counted to an older one.
        uint32_t floor = 0;
        for (size_t i = 0; i < rowCount; ++i)
        {
            if (kRows[i].since)
            {
                floor = kRows[i].id;
                continue;
            }
            char what[200];
            std::snprintf(what, sizeof what, "event %u sits below WXL_EVENTS_SINCE(..., %u), so its id "
                          "is at least that", kRows[i].id, floor);
            Check(kRows[i].id >= floor, what);
        }

        constexpr wxl::reflect::Version first = wxl::reflect::SinceOf(WXL_EV_Update);
        static_assert(first.major == 0 && first.minor == 1 && first.feature == 5,
                      "Update came with the first contract");
        Check(wxl::reflect::SinceOf(WXL_EV_CombatMiss).feature == 5 &&
                  wxl::reflect::SinceOf(WXL_EV_ZoneChanged).feature == 6 &&
                  wxl::reflect::SinceOf(WXL_EV_ChatMessage).feature == 6 &&
                  wxl::reflect::SinceOf(WXL_EV_ChatAddonMessage).feature == 7,
              "CombatMiss came with 0.1.5, ZoneChanged and ChatMessage with 0.1.6, ChatAddonMessage with 0.1.7");
    }

    std::printf("the enumerations name every value once\n");
    {
        uint32_t                  enumCount = 0;
        const wxl::reflect::Enum* enums     = wxl::reflect::Enums(&enumCount);
        Check(enumCount > 0, "enums.def declares enumerations");
        for (uint32_t e = 0; e < enumCount; ++e)
        {
            const wxl::reflect::Enum& one = enums[e];
            char                      what[200];
            std::snprintf(what, sizeof what, "%s has values", one.type);
            Check(one.count > 0, what);
            for (uint32_t i = 0; i < one.count; ++i)
                for (uint32_t j = i + 1; j < one.count; ++j)
                {
                    std::snprintf(what, sizeof what, "%s: %s and %s differ in number and in name",
                                  one.type, one.values[i].constant, one.values[j].constant);
                    Check(one.values[i].value != one.values[j].value &&
                              std::strcmp(one.values[i].name, one.values[j].name) != 0,
                          what);
                }
            std::snprintf(what, sizeof what, "%s is found by its type", one.type);
            Check(wxl::reflect::FindEnum(one.type) == &one, what);
        }
        for (uint32_t i = 0; i < fieldCount; ++i)
        {
            if (!fields[i].enumeration) continue;
            char what[200];
            std::snprintf(what, sizeof what, "%s.%s holds %s in a u32", fields[i].args,
                          fields[i].name, fields[i].enumeration->type);
            Check(fields[i].kind == Kind::u32, what);
        }
        const wxl::reflect::Event* ready   = wxl::reflect::Find(WXL_EV_ScriptVmReady);
        const wxl::reflect::Field* context = ready ? wxl::reflect::FieldOf(*ready, "context") : nullptr;
        Check(context && context->enumeration &&
                  std::strcmp(context->enumeration->type, "wxl_script_context") == 0 &&
                  std::strcmp(wxl::reflect::NameOf(*context->enumeration, WXL_SCRIPT_CONTEXT_GAME),
                              "game") == 0 &&
                  wxl::reflect::NameOf(*context->enumeration, 9) == nullptr,
              "a member knows its enumeration, which names a value and not a number it lacks");
        const wxl::reflect::Event* create = wxl::reflect::Find(WXL_EV_ObjectCreate);
        const wxl::reflect::Field* guid   = create ? wxl::reflect::FieldOf(*create, "guid") : nullptr;
        Check(guid && !guid->enumeration, "a member of another kind has none");
    }

    std::printf("the update fields cover each kind's block as the client counts it\n");
    {
        namespace field = wxl::field;
        // The client's dwords per kind, s_objDwordCount (0x00AC3D04), in OBJECT_TYPE_ID order:
        // where each kind's block ends.
        const uint32_t dwordCount[] = {0x6, 0x40, 0x8A, 0x94, 0x52E, 0x12, 0xC, 0x24};
        uint32_t           kindCount = 0, rowCount = 0, runCount = 0;
        const field::Kind* kinds = field::Kinds(&kindCount);
        const field::Row*  rows  = field::Rows(&rowCount);
        const field::WatchRun* runs = field::WatchRuns(&runCount);
        Check(kindCount == 8, "eight kinds, the client's");
        char what[256];
        for (uint32_t k = 0; k < kindCount && k < 8; ++k)
        {
            const field::Kind& kind = kinds[k];
            std::snprintf(what, sizeof what, "%s is wxl_object_type %u and ends where s_objDwordCount says, 0x%X",
                          kind.name, k, dwordCount[k]);
            Check(static_cast<uint32_t>(kind.type) == k && kind.end == dwordCount[k], what);
            const field::Kind* parent = field::KindOf(kind.parent);
            std::snprintf(what, sizeof what, "%s's fields start where its parent's end", kind.name);
            Check(parent && (k == 0 ? kind.first == 0 : kind.first == parent->end), what);
            std::snprintf(what, sizeof what, "%s's block ends at its end for every object but another player", kind.name);
            Check(kind.others_end == (kind.type == WXL_OBJECT_PLAYER ? 0x144u : kind.end), what);

            // Its rows: ascending, contiguous, whole elements, one code name each.
            uint32_t cursor = kind.first;
            for (uint32_t i = 0; i < rowCount; ++i)
            {
                const field::Row& row = rows[i];
                if (row.kind != kind.type) continue;
                std::snprintf(what, sizeof what, "%s starts where the field before it ends (0x%X)", row.name, cursor);
                Check(row.index == cursor, what);
                cursor = row.index + row.size;
                std::snprintf(what, sizeof what, "%s holds whole elements", row.name);
                Check(row.size && row.size % row.Dwords() == 0, what);
                for (uint32_t j = i + 1; j < rowCount; ++j)
                    if (rows[j].kind == row.kind && std::strcmp(rows[j].code, row.code) == 0)
                    {
                        std::snprintf(what, sizeof what, "%s and %s share the code name %s", row.name,
                                      rows[j].name, row.code);
                        Check(false, what);
                    }
                std::snprintf(what, sizeof what, "%s is found by its name and by %s.%s", row.name, kind.name, row.code);
                const std::string qualified = std::string(kind.name) + "." + row.code;
                Check(field::Find(kind.type, row.name) == &row && field::Find(kind.type, qualified) == &row &&
                          field::RowAt(kind.type, row.index + row.size - 1) == &row,
                      what);

                // The watch column agrees with the WATCH rows, element by element.
                bool any = false, local = false, none = false;
                for (uint32_t e = row.index; e < row.index + row.size; e += row.Dwords())
                {
                    field::Watch scope = field::WatchOf(kind.type, e);
                    if (row.Dwords() == 2 && field::WatchOf(kind.type, e + 1) != scope) scope = field::Watch::none;
                    any |= scope == field::Watch::any;
                    local |= scope == field::Watch::local;
                    none |= scope == field::Watch::none;
                }
                const field::Watch says = int(any) + int(local) + int(none) > 1 ? field::Watch::some
                                          : any                                 ? field::Watch::any
                                          : local                               ? field::Watch::local
                                                                                : field::Watch::none;
                std::snprintf(what, sizeof what, "%s's watch column agrees with the WATCH rows", row.name);
                Check(says == row.watch, what);
            }
            std::snprintf(what, sizeof what, "%s's fields cover dwords 0x%X..0x%X", kind.name, kind.first, kind.end);
            Check(cursor == kind.end, what);
        }
        uint32_t last = 0, lastKind = 0;
        for (uint32_t r = 0; r < runCount; ++r)
        {
            const field::Kind* kind = field::KindOf(runs[r].kind);
            std::snprintf(what, sizeof what, "WATCH run 0x%X..0x%X lies in its kind's block, in order",
                          runs[r].first, runs[r].end);
            Check(kind && runs[r].first >= kind->first && runs[r].end <= kind->end && runs[r].first < runs[r].end &&
                      (runs[r].local ? kind->type == WXL_OBJECT_PLAYER : true) &&
                      (uint32_t(runs[r].kind) != lastKind || runs[r].local || runs[r].first >= last),
                  what);
            if (!runs[r].local) last = runs[r].end;
            lastKind = uint32_t(runs[r].kind);
        }

        Check(field::Unit::Health.index == 0x18 && field::Player::Xp.index == 0x27A &&
                  field::Object::Guid.Dwords() == 2 && field::Unit::Resistances.Elements() == 7,
              "the constants are the rows: UNIT_FIELD_HEALTH at 0x18, PLAYER_XP at 0x27A");
        bool ambiguous = false;
        Check(field::Find(WXL_OBJECT_PLAYER, "Flags", &ambiguous) == nullptr && ambiguous &&
                  field::Find(WXL_OBJECT_UNIT, "Flags") == field::Find(WXL_OBJECT_UNIT, "UNIT_FIELD_FLAGS") &&
                  field::Find(WXL_OBJECT_GAMEOBJECT, "Health") == nullptr,
              "a bare name two kinds of the object have is ambiguous; one of another kind is not found");
        Check(field::OwnerOf(WXL_OBJECT_PLAYER, 0x18)->type == WXL_OBJECT_UNIT &&
                  field::OwnerOf(WXL_OBJECT_ITEM, 0x18)->type == WXL_OBJECT_ITEM &&
                  field::OwnerOf(WXL_OBJECT_UNIT, 0x94) == nullptr,
              "a dword belongs to the kind along the object's own whose block holds it");

        const wxl::reflect::Event* changed = wxl::reflect::Find(WXL_EV_ObjectFieldChanged);
        const wxl::reflect::Field* was     = changed ? wxl::reflect::FieldOf(*changed, "old_value") : nullptr;
        const wxl::reflect::Field* now     = changed ? wxl::reflect::FieldOf(*changed, "new_value") : nullptr;
        Check(was && now && was->value && now->value && was->kind == Kind::u64 &&
                  was->value_type_offset == offsetof(wxl_object_field_args, type) &&
                  now->value_field_offset == offsetof(wxl_object_field_args, field),
              "ObjectFieldChanged's values know the members naming their field");
        const wxl::reflect::Version since = wxl::reflect::SinceOf(WXL_EV_ObjectFieldChanged);
        Check(since.major == 0 && since.minor == 1 && since.feature == 9, "ObjectFieldChanged came with 0.1.9");
    }

    std::printf("lookups\n");
    const wxl::reflect::Event* death = wxl::reflect::Find("Object", "UnitDeath");
    Check(death && death->id == WXL_EV_UnitDeath, "an event is found by family and name");
    Check(wxl::reflect::Find(WXL_EV_MessageSent) &&
              std::strcmp(wxl::reflect::Find(WXL_EV_MessageSent)->family, "Net") == 0,
          "and by id");
    Check(wxl::reflect::Find("Object", "Update") == nullptr, "a name of another family is not");
    const wxl::reflect::Event* sent = wxl::reflect::Find(WXL_EV_MessageSent);
    const wxl::reflect::Field* body = sent ? wxl::reflect::FieldOf(*sent, "data") : nullptr;
    Check(body && body->kind == Kind::bytes && std::strcmp(body->extent, "size") == 0 &&
              body->extent_offset == offsetof(wxl_message_args, size),
          "a bytes field knows where its length is");
    const wxl::reflect::Field* flag = sent ? wxl::reflect::FieldOf(*sent, "suppressed") : nullptr;
    Check(flag && flag->kind == Kind::answer, "and an answer is told apart from a value");

    std::printf("service tables\n");
    {
        uint32_t                     serviceCount = 0, rowCount = 0;
        const wxl::api::ServiceRow*  services     = wxl::api::Services(&serviceCount);
        const wxl::api::Row*         rows         = wxl::api::Rows(&rowCount);
        Check(serviceCount == 28, "twenty-eight service tables: script, storage, data, hook, hook.raw, patch, footprint, profile, point, action, m2file, m2, gx, engine, relay, camera, input, world, pose, imgui, highlight, selectioncircle, texture, cvar, render, vk, terrain, draw");

        // A service's rows follow each other, from the first member after struct_size to the
        // last, one function pointer apart: a member missing from its table, or one the table
        // has twice, breaks the run.
        uint32_t at = 0;
        for (uint32_t s = 0; s < serviceCount; ++s)
        {
            const wxl::api::ServiceRow& service = services[s];
            uint32_t expected = uint32_t(sizeof(void*)); // struct_size, padded to a pointer
            bool     ordered  = true;
            uint32_t count    = 0;
            for (; at < rowCount && std::strcmp(rows[at].service, service.id) == 0; ++at, ++count)
            {
                if (rows[at].offset != expected) ordered = false;
                expected = rows[at].offset + uint32_t(sizeof(void*));
            }
            const std::string what = std::string("wxl_") + service.id +
                                     "_api: every member has a row, in member order, and no more";
            Check(count > 0 && ordered && expected == service.api_size, what.c_str());
            const std::string named = std::string("wxl.") + service.id + " is published as " + service.name;
            Check(std::strncmp(service.name, "wxl.", 4) == 0, named.c_str());
        }
        Check(at == rowCount, "every row belongs to the table it follows");

        // The version each table and member came with: none newer than abi.h, none older than
        // the oldest abi a mod can ask for, a member never older than its table.
        const wxl::api::Version current{WXL_ABI_VERSION_MAJOR, WXL_ABI_VERSION_MINOR, WXL_ABI_VERSION_FEATURE};
        const wxl::api::Version oldest{0, 1, 5};
        for (uint32_t s = 0; s < serviceCount; ++s)
        {
            const std::string what = std::string("wxl.") + services[s].id +
                                     " came with an abi from 0.1.5 to abi.h's";
            Check(wxl::api::AtLeast(services[s].since, oldest) && wxl::api::AtLeast(current, services[s].since),
                  what.c_str());
        }
        uint32_t                    sinceCount = 0;
        const wxl::api::SinceRow*   sinces     = wxl::api::Sinces(&sinceCount);
        for (uint32_t i = 0; i < sinceCount; ++i)
        {
            const wxl::api::ServiceRow* service = wxl::api::FindService(sinces[i].service);
            const std::string what = std::string("WXL_API_SINCE(") + sinces[i].service + ", " + sinces[i].member +
                                     ") is newer than its table and the one above it, and no newer than abi.h";
            bool newer = service && wxl::api::AtLeast(sinces[i].since, service->since) &&
                         !wxl::api::AtLeast(service->since, sinces[i].since);
            if (i > 0 && std::strcmp(sinces[i - 1].service, sinces[i].service) == 0)
                newer = newer && !wxl::api::AtLeast(sinces[i - 1].since, sinces[i].since);
            Check(newer && wxl::api::AtLeast(current, sinces[i].since), what.c_str());
        }
        uint32_t marks = 0;
        const uint32_t entryCount = uint32_t(sizeof service_order::kEntries / sizeof service_order::kEntries[0]);
        for (uint32_t i = 0; i < entryCount; ++i)
        {
            const service_order::Entry& entry = service_order::kEntries[i];
            if (!entry.since) continue;
            ++marks;
            const service_order::Entry* below = i + 1 < entryCount ? &service_order::kEntries[i + 1] : nullptr;
            const std::string what = std::string("WXL_API_SINCE(") + entry.service + ", " + entry.member +
                                     ") sits right above that member's row";
            Check(below && !below->since && std::strcmp(below->service, entry.service) == 0 &&
                      std::strcmp(below->member, entry.member) == 0,
                  what.c_str());
        }
        Check(marks == sinceCount, "one SinceRow per WXL_API_SINCE row");
        const wxl::api::Row* removed = nullptr;
        const wxl::api::Row* read    = nullptr;
        for (uint32_t i = 0; i < rowCount; ++i)
        {
            if (std::strcmp(rows[i].member, "remove_search_path") == 0) removed = &rows[i];
            if (std::strcmp(rows[i].service, "storage") == 0 && std::strcmp(rows[i].member, "read") == 0)
                read = &rows[i];
        }
        constexpr const wxl::api::ServiceRow* profileRow = wxl::api::FindService("profile");
        static_assert(profileRow && profileRow->since.feature == 12, "a table's version reads at compile time");
        Check(removed && read && wxl::api::SinceOf(*removed).feature == 10 && wxl::api::SinceOf(*read).feature == 5,
              "a member below a WXL_API_SINCE row came with it, one above it with its table");

        // A resource is handed out by one row and given back by one close row of its service.
        uint32_t                     resourceCount = 0;
        const wxl::api::ResourceRow* resources     = wxl::api::Resources(&resourceCount);
        for (uint32_t r = 0; r < resourceCount; ++r)
        {
            uint32_t closers = 0, givers = 0;
            for (uint32_t i = 0; i < rowCount; ++i)
            {
                if (std::strcmp(rows[i].service, resources[r].service) != 0) continue;
                if (rows[i].closes != wxl::api::When::never &&
                    std::string(rows[i].params) == std::string("(") + resources[r].name + " self)")
                    ++closers;
                if (std::strcmp(rows[i].answer, resources[r].name) == 0) ++givers;
            }
            const std::string what = std::string("the ") + resources[r].name +
                                     " of wxl." + resources[r].service +
                                     " is handed out by a row and given back by exactly one";
            Check(closers == 1 && givers >= 1, what.c_str());
        }

        const wxl::api::Row* add = nullptr;
        for (uint32_t i = 0; i < rowCount; ++i)
            if (std::strcmp(rows[i].member, "add_search_path") == 0) add = &rows[i];
        Check(add && std::strcmp(add->name, "AddSearchPath") == 0 && std::strcmp(add->answer, "result") == 0 &&
                  std::strcmp(add->params, "(str directory, i32 priority)") == 0 &&
                  add->lua == wxl::api::Lua::shim && add->cxx == wxl::api::Cxx::generated &&
                  add->offset == offsetof(wxl_storage_api, add_search_path),
              "a row reads back as written: its name, kinds, parameter names and columns");

        uint32_t                  fieldCount = 0;
        const wxl::api::FieldRow* fields     = wxl::api::Fields(&fieldCount);
        uint32_t hookFields = 0;
        uint32_t firstHook  = fieldCount;
        for (uint32_t i = 0; i < fieldCount; ++i)
            if (fields[i].of == &wxl::api::TypeTag<wxl_hook_point>)
            {
                if (firstHook == fieldCount) firstHook = i;
                ++hookFields;
            }
        Check(hookFields == 2 && firstHook + 1 < fieldCount &&
                  fields[firstHook + 1].offset == offsetof(wxl_hook_point, signature),
              "a struct's fields name it by type, for a list<struct> answer to find them");
    }

    if (g_failures == 0)
    {
        std::printf("\nall checks passed\n");
        return 0;
    }
    std::printf("\n%d check(s) failed\n", g_failures);
    return 1;
}
