// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// Service tables that keep their promises and count what is asked of them, so the C++ layer can
// be checked against the contract without a client: that a read through a stale handle is
// absent rather than zero, that a resource is released exactly once, that a typed call reaches
// the table with its arguments intact.

#ifndef WXL_TEST_FAKE_SERVICES_HPP
#define WXL_TEST_FAKE_SERVICES_HPP

#include <cctype>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "wxl/objects.hpp"
#include "wxl/services/action.h"
#include "wxl/services/camera.h"
#include "wxl/services/input.h"
#include "wxl/services/world.h"
#include "wxl/services/pose.h"
#include "wxl/services/highlight.h"
#include "wxl/services/selectioncircle.h"
#include "wxl/services/texture.h"
#include "wxl/services/cvar.h"
#include "wxl/services/imgui.h"
#include "wxl/services/data.h"
#include "wxl/services/hook.h"
#include "wxl/services/net.h"
#include "wxl/services/object.h"
#include "wxl/services/patch.h"
#include "wxl/services/footprint.h"
#include "wxl/services/gx.h"
#include "wxl/services/engine.h"
#include "wxl/services/m2.h"
#include "wxl/services/m2file.h"
#include "wxl/services/profile.h"
#include "wxl/services/relay.h"
#include "wxl/services/script.h"
#include "wxl/services/storage.h"
#include "wxl/services/ui.h"

/// The opaque call of the script service, as this fake defines it.
struct wxl_script_call
{
    std::vector<wxl_value> args;
    std::vector<wxl_value> results;
    std::string            error;
};

namespace wxltest
{
    // --- wxl.object -------------------------------------------------------------------------
    //
    // Three objects: 42 the local player (dead), 43 a boar, 44 a chest. Anything else is stale,
    // except the wolves a test spawns: units with a guid, one handle per visibility episode.

    struct FakeObjects
    {
        static constexpr wxl_handle kPlayer = 42;
        static constexpr wxl_handle kBoar   = 43;
        static constexpr wxl_handle kChest  = 44;
        static constexpr wxl_handle kItem   = 45; ///< in view, but not among InView's
        static constexpr wxl_handle kStale  = 7;

        /// handle -> guid of the spawned units still in view.
        static std::map<wxl_handle, wxl_guid>& Spawned()
        {
            static std::map<wxl_handle, wxl_guid> spawned;
            return spawned;
        }

        static void Spawn(wxl_handle h, wxl_guid guid) { Spawned()[h] = guid; }
        static void Despawn(wxl_handle h) { Spawned().erase(h); }

        static bool IsSpawned(wxl_handle h) { return Spawned().count(h) != 0; }
        static bool Live(wxl_handle h)
        {
            return h == kPlayer || h == kBoar || h == kChest || h == kItem || IsSpawned(h);
        }
        static bool IsUnit(wxl_handle h) { return h == kPlayer || h == kBoar || IsSpawned(h); }

        static wxl_bool WXL_CALL Valid(wxl_handle h) { return Live(h) ? WXL_TRUE : WXL_FALSE; }

        static wxl_guid WXL_CALL Guid(wxl_handle h)
        {
            if (h == kPlayer) return 0x100;
            if (h == kBoar) return 0x200;
            if (IsSpawned(h)) return Spawned()[h];
            return WXL_GUID_NONE;
        }

        static uint32_t WXL_CALL Type(wxl_handle h)
        {
            if (h == kPlayer) return WXL_OBJECT_PLAYER;
            if (h == kBoar || IsSpawned(h)) return WXL_OBJECT_UNIT;
            if (h == kChest) return WXL_OBJECT_GAMEOBJECT;
            if (h == kItem) return WXL_OBJECT_ITEM;
            return WXL_OBJECT_UNKNOWN;
        }

        static wxl_handle WXL_CALL FromGuid(wxl_guid guid)
        {
            if (guid == 0x100) return kPlayer;
            if (guid == 0x200) return kBoar;
            for (const auto& spawned : Spawned())
                if (spawned.second == guid) return spawned.first;
            return WXL_HANDLE_NONE;
        }

        /// A chest is a game object of type 3.
        static wxl_result WXL_CALL GameObjectType(wxl_handle h, uint32_t* out)
        {
            if (!out) return WXL_E_INVALID;
            if (!Live(h)) return WXL_E_NOT_FOUND;
            if (h != kChest) return WXL_E_INVALID;
            *out = 3;
            return WXL_OK;
        }

        static wxl_handle WXL_CALL LocalPlayer() { return kPlayer; }

        static wxl_result WXL_CALL Position(wxl_handle h, wxl_vec3* out)
        {
            if (!out) return WXL_E_INVALID;
            if (!Live(h)) return WXL_E_NOT_FOUND;
            *out = wxl_vec3{static_cast<float>(h), 2.0f, 3.0f};
            return WXL_OK;
        }

        static wxl_result WXL_CALL Facing(wxl_handle h, float* out)
        {
            if (!out) return WXL_E_INVALID;
            if (!Live(h)) return WXL_E_NOT_FOUND;
            *out = 1.5f;
            return WXL_OK;
        }

        static wxl_result WXL_CALL Name(wxl_handle h, wxl_str* out)
        {
            if (!out) return WXL_E_INVALID;
            if (!Live(h)) return WXL_E_NOT_FOUND;
            static const char* names[] = {"Thorgrim", "Boar", "Chest", "Hearthstone"};
            const char*        name    = IsSpawned(h) ? "Wolf" : names[h - kPlayer];
            *out                       = wxl_str{name, static_cast<uint32_t>(std::strlen(name))};
            return WXL_OK;
        }

        template <class V>
        static wxl_result UnitRead(wxl_handle h, V* out, V player, V boar)
        {
            if (!out) return WXL_E_INVALID;
            if (!Live(h)) return WXL_E_NOT_FOUND;
            if (!IsUnit(h)) return WXL_E_INVALID;
            *out = h == kPlayer ? player : h == kBoar ? boar : V(50);
            return WXL_OK;
        }

        static wxl_result WXL_CALL Health(wxl_handle h, int32_t* out) { return UnitRead<int32_t>(h, out, 0, 90); }
        static wxl_result WXL_CALL MaxHealth(wxl_handle h, uint32_t* out) { return UnitRead<uint32_t>(h, out, 5000, 120); }
        static wxl_result WXL_CALL Level(wxl_handle h, uint32_t* out) { return UnitRead<uint32_t>(h, out, 80, 3); }

        static wxl_result WXL_CALL Target(wxl_handle h, wxl_handle* out)
        {
            if (!out) return WXL_E_INVALID;
            if (!Live(h)) return WXL_E_NOT_FOUND;
            if (!IsUnit(h)) return WXL_E_INVALID;
            *out = h == kPlayer ? kBoar : WXL_HANDLE_NONE;
            return WXL_OK;
        }

        static uint32_t WXL_CALL Enumerate(uint32_t type, wxl_handle* out, uint32_t capacity)
        {
            uint32_t count = 0;
            for (wxl_handle h : {kPlayer, kBoar, kChest})
            {
                if (type != WXL_OBJECT_UNKNOWN && Type(h) != type) continue;
                if (out && count < capacity) out[count] = h;
                ++count;
            }
            return count;
        }

        static wxl_result WXL_CALL Positions(const wxl_handle* objects, uint32_t count, wxl_vec3* out)
        {
            if (!objects || !out) return WXL_E_INVALID;
            for (uint32_t i = 0; i < count; ++i)
                if (Position(objects[i], &out[i]) != WXL_OK) out[i] = wxl_vec3{0.0f, 0.0f, 0.0f};
            return WXL_OK;
        }

        // The descriptor blocks: the player's whole (it is the active player), a unit's for the
        // boar and the wolves, a game object's for the chest. A few fields are seeded, by index:
        // UNIT_FIELD_TARGET 0x12, UNIT_FIELD_BYTES_0 0x17, UNIT_FIELD_HEALTH 0x18,
        // UNIT_FIELD_MAXHEALTH 0x20, UNIT_FIELD_BOUNDINGRADIUS 0x41, GAMEOBJECT_FLAGS 0x9.
        static std::vector<uint32_t>* Block(wxl_handle h)
        {
            static std::map<wxl_handle, std::vector<uint32_t>> blocks;
            if (!Live(h)) return nullptr;
            auto found = blocks.find(h);
            if (found != blocks.end()) return &found->second;
            std::vector<uint32_t> block(h == kPlayer ? 0x52E : h == kChest ? 0x12 : 0x94, 0u);
            if (IsUnit(h))
            {
                const float radius = 1.5f;
                std::memcpy(&block[0x41], &radius, sizeof radius);
                block[0x18] = h == kPlayer ? 0 : 90;
                block[0x20] = h == kPlayer ? 5000 : 120;
            }
            if (h == kPlayer)
            {
                block[0x12] = 0x200; // the boar, as a guid of two dwords
                block[0x17] = 0x01000201u; // race 1, class 2, sex 0, power 1
            }
            if (h == kChest) block[0x9] = 32;
            return &blocks.emplace(h, std::move(block)).first->second;
        }

        static uint32_t WXL_CALL FieldCount(wxl_handle h)
        {
            const std::vector<uint32_t>* block = Block(h);
            return block ? static_cast<uint32_t>(block->size()) : 0;
        }

        static wxl_result WXL_CALL ReadFields(wxl_handle h, uint32_t first, uint32_t count, uint32_t* out)
        {
            if (!out) return WXL_E_INVALID;
            const std::vector<uint32_t>* block = Block(h);
            if (!block) return WXL_E_NOT_FOUND;
            if (!count || first >= block->size() || count > block->size() - first) return WXL_E_INVALID;
            std::memcpy(out, block->data() + first, count * sizeof(uint32_t));
            return WXL_OK;
        }

        static wxl_result WXL_CALL ReadFieldMany(const wxl_handle* objects, uint32_t count, uint32_t field,
                                                 uint32_t dwords, uint32_t* out, wxl_result* results)
        {
            if ((!objects || !out) && count) return WXL_E_INVALID;
            for (uint32_t i = 0; i < count; ++i)
            {
                const wxl_result result = ReadFields(objects[i], field, dwords, out + i * dwords);
                if (result != WXL_OK) std::memset(out + i * dwords, 0, dwords * sizeof(uint32_t));
                if (results) results[i] = result;
            }
            return WXL_OK;
        }

        static wxl_result WXL_CALL WriteFields(wxl_handle h, uint32_t first, uint32_t count, const uint32_t* in,
                                               uint32_t flags)
        {
            if (!in || flags) return WXL_E_INVALID;
            std::vector<uint32_t>* block = Block(h);
            if (!block) return WXL_E_NOT_FOUND;
            if (!count || first < 3 || first >= block->size() || count > block->size() - first) return WXL_E_INVALID;
            std::memcpy(block->data() + first, in, count * sizeof(uint32_t));
            return WXL_OK;
        }

        /// What watch_field was asked, in order. UNIT_FIELD_CREATEDBY (0x10) is refused, as a
        /// field the client keeps no copy of.
        struct Watch
        {
            uint32_t type;
            uint32_t first;
            uint32_t count;
        };

        static std::vector<Watch>& Watches()
        {
            static std::vector<Watch> watches;
            return watches;
        }

        static wxl_result WXL_CALL WatchField(uint32_t type, uint32_t first, uint32_t count)
        {
            if (first == 0x10) return WXL_E_UNSUPPORTED;
            Watches().push_back(Watch{type, first, count});
            return WXL_OK;
        }

        /// unwatch_field takes back the first watch it matches, as a runtime does.
        static wxl_result WXL_CALL UnwatchField(uint32_t type, uint32_t first, uint32_t count)
        {
            std::vector<Watch>& watches = Watches();
            for (auto it = watches.begin(); it != watches.end(); ++it)
            {
                if (it->type != type || it->first != first || it->count != count) continue;
                watches.erase(it);
                break;
            }
            return WXL_OK;
        }

        // --- every row of object_reads.def -----------------------------------------------
        //
        // A row the fakes above do not answer answers a seed of its own: the line it stands on
        // in object_reads.def, as the type it writes, for an object of a kind it accepts -- the
        // player for the active player's rows, the boar too for a unit's, the chest for a game
        // object's, the item for an item's. A number of an enumeration is its value 1. The
        // tests compute the same seed from the same rows and check each method answers it.

        static bool Accepts(wxl_handle h, const char* kinds)
        {
            const std::string k = kinds;
            if (k == "Any") return Live(h);
            if (k == "Units") return IsUnit(h);
            if (k == "Players" || k == "Active") return h == kPlayer;
            if (k == "Items") return h == kItem;
            if (k == "GameObjects") return h == kChest;
            return false;
        }

        template <class V>
        static V Seed(uint32_t line, bool named)
        {
            V value{};
            if constexpr (std::is_same_v<V, wxl_vec3>) value = wxl_vec3{float(line), 2.0f, 3.0f};
            else if constexpr (std::is_same_v<V, wxl_aura>)
            {
                value.struct_size = sizeof value;
                value.spell_id    = line;
                value.caster      = 0x200;
                value.stacks      = 3;
            }
            else if constexpr (std::is_same_v<V, wxl_skill>)
            {
                value.struct_size = sizeof value;
                value.id          = line;
                value.value       = 150;
                value.max         = 300;
            }
            else if constexpr (std::is_same_v<V, float>) value = float(line) + 0.5f;
            else value = named ? V(1) : V(line);
            return value;
        }

        template <class V>
        static void Write(V* out, const V& value)
        {
            if constexpr (std::is_same_v<V, wxl_aura> || std::is_same_v<V, wxl_skill>)
            {
                const uint32_t size = out->struct_size && out->struct_size < sizeof(V) ? out->struct_size : uint32_t(sizeof(V));
                std::memcpy(out, &value, size);
                out->struct_size = size;
            }
            else
            {
                *out = value;
            }
        }

        template <class V, uint32_t Line, bool Named, bool Handle>
        static wxl_result WXL_CALL Seeded(wxl_handle h, V* out)
        {
            if (!out) return WXL_E_INVALID;
            if (!Live(h)) return WXL_E_NOT_FOUND;
            if (!Accepts(h, KindsAt(Line))) return WXL_E_INVALID;
            if constexpr (Handle) Write(out, V(kBoar));
            else Write(out, Seed<V>(Line, Named));
            return WXL_OK;
        }

        template <class V, class A, uint32_t Line, bool Named, bool Handle>
        static wxl_result WXL_CALL SeededAt(wxl_handle h, A, V* out)
        {
            return Seeded<V, Line, Named, Handle>(h, out);
        }

        /// The kinds column of the row on a line of object_reads.def.
        static const char* KindsAt(uint32_t line)
        {
            static const std::map<uint32_t, const char*> kinds = {
#define WXL_OBJECT_READ(kinds, member, Method, type, source, Cxx, cxx, doc) {__LINE__, #kinds},
#define WXL_OBJECT_READ_AT(kinds, member, Method, type, source, Cxx, cxx, Arg, arg, doc) {__LINE__, #kinds},
#include "wxl/runtime/object_reads.def"
#undef WXL_OBJECT_READ_AT
#undef WXL_OBJECT_READ
            };
            const auto found = kinds.find(line);
            return found == kinds.end() ? "" : found->second;
        }

        static const wxl_object_api* Table()
        {
            static wxl_object_api api = [] {
                wxl_object_api t{};
                t.struct_size     = sizeof(wxl_object_api);
                {
                    // The rows from 0.1.18 on: the older ones are the fakes above, or absent.
                    using namespace ::wxl;
                    uint32_t since = 0;
#define WXL_OBJECT_SINCE(major, minor, feature) since = since ? since : __LINE__;
#define WXL_OBJECT_READ(kinds, member, Method, type, source, Cxx, cxx, doc)
#define WXL_OBJECT_READ_AT(kinds, member, Method, type, source, Cxx, cxx, Arg, arg, doc)
#include "wxl/runtime/object_reads.def"
#undef WXL_OBJECT_READ_AT
#undef WXL_OBJECT_READ
#undef WXL_OBJECT_SINCE
#define WXL_OBJECT_READ(kinds, member, Method, type, source, Cxx, cxx, doc)                                \
    if (__LINE__ > since)                                                                                   \
        t.member = &Seeded<type, __LINE__, std::is_enum_v<cxx>, std::is_class_v<cxx> && std::is_same_v<type, wxl_handle>>;
#define WXL_OBJECT_READ_AT(kinds, member, Method, type, source, Cxx, cxx, Arg, arg, doc)                    \
    if (__LINE__ > since)                                                                                   \
        t.member = &SeededAt<type, Arg, __LINE__, std::is_enum_v<cxx>,                                      \
                             std::is_class_v<cxx> && std::is_same_v<type, wxl_handle>>;
#include "wxl/runtime/object_reads.def"
#undef WXL_OBJECT_READ_AT
#undef WXL_OBJECT_READ
                }
                t.valid           = &Valid;
                t.guid            = &Guid;
                t.type            = &Type;
                t.from_guid       = &FromGuid;
                t.local_player    = &LocalPlayer;
                t.position        = &Position;
                t.facing          = &Facing;
                t.name            = &Name;
                t.unit_health     = &Health;
                t.unit_max_health = &MaxHealth;
                t.unit_level      = &Level;
                t.unit_target     = &Target;
                t.enumerate       = &Enumerate;
                t.positions       = &Positions;
                t.gameobject_type = &GameObjectType;
                t.field_count     = &FieldCount;
                t.read_fields     = &ReadFields;
                t.read_field_many = &ReadFieldMany;
                t.write_fields    = &WriteFields;
                t.watch_field     = &WatchField;
                t.unwatch_field   = &UnwatchField;
                return t;
            }();
            return &api;
        }
    };

    /// What the facade should answer for a row's seed: the C value as the row's C++ type.
    template <class V, class T>
    T SeedAs(uint32_t line)
    {
        if constexpr (std::is_class_v<T> && std::is_same_v<V, wxl_handle>)
            return ::wxl::detail::FromC<T>(V(FakeObjects::kBoar));
        else
            return ::wxl::detail::FromC<T>(FakeObjects::Seed<V>(line, std::is_enum_v<T>));
    }

    /// Whether two answers are the same seed.
    template <class T>
    bool SameSeed(const T& got, const T& want)
    {
        if constexpr (std::is_base_of_v<::wxl::Object, T>) return got.Handle() == want.Handle();
        else if constexpr (std::is_same_v<T, wxl_aura>) return got.spell_id == want.spell_id && got.stacks == want.stacks;
        else if constexpr (std::is_same_v<T, wxl_skill>) return got.id == want.id && got.max == want.max;
        else if constexpr (std::is_same_v<T, wxl_vec3>) return got.x == want.x && got.z == want.z;
        else return got == want;
    }

    // --- wxl.action -------------------------------------------------------------------------
    //
    // Every call is recorded, with its arguments; an object the fake objects do not hold is
    // gone, and a spell id of 0 is one the client refuses.

    struct FakeActions
    {
        struct Call
        {
            std::string name;
            wxl_handle  object = WXL_HANDLE_NONE;
            wxl_handle  target = WXL_HANDLE_NONE;
            uint32_t    spell  = 0;
            wxl_vec3    point{};
        };

        static std::vector<Call>& Calls()
        {
            static std::vector<Call> calls;
            return calls;
        }

        static wxl_result Record(const char* name, wxl_handle object, wxl_handle target = WXL_HANDLE_NONE,
                                 uint32_t spell = 0, wxl_vec3 point = {})
        {
            Calls().push_back(Call{name, object, target, spell, point});
            return WXL_OK;
        }

        static wxl_result WXL_CALL Target(wxl_handle o) { return FakeObjects::Live(o) ? Record("target", o) : WXL_E_NOT_FOUND; }
        static wxl_result WXL_CALL ClearTarget() { return Record("clear_target", WXL_HANDLE_NONE); }
        static wxl_result WXL_CALL Interact(wxl_handle o)
        {
            return FakeObjects::Live(o) ? Record("interact", o) : WXL_E_NOT_FOUND;
        }
        static wxl_result WXL_CALL CastSpell(uint32_t spell, wxl_handle target)
        {
            if (!spell) return WXL_E_FAILED;
            return Record("cast_spell", WXL_HANDLE_NONE, target, spell);
        }
        static wxl_result WXL_CALL StopCasting() { return Record("stop_casting", WXL_HANDLE_NONE); }
        static wxl_result WXL_CALL UseItem(wxl_handle item, wxl_handle target)
        {
            if (!FakeObjects::Live(item)) return WXL_E_NOT_FOUND;
            return FakeObjects::Type(item) == WXL_OBJECT_ITEM ? Record("use_item", item, target) : WXL_E_INVALID;
        }
        static wxl_result WXL_CALL Follow(wxl_handle u)
        {
            if (!FakeObjects::Live(u)) return WXL_E_NOT_FOUND;
            return FakeObjects::IsUnit(u) ? Record("follow", u) : WXL_E_INVALID;
        }
        static wxl_result WXL_CALL Assist(wxl_handle u)
        {
            if (!FakeObjects::Live(u)) return WXL_E_NOT_FOUND;
            return FakeObjects::IsUnit(u) ? Record("assist", u) : WXL_E_INVALID;
        }
        static wxl_result WXL_CALL MoveTo(wxl_vec3 point) { return Record("move_to", WXL_HANDLE_NONE, WXL_HANDLE_NONE, 0, point); }
        static wxl_result WXL_CALL Face(double facing)
        {
            return Record("face", WXL_HANDLE_NONE, WXL_HANDLE_NONE, 0, wxl_vec3{static_cast<float>(facing), 0.0f, 0.0f});
        }

        static const wxl_action_api* Table()
        {
            static const wxl_action_api api = [] {
                wxl_action_api t{};
                t.struct_size  = sizeof t;
                t.target       = &Target;
                t.clear_target = &ClearTarget;
                t.interact     = &Interact;
                t.cast_spell   = &CastSpell;
                t.stop_casting = &StopCasting;
                t.use_item     = &UseItem;
                t.follow       = &Follow;
                t.assist       = &Assist;
                t.move_to      = &MoveTo;
                t.face         = &Face;
                return t;
            }();
            return &api;
        }
    };

    // --- wxl.camera, wxl.input, wxl.world ------------------------------------------------------
    //
    // A camera at (1, 2, 3) looking north with a field of view of 1.5; the views set and
    // released, the captures and the traces are recorded. A wall stands at x = 10.

    struct FakeView
    {
        struct Call
        {
            std::string name;
            wxl_vec3    position{};
            wxl_vec3    forward{};
            double      fov  = 0.0;
            double      roll = 0.0;
        };

        static std::vector<Call>& Calls()
        {
            static std::vector<Call> calls;
            return calls;
        }

        static int& Captures()
        {
            static int held = 0;
            return held;
        }

        static wxl_result WXL_CALL Position(wxl_vec3* out) { *out = wxl_vec3{1.0f, 2.0f, 3.0f}; return WXL_OK; }
        static wxl_result WXL_CALL Forward(wxl_vec3* out) { *out = wxl_vec3{1.0f, 0.0f, 0.0f}; return WXL_OK; }
        static wxl_result WXL_CALL Up(wxl_vec3* out) { *out = wxl_vec3{0.0f, 0.0f, 1.0f}; return WXL_OK; }
        static wxl_result WXL_CALL Fov(double* out) { *out = 1.5; return WXL_OK; }
        static wxl_result WXL_CALL Pivot(wxl_vec3* out) { *out = wxl_vec3{5.0f, 5.0f, 1.75f}; return WXL_OK; }
        static wxl_bool WXL_CALL Cinematic() { return WXL_FALSE; }
        static wxl_result WXL_CALL SetView(wxl_vec3 position, wxl_vec3 forward, double fov)
        {
            if (fov < 0.0) return WXL_E_INVALID;
            Calls().push_back(Call{"set_view", position, forward, fov});
            return WXL_OK;
        }
        static wxl_result WXL_CALL ReleaseView()
        {
            Calls().push_back(Call{"release_view"});
            return WXL_OK;
        }
        static wxl_result WXL_CALL SetViewRolled(wxl_vec3 position, wxl_vec3 forward, double fov, double roll)
        {
            if (fov < 0.0) return WXL_E_INVALID;
            Calls().push_back(Call{"set_view_rolled", position, forward, fov, roll});
            return WXL_OK;
        }
        static wxl_result WXL_CALL Roll(double* out) { *out = 0.25; return WXL_OK; }
        /// Where the fake holds the pointer, x and y; z is 1 while captured.
        static wxl_vec3& Point()
        {
            static wxl_vec3 point{};
            return point;
        }
        static wxl_result WXL_CALL CaptureCursorAt(double x, double y)
        {
            if (!(x >= 0.0 && x <= 1.0 && y >= 0.0 && y <= 1.0)) return WXL_E_INVALID;
            Calls().push_back(Call{"capture_at", {static_cast<float>(x), static_cast<float>(y), 0.0f}});
            Captures() = 1;
            Point()    = wxl_vec3{static_cast<float>(x), static_cast<float>(y), 0.0f};
            return WXL_OK;
        }
        static wxl_result WXL_CALL CapturePoint(wxl_vec3* out)
        {
            if (!Captures()) return WXL_E_NOT_FOUND;
            *out = Point();
            return WXL_OK;
        }

        static wxl_result WXL_CALL CaptureCursor(wxl_bool captured)
        {
            Calls().push_back(Call{captured ? "capture" : "free"});
            Captures() = captured ? 1 : 0;
            Point()    = wxl_vec3{0.5f, 0.5f, 0.0f};
            return WXL_OK;
        }
        static wxl_bool WXL_CALL CursorCaptured() { return Captures() ? WXL_TRUE : WXL_FALSE; }
        static wxl_bool WXL_CALL PointerOverWorld() { return WXL_TRUE; }
        static int& Blocks()
        {
            static int blocked = 0;
            return blocked;
        }
        static wxl_result WXL_CALL BlockMouseLook(wxl_bool blocked)
        {
            Calls().push_back(Call{blocked ? "block" : "unblock"});
            Blocks() = blocked ? 1 : 0;
            return WXL_OK;
        }
        static wxl_bool WXL_CALL MouseLookBlocked() { return Blocks() ? WXL_TRUE : WXL_FALSE; }
        static wxl_result WXL_CALL Head(wxl_handle unit, double yaw, double pitch, double smoothing)
        {
            if (!FakeObjects::Live(unit)) return WXL_E_NOT_FOUND;
            Calls().push_back(Call{"head", {static_cast<float>(yaw), static_cast<float>(pitch), static_cast<float>(smoothing)},
                                   {}, static_cast<double>(unit)});
            return WXL_OK;
        }
        static wxl_result WXL_CALL ReleaseHead(wxl_handle unit)
        {
            Calls().push_back(Call{"release_head", {}, {}, static_cast<double>(unit)});
            return WXL_OK;
        }
        // wxl.highlight: drawn, with the colour of every set kept apart (a float would round it).
        static std::vector<uint32_t>& HighlightColors()
        {
            static std::vector<uint32_t> colors;
            return colors;
        }
        static wxl_bool WXL_CALL HighlightSupported() { return WXL_TRUE; }
        static wxl_str WXL_CALL HighlightUnsupportedReason() { return wxl_str{"", 0}; }
        static wxl_result WXL_CALL HighlightSet(wxl_handle target, uint32_t color, float outline, float glow, float strength)
        {
            if (!FakeObjects::Live(target)) return WXL_E_NOT_FOUND;
            if (!(outline >= 0.0f) || !(glow >= 0.0f) || !(strength >= 0.0f)) return WXL_E_INVALID;
            Calls().push_back(Call{"highlight_set", {outline, glow, strength}, {}, static_cast<double>(target)});
            HighlightColors().push_back(color);
            return WXL_OK;
        }
        static wxl_result WXL_CALL HighlightClear(wxl_handle target)
        {
            Calls().push_back(Call{"highlight_clear", {}, {}, static_cast<double>(target)});
            return WXL_OK;
        }
        static void WXL_CALL HighlightClearAll() { Calls().push_back(Call{"highlight_clear_all", {}, {}, 0.0}); }
        static uint32_t WXL_CALL HighlightDrawn() { return 7; }
        // wxl.selectioncircle: every call kept whole, with its masks, texture and colour.
        struct CircleCall
        {
            std::string name;
            uint32_t    kinds = 0, reactions = 0;
            std::string texture;
            uint32_t    color = 0;
            float       scale = 0.0f;
            wxl_handle  target = WXL_HANDLE_NONE;
        };
        static std::vector<CircleCall>& CircleCalls()
        {
            static std::vector<CircleCall> calls;
            return calls;
        }
        static bool CircleMasks(uint32_t kinds, uint32_t reactions)
        {
            return !(kinds & ~WXL_SELECTIONCIRCLE_ALL_KINDS) && !(reactions & ~WXL_SELECTIONCIRCLE_ALL_REACTIONS);
        }
        static bool CircleScale(float scale) { return scale >= 0.0f && scale <= WXL_SELECTIONCIRCLE_MAX_SCALE; }
        static wxl_result WXL_CALL CircleHide(uint32_t kinds, uint32_t reactions)
        {
            if (!CircleMasks(kinds, reactions)) return WXL_E_INVALID;
            CircleCalls().push_back(CircleCall{"hide", kinds, reactions, {}, 0, 0.0f, WXL_HANDLE_NONE});
            return WXL_OK;
        }
        static wxl_result WXL_CALL CircleSetStyle(uint32_t kinds, uint32_t reactions, wxl_str texture, uint32_t color, float scale)
        {
            if (!CircleMasks(kinds, reactions) || !CircleScale(scale)) return WXL_E_INVALID;
            CircleCalls().push_back(CircleCall{"set_style", kinds, reactions, std::string(texture.data ? texture.data : "", texture.size), color, scale, WXL_HANDLE_NONE});
            return WXL_OK;
        }
        static wxl_result WXL_CALL CircleReset(uint32_t kinds, uint32_t reactions)
        {
            if (!CircleMasks(kinds, reactions)) return WXL_E_INVALID;
            CircleCalls().push_back(CircleCall{"reset", kinds, reactions, {}, 0, 0.0f, WXL_HANDLE_NONE});
            return WXL_OK;
        }
        static wxl_result WXL_CALL CircleHideObject(wxl_handle target)
        {
            if (!FakeObjects::Live(target)) return WXL_E_NOT_FOUND;
            CircleCalls().push_back(CircleCall{"hide_object", 0, 0, {}, 0, 0.0f, target});
            return WXL_OK;
        }
        static wxl_result WXL_CALL CircleSetObjectStyle(wxl_handle target, wxl_str texture, uint32_t color, float scale)
        {
            if (!FakeObjects::Live(target)) return WXL_E_NOT_FOUND;
            if (!CircleScale(scale)) return WXL_E_INVALID;
            CircleCalls().push_back(CircleCall{"set_object_style", 0, 0, std::string(texture.data ? texture.data : "", texture.size), color, scale, target});
            return WXL_OK;
        }
        static wxl_result WXL_CALL CircleResetObject(wxl_handle target)
        {
            CircleCalls().push_back(CircleCall{"reset_object", 0, 0, {}, 0, 0.0f, target});
            return WXL_OK;
        }
        static void WXL_CALL CircleResetAll() { CircleCalls().push_back(CircleCall{"reset_all", 0, 0, {}, 0, 0.0f, WXL_HANDLE_NONE}); }
        static uint32_t WXL_CALL CircleDrawn() { return 2; }
        static uint32_t WXL_CALL CircleHidden() { return 1; }
        // wxl.texture: every request kept whole; a source naming "missing" is no file.
        struct TextureGroupCall
        {
            std::string id;
            uint32_t    set = 0, color = 0;
            float       width   = 0.0f;
            bool        visible = true;
        };
        struct TextureCall
        {
            std::string                   source;
            uint32_t                      width = 0, height = 0, tint = 0;
            std::vector<TextureGroupCall> groups;
        };
        static std::vector<TextureCall>& TextureCalls()
        {
            static std::vector<TextureCall> calls;
            return calls;
        }
        static wxl_result WXL_CALL TextureMake(const wxl_texture_request* request, wxl_texture* out)
        {
            if (!request || !out) return WXL_E_INVALID;
            TextureCall call;
            call.source.assign(request->source.data ? request->source.data : "", request->source.size);
            call.width  = request->width;
            call.height = request->height;
            call.tint   = request->tint;
            for (uint32_t i = 0; i < request->group_count; ++i)
            {
                const wxl_texture_group& g = request->groups[i];
                call.groups.push_back(TextureGroupCall{std::string(g.id.data, g.id.size), g.set, g.color, g.width,
                                                       g.visible != WXL_FALSE});
            }
            TextureCalls().push_back(call);
            if (call.source.find("missing") != std::string::npos)
            {
                std::snprintf(out->reason, sizeof out->reason, "no file %s", call.source.c_str());
                return WXL_E_NOT_FOUND;
            }
            std::snprintf(out->path, sizeof out->path, "Interface\\WXL\\fake\\made_%ux%u.blp", call.width, call.height);
            out->width          = call.width;
            out->height         = call.height;
            out->texture_width  = 64;
            out->texture_height = 32;
            out->right          = static_cast<float>(call.width) / 64.0f;
            out->bottom         = static_cast<float>(call.height) / 32.0f;
            out->reused         = TextureCalls().size() > 1 ? WXL_TRUE : WXL_FALSE;
            return WXL_OK;
        }
        static wxl_result WXL_CALL TextureStats(wxl_texture_stats* out)
        {
            if (!out) return WXL_E_INVALID;
            out->textures      = 3;
            out->resident      = 2;
            out->texture_limit = 2048;
            out->bytes         = 4096;
            out->byte_limit    = 64u << 20;
            out->made          = 3;
            out->reused        = 5;
            out->released      = 1;
            return WXL_OK;
        }
        static wxl_str WXL_CALL ImGuiVersion() { return wxl_str{"1.0;0.1", 7}; }
        static void* WXL_CALL ImGuiContext() { return reinterpret_cast<void*>(0x1000); }
        static void* WXL_CALL PlotContext() { return reinterpret_cast<void*>(0x2000); }
        static void WXL_CALL Allocators(void** alloc, void** release, void** user)
        {
            if (alloc) *alloc = reinterpret_cast<void*>(0x3000);
            if (release) *release = reinterpret_cast<void*>(0x4000);
            if (user) *user = nullptr;
        }

        static wxl_bool WXL_CALL Trace(wxl_vec3 start, wxl_vec3 finish, uint32_t flags, wxl_vec3* hit)
        {
            Calls().push_back(Call{"trace", start, finish, static_cast<double>(flags)});
            if ((start.x < 10.0f) == (finish.x < 10.0f)) return WXL_FALSE;
            *hit = wxl_vec3{10.0f, start.y, start.z};
            return WXL_TRUE;
        }

        static const wxl_camera_api* CameraTable()
        {
            static const wxl_camera_api api = [] {
                wxl_camera_api t{};
                t.struct_size  = sizeof t;
                t.position     = &Position;
                t.forward      = &Forward;
                t.up           = &Up;
                t.fov          = &Fov;
                t.pivot        = &Pivot;
                t.cinematic    = &Cinematic;
                t.set_view     = &SetView;
                t.release_view    = &ReleaseView;
                t.set_view_rolled = &SetViewRolled;
                t.roll            = &Roll;
                return t;
            }();
            return &api;
        }

        static const wxl_input_api* InputTable()
        {
            static const wxl_input_api api = [] {
                wxl_input_api t{};
                t.struct_size        = sizeof t;
                t.capture_cursor     = &CaptureCursor;
                t.cursor_captured    = &CursorCaptured;
                t.pointer_over_world = &PointerOverWorld;
                t.block_mouse_look   = &BlockMouseLook;
                t.mouse_look_blocked = &MouseLookBlocked;
                t.capture_cursor_at  = &CaptureCursorAt;
                t.capture_point      = &CapturePoint;
                return t;
            }();
            return &api;
        }

        static const wxl_pose_api* PoseTable()
        {
            static const wxl_pose_api api = [] {
                wxl_pose_api t{};
                t.struct_size  = sizeof t;
                t.head         = &Head;
                t.release_head = &ReleaseHead;
                return t;
            }();
            return &api;
        }

        static const wxl_highlight_api* HighlightTable()
        {
            static const wxl_highlight_api api = [] {
                wxl_highlight_api t{};
                t.struct_size        = sizeof t;
                t.supported          = &HighlightSupported;
                t.unsupported_reason = &HighlightUnsupportedReason;
                t.set                = &HighlightSet;
                t.clear              = &HighlightClear;
                t.clear_all          = &HighlightClearAll;
                t.drawn              = &HighlightDrawn;
                return t;
            }();
            return &api;
        }

        // wxl.cvar: variables kept by name without case, every register and release kept whole.
        struct FakeCVar
        {
            std::string value, defaultValue, help;
            uint32_t    flags = 0;
            double      min = 0.0, max = 0.0;
        };
        struct CVarCall
        {
            std::string what, name, value, help;
            uint32_t    flags = 0;
        };
        static std::map<std::string, FakeCVar>& CVars()
        {
            static std::map<std::string, FakeCVar> vars;
            return vars;
        }
        static std::vector<CVarCall>& CVarCalls()
        {
            static std::vector<CVarCall> calls;
            return calls;
        }
        static std::string CVarKey(wxl_str name)
        {
            std::string key(name.data ? name.data : "", name.size);
            for (char& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return key;
        }
        static FakeCVar* FindCVar(wxl_str name)
        {
            const auto found = CVars().find(CVarKey(name));
            return found == CVars().end() ? nullptr : &found->second;
        }
        static wxl_result WXL_CALL CVarRegister(wxl_str name, wxl_str value, uint32_t flags, wxl_str help)
        {
            if (!name.size || (flags & ~WXL_CVAR_ALL_FLAGS)) return WXL_E_INVALID;
            CVarCalls().push_back(CVarCall{"register", std::string(name.data, name.size), std::string(value.data, value.size),
                                           std::string(help.data ? help.data : "", help.size), flags});
            FakeCVar& var = CVars()[CVarKey(name)];
            if (var.defaultValue.empty() && var.value.empty()) var.value = std::string(value.data, value.size);
            var.defaultValue = std::string(value.data, value.size);
            var.help         = std::string(help.data ? help.data : "", help.size);
            var.flags        = flags | WXL_CVAR_MOD;
            return WXL_OK;
        }
        static wxl_result WXL_CALL CVarRelease(wxl_str name)
        {
            CVarCalls().push_back(CVarCall{"release", std::string(name.data, name.size), {}, {}, 0});
            return FindCVar(name) ? WXL_OK : WXL_E_NOT_FOUND;
        }
        static wxl_result WXL_CALL CVarSetRange(wxl_str name, double min, double max)
        {
            FakeCVar* var = FindCVar(name);
            if (!var) return WXL_E_NOT_FOUND;
            if (!(min <= max)) return WXL_E_INVALID;
            var->min = min;
            var->max = max;
            return WXL_OK;
        }
        static wxl_bool WXL_CALL CVarExists(wxl_str name) { return FindCVar(name) ? WXL_TRUE : WXL_FALSE; }
        static wxl_result WXL_CALL CVarGet(wxl_str name, wxl_str* out)
        {
            const FakeCVar* var = FindCVar(name);
            if (!var) return WXL_E_NOT_FOUND;
            *out = wxl_str{var->value.c_str(), static_cast<uint32_t>(var->value.size())};
            return WXL_OK;
        }
        static wxl_result WXL_CALL CVarGetNumber(wxl_str name, double* out)
        {
            const FakeCVar* var = FindCVar(name);
            if (!var) return WXL_E_NOT_FOUND;
            char* end = nullptr;
            *out      = std::strtod(var->value.c_str(), &end);
            return end && !*end && end != var->value.c_str() ? WXL_OK : WXL_E_INVALID;
        }
        static wxl_result WXL_CALL CVarGetBool(wxl_str name, wxl_bool* out)
        {
            const FakeCVar* var = FindCVar(name);
            if (!var) return WXL_E_NOT_FOUND;
            *out = !var->value.empty() && var->value[0] >= '1' && var->value[0] <= '9' ? WXL_TRUE : WXL_FALSE;
            return WXL_OK;
        }
        static wxl_result WXL_CALL CVarGetDefault(wxl_str name, wxl_str* out)
        {
            const FakeCVar* var = FindCVar(name);
            if (!var) return WXL_E_NOT_FOUND;
            *out = wxl_str{var->defaultValue.c_str(), static_cast<uint32_t>(var->defaultValue.size())};
            return WXL_OK;
        }
        static wxl_result WXL_CALL CVarSet(wxl_str name, wxl_str value)
        {
            FakeCVar* var = FindCVar(name);
            if (!var) return WXL_E_NOT_FOUND;
            const std::string text(value.data ? value.data : "", value.size);
            if (var->min != var->max)
            {
                char*        end    = nullptr;
                const double number = std::strtod(text.c_str(), &end);
                if (!end || *end || end == text.c_str() || number < var->min || number > var->max) return WXL_E_INVALID;
            }
            var->value = text;
            return WXL_OK;
        }
        static wxl_result WXL_CALL CVarSetNumber(wxl_str name, double value)
        {
            const std::string text = std::to_string(value);
            return CVarSet(name, wxl_str{text.c_str(), static_cast<uint32_t>(text.size())});
        }
        static wxl_result WXL_CALL CVarSetBool(wxl_str name, wxl_bool value)
        {
            return CVarSet(name, wxl_str{value ? "1" : "0", 1});
        }
        static wxl_result WXL_CALL CVarReset(wxl_str name)
        {
            FakeCVar* var = FindCVar(name);
            if (!var) return WXL_E_NOT_FOUND;
            var->value = var->defaultValue;
            return WXL_OK;
        }
        static wxl_result WXL_CALL CVarFlags(wxl_str name, uint32_t* out)
        {
            const FakeCVar* var = FindCVar(name);
            if (!var) return WXL_E_NOT_FOUND;
            *out = var->flags;
            return WXL_OK;
        }

        static const wxl_cvar_api* CVarTable()
        {
            static const wxl_cvar_api api = [] {
                wxl_cvar_api t{};
                t.struct_size   = sizeof t;
                t.register_cvar = &CVarRegister;
                t.release       = &CVarRelease;
                t.set_range     = &CVarSetRange;
                t.exists        = &CVarExists;
                t.get           = &CVarGet;
                t.get_number    = &CVarGetNumber;
                t.get_bool      = &CVarGetBool;
                t.get_default   = &CVarGetDefault;
                t.set           = &CVarSet;
                t.set_number    = &CVarSetNumber;
                t.set_bool      = &CVarSetBool;
                t.reset         = &CVarReset;
                t.flags         = &CVarFlags;
                return t;
            }();
            return &api;
        }

        static const wxl_texture_api* TextureTable()
        {
            static const wxl_texture_api api = [] {
                wxl_texture_api t{};
                t.struct_size = sizeof t;
                t.make        = &TextureMake;
                t.stats       = &TextureStats;
                return t;
            }();
            return &api;
        }

        static const wxl_selectioncircle_api* SelectionCircleTable()
        {
            static const wxl_selectioncircle_api api = [] {
                wxl_selectioncircle_api t{};
                t.struct_size      = sizeof t;
                t.hide             = &CircleHide;
                t.set_style        = &CircleSetStyle;
                t.reset            = &CircleReset;
                t.hide_object      = &CircleHideObject;
                t.set_object_style = &CircleSetObjectStyle;
                t.reset_object     = &CircleResetObject;
                t.reset_all        = &CircleResetAll;
                t.drawn            = &CircleDrawn;
                t.hidden           = &CircleHidden;
                return t;
            }();
            return &api;
        }

        static const wxl_imgui_api* ImGuiTable()
        {
            static const wxl_imgui_api api = [] {
                wxl_imgui_api t{};
                t.struct_size  = sizeof t;
                t.version      = &ImGuiVersion;
                t.context      = &ImGuiContext;
                t.plot_context = &PlotContext;
                t.allocators   = &Allocators;
                return t;
            }();
            return &api;
        }

        static const wxl_world_api* WorldTable()
        {
            static const wxl_world_api api = [] {
                wxl_world_api t{};
                t.struct_size = sizeof t;
                t.trace       = &Trace;
                return t;
            }();
            return &api;
        }
    };

    // --- wxl.script -------------------------------------------------------------------------
    //
    // A context is live from the start, the game's, so a test that only registers needs nothing
    // more. Open and Close drive the lifecycle a runtime has: Open raises ScriptVmReady for a new,
    // empty context; Close raises ScriptVmClosing while the context still holds what was
    // registered, then forgets it all. With no context live, every call that needs one answers
    // WXL_E_FAILED, as script.h says.

    struct FakeScript
    {
        /// How a test hands an event to the mods: its host's Raise.
        using Raise = std::function<void(wxl_event event, const void* args)>;

        static constexpr int kNone = -1;

        /// The live context, a wxl_script_context value, or kNone between two of them.
        static int& Live()
        {
            static int live = WXL_SCRIPT_CONTEXT_GAME;
            return live;
        }

        /// The event names declared on the live context.
        static std::set<std::string>& Declared()
        {
            static std::set<std::string> declared;
            return declared;
        }

        /// Opens a new context: empty, live, then announced with ScriptVmReady.
        static void Open(wxl_script_context context, const Raise& raise)
        {
            Functions().clear();
            Declared().clear();
            Live() = context;
            const wxl_script_vm_args args{static_cast<uint32_t>(context), nullptr};
            if (raise) raise(WXL_EV_ScriptVmReady, &args);
        }

        /// Closes the live context: ScriptVmClosing first, then everything registered goes.
        static void Close(const Raise& raise)
        {
            if (Live() == kNone) return;
            const wxl_script_vm_args args{static_cast<uint32_t>(Live()), nullptr};
            if (raise) raise(WXL_EV_ScriptVmClosing, &args);
            Functions().clear();
            Declared().clear();
            Live() = kNone;
        }

        struct Registered
        {
            wxl_script_fn fn;
            void*         user;
        };

        static std::map<std::string, Registered>& Functions()
        {
            static std::map<std::string, Registered> functions;
            return functions;
        }

        static std::vector<std::string>& Events()
        {
            static std::vector<std::string> events;
            return events;
        }

        static std::vector<std::string>& Chunks()
        {
            static std::vector<std::string> chunks;
            return chunks;
        }

        /// The live context; the glue's between two, where live() tells it from a live one.
        static uint32_t WXL_CALL Context()
        {
            return Live() == kNone ? WXL_SCRIPT_CONTEXT_GLUE : static_cast<uint32_t>(Live());
        }

        /// Every call that needs a context and came while none was live, which a well-behaved
        /// caller never makes.
        static int& Refused()
        {
            static int refused = 0;
            return refused;
        }

        static wxl_bool WXL_CALL IsLive() { return Live() == kNone ? WXL_FALSE : WXL_TRUE; }

        static wxl_result WXL_CALL RegisterFunction(const char* name, wxl_script_fn fn, void* user)
        {
            if (Live() == kNone) return ++Refused(), WXL_E_FAILED;
            if (!name || !fn) return WXL_E_INVALID;
            if (Functions().count(name)) return WXL_E_INVALID;
            Functions()[name] = Registered{fn, user};
            return WXL_OK;
        }

        static wxl_result WXL_CALL RegisterEvent(const char* name)
        {
            if (Live() == kNone) return ++Refused(), WXL_E_FAILED;
            Declared().insert(name);
            Events().push_back(name);
            return WXL_OK;
        }

        static wxl_result WXL_CALL FireEvent(const char* name, const wxl_value* args, uint32_t count)
        {
            if (Live() == kNone) return WXL_E_FAILED;
            if (!Declared().count(name)) return WXL_E_NOT_FOUND;
            std::string line = name;
            for (uint32_t i = 0; i < count; ++i)
            {
                line += args[i].type == WXL_VALUE_NUMBER ? " n" : args[i].type == WXL_VALUE_STRING ? " s"
                                                           : args[i].type == WXL_VALUE_BOOL     ? " b"
                                                                                                : " -";
            }
            Events().push_back(line);
            return WXL_OK;
        }

        static wxl_result WXL_CALL Run(wxl_str chunk, const char* name)
        {
            if (Live() == kNone) return WXL_E_FAILED;
            Chunks().push_back(std::string(name) + ": " + std::string(chunk.data, chunk.size));
            return WXL_OK;
        }

        static uint32_t WXL_CALL ArgCount(wxl_script_call* call)
        {
            return static_cast<uint32_t>(call->args.size());
        }

        static const wxl_value* Arg(wxl_script_call* call, uint32_t index, uint32_t type)
        {
            if (index >= call->args.size() || call->args[index].type != type) return nullptr;
            return &call->args[index];
        }

        static wxl_bool WXL_CALL ArgNumber(wxl_script_call* call, uint32_t index, double* out)
        {
            const wxl_value* value = Arg(call, index, WXL_VALUE_NUMBER);
            if (!value) return WXL_FALSE;
            *out = value->as.number;
            return WXL_TRUE;
        }

        static wxl_bool WXL_CALL ArgString(wxl_script_call* call, uint32_t index, wxl_str* out)
        {
            const wxl_value* value = Arg(call, index, WXL_VALUE_STRING);
            if (!value) return WXL_FALSE;
            *out = value->as.string;
            return WXL_TRUE;
        }

        static wxl_bool WXL_CALL ArgBool(wxl_script_call* call, uint32_t index, wxl_bool* out)
        {
            const wxl_value* value = Arg(call, index, WXL_VALUE_BOOL);
            if (!value) return WXL_FALSE;
            *out = value->as.boolean;
            return WXL_TRUE;
        }

        static uint32_t WXL_CALL ArgType(wxl_script_call* call, uint32_t index)
        {
            return index < call->args.size() ? call->args[index].type : WXL_VALUE_NIL;
        }

        static void WXL_CALL PushNumber(wxl_script_call* call, double value)
        {
            wxl_value v{};
            v.type      = WXL_VALUE_NUMBER;
            v.as.number = value;
            call->results.push_back(v);
        }

        static void WXL_CALL PushString(wxl_script_call* call, wxl_str value)
        {
            wxl_value v{};
            v.type      = WXL_VALUE_STRING;
            v.as.string = value;
            call->results.push_back(v);
        }

        static void WXL_CALL PushBool(wxl_script_call* call, wxl_bool value)
        {
            wxl_value v{};
            v.type       = WXL_VALUE_BOOL;
            v.as.boolean = value;
            call->results.push_back(v);
        }

        static void WXL_CALL PushNil(wxl_script_call* call)
        {
            wxl_value v{};
            v.type = WXL_VALUE_NIL;
            call->results.push_back(v);
        }

        static void WXL_CALL Fail(wxl_script_call* call, wxl_str message)
        {
            call->error = std::string(message.data, message.size);
        }

        /// Invokes a registered function the way the VM would.
        static bool Invoke(const char* name, wxl_script_call& call)
        {
            const auto found = Functions().find(name);
            if (found == Functions().end()) return false;
            found->second.fn(&call, found->second.user);
            return true;
        }

        static const wxl_script_api* Table()
        {
            static wxl_script_api api = [] {
                wxl_script_api t{};
                t.struct_size       = sizeof(wxl_script_api);
                t.context           = &Context;
                t.register_function = &RegisterFunction;
                t.register_event    = &RegisterEvent;
                t.fire_event        = &FireEvent;
                t.run               = &Run;
                t.arg_count         = &ArgCount;
                t.arg_number        = &ArgNumber;
                t.arg_string        = &ArgString;
                t.arg_bool          = &ArgBool;
                t.arg_type          = &ArgType;
                t.push_number       = &PushNumber;
                t.push_string       = &PushString;
                t.push_bool         = &PushBool;
                t.push_nil          = &PushNil;
                t.fail              = &Fail;
                t.live              = &IsLive;
                return t;
            }();
            return &api;
        }
    };

    // --- wxl.storage ------------------------------------------------------------------------
    //
    // Two files of the client's own, and the folders a mod adds, read from the disk: a name found
    // in a folder of a priority above 0 wins over the client's, one below only fills a gap.

    struct FakeStorage
    {
        /// The folders added, in the order they were.
        static std::vector<std::pair<std::string, int32_t>>& SearchPaths()
        {
            static std::vector<std::pair<std::string, int32_t>> paths;
            return paths;
        }

        /// Files opened from a folder, by handle; their handles start above the client's.
        static std::map<wxl_handle, std::string>& OpenFromDisk()
        {
            static std::map<wxl_handle, std::string> open;
            return open;
        }

        static constexpr wxl_handle kDiskHandles = 1000;

        static bool Slurp(const std::filesystem::path& file, std::string& out)
        {
            std::error_code ec;
            if (!std::filesystem::is_regular_file(file, ec)) return false;
            std::ifstream      stream(file, std::ios::binary);
            std::ostringstream text;
            text << stream.rdbuf();
            out = text.str();
            return true;
        }

        /// The file a folder serves for a client name, the highest priority first: above 0
        /// when @p winning, otherwise any.
        static bool FromFolder(const std::string& name, bool winning, std::string& out)
        {
            std::string relative = name;
            for (char& c : relative)
                if (c == '\\') c = '/';
            const std::pair<std::string, int32_t>* best = nullptr;
            for (const auto& folder : SearchPaths())
            {
                if (winning && folder.second <= 0) continue;
                std::error_code ec;
                if (!std::filesystem::is_regular_file(std::filesystem::path(folder.first) / relative, ec)) continue;
                if (!best || folder.second > best->second) best = &folder;
            }
            return best && Slurp(std::filesystem::path(best->first) / relative, out);
        }

        /// The names redirected, as the runtime holds them: one target per name, by its key.
        static std::map<std::string, std::string>& Redirects()
        {
            static std::map<std::string, std::string> redirects;
            return redirects;
        }

        static std::string NameKey(std::string name)
        {
            for (char& c : name) c = c == '/' ? '\\' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return name;
        }

        /// The name an open of @p name reaches: its redirect, or itself.
        static std::string Redirected(const std::string& name)
        {
            const auto found = Redirects().find(NameKey(name));
            return found != Redirects().end() ? found->second : name;
        }

        /// The bytes served under each name, as the runtime holds them: one set per name, by its key.
        static std::map<std::string, std::string>& Served()
        {
            static std::map<std::string, std::string> served;
            return served;
        }

        /// What a name reads as: served bytes, a winning folder's file, the client's, or a folder's.
        static bool Contents(const std::string& asked, std::string& out)
        {
            const std::string name = Redirected(asked);
            const auto        served = Served().find(NameKey(name));
            if (served != Served().end())
            {
                out = served->second;
                return true;
            }
            if (FromFolder(name, true, out)) return true;
            const auto found = Files().find(name);
            if (found != Files().end())
            {
                out = found->second;
                return true;
            }
            return FromFolder(name, false, out);
        }

        static std::map<std::string, std::string>& Files()
        {
            static std::map<std::string, std::string> files = {
                {"Interface\\hello.txt", "hello"},
                {"Interface\\big.bin", std::string(1000, 'x')},
            };
            return files;
        }

        static int& Opens()
        {
            static int opens = 0;
            return opens;
        }

        static int& Closes()
        {
            static int closes = 0;
            return closes;
        }

        static std::string Key(wxl_str path) { return std::string(path.data, path.size); }

        static wxl_bool WXL_CALL Exists(wxl_str path)
        {
            std::string ignored;
            return Contents(Key(path), ignored) ? WXL_TRUE : WXL_FALSE;
        }

        static wxl_result WXL_CALL Size(wxl_str path, uint64_t* out)
        {
            std::string data;
            if (!Contents(Key(path), data)) return WXL_E_NOT_FOUND;
            *out = data.size();
            return WXL_OK;
        }

        static wxl_result WXL_CALL Read(wxl_str path, void* buffer, uint64_t capacity, uint64_t* out_read)
        {
            std::string data;
            if (!Contents(Key(path), data)) return WXL_E_NOT_FOUND;
            *out_read = data.size();
            if (capacity < data.size()) return WXL_E_INVALID;
            if (!data.empty()) std::memcpy(buffer, data.data(), data.size());
            return WXL_OK;
        }

        static wxl_handle WXL_CALL Open(wxl_str asked)
        {
            const std::string redirected = Redirected(Key(asked));
            const wxl_str     path{redirected.c_str(), static_cast<uint32_t>(redirected.size())};
            std::string       data;
            if (FromFolder(Key(path), true, data) ||
                (!Files().count(Key(path)) && FromFolder(Key(path), false, data)))
            {
                ++Opens();
                const wxl_handle handle = kDiskHandles + OpenFromDisk().size() + 1;
                OpenFromDisk()[handle]  = data;
                return handle;
            }
            const auto found = Files().find(Key(path));
            if (found == Files().end()) return WXL_HANDLE_NONE;
            ++Opens();
            return static_cast<wxl_handle>(std::distance(Files().begin(), found) + 1);
        }

        static wxl_result WXL_CALL ReadAt(wxl_handle file, uint64_t offset, void* buffer, uint64_t size,
                                          uint64_t* out_read)
        {
            const auto disk = OpenFromDisk().find(file);
            if (file == WXL_HANDLE_NONE || (disk == OpenFromDisk().end() && file > Files().size()))
                return WXL_E_NOT_FOUND;
            auto it = Files().begin();
            if (disk == OpenFromDisk().end()) std::advance(it, static_cast<std::ptrdiff_t>(file - 1));
            const std::string& data = disk != OpenFromDisk().end() ? disk->second : it->second;
            if (offset >= data.size())
            {
                *out_read = 0;
                return WXL_OK;
            }
            const uint64_t n = data.size() - offset < size ? data.size() - offset : size;
            std::memcpy(buffer, data.data() + offset, static_cast<size_t>(n));
            *out_read = n;
            return WXL_OK;
        }

        static void WXL_CALL Close(wxl_handle file)
        {
            ++Closes();
            OpenFromDisk().erase(file);
        }

        /// A folder as this system writes it: a Windows path also works where '/' separates.
        static std::string Native(std::string path)
        {
#ifndef _WIN32
            for (char& c : path)
                if (c == '\\') c = '/';
#endif
            return path;
        }

        static wxl_result WXL_CALL AddSearchPath(wxl_str directory, int32_t priority)
        {
            const std::string folder = Native(Key(directory));
            std::error_code   ec;
            if (!std::filesystem::is_directory(folder, ec)) return WXL_E_NOT_FOUND;
            for (auto& known : SearchPaths())
                if (known.first == folder)
                {
                    known.second = priority;
                    return WXL_OK;
                }
            SearchPaths().emplace_back(folder, priority);
            return WXL_OK;
        }

        static wxl_result WXL_CALL RemoveSearchPath(wxl_str directory)
        {
            const std::string folder = Native(Key(directory));
            for (auto it = SearchPaths().begin(); it != SearchPaths().end(); ++it)
                if (it->first == folder)
                {
                    SearchPaths().erase(it);
                    return WXL_OK;
                }
            return WXL_E_NOT_FOUND;
        }

        static wxl_result WXL_CALL MountArchive(wxl_str, int32_t) { return WXL_OK; }

        static wxl_result WXL_CALL Redirect(wxl_str from, wxl_str to)
        {
            if (!from.size || !to.size) return WXL_E_INVALID;
            Redirects()[NameKey(Key(from))] = Key(to);
            return WXL_OK;
        }

        static wxl_result WXL_CALL Unredirect(wxl_str from)
        {
            return Redirects().erase(NameKey(Key(from))) ? WXL_OK : WXL_E_NOT_FOUND;
        }

        static wxl_result WXL_CALL Serve(wxl_str path, const void* data, uint32_t size, int32_t)
        {
            if (!path.size || (size && !data)) return WXL_E_INVALID;
            Served()[NameKey(Key(path))] = std::string(static_cast<const char*>(data), size);
            return WXL_OK;
        }

        static wxl_result WXL_CALL Unserve(wxl_str path)
        {
            return Served().erase(NameKey(Key(path))) ? WXL_OK : WXL_E_NOT_FOUND;
        }

        // --- hashes, overrides, reloads (0.1.23) ------------- ------------------------------
        //
        // A hash is no MD5 here: its sixteen bytes are the file's length, then 1, 2, 3...; the
        // runtime's MD5 is checked by core's harness. An asynchronous one waits for PumpHashes.

        static std::map<std::string, std::string>& Overrides()
        {
            static std::map<std::string, std::string> overrides;
            return overrides;
        }

        struct WaitingHash
        {
            uint64_t         request;
            std::string      name;
            wxl_file_hash_fn fn;
            void*            user;
        };

        static std::vector<WaitingHash>& WaitingHashes()
        {
            static std::vector<WaitingHash> waiting;
            return waiting;
        }

        static wxl_result Hashed(const std::string& asked, wxl_file_hash* out)
        {
            const auto  overridden = Overrides().find(NameKey(asked));
            std::string data;
            if (!Contents(overridden != Overrides().end() ? overridden->second : asked, data)) return WXL_E_NOT_FOUND;
            static const char kSource[] = "Data\\fake.MPQ";
            out->layer                  = WXL_FILE_LAYER_ARCHIVE;
            out->size                   = data.size();
            for (uint8_t i = 0; i < 16; ++i) out->md5[i] = i;
            out->md5[0] = static_cast<uint8_t>(data.size());
            out->source = wxl_str{kSource, static_cast<uint32_t>(sizeof kSource - 1)};
            return WXL_OK;
        }

        static wxl_result WXL_CALL HashFile(wxl_str path, uint32_t, wxl_file_hash* out)
        {
            if (!path.size || !out) return WXL_E_INVALID;
            return Hashed(Key(path), out);
        }

        static wxl_result WXL_CALL HashFileAsync(wxl_str path, uint32_t, wxl_file_hash_fn fn, void* user, uint64_t* out)
        {
            static uint64_t next = 100;
            if (!path.size || !fn || !out) return WXL_E_INVALID;
            *out = ++next;
            WaitingHashes().push_back(WaitingHash{*out, Key(path), fn, user});
            return WXL_OK;
        }

        /// Answers every hash asked asynchronously, as the runtime's main thread does.
        static void PumpHashes()
        {
            std::vector<WaitingHash> ready;
            ready.swap(WaitingHashes());
            for (const WaitingHash& waiting : ready)
            {
                wxl_file_hash    hash{};
                hash.struct_size        = sizeof hash;
                const wxl_result result = Hashed(waiting.name, &hash);
                waiting.fn(waiting.user, waiting.request, result, result == WXL_OK ? &hash : nullptr);
            }
        }

        static wxl_result WXL_CALL OverrideFile(wxl_str path, wxl_str with)
        {
            if (!path.size || !with.size) return WXL_E_INVALID;
            Overrides()[NameKey(Key(path))] = Key(with);
            return WXL_OK;
        }

        static wxl_result WXL_CALL RestoreFile(wxl_str path)
        {
            return Overrides().erase(NameKey(Key(path))) ? WXL_OK : WXL_E_NOT_FOUND;
        }

        static wxl_result WXL_CALL ReloadAsset(wxl_str path, wxl_asset_reload* out)
        {
            const std::string name = NameKey(Key(path));
            if (name.size() < 4 || name.compare(name.size() - 4, 4, ".blp") != 0) return WXL_E_UNSUPPORTED;
            wxl_asset_reload done{};
            done.struct_size = sizeof done;
            done.textures    = 3;
            done.busy        = 1;
            if (out) *out = done;
            return WXL_OK;
        }

        static uint32_t WXL_CALL Find(wxl_str pattern, wxl_str* out, uint32_t capacity)
        {
            const std::string prefix = Key(pattern).substr(0, Key(pattern).find('*'));
            uint32_t          count  = 0;
            for (const auto& entry : Files())
            {
                if (entry.first.compare(0, prefix.size(), prefix) != 0) continue;
                if (out && count < capacity)
                    out[count] = wxl_str{entry.first.c_str(), static_cast<uint32_t>(entry.first.size())};
                ++count;
            }
            return count;
        }

        static const wxl_storage_api* Table()
        {
            static wxl_storage_api api = [] {
                wxl_storage_api t{};
                t.struct_size     = sizeof(wxl_storage_api);
                t.exists          = &Exists;
                t.size            = &Size;
                t.read            = &Read;
                t.open            = &Open;
                t.read_at         = &ReadAt;
                t.close           = &Close;
                t.add_search_path = &AddSearchPath;
                t.mount_archive   = &MountArchive;
                t.remove_search_path = &RemoveSearchPath;
                t.find            = &Find;
                t.redirect        = &Redirect;
                t.unredirect      = &Unredirect;
                t.serve           = &Serve;
                t.unserve         = &Unserve;
                t.hash_file       = &HashFile;
                t.hash_file_async = &HashFileAsync;
                t.override_file   = &OverrideFile;
                t.restore_file    = &RestoreFile;
                t.reload_asset    = &ReloadAsset;
                return t;
            }();
            return &api;
        }
    };

    // --- wxl.relay --------------------------------------------------------------------------
    //
    // The runtime's relay in small: requests and answers queued, handed over at Pump.

    struct FakeRelay
    {
        struct Request
        {
            std::string            key;
            std::string            method;
            std::vector<wxl_value> args;
            std::vector<std::string> strings;
            wxl_relay_reply_fn     reply = nullptr;
            void*                  user  = nullptr;
            bool                   delivered = false;
            const wxl_relay_endpoint* to = nullptr;
        };

        struct Answer
        {
            uint64_t                 request;
            wxl_result               result;
            std::vector<wxl_value>   values;
            std::vector<std::string> strings;
        };

        static std::map<std::string, wxl_relay_endpoint>& Endpoints()
        {
            static std::map<std::string, wxl_relay_endpoint> endpoints;
            return endpoints;
        }

        static std::map<uint64_t, Request>& Requests()
        {
            static std::map<uint64_t, Request> requests;
            return requests;
        }

        static std::vector<uint64_t>& ToDeliver()
        {
            static std::vector<uint64_t> list;
            return list;
        }

        static std::vector<Answer>& Answers()
        {
            static std::vector<Answer> list;
            return list;
        }

        static std::vector<std::pair<const wxl_relay_endpoint*, uint64_t>>& Cancels()
        {
            static std::vector<std::pair<const wxl_relay_endpoint*, uint64_t>> list;
            return list;
        }

        static std::string Lower(wxl_str text)
        {
            std::string key(text.data, text.size);
            for (char& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return key;
        }

        /// Points the string values at the strings kept beside them, in order: after a move.
        static void Fix(std::vector<wxl_value>& values, std::vector<std::string>& strings)
        {
            size_t next = 0;
            for (wxl_value& value : values)
                if (value.type == WXL_VALUE_STRING && next < strings.size())
                {
                    value.as.string = wxl_str{strings[next].c_str(), static_cast<uint32_t>(strings[next].size())};
                    ++next;
                }
        }

        /// Values copied, their strings kept beside them.
        static void Copy(const wxl_value* values, uint32_t count, std::vector<wxl_value>& out, std::vector<std::string>& strings)
        {
            strings.reserve(count);
            for (uint32_t i = 0; i < count; ++i)
            {
                wxl_value value = values[i];
                if (value.type == WXL_VALUE_STRING)
                {
                    strings.emplace_back(value.as.string.data, value.as.string.size);
                    value.as.string = wxl_str{strings.back().c_str(), static_cast<uint32_t>(strings.back().size())};
                }
                out.push_back(value);
            }
        }

        static wxl_result WXL_CALL Publish(wxl_str name, const wxl_relay_endpoint* endpoint)
        {
            if (!name.size || !endpoint || !endpoint->request) return WXL_E_INVALID;
            Endpoints()[Lower(name)] = *endpoint;
            return WXL_OK;
        }

        static void FailFor(const std::string& key)
        {
            for (auto& [id, request] : Requests())
                if (request.key == key) Answers().push_back(Answer{id, WXL_E_NOT_FOUND, {}, {}});
        }

        static wxl_result WXL_CALL Unpublish(wxl_str name)
        {
            if (!Endpoints().erase(Lower(name))) return WXL_E_NOT_FOUND;
            FailFor(Lower(name));
            return WXL_OK;
        }

        static wxl_bool WXL_CALL Published(wxl_str name) { return Endpoints().count(Lower(name)) ? WXL_TRUE : WXL_FALSE; }

        static wxl_result WXL_CALL Call(wxl_str name, wxl_str method, const wxl_value* args, uint32_t count,
                                        wxl_relay_reply_fn reply, void* user, uint64_t* out)
        {
            static uint64_t next = 0;
            if (!name.size || !reply || !out) return WXL_E_INVALID;
            Request request;
            request.key    = Lower(name);
            request.method = std::string(method.data, method.size);
            request.reply  = reply;
            request.user   = user;
            Copy(args, count, request.args, request.strings);
            *out = ++next;
            Requests()[*out] = std::move(request);
            ToDeliver().push_back(*out);
            return WXL_OK;
        }

        static wxl_result WXL_CALL AnswerRequest(uint64_t id, const wxl_value* values, uint32_t count)
        {
            const auto it = Requests().find(id);
            if (it == Requests().end() || !it->second.delivered) return WXL_E_NOT_FOUND;
            Answer answer{id, WXL_OK, {}, {}};
            Copy(values, count, answer.values, answer.strings);
            Answers().push_back(std::move(answer));
            it->second.delivered = false;
            return WXL_OK;
        }

        static wxl_result WXL_CALL Fail(uint64_t id, wxl_str reason)
        {
            const auto it = Requests().find(id);
            if (it == Requests().end() || !it->second.delivered) return WXL_E_NOT_FOUND;
            wxl_value value{};
            value.type      = WXL_VALUE_STRING;
            value.as.string = reason;
            Answer answer{id, WXL_E_FAILED, {}, {}};
            Copy(&value, 1, answer.values, answer.strings);
            Answers().push_back(std::move(answer));
            it->second.delivered = false;
            return WXL_OK;
        }

        static wxl_result WXL_CALL Cancel(uint64_t id)
        {
            const auto it = Requests().find(id);
            if (it == Requests().end()) return WXL_E_NOT_FOUND;
            if (it->second.delivered && it->second.to) Cancels().emplace_back(it->second.to, id);
            Requests().erase(it);
            return WXL_OK;
        }

        /// Hands requests to their services, answers to their callers, cancellations: one round.
        static void Pump()
        {
            std::vector<uint64_t> deliver;
            deliver.swap(ToDeliver());
            for (uint64_t id : deliver)
            {
                const auto it = Requests().find(id);
                if (it == Requests().end()) continue;
                const auto endpoint = Endpoints().find(it->second.key);
                if (endpoint == Endpoints().end())
                {
                    Answers().push_back(Answer{id, WXL_E_NOT_FOUND, {}, {}});
                    continue;
                }
                it->second.delivered = true;
                it->second.to        = &endpoint->second;
                Request& request     = it->second;
                Fix(request.args, request.strings);
                endpoint->second.request(endpoint->second.user, id,
                                         wxl_str{request.method.c_str(), static_cast<uint32_t>(request.method.size())},
                                         request.args.data(), static_cast<uint32_t>(request.args.size()));
            }
            std::vector<Answer> answers;
            answers.swap(Answers());
            for (Answer& answer : answers)
            {
                const auto it = Requests().find(answer.request);
                if (it == Requests().end()) continue;
                const Request request = it->second;
                Requests().erase(it);
                Fix(answer.values, answer.strings);
                request.reply(request.user, answer.request, answer.result, answer.values.data(),
                              static_cast<uint32_t>(answer.values.size()));
            }
            std::vector<std::pair<const wxl_relay_endpoint*, uint64_t>> cancels;
            cancels.swap(Cancels());
            for (const auto& [endpoint, id] : cancels)
                if (endpoint->cancelled) endpoint->cancelled(endpoint->user, id);
        }

        static const wxl_relay_api* Table()
        {
            static wxl_relay_api api = [] {
                wxl_relay_api t{};
                t.struct_size = sizeof t;
                t.publish     = &Publish;
                t.unpublish   = &Unpublish;
                t.published   = &Published;
                t.call        = &Call;
                t.answer      = &AnswerRequest;
                t.fail        = &Fail;
                t.cancel      = &Cancel;
                return t;
            }();
            return &api;
        }
    };

    // --- wxl.data ---------------------------------------------------------------------------
    //
    // Spell.dbc is read already, every other table is not; a table is served from a path the
    // storage fake can read.

    struct FakeData
    {
        static std::map<std::string, std::string>& Served()
        {
            static std::map<std::string, std::string> served;
            return served;
        }

        static std::string Key(wxl_str table)
        {
            std::string key(table.data ? table.data : "", table.size);
            const size_t slash = key.find_last_of('\\');
            if (slash != std::string::npos) key = key.substr(slash + 1);
            for (char& c : key) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
            return key;
        }

        static wxl_result WXL_CALL ServeTable(wxl_str table, wxl_str path)
        {
            if (!table.size || !path.size) return WXL_E_INVALID;
            if (FakeStorage::Exists(path) != WXL_TRUE) return WXL_E_NOT_FOUND;
            if (Served().count(Key(table))) return WXL_E_FAILED;
            Served()[Key(table)] = std::string(path.data, path.size);
            return WXL_OK;
        }

        static wxl_result WXL_CALL RestoreTable(wxl_str table)
        {
            return Served().erase(Key(table)) ? WXL_OK : WXL_E_NOT_FOUND;
        }

        static wxl_str WXL_CALL ServedFrom(wxl_str table)
        {
            const auto found = Served().find(Key(table));
            if (found == Served().end()) return wxl_str{nullptr, 0};
            return wxl_str{found->second.c_str(), static_cast<uint32_t>(found->second.size())};
        }

        static wxl_bool WXL_CALL IsLoaded(wxl_str table)
        {
            return Key(table) == "spell.dbc" ? WXL_TRUE : WXL_FALSE;
        }

        static const wxl_data_api* Table()
        {
            static wxl_data_api api = [] {
                wxl_data_api t{};
                t.struct_size   = sizeof(wxl_data_api);
                t.serve_table   = &ServeTable;
                t.restore_table = &RestoreTable;
                t.served_from   = &ServedFrom;
                t.is_loaded     = &IsLoaded;
                return t;
            }();
            return &api;
        }
    };

    // --- wxl.footprint ----------------------------------------------------------------------
    //
    // A fixed picture of a client: two Storm sources, a few consumers, two archives, a database,
    // the address space of a 32-bit process running short. What mods report joins the
    // categories; the last threshold asked for is kept.

    struct FakeFootprint
    {
        struct Reported
        {
            uint64_t bytes;
            uint64_t count;
        };

        static bool&     Running() { static bool running = false; return running; }
        static uint32_t& Snapshots() { static uint32_t snapshots = 0; return snapshots; }
        static uint64_t& Threshold() { static uint64_t threshold = 0; return threshold; }
        static std::map<std::string, Reported>& Reports()
        {
            static std::map<std::string, Reported> reports;
            return reports;
        }

        static wxl_str S(const char* text) { return wxl_str{text, static_cast<uint32_t>(std::strlen(text))}; }

        static wxl_result WXL_CALL Start() { Running() = true; return WXL_OK; }
        static void WXL_CALL Stop() { Running() = false; }
        static wxl_bool WXL_CALL IsRunning() { return Running() ? WXL_TRUE : WXL_FALSE; }
        static wxl_result WXL_CALL Snapshot() { ++Snapshots(); return WXL_OK; }

        static uint32_t WXL_CALL Categories(wxl_footprint_category* out, uint32_t capacity)
        {
            static const struct { const char* name; uint64_t bytes, count, peak; } kFixed[] = {
                {"storm", 900u << 20, 1500000, 950u << 20},
                {"textures.managed", 400u << 20, 6000, 400u << 20},
                {"sound", 40u << 20, 0, 48u << 20},
            };
            const uint32_t fixed = 3;
            const uint32_t total = fixed + static_cast<uint32_t>(Reports().size());
            uint32_t       i     = 0;
            for (; out && i < capacity && i < fixed; ++i)
                out[i] = wxl_footprint_category{sizeof(wxl_footprint_category), S(kFixed[i].name), S("client"),
                                                kFixed[i].bytes, kFixed[i].count, kFixed[i].peak};
            for (const auto& [name, reported] : Reports())
            {
                if (!out || i >= capacity) break;
                out[i++] = wxl_footprint_category{sizeof(wxl_footprint_category), S(name.c_str()), S("wxl.lua"),
                                                  reported.bytes, reported.count, reported.bytes};
            }
            return total;
        }

        static uint32_t WXL_CALL Sources(wxl_footprint_source* out, uint32_t capacity)
        {
            static const wxl_footprint_source kRows[] = {
                {sizeof(wxl_footprint_source), S("new"), 500u << 20, 1000000, 520u << 20, 9000000},
                {sizeof(wxl_footprint_source), S(".\\WowClientDB.cpp"), 60u << 20, 900, 60u << 20, 900},
            };
            for (uint32_t i = 0; out && i < capacity && i < 2; ++i) out[i] = kRows[i];
            return 2;
        }

        static uint32_t WXL_CALL Archives(wxl_footprint_archive* out, uint32_t capacity)
        {
            static const wxl_footprint_archive kRows[] = {
                {sizeof(wxl_footprint_archive), S("Data\\common.MPQ"), 0, 65536, 40000, 3u << 20},
                {sizeof(wxl_footprint_archive), S("Data\\patch-3.MPQ"), 30, 16384, 9000, 1u << 20},
            };
            for (uint32_t i = 0; out && i < capacity && i < 2; ++i) out[i] = kRows[i];
            return 2;
        }

        static uint32_t WXL_CALL Tables(wxl_footprint_table* out, uint32_t capacity)
        {
            if (out && capacity)
                out[0] = wxl_footprint_table{sizeof(wxl_footprint_table), S("DBFilesClient\\Spell.dbc"), 49839, 680,
                                             40u << 20};
            return 1;
        }

        static uint32_t WXL_CALL Regions(wxl_footprint_region* out, uint32_t capacity)
        {
            static const wxl_footprint_region kRows[] = {
                {sizeof(wxl_footprint_region), S("committed"), S("private"), 2600ull << 20, 9000, 64u << 20},
                {sizeof(wxl_footprint_region), S("committed"), S("image"), 120u << 20, 400, 16u << 20},
                {sizeof(wxl_footprint_region), S("free"), S(""), 700u << 20, 3000, 90u << 20},
            };
            for (uint32_t i = 0; out && i < capacity && i < 3; ++i) out[i] = kRows[i];
            return 3;
        }

        static wxl_result WXL_CALL Report(wxl_str name, uint64_t bytes, uint64_t count)
        {
            if (!name.size) return WXL_E_INVALID;
            const std::string key(name.data, name.size);
            if (bytes == 0 && count == 0)
                Reports().erase(key);
            else
                Reports()[key] = Reported{bytes, count};
            return WXL_OK;
        }

        static wxl_result WXL_CALL WatchPressure(uint64_t bytes)
        {
            Threshold() = bytes;
            return WXL_OK;
        }

        static const wxl_footprint_api* Table()
        {
            static wxl_footprint_api api = [] {
                wxl_footprint_api t{};
                t.struct_size    = sizeof(wxl_footprint_api);
                t.start          = &Start;
                t.stop           = &Stop;
                t.running        = &IsRunning;
                t.snapshot       = &Snapshot;
                t.categories     = &Categories;
                t.sources        = &Sources;
                t.archives       = &Archives;
                t.tables         = &Tables;
                t.regions        = &Regions;
                t.report         = &Report;
                t.watch_pressure = &WatchPressure;
                return t;
            }();
            return &api;
        }
    };

    // --- wxl.profile -------------------------------------------------------------------------
    //
    // A viewer that is connected when the test says, and a trace of what was opened and closed:
    // "+name", "-name", "text:..." in order.

    struct FakeProfile
    {
        static bool& Connected() { static bool connected = false; return connected; }
        static std::vector<std::string>& Names() { static std::vector<std::string> names; return names; }
        static std::vector<std::string>& Trace() { static std::vector<std::string> trace; return trace; }

        static std::string Text(wxl_str text) { return std::string(text.data ? text.data : "", text.size); }

        static wxl_bool WXL_CALL IsConnected() { return Connected() ? WXL_TRUE : WXL_FALSE; }
        static uintptr_t WXL_CALL Location(wxl_str name, wxl_str, wxl_str, uint32_t, uint32_t)
        {
            if (!name.size) return 0;
            for (size_t i = 0; i < Names().size(); ++i)
                if (Names()[i] == Text(name)) return i + 1;
            Names().push_back(Text(name));
            return Names().size();
        }
        static uintptr_t WXL_CALL Begin(uintptr_t location)
        {
            if (!Connected() || !location) return 0;
            Trace().push_back("+" + Names()[location - 1]);
            return location;
        }
        static void WXL_CALL End(uintptr_t zone)
        {
            if (zone) Trace().push_back("-" + Names()[zone - 1]);
        }
        static void WXL_CALL ZoneText(uintptr_t zone, wxl_str text)
        {
            if (zone) Trace().push_back("text:" + Text(text));
        }
        static void WXL_CALL Plot(wxl_str name, double value)
        {
            if (Connected()) Trace().push_back("plot:" + Text(name) + "=" + std::to_string(static_cast<int>(value)));
        }
        static void WXL_CALL Message(wxl_str text)
        {
            if (Connected()) Trace().push_back("message:" + Text(text));
        }
        static void WXL_CALL FrameMark(wxl_str name)
        {
            if (Connected()) Trace().push_back("frame:" + Text(name));
        }

        static const wxl_profile_api* Table()
        {
            static wxl_profile_api api = [] {
                wxl_profile_api t{};
                t.struct_size = sizeof(wxl_profile_api);
                t.connected   = &IsConnected;
                t.location    = &Location;
                t.zone_begin  = &Begin;
                t.zone_end    = &End;
                t.zone_text   = &ZoneText;
                t.plot        = &Plot;
                t.message     = &Message;
                t.frame_mark  = &FrameMark;
                return t;
            }();
            return &api;
        }
    };

    // --- wxl.net ----------------------------------------------------------------------------

    struct FakeNet
    {
        static std::set<uint32_t>& Claimed()
        {
            static std::set<uint32_t> claimed;
            return claimed;
        }

        static int& Releases()
        {
            static int releases = 0;
            return releases;
        }

        static std::vector<std::pair<uint32_t, uint32_t>>& Sent()
        {
            static std::vector<std::pair<uint32_t, uint32_t>> sent;
            return sent;
        }

        static uint32_t WXL_CALL State() { return WXL_NET_AUTHENTICATED; }

        static wxl_result WXL_CALL Send(uint32_t opcode, const void*, uint32_t size)
        {
            Sent().emplace_back(opcode, size);
            return WXL_OK;
        }

        static wxl_result WXL_CALL Claim(uint32_t opcode)
        {
            if (Claimed().count(opcode)) return WXL_E_INVALID;
            Claimed().insert(opcode);
            return WXL_OK;
        }

        static wxl_result WXL_CALL Release(uint32_t opcode)
        {
            ++Releases();
            return Claimed().erase(opcode) ? WXL_OK : WXL_E_NOT_FOUND;
        }

        static uint32_t WXL_CALL ReservedLimit() { return 0x51F; }

        static const wxl_net_api* Table()
        {
            static wxl_net_api api = [] {
                wxl_net_api t{};
                t.struct_size           = sizeof(wxl_net_api);
                t.state                 = &State;
                t.send                  = &Send;
                t.claim_opcode          = &Claim;
                t.release_opcode        = &Release;
                t.reserved_opcode_limit = &ReservedLimit;
                return t;
            }();
            return &api;
        }
    };

    // --- wxl.ui -----------------------------------------------------------------------------

    struct FakeUi
    {
        struct Registered
        {
            wxl_panel_fn fn;
            void*        user;
        };

        static std::map<std::string, Registered>& Panels()
        {
            static std::map<std::string, Registered> panels;
            return panels;
        }

        static int& Removes()
        {
            static int removes = 0;
            return removes;
        }

        static std::vector<std::string>& Drawn()
        {
            static std::vector<std::string> drawn;
            return drawn;
        }

        static wxl_result WXL_CALL AddPanel(const char* title, wxl_panel_fn fn, void* user)
        {
            if (Panels().count(title)) return WXL_E_INVALID;
            Panels()[title] = Registered{fn, user};
            return WXL_OK;
        }

        static wxl_result WXL_CALL RemovePanel(const char* title)
        {
            ++Removes();
            return Panels().erase(title) ? WXL_OK : WXL_E_NOT_FOUND;
        }

        static wxl_bool WXL_CALL IsOpen() { return WXL_TRUE; }
        static void WXL_CALL Text(wxl_str text) { Drawn().push_back(std::string(text.data, text.size)); }
        static void WXL_CALL Separator() { Drawn().push_back("---"); }
        static void WXL_CALL SameLine() {}
        static wxl_bool WXL_CALL Button(const char* label)
        {
            Drawn().push_back(std::string("[") + label + "]");
            return WXL_TRUE;
        }
        static wxl_bool WXL_CALL Checkbox(const char*, wxl_bool* value)
        {
            *value = *value ? WXL_FALSE : WXL_TRUE;
            return WXL_TRUE;
        }
        static wxl_bool WXL_CALL SliderFloat(const char*, float* value, float, float max)
        {
            *value = max;
            return WXL_TRUE;
        }
        static wxl_bool WXL_CALL SliderInt(const char*, int32_t* value, int32_t, int32_t max)
        {
            *value = max;
            return WXL_TRUE;
        }
        static wxl_bool WXL_CALL ColorEdit(const char*, float rgba[4])
        {
            rgba[3] = 0.5f;
            return WXL_TRUE;
        }
        static wxl_bool WXL_CALL Combo(const char*, int32_t* index, const char* const*, int32_t count)
        {
            *index = count - 1;
            return WXL_TRUE;
        }
        static wxl_bool WXL_CALL CollapsingHeader(const char*) { return WXL_TRUE; }
        static wxl_bool WXL_CALL InputText(const char*, char* buffer, uint32_t capacity)
        {
            const char* typed = "typed";
            uint32_t    i     = 0;
            for (; typed[i] && i + 1 < capacity; ++i) buffer[i] = typed[i];
            buffer[i] = '\0';
            return WXL_TRUE;
        }

        /// Draws every panel, as the overlay would.
        static void DrawAll()
        {
            for (auto& entry : Panels()) entry.second.fn(entry.second.user);
        }

        static wxl_ui_api Make()
        {
            wxl_ui_api t{};
            t.struct_size       = sizeof(wxl_ui_api);
            t.add_panel         = &AddPanel;
            t.is_open           = &IsOpen;
            t.text              = &Text;
            t.separator         = &Separator;
            t.same_line         = &SameLine;
            t.button            = &Button;
            t.checkbox          = &Checkbox;
            t.slider_float      = &SliderFloat;
            t.slider_int        = &SliderInt;
            t.color_edit        = &ColorEdit;
            t.combo             = &Combo;
            t.collapsing_header = &CollapsingHeader;
            t.input_text        = &InputText;
            t.remove_panel      = &RemovePanel;
            return t;
        }

        static const wxl_ui_api* Table()
        {
            static wxl_ui_api api = Make();
            return &api;
        }

        /// A table from a runtime built before remove_panel was appended.
        static const wxl_ui_api* OlderTable()
        {
            static wxl_ui_api api = [] {
                wxl_ui_api t   = Make();
                t.struct_size  = offsetof(wxl_ui_api, remove_panel);
                t.remove_panel = nullptr;
                return t;
            }();
            return &api;
        }
    };

    // --- wxl.hook and wxl.hook.raw ----------------------------------------------------------

    struct FakeHooks
    {
        static int& Attaches()
        {
            static int attaches = 0;
            return attaches;
        }

        static int& Detaches()
        {
            static int detaches = 0;
            return detaches;
        }

        static std::set<wxl_hook>& Live()
        {
            static std::set<wxl_hook> live;
            return live;
        }

        static void* WXL_CALL Engine() { return nullptr; }

        static uint32_t WXL_CALL Points(wxl_hook_point* out, uint32_t capacity)
        {
            if (out && capacity >= 1)
            {
                out[0].name      = wxl_str{"Thing", 5};
                out[0].signature = wxl_str{"int(int)", 8};
            }
            return 1;
        }

        static wxl_bool WXL_CALL HasPoint(const char* name)
        {
            return std::strcmp(name, "Thing") == 0 ? WXL_TRUE : WXL_FALSE;
        }

        static wxl_hook WXL_CALL Attach(const char* name, void* detour, void** original, int32_t)
        {
            if (!HasPoint(name) || !detour) return WXL_HOOK_NONE;
            ++Attaches();
            if (original) *original = reinterpret_cast<void*>(&Engine);
            const wxl_hook hook = static_cast<wxl_hook>(Attaches());
            Live().insert(hook);
            return hook;
        }

        static wxl_hook WXL_CALL AttachAt(const char*, uintptr_t target, void* detour, void** original,
                                          int32_t)
        {
            if (!target || !detour) return WXL_HOOK_NONE;
            ++Attaches();
            if (original) *original = reinterpret_cast<void*>(&Engine);
            const wxl_hook hook = static_cast<wxl_hook>(Attaches());
            Live().insert(hook);
            return hook;
        }

        static wxl_result WXL_CALL Detach(wxl_hook hook)
        {
            ++Detaches();
            return Live().erase(hook) ? WXL_OK : WXL_E_NOT_FOUND;
        }

        static uintptr_t WXL_CALL ImageBase() { return 0x400000; }
        static uint32_t WXL_CALL ImageBuild() { return 12340; }

        static const wxl_hook_api* Table()
        {
            static wxl_hook_api api = [] {
                wxl_hook_api t{};
                t.struct_size = sizeof(wxl_hook_api);
                t.points      = &Points;
                t.has_point   = &HasPoint;
                t.attach      = &Attach;
                t.detach      = &Detach;
                return t;
            }();
            return &api;
        }

        static const wxl_hook_raw_api* RawTable()
        {
            static wxl_hook_raw_api api = [] {
                wxl_hook_raw_api t{};
                t.struct_size = sizeof(wxl_hook_raw_api);
                t.image_base  = &ImageBase;
                t.image_build = &ImageBuild;
                t.attach_at   = &AttachAt;
                t.detach      = &Detach;
                return t;
            }();
            return &api;
        }
    };

    // --- wxl.patch --------------------------------------------------------------------------
    //
    // An image of sixteen bytes at address 0x1000, journaled the way a runtime would.

    struct FakePatch
    {
        static constexpr uintptr_t kBase = 0x1000;

        struct Journal
        {
            uintptr_t            address;
            std::vector<uint8_t> saved;
        };

        static std::vector<uint8_t>& Image()
        {
            static std::vector<uint8_t> image(16, 0x90);
            return image;
        }

        static std::map<wxl_patch, Journal>& Live()
        {
            static std::map<wxl_patch, Journal> live;
            return live;
        }

        static std::vector<wxl_patch>& RestoreOrder()
        {
            static std::vector<wxl_patch> order;
            return order;
        }

        static int& Applies()
        {
            static int applies = 0;
            return applies;
        }

        static bool InImage(uintptr_t address, uint32_t size)
        {
            return address >= kBase && address + size <= kBase + Image().size();
        }

        static wxl_patch WXL_CALL Apply(const char* name, uintptr_t address, const void* expected,
                                        const void* bytes, uint32_t size)
        {
            if (!name || !expected || !bytes || !InImage(address, size)) return WXL_PATCH_NONE;
            uint8_t* at = Image().data() + (address - kBase);
            if (std::memcmp(at, expected, size) != 0) return WXL_PATCH_NONE;
            Journal journal{address, std::vector<uint8_t>(at, at + size)};
            std::memcpy(at, bytes, size);
            const wxl_patch patch = static_cast<wxl_patch>(++Applies());
            Live()[patch]         = std::move(journal);
            return patch;
        }

        static wxl_result WXL_CALL Restore(wxl_patch patch)
        {
            RestoreOrder().push_back(patch);
            const auto found = Live().find(patch);
            if (found == Live().end()) return WXL_E_NOT_FOUND;
            std::memcpy(Image().data() + (found->second.address - kBase), found->second.saved.data(),
                        found->second.saved.size());
            Live().erase(found);
            return WXL_OK;
        }

        static wxl_bool WXL_CALL Matches(uintptr_t address, const void* expected, uint32_t size)
        {
            if (!InImage(address, size)) return WXL_FALSE;
            return std::memcmp(Image().data() + (address - kBase), expected, size) == 0 ? WXL_TRUE
                                                                                         : WXL_FALSE;
        }

        static uintptr_t WXL_CALL Find(const void* pattern, uint32_t size, uintptr_t after)
        {
            const std::vector<uint8_t>& image = Image();
            for (size_t i = after ? after - kBase + 1 : 0; i + size <= image.size(); ++i)
                if (std::memcmp(image.data() + i, pattern, size) == 0) return kBase + i;
            return 0;
        }

        static const wxl_patch_api* Table()
        {
            static wxl_patch_api api = [] {
                wxl_patch_api t{};
                t.struct_size = sizeof(wxl_patch_api);
                t.apply       = &Apply;
                t.restore     = &Restore;
                t.matches     = &Matches;
                t.find        = &Find;
                return t;
            }();
            return &api;
        }
    };

    // --- wxl.m2, wxl.m2file and wxl.gx ------------------------------------------------------
    //
    // Models are records the fake makes and frees, a parent by address; a model the fake did not
    // make is refused, as the runtime refuses a model the mod did not make. The arrays are kept
    // as the recompiled client keeps them: an offset from the array itself.

    struct FakeModels
    {
        struct Model
        {
            std::string path;
            void*       parent     = nullptr;
            uint32_t    attachment = 0;
            uint32_t    flags      = 0;
            uint32_t    first = 0, last = 0;
            bool        visible    = true;
            std::string texture;
            uint32_t    key_bone   = 0;
            wxl_vec3    offset{};
            bool             world = false; ///< made in the world's scene
            wxl_m2_placement placed{};      ///< the last Place
            wxl_m2_placement transform{};   ///< the last SetTransform
            uint32_t         anim  = 0;
            float            speed = 0.0f;
            float            alpha = 1.0f;
            bool             emitting = true;
        };

        static std::map<void*, Model*>& Made()
        {
            static std::map<void*, Model*> made;
            return made;
        }
        static int& Released()
        {
            static int released = 0;
            return released;
        }
        static int& Invalidated()
        {
            static int invalidated = 0;
            return invalidated;
        }
        static int& Animated()
        {
            static int animated = 0;
            return animated;
        }

        /// The model a world object draws with: the unit's, an address of the fake's own.
        static void* UnitModel()
        {
            static char model = 0;
            return &model;
        }

        static Model* Known(void* model)
        {
            const auto found = Made().find(model);
            return found == Made().end() ? nullptr : found->second;
        }

        static void* WXL_CALL ObjectModel(wxl_handle object)
        {
            return FakeObjects::Live(object) && FakeObjects::IsUnit(object) ? UnitModel() : nullptr;
        }
        static wxl_result WXL_CALL ItemDisplay(uint32_t id, wxl_m2_item_display* out)
        {
            if (id != 416036) return WXL_E_NOT_FOUND;
            out->id       = id;
            out->model[0] = "Buckle.mdx";
            out->model[1] = "";
            return WXL_OK;
        }
        static wxl_result WXL_CALL RacePrefix(uint32_t race, wxl_str* out)
        {
            if (race != 1) return WXL_E_NOT_FOUND;
            *out = wxl_str{"Hu", 2};
            return WXL_OK;
        }
        static void* WXL_CALL CreateModel(void* scene, wxl_str path)
        {
            if (!scene || !path.size) return nullptr;
            Model* model = new Model{};
            model->path.assign(path.data, path.size);
            Made()[model] = model;
            return model;
        }
        static void WXL_CALL Release(void* model)
        {
            Model* known = Known(model);
            if (!known) return;
            Made().erase(model);
            delete known;
            ++Released();
        }
        static wxl_result WXL_CALL Attach(void* model, void* parent, uint32_t attachment, uint32_t flags)
        {
            Model* known = Known(model);
            if (!known || !parent) return WXL_E_INVALID;
            if (attachment == 99 && !(flags & WXL_M2_ATTACH_ROOT)) return WXL_E_NOT_FOUND;
            known->parent = parent, known->attachment = attachment, known->flags = flags;
            return WXL_OK;
        }
        static void WXL_CALL Detach(void* model)
        {
            if (Model* known = Known(model)) known->parent = nullptr;
        }
        static void* WXL_CALL Parent(void* model)
        {
            Model* known = Known(model);
            return known ? known->parent : nullptr;
        }
        static void WXL_CALL SetGeosets(void* model, uint32_t first, uint32_t last, wxl_bool visible)
        {
            if (Model* known = Known(model)) known->first = first, known->last = last, known->visible = visible == WXL_TRUE;
        }
        static wxl_result WXL_CALL ReplaceTexture(void* model, uint32_t, wxl_str path)
        {
            Model* known = Known(model);
            if (!known) return WXL_E_INVALID;
            known->texture.assign(path.data, path.size);
            return known->texture == "missing.blp" ? WXL_E_NOT_FOUND : WXL_OK;
        }
        static wxl_result WXL_CALL AttachmentPoint(const void* model, uint32_t attachment, wxl_vec3* out)
        {
            if (model != UnitModel() || attachment != 32) return WXL_E_NOT_FOUND;
            *out = wxl_vec3{0.25f, 0.5f, 1.5f};
            return WXL_OK;
        }
        static wxl_result WXL_CALL AttachToBone(void* model, void* parent, uint32_t attachment, uint32_t key_bone,
                                                wxl_vec3 offset)
        {
            Model* known = Known(model);
            if (!known || !parent) return WXL_E_INVALID;
            if (key_bone == 99) return WXL_E_NOT_FOUND;
            known->parent = parent, known->attachment = attachment, known->key_bone = key_bone, known->offset = offset;
            return WXL_OK;
        }

        static void* WXL_CALL CreateWorldModel(wxl_str path)
        {
            static char scene = 0;
            void*       made  = CreateModel(&scene, path);
            if (Model* known = Known(made)) known->world = true;
            return made;
        }
        static wxl_result WXL_CALL Place(void* model, const wxl_m2_placement* placement)
        {
            Model* known = Known(model);
            if (!known || !placement) return WXL_E_INVALID;
            known->placed = *placement;
            known->parent = nullptr;
            return WXL_OK;
        }
        static wxl_result WXL_CALL SetTransform(void* model, const wxl_m2_placement* placement)
        {
            Model* known = Known(model);
            if (!known || !placement) return WXL_E_INVALID;
            known->transform = *placement;
            return WXL_OK;
        }
        static wxl_result WXL_CALL PlayAnimation(void* model, uint32_t anim, float speed)
        {
            Model* known = Known(model);
            if (!known) return WXL_E_INVALID;
            known->anim = anim, known->speed = speed;
            return WXL_OK;
        }
        static wxl_bool WXL_CALL ModelLoaded(void* model)
        {
            return Known(model) || model == UnitModel() ? WXL_TRUE : WXL_FALSE;
        }
        static wxl_result WXL_CALL AnimationLength(void* model, uint32_t anim, uint32_t* out)
        {
            if (!ModelLoaded(model)) return WXL_E_FAILED;
            *out = anim == 0 ? 1333u : 0u;
            return WXL_OK;
        }
        static void WXL_CALL SetAlpha(void* model, float alpha)
        {
            if (Model* known = Known(model)) known->alpha = alpha;
        }
        static void WXL_CALL SetEmitting(void* model, wxl_bool emitting)
        {
            if (Model* known = Known(model)) known->emitting = emitting == WXL_TRUE;
        }
        static wxl_result WXL_CALL AttachmentWorld(void* model, uint32_t attachment, wxl_vec3* out)
        {
            if (model != UnitModel() || attachment != 32) return WXL_E_NOT_FOUND;
            *out = wxl_vec3{10.0f, 20.0f, 31.5f};
            return WXL_OK;
        }
        static wxl_result WXL_CALL GroundHeight(const wxl_vec3* at, float up, float down, float* out)
        {
            if (!at || up < 0.0f || down < 0.0f) return WXL_E_INVALID;
            if (down < 2.0f) return WXL_E_NOT_FOUND;
            *out = at->z - 2.0f;
            return WXL_OK;
        }

        /// One bone watcher at a time, its token 7; a file of two bones.
        struct Watch
        {
            wxl_m2_bones_filter_fn filter = nullptr;
            wxl_m2_bones_fn        fn     = nullptr;
            void*                  user   = nullptr;
            uint32_t               token  = 0;
        };
        static Watch& Watching()
        {
            static Watch watch;
            return watch;
        }
        static wxl_result WXL_CALL WatchBones(wxl_m2_bones_filter_fn filter, wxl_m2_bones_fn fn, void* user,
                                              uint32_t* out)
        {
            if (!fn || Watching().token) return fn ? WXL_E_FAILED : WXL_E_INVALID;
            Watching() = Watch{filter, fn, user, 7};
            *out       = 7;
            return WXL_OK;
        }
        static void WXL_CALL UnwatchBones(uint32_t token)
        {
            if (token && token == Watching().token) Watching() = Watch{};
        }
        static wxl_result WXL_CALL BoneInfo(void* file, uint32_t index, int16_t* parent, wxl_vec3* pivot)
        {
            if (!file || index >= 2) return WXL_E_NOT_FOUND;
            *parent = index == 0 ? int16_t(-1) : int16_t(0);
            *pivot  = wxl_vec3{0.0f, 0.0f, static_cast<float>(index)};
            return WXL_OK;
        }

        static void* WXL_CALL AllocNear(const void*, uint32_t size)
        {
            static std::vector<std::vector<uint8_t>> blocks;
            blocks.emplace_back(size, uint8_t(0));
            return blocks.back().data();
        }
        static void WXL_CALL AnimateBones(void* file) { if (file) ++Animated(); }
        static void* WXL_CALL ArrayData(const void* array)
        {
            const uint32_t* words = static_cast<const uint32_t*>(array);
            if (!words[0]) return nullptr;
            return const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(array) + static_cast<int32_t>(words[1]));
        }
        static wxl_result WXL_CALL SetArray(void* array, const void* data, uint32_t count)
        {
            const int64_t offset = reinterpret_cast<const uint8_t*>(data) - static_cast<const uint8_t*>(array);
            if (data && (offset < INT32_MIN || offset > INT32_MAX)) return WXL_E_INVALID;
            uint32_t* words = static_cast<uint32_t*>(array);
            words[0]        = count;
            words[1]        = data ? static_cast<uint32_t>(static_cast<int32_t>(offset)) : 0;
            return WXL_OK;
        }

        static void* WXL_CALL Device()
        {
            static char device = 0;
            return &device;
        }
        static void WXL_CALL Invalidate() { ++Invalidated(); }

        static const wxl_m2_api* Table()
        {
            static const wxl_m2_api api = [] {
                wxl_m2_api t{};
                t.struct_size      = sizeof t;
                t.object_model     = &ObjectModel;
                t.item_display     = &ItemDisplay;
                t.race_prefix      = &RacePrefix;
                t.create_model     = &CreateModel;
                t.release_model    = &Release;
                t.attach_model     = &Attach;
                t.detach_model     = &Detach;
                t.model_parent     = &Parent;
                t.set_geosets      = &SetGeosets;
                t.replace_texture  = &ReplaceTexture;
                t.attachment_point = &AttachmentPoint;
                t.attach_to_bone   = &AttachToBone;
                t.create_world_model  = &CreateWorldModel;
                t.place_model         = &Place;
                t.set_model_transform = &SetTransform;
                t.play_animation      = &PlayAnimation;
                t.model_loaded        = &ModelLoaded;
                t.animation_length    = &AnimationLength;
                t.set_alpha           = &SetAlpha;
                t.set_emitting        = &SetEmitting;
                t.attachment_world    = &AttachmentWorld;
                t.ground_height       = &GroundHeight;
                t.watch_bones         = &WatchBones;
                t.unwatch_bones       = &UnwatchBones;
                t.bone_info           = &BoneInfo;
                return t;
            }();
            return &api;
        }
        static const wxl_m2file_api* FileTable()
        {
            static const wxl_m2file_api api = [] {
                wxl_m2file_api t{};
                t.struct_size   = sizeof t;
                t.alloc_near    = &AllocNear;
                t.animate_bones = &AnimateBones;
                t.array_data    = &ArrayData;
                t.set_array     = &SetArray;
                return t;
            }();
            return &api;
        }
        static const wxl_gx_api* GxTable()
        {
            static const wxl_gx_api api = [] {
                wxl_gx_api t{};
                t.struct_size      = sizeof t;
                t.d3d9_device      = &Device;
                t.invalidate_state = &Invalidate;
                return t;
            }();
            return &api;
        }
    };

    // Three engine types as a mod's headers would describe them.
    struct EngineShared
    {
        static constexpr const char* kName       = "CM2Shared";
        static constexpr uint64_t    kLayoutHash = 0x1122334455667788ULL;
    };
    struct EngineData
    {
        static constexpr const char* kName       = "M2Data";
        static constexpr uint64_t    kLayoutHash = 0x0102030405060708ULL;
    };
    struct EngineAbsent
    {
        static constexpr const char* kName       = "CNoSuchType";
        static constexpr uint64_t    kLayoutHash = 1;
    };

    // --- wxl.engine ----------------------------------------------------------------------------
    // Answers one type's hash, and another's wrongly when told to: what a runtime built from
    // another fork would say.
    struct FakeEngine
    {
        static inline const char* name      = "CM2Shared";
        static inline uint64_t    hash      = 0;
        static inline const char* wrongName = "M2Data";
        static inline uint64_t    wrongHash = 0;

        static wxl_result WXL_CALL Layout(const char* type, uint64_t* out)
        {
            if (!type || !out) return WXL_E_INVALID;
            if (std::strcmp(type, name) == 0)
            {
                *out = hash;
                return WXL_OK;
            }
            if (std::strcmp(type, wrongName) == 0)
            {
                *out = wrongHash;
                return WXL_OK;
            }
            return WXL_E_NOT_FOUND;
        }

        static const wxl_engine_api* Table()
        {
            static const wxl_engine_api api = [] {
                wxl_engine_api t{};
                t.struct_size = sizeof t;
                t.layout      = &Layout;
                return t;
            }();
            return &api;
        }
    };

} // namespace wxltest

#endif // WXL_TEST_FAKE_SERVICES_HPP
