// SPDX-License-Identifier: GPL-3.0-or-later
// wxl-objects-check.exe: Object, Unit, Player and GameObject over a fake objects service.

#include "FakeCore.hpp"

#include "wxl/Extension.hpp"
#include "wxl/objects/GameObject.hpp"
#include "wxl/objects/Player.hpp"
#include "wxl/offsets/CGObject_C.hpp"

#include <cstdio>
#include <cstring>
#include <unordered_set>
#include <vector>

namespace
{
    int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++g_failures; } } while (0)

    namespace off = wxl::offsets;
    namespace gw  = wxl::game::world;

    // The object header: the descriptor block's GUID and type mask (CGObjectData).
    struct ObjectHeader
    {
        uint64_t guid;
        uint32_t typeMask;
    };

    // --- fake client objects: a vtable with the position and facing slots, a header ----------------
    void __fastcall FakePosition(void* self, void*, float* out);
    float __fastcall FakeFacing(void* self, void*);

    struct FakeObject
    {
        void* const* vtable;
        uint8_t      pad[off::CGObject_C::fields::m_obj - sizeof(void*)];
        ObjectHeader* header;
        ObjectHeader  ownHeader;
        float        position[3];
        float        facing;
    };

    void* g_vtable[32];

    void __fastcall FakePosition(void* self, void*, float* out)
    {
        std::memcpy(out, static_cast<FakeObject*>(self)->position, sizeof(float) * 3);
    }
    float __fastcall FakeFacing(void* self, void*) { return static_cast<FakeObject*>(self)->facing; }

    FakeObject* MakeObject(uint64_t guid, uint32_t typeMask, float x)
    {
        auto* o              = new FakeObject{};
        o->vtable            = g_vtable;
        o->header            = &o->ownHeader;
        o->ownHeader.guid    = guid;
        o->ownHeader.typeMask = typeMask;
        o->position[0]       = x;
        o->facing            = 1.5f;
        return o;
    }

    // --- a fake objects service: one slot per object seen, a generation per slot ------------------
    struct Slot { FakeObject* object; uint32_t generation; bool live; };
    std::vector<Slot>  g_slots;
    std::vector<FakeObject*> g_world; // the resident objects
    bool g_inWorld = true;

    FakeObject* Find(uint64_t guid)
    {
        for (FakeObject* o : g_world) if (o->ownHeader.guid == guid) return o;
        return nullptr;
    }

    void Despawn(FakeObject* o)
    {
        for (size_t i = 0; i < g_world.size(); ++i)
            if (g_world[i] == o) { g_world.erase(g_world.begin() + i); break; }
        for (Slot& s : g_slots)
            if (s.object == o && s.live) { s.live = false; ++s.generation; }
    }

    int __cdecl FromPointer(void* object, WXL_Handle* out)
    {
        if (!g_inWorld) return WXL_E_NOT_IN_WORLD;
        auto* o = static_cast<FakeObject*>(object);
        bool resident = false;
        for (FakeObject* w : g_world) resident |= w == o;
        if (!resident) return WXL_E_NOT_FOUND;
        for (uint32_t i = 0; i < g_slots.size(); ++i)
            if (g_slots[i].live && g_slots[i].object == o) { *out = { i, g_slots[i].generation }; return WXL_OK; }
        g_slots.push_back({ o, 1, true });
        *out = { uint32_t(g_slots.size() - 1), 1 };
        return WXL_OK;
    }
    int __cdecl FromGuid(uint64_t guid, WXL_Handle* out)
    {
        if (!g_inWorld) return WXL_E_NOT_IN_WORLD;
        FakeObject* o = Find(guid);
        return o ? FromPointer(o, out) : WXL_E_NOT_FOUND;
    }
    int __cdecl Resolve(WXL_Handle h, uint32_t typeMask, void** out)
    {
        if (h.generation == 0) return WXL_E_NULL_HANDLE;
        if (!g_inWorld) return WXL_E_NOT_IN_WORLD;
        if (h.index >= g_slots.size() || g_slots[h.index].generation != h.generation) return WXL_E_EXPIRED;
        FakeObject* o = g_slots[h.index].object;
        if (!(o->ownHeader.typeMask & typeMask)) return WXL_E_WRONG_KIND;
        *out = o;
        return WXL_OK;
    }
    int __cdecl Guid(WXL_Handle h, uint64_t* out)
    {
        void* o = nullptr;
        if (const int s = Resolve(h, 0xFFFFFFFFu, &o)) return s;
        *out = static_cast<FakeObject*>(o)->ownHeader.guid;
        return WXL_OK;
    }

    const WXL_ObjectsApi g_objects = { sizeof(WXL_ObjectsApi), WXL_OBJECTS_API_VERSION,
                                       &FromGuid, &FromPointer, &Resolve, &Guid };

}

int main()
{
    g_vtable[gw::kSlotPosition]     = reinterpret_cast<void*>(&FakePosition);
    g_vtable[gw::kSlotNamePosition] = reinterpret_cast<void*>(&FakePosition);
    g_vtable[gw::kSlotFacing]       = reinterpret_cast<void*>(&FakeFacing);

    // Static: statics of the SDK (the scheduler's connection) still reach it at exit.
    static const WXL_Api api = fake::MakeApi();

    // Before the core is bound, a non-null handle has no service to ask.
    {
        wxl::Object early(WXL_Handle{ 0, 1 });
        CHECK(early.Guid().Code() == wxl::Error::Unsupported);
        CHECK(!early);
    }
    CHECK(wxl::Bind(&api, "objects-check"));
    fake::Publish(WXL_OBJECTS_API_NAME, WXL_OBJECTS_API_VERSION, const_cast<WXL_ObjectsApi*>(&g_objects));

    const uint32_t kUnitMask   = gw::kTypeMaskObject | gw::kTypeMaskUnit;
    const uint32_t kPlayerMask = kUnitMask | gw::kTypeMaskPlayer;
    FakeObject* wolf   = MakeObject(0xF130000001000001ull, kUnitMask, 10.0f);
    FakeObject* hero   = MakeObject(0x0000000000000042ull, kPlayerMask, 20.0f);
    FakeObject* chest  = MakeObject(0xF110000002000001ull, gw::kTypeMaskObject | gw::kTypeMaskGameObject, 30.0f);
    g_world = { wolf, hero, chest };

    // The empty handle.
    wxl::Object none;
    CHECK(none.IsNull() && !none && !none.Exists());
    CHECK(none.Guid().Code() == wxl::Error::NullHandle);
    CHECK(none.Position().Code() == wxl::Error::NullHandle);
    CHECK(!none.IsUnit() && !none.AsUnit().Exists());

    // Lookup and reads.
    wxl::Unit unit = wxl::Unit::FromGuid(wolf->ownHeader.guid);
    CHECK(!unit.IsNull() && unit);
    CHECK(unit.Guid() && *unit.Guid() == wolf->ownHeader.guid);
    CHECK(unit.Position() && unit.Position()->x == 10.0f);
    CHECK(unit.Facing().ValueOr(0.0f) == 1.5f);
    CHECK(unit.IsUnit() && !unit.IsPlayer());
    CHECK(unit.As<wxl::Player>().Code() == wxl::Error::WrongKind);
    CHECK(!unit.AsPlayer().Exists());
    CHECK(wxl::Player::FromGuid(wolf->ownHeader.guid).IsNull());

    wxl::Player player = wxl::Player::FromGuid(hero->ownHeader.guid);
    CHECK(player && player.IsPlayer() && player.IsUnit());
    auto asUnit = wxl::Object(player).As<wxl::Unit>();
    CHECK(asUnit && *asUnit == player);

    wxl::GameObject box = wxl::GameObject::FromGuid(chest->ownHeader.guid);
    CHECK(box && box.IsGameObject() && !box.IsUnit());
    CHECK(box.Resolve(gw::kTypeMaskUnit).Code() == wxl::Error::WrongKind);

    // The same object, twice, is one handle; a map key.
    CHECK(wxl::Object::FromGuid(wolf->ownHeader.guid) == unit);
    std::unordered_set<wxl::Object> seen{ unit, player, box, unit };
    CHECK(seen.size() == 3);

    // Gone: every read says so, and the handle never comes back.
    Despawn(wolf);
    CHECK(!unit && !unit.Exists());
    CHECK(unit.Position().Code() == wxl::Error::Expired);
    CHECK(unit.Guid().Code() == wxl::Error::Expired);
    CHECK(!unit.IsUnit());
    g_world.push_back(wolf); // back in view: a new handle, same GUID
    wxl::Unit again = wxl::Unit::FromGuid(wolf->ownHeader.guid);
    CHECK(again && again != unit && !unit);
    CHECK(*again.Guid() == wolf->ownHeader.guid);

    // Value() on an error throws BadResult.
    bool threw = false;
    try { (void)unit.Position().Value(); }
    catch (const wxl::BadResult& e) { threw = e.Code() == wxl::Error::Expired; }
    CHECK(threw);

    // Out of the world.
    g_inWorld = false;
    CHECK(player.Position().Code() == wxl::Error::NotInWorld);
    CHECK(wxl::Object::FromGuid(hero->ownHeader.guid).IsNull());
    g_inWorld = true;

    // Reads and observables of a unit: descriptor fields, change signals, the end on expiry.
    {
        FakeObject* boar = MakeObject(0xF130000003000001ull, kUnitMask, 5.0f);
        uint32_t fields[0x40] = {};
        fields[0] = uint32_t(boar->ownHeader.guid);
        fields[1] = uint32_t(boar->ownHeader.guid >> 32);
        fields[2] = kUnitMask;
        fields[0x18] = 50;  // health
        fields[0x20] = 80;  // max health
        fields[0x36] = 12;  // level
        fields[0x12] = uint32_t(hero->ownHeader.guid); // target
        boar->header = reinterpret_cast<ObjectHeader*>(fields);
        g_world.push_back(boar);

        wxl::Unit u = wxl::Unit::FromGuid(boar->ownHeader.guid);
        CHECK(*u.Health() == 50 && *u.MaxHealth() == 80 && *u.Level() == 12);
        CHECK(u.Target() && *u.Target() == player);

        std::vector<wxl::Change<uint32_t>> changes;
        auto watch = u.HealthChanged().Connect([&](const wxl::Change<uint32_t>& c) { changes.push_back(c); });
        CHECK(watch);
        fake::Raise<wxl::events::Event::OnUpdate>({ 0.1f, 1 });
        CHECK(changes.empty());
        fields[0x18] = 30;
        fake::Raise<wxl::events::Event::OnUpdate>({ 0.1f, 2 });
        fake::Raise<wxl::events::Event::OnUpdate>({ 0.1f, 3 });
        CHECK(changes.size() == 1 && changes[0].previous == 50 && changes[0].value == 30);

        Despawn(boar);
        fake::Raise<wxl::events::Event::OnUpdate>({ 0.1f, 4 });
        CHECK(!watch->Connected());
    }

    std::printf("%d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
