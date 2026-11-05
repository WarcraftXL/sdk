// SPDX-License-Identifier: GPL-3.0-or-later
// wxl-extension-check.exe: Extension overrides, On, OnReady, Log, signals, Hook and Service on the fake core.

#include "FakeCore.hpp"

#include "wxl/Extension.hpp"
#include "wxl/Hook.hpp"
#include "wxl/Service.hpp"
#include "wxl/detail/Manifest.hpp"
#include "wxl/objects/Camera.hpp"
#include "wxl/objects/Doodad.hpp"
#include "wxl/objects/GameObject.hpp"
#include "wxl/objects/MapChunk.hpp"
#include "wxl/objects/MapTile.hpp"
#include "wxl/objects/Model.hpp"
#include "wxl/objects/Player.hpp"
#include "wxl/objects/Wmo.hpp"

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <type_traits>

using namespace wxl;

namespace
{
    int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++g_failures; } } while (0)

    using fake::Raise;
    namespace ev = events;

    uint32_t Live(events::Event e) { return fake::Live(uint32_t(e)); }

    using ProbeFn    = int __cdecl(int);
    using ProbeFnPtr = int(__cdecl*)(int); // the shape every offsets/ alias has
    int __cdecl ChainEnd(int v) { return v + 1; }
    const char* g_attachedName     = nullptr;
    int         g_attachedPriority = -1;

    int __cdecl FakeHookAttachByName(const char* pointName, void*, void** original, int priority)
    {
        if (!pointName || std::strcmp(pointName, "Probe.Point") != 0) return 0;
        g_attachedName     = pointName;
        g_attachedPriority = priority;
        *original          = reinterpret_cast<void*>(&ChainEnd);
        return 1;
    }

    struct FakeService { uint32_t structSize; int value; };

    /// A wrapper over FakeService, one per version asked for.
    template <uint32_t V>
    struct Probe
    {
        Probe() = default;
        explicit Probe(const FakeService* t) : table(t) {}
        int Value() const { return table->value; }
        const FakeService* table = nullptr;
    };
}

template <uint32_t V>
struct wxl::ServiceTraits<Probe<V>>
{
    using Table                           = FakeService;
    static constexpr const char* kName    = "probe.service";
    static constexpr uint32_t    kVersion = V;
};

namespace
{
    /// Overrides three events, one of them privately; started and stopped by hand, as Export does.
    class Both final : public Extension
    {
    public:
        void OnUpdate(float dt, uint32_t timeMs) override { lastDt = dt; lastTime = timeMs; ++updates; }
        void OnEndScene(void* device) override { lastDevice = device; }
        void OnStart() override { ++starts; }
        void OnStop() override { ++stops; }
        float lastDt = 0; uint32_t lastTime = 0; int updates = 0; void* lastDevice = nullptr;
        int starts = 0, stops = 0;

    private:
        void OnInput(uint32_t message, uintptr_t, uintptr_t, bool& handled) override { if (message == 7) handled = true; }
    };

    /// Overrides through an intermediate class: still connected.
    class Base : public Extension
    {
    public:
        void OnWorldEnter(MapId map) override { lastMap = map; }
        MapId lastMap = 0;
    };
    class Derived final : public Base {};

    class Thrower final : public Extension
    {
        void OnWorldLeave(MapId) override { throw std::runtime_error("boom"); }
    };

    class Nothing final : public Extension {};

    static_assert(detail::Overrides<Both>::Update && detail::Overrides<Both>::EndScene && detail::Overrides<Both>::Input);
    static_assert(!detail::Overrides<Both>::WorldEnter && !detail::Overrides<Both>::Frame);
    static_assert(detail::Overrides<Derived>::WorldEnter && !detail::Overrides<Derived>::Update);
    static_assert(!detail::Overrides<Nothing>::Update && !detail::Overrides<Nothing>::ChatMessage);
    static_assert(std::is_same_v<TargetChanged::Args, TargetChangedArgs> && WorldEnter::kEvent == ev::Event::OnWorldEnter);
}

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    static const WXL_Api api = [] {
        WXL_Api a          = fake::MakeApi();
        a.HookAttachByName = &FakeHookAttachByName;
        return a;
    }();
    CHECK(Bind(&api, "check", "1.2.3"));
    CHECK(Api() == &api && std::strcmp(ExtensionName(), "check") == 0 && std::strcmp(ExtensionVersion(), "1.2.3") == 0);

    // Overrides: only the events an extension overrides are connected, at Start.
    Both both;
    CHECK(both.Connected() == 0);
    detail::Lifecycle<Both>::Start(both);
    CHECK(both.starts == 1 && both.Connected() == 3);
    CHECK(Live(ev::Event::OnUpdate) == 1 && Live(ev::Event::OnInput) == 1 && Live(ev::Event::OnWorldEnter) == 0);

    Raise<ev::Event::OnUpdate>({ 0.25f, 1234 });
    CHECK(both.updates == 1 && both.lastDt == 0.25f && both.lastTime == 1234);
    bool handled = false;
    Raise<ev::Event::OnInput>({ 7, 0, 0, &handled });
    CHECK(handled);
    int device = 0;
    Raise<ev::Event::OnEndScene>({ &device });
    CHECK(both.lastDevice == &device);

    Derived derived;
    detail::Lifecycle<Derived>::Start(derived);
    CHECK(derived.Connected() == 1);
    Raise<ev::Event::OnWorldEnter>({ 571 });
    CHECK(derived.lastMap == 571);
    Nothing nothing;
    detail::Lifecycle<Nothing>::Start(nothing);
    CHECK(nothing.Connected() == 0);

    // A std::exception leaving an override is logged with its message, then left to the core's guard.
    Thrower thrower;
    detail::Lifecycle<Thrower>::Start(thrower);
    bool threw = false;
    try { Raise<ev::Event::OnWorldLeave>({ 1 }); } catch (const std::runtime_error&) { threw = true; }
    CHECK(threw && fake::Logged("check: OnWorldLeave threw: boom"));
    fake::g_walking = 0;

    // Log: std::format syntax, under the extension's name; long lines are cut, not allocated.
    Log::Info("x={} {:04X} {:.1f} {}", 42, 0xBEEFu, 0.25, "ok");
    CHECK(fake::Logged("check: x=42 BEEF 0.2 ok") || fake::Logged("check: x=42 BEEF 0.3 ok"));
    Log::Info("{}", std::string(3000, 'a'));
    CHECK(fake::g_log.back().size() == std::strlen("check: ") + Log::kLineSize - 1);
    detail::g_link.level = WXL_LOG_WARN;
    const size_t lines = fake::g_log.size();
    Log::Info("below the threshold");
    Log::Warn("at it");
    CHECK(fake::g_log.size() == lines + 1 && fake::Logged("check: at it"));
    detail::g_link.level = WXL_LOG_INFO;

    // On: priority order, Once, TryOn, a handler without arguments, Stop.
    static std::string order; // the Last handler lives until the extension stops
    {
        On<Update>([&](const UpdateArgs&) { order += "L"; }, priority::Last);
        Listener first = On<Update>([&](auto&) { order += "F"; }, priority::First);
        Once<Update>([&] { order += "O"; });
        auto tried = TryOn<Update>([&](const UpdateArgs& a) { if (a.dt > 1.0f) order += "B"; });
        CHECK(tried.HasValue() && first.Active());
        Raise<ev::Event::OnUpdate>({ 0.5f, 2 });
        Raise<ev::Event::OnUpdate>({ 2.0f, 3 });
        CHECK(order == "FOLFBL");
        const uint32_t releases = fake::g_releases;
        first.Stop();
        CHECK(!first.Active() && fake::g_releases == releases + 1);
        tried->Stop();
    }
    CHECK(Live(ev::Event::OnUpdate) == 2); // Both's override and the Last handler

    // Emit: an extension raises an event; every handler hears it.
    {
        int sounds = 0;
        Listener heard = On<SoundPlay>([&](const SoundPlayArgs& e) { sounds += e.scriptState == &sounds; });
        Emit<SoundPlay>({ &sounds });
        CHECK(sounds == 1);
        heard.Stop();
    }

    // Answers: each handler sees the answer left by the ones before it; the last word stands.
    {
        Listener yes = On<Input>([](const InputArgs& a) { *a.handled = true; });
        bool seen = false;
        Listener no = On<Input>([&](const InputArgs& a) { seen = *a.handled; *a.handled = false; }, priority::Last);
        bool answer = false;
        Raise<ev::Event::OnInput>({ 1, 0, 0, &answer });
        CHECK(seen && !answer);
        yes.Stop();
        no.Stop();
    }
    static_assert(ev::AnswerOf(ev::Event::OnInput).size == sizeof(bool), "OnInput asks");
    static_assert(ev::AnswerOf(ev::Event::OnUpdate).size == 0, "OnUpdate does not ask");

    // Signals still connect for a Connection the caller keeps; a subject limits them and ends them.
    {
        const WXL_Handle wolf{ 3, 1 }, boar{ 4, 1 };
        int calls = 0;
        auto c = SignalOf<TargetChanged>(wolf).Connect([&](const TargetChangedArgs&) { ++calls; });
        TargetChangedArgs args{ nullptr };
        fake::EmitFor(uint32_t(ev::Event::OnTargetChanged), boar, &args);
        fake::EmitFor(uint32_t(ev::Event::OnTargetChanged), wolf, &args);
        fake::Emit(uint32_t(ev::Event::OnTargetChanged), &args);
        CHECK(calls == 1);
        fake::Expire(wolf);
        CHECK(!c->Connected());
    }

    // On(signal): owned by the extension like On<Tag>; a filtered signal only passes what it accepts.
    int big = 0;
    Listener bigTicks = On(SignalOf<Update>().Filter([](const UpdateArgs& a) { return a.dt > 1.0f; }), [&](auto&) { ++big; });
    Raise<ev::Event::OnUpdate>({ 0.5f, 4 });
    Raise<ev::Event::OnUpdate>({ 1.5f, 5 });
    CHECK(big == 1 && bigTicks.Active());

    // OnReady: a service published before the call runs it at once; one published later, at the next poll.
    static const FakeService s_v1{ sizeof(FakeService), 1 }, s_v3{ sizeof(FakeService), 3 };
    {
        Service<Probe<1>> early;
        CHECK(!early);
        CHECK(Publish<Probe<1>>(&s_v1));
        int seen = 0;
        early.OnReady([&](Probe<1>& p) { seen = p.Value(); });
        CHECK(seen == 1);

        Service<Probe<2>> late;
        int lateSeen = 0, calls = 0;
        late.OnReady([&](auto& p) { lateSeen = p.Value(); ++calls; });
        CHECK(lateSeen == 0 && Live(ev::Event::OnExtensionsLoaded) == 1);
        Raise<ev::Event::OnUpdate>({ 0.1f, 6 });
        CHECK(lateSeen == 0);
        CHECK(Publish<Probe<3>>(&s_v3));
        Raise<ev::Event::OnExtensionsLoaded>({ 1 });
        Raise<ev::Event::OnUpdate>({ 0.1f, 7 });
        CHECK(lateSeen == 3 && calls == 1 && Live(ev::Event::OnExtensionsLoaded) == 0);
    }

    // Service: the highest published version at or above the traits' one, then kept.
    {
        Service<Probe<1>> v1;
        Service<Probe<2>> v2;
        Service<Probe<4>> v4;
        CHECK(v1 && v1->Value() == 3 && v2 && v2->Value() == 3 && !v4);
        CHECK(std::strcmp(v1.Name(), "probe.service") == 0 && v4.Version() == 4);
    }

    // Stop: OnStop runs, then every override and every On handler of the extension ends.
    CHECK(detail::ListenerCount() > 0);
    detail::Lifecycle<Both>::Stop(both);
    CHECK(both.stops == 1 && both.Connected() == 0 && !bigTicks.Active());
    CHECK(detail::ListenerCount() == 0);
    detail::Lifecycle<Derived>::Stop(derived);
    detail::Lifecycle<Thrower>::Stop(thrower);
    CHECK(Live(ev::Event::OnUpdate) == 0 && Live(ev::Event::OnWorldEnter) == 0 && Live(ev::Event::OnInput) == 0);

    // The manifest reader: extension.id and extension.version, whatever surrounds them.
    {
        const char* json = R"({ "manifest": 1, "id": "outer", "extension": { "requires": { "id": "x" },
                               "id": "wxl-probe", "version": "2.5.1", "abi": "1.3" }, "version": "9" })";
        char id[32] = "", version[16] = "";
        CHECK(detail::ManifestText(json).Find("extension", "id", id, sizeof id) && std::strcmp(id, "wxl-probe") == 0);
        CHECK(detail::ManifestText(json).Find("extension", "version", version, sizeof version));
        CHECK(std::strcmp(version, "2.5.1") == 0 && detail::PackVersion(version) == (2u << 16 | 5u << 8 | 1u));
        CHECK(!detail::ManifestText("{}").Find("extension", "id", id, sizeof id));
    }

    // The null handles of the object model, and the asset views without a client behind them.
    Object none;
    CHECK(none.IsNull() && !none && !none.IsUnit() && none.Guid().Code() == Error::NullHandle);
    CHECK(none.AsUnit().IsNull() && none.AsPlayer().IsNull() && none.AsGameObject().IsNull());
    Unit noUnit;
    CHECK(!noUnit.Reaction(noUnit) && !noUnit.Model() && !noUnit.Health());
    {
        Listener watch = On(noUnit.HealthChanged(), [](const Change<uint32_t>&) {});
        CHECK(watch.Active());
        Raise<ev::Event::OnUpdate>({ 0.1f, 8 }); // the read answers NullHandle: the watch ends
        CHECK(!watch.Active());
    }

    Doodad noDoodad;
    CHECK(!noDoodad && !noDoodad.Instance() && !noDoodad.ModelName());
    CHECK(noDoodad.Scale() == 1.0f && noDoodad.Position().x == 0.0f && noDoodad.Center().z == 0.0f);
    Vec3 lo{}, hi{};
    CHECK(!noDoodad.BBox(lo, hi) && !noDoodad.LocalBounds(lo, hi));
    char nameBuf[8] = { 'x' };
    CHECK(!noDoodad.ModelName(nameBuf, sizeof nameBuf));

    MapChunk noChunk;
    CHECK(!noChunk && noChunk.NearObjectCount(0) == 0);
    int visited = 0;
    CHECK(noChunk.ForEachDoodad([&](Doodad) { ++visited; }) == 0 && visited == 0);

    MapTile noTile;
    CHECK(!noTile && noTile.TileFirst() < 0 && noTile.TileSecond() < 0);

    // A null camera: the field-of-view binding answers with its own fallback and the setter no-ops.
    Camera noCamera;
    CHECK(!noCamera && noCamera.Fov() > 0.0f);
    noCamera.SetFov(1.0f);

    Model noModel;
    Wmo noWmo;
    WmoGroup noWmoGroup;
    CHECK(!noModel && !noWmo && !noWmoGroup);

    // Hook: an unknown point fails and leaves the chain link null; a known one fills it, and calling
    // the hook passes through to what the core handed back.
    Hook<ProbeFn> hook;
    CHECK(!hook && !hook.Original());
    CHECK(!hook.Attach("Probe.Unknown", &ChainEnd));
    CHECK(!hook);
    CHECK(hook.Attach("Probe.Point", &ChainEnd, 3));
    CHECK(hook && hook.Original() == &ChainEnd);
    CHECK(std::strcmp(g_attachedName, "Probe.Point") == 0 && g_attachedPriority == 3);
    CHECK(hook(41) == 42);

    // A function-pointer alias names the same hook: this is the form every offsets/ header declares,
    // and the two spellings must land on one type.
    Hook<ProbeFnPtr> aliased;
    CHECK(aliased.Attach("Probe.Point", &ChainEnd));
    CHECK(aliased(41) == 42);
    static_assert(std::is_same_v<Hook<ProbeFnPtr>::Function, Hook<ProbeFn>::Function>,
                  "Hook must accept a function type and a pointer to it alike");

    std::printf("%d failure(s)\n", g_failures);
    ExitProcess(g_failures ? 1 : 0); // the SDK's statics still reach the fake core at exit
}
