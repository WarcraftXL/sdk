// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The conformance checks a runtime has to pass. Run here against the fake host; a real runtime
// passes the same file by linking its own host in place of the fake one.

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <set>
#include <stdexcept>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "fake_host.hpp"
#include "fake_services.hpp"
#include "wxl/services.hpp"
#include "wxl/services/imgui.h"
#include "wxl/engine/check.hpp"

namespace
{
    int g_failures = 0;

    void Check(bool condition, const char* what)
    {
        if (condition) return;
        std::printf("  FAIL  %s\n", what);
        ++g_failures;
    }

    void Section(const char* title) { std::printf("%s\n", title); }

    // --- the event table -------------------------------------------------------------------

    /// Every id the table declares, in table order.
    constexpr uint32_t kEventIds[] = {
#define WXL_EVENT(family, name, id, args) id,
#include "wxl/events.def"
#undef WXL_EVENT
    };

    constexpr size_t kEventRows = sizeof(kEventIds) / sizeof(kEventIds[0]);

    /// Ids are the ABI: a duplicate silently routes one event's args to another's handler.
    bool IdsAreUnique()
    {
        for (size_t i = 0; i < kEventRows; ++i)
            for (size_t j = i + 1; j < kEventRows; ++j)
                if (kEventIds[i] == kEventIds[j]) return false;
        return true;
    }

    /// No gaps: a hole means an id was removed, which the table forbids.
    bool IdsAreDense()
    {
        for (uint32_t id = 0; id < kEventRows; ++id)
        {
            bool found = false;
            for (size_t i = 0; i < kEventRows && !found; ++i) found = kEventIds[i] == id;
            if (!found) return false;
        }
        return true;
    }

    constexpr uint32_t CountFamilyWorld()
    {
        uint32_t n = 0;
#define WXL_EVENT_World(name, id, args) ++n;
#include "wxl/events.def"
#undef WXL_EVENT_World
        return n;
    }

    constexpr uint32_t CountFamilyRender()
    {
        uint32_t n = 0;
#define WXL_EVENT_Render(name, id, args) ++n;
#include "wxl/events.def"
#undef WXL_EVENT_Render
        return n;
    }

    // --- scripts under test ----------------------------------------------------------------

    struct CountingWorldScript final : wxl::WorldScript
    {
        int      updates     = 0;
        int      worldEnters = 0;
        float    lastDelta   = 0.0f;
        uint32_t lastMap     = 0;

        void OnUpdate(float delta) override
        {
            ++updates;
            lastDelta = delta;
        }

        void OnWorldEnter(uint32_t map_id, wxl_vec3) override
        {
            ++worldEnters;
            lastMap = map_id;
        }

        void OnInput(uint32_t message, uint64_t, int64_t, wxl::Answer handled) override
        {
            if (!handled.Value() && message == 0x100) handled.Set();
        }
    };

    struct CountingRenderScript final : wxl::RenderScript
    {
        int frames = 0;
        void OnFrameBegin(uint64_t, float) override { ++frames; }
    };

    struct SecondWorldScript final : wxl::WorldScript
    {
        int updates = 0;
        void OnUpdate(float) override { ++updates; }
    };

    struct CountingNetScript final : wxl::NetScript
    {
        int      received   = 0;
        uint32_t lastOpcode = 0;

        void OnMessageReceived(uint32_t opcode, wxl::Bytes, wxl::Answer suppressed) override
        {
            ++received;
            lastOpcode = opcode;
            if (opcode == 0xF001) suppressed.Set();
        }
    };

    // What a script handles. Each is a way an override could hide from the check that decides
    // what to subscribe; each must still be subscribed, and must still run.
    struct NothingWorldScript final : wxl::WorldScript
    {
    };

    struct LeaveWorldScript final : wxl::WorldScript
    {
        int leaves = 0;
        void OnWorldLeave(uint32_t, wxl_vec3) override { ++leaves; }
    };

    struct PrivateWorldScript final : wxl::WorldScript
    {
        int clicks = 0;

    private:
        void OnWorldClick(wxl_vec3, wxl::Object, wxl_mouse_button) override { ++clicks; }
    };

    struct ProtectedWorldScript final : wxl::WorldScript
    {
        int sounds = 0;

    protected:
        void OnSoundPlay(uint32_t, std::string_view, wxl::Answer) override { ++sounds; }
    };

    struct MidWorldScript : wxl::WorldScript
    {
        int updates = 0;
        void OnUpdate(float) override { ++updates; }
    };

    struct LeafWorldScript final : MidWorldScript
    {
    };

    /// Changes its own family from inside its handler.
    struct RearrangingWorldScript final : wxl::WorldScript
    {
        int                updates    = 0;
        SecondWorldScript* add        = nullptr;
        SecondWorldScript* remove     = nullptr;
        bool               removeSelf = false;

        void OnUpdate(float) override
        {
            ++updates;
            if (add) wxl::ScriptMgr::Add(std::exchange(add, nullptr));
            if (remove) wxl::ScriptMgr::Remove(std::exchange(remove, nullptr));
            if (removeSelf) wxl::ScriptMgr::Remove(this);
        }
    };

    struct CountingLuaScript final : wxl::LuaScript
    {
        int      ready       = 0;
        int      closing     = 0;
        uint32_t lastContext = 0;

        void OnScriptVmReady(wxl_script_context context, void*) override
        {
            ++ready;
            lastContext = context;
        }

        void OnScriptVmClosing(wxl_script_context, void*) override { ++closing; }
    };

    /// A typed handler for the free-standing Subscribe.
    struct TickHandler
    {
        int   ticks = 0;
        float last  = 0.0f;
        void  operator()(float delta)
        {
            ++ticks;
            last = delta;
        }
    };

    /// Reads every kind of parameter a handler can be given, and answers through the writable ones.
    struct ParameterScript final : wxl::AssetScript
    {
        std::string path;
        std::string redirectSeen;
        uint32_t    capacity = 0;
        bool        wrote    = false;
        std::string body;

        void OnFileOpen(std::string_view asked, wxl::Buffer redirect) override
        {
            path         = std::string(asked);
            redirectSeen = std::string(redirect.Value());
            capacity     = redirect.Capacity();
            wrote        = redirect.Write("Textures\\swapped.blp");
        }

        void OnMapObjectLoad(std::string_view, wxl::Bytes data) override
        {
            body.assign(data.View());
            for (uint8_t byte : data) body += static_cast<char>('0' + byte % 10);
        }
    };

    /// The model events of three families: the loader's stages, an appearance, a drawn batch.
    struct ModelStages final : wxl::AssetScript
    {
        std::string parsed, skinned, sequenced;
        void*       file = nullptr;
        uint32_t    index = 0, bytes = 0;
        void OnModelParsed(std::string_view path, void* f, void* header, uint32_t size) override
        {
            parsed = std::string(path), file = f, bytes = header ? size : 0;
        }
        void OnSkinProfileParsed(std::string_view path, void*, void*, void* skin) override
        {
            if (skin) skinned = std::string(path);
        }
        void OnModelSequenceLoad(std::string_view path, void*, uint32_t sequence, wxl::Bytes data) override
        {
            sequenced = std::string(path) + ":" + std::string(data.View());
            index     = sequence;
        }
    };

    struct Wardrobe final : wxl::ObjectScript
    {
        wxl_appearance_change change = WXL_APPEARANCE_FREED;
        void*                 model  = nullptr;
        uint32_t              slot = 0, display = 0;
        bool                  npc = true;
        void OnAppearanceChanged(wxl_appearance_change c, void*, void* m, uint32_t, uint32_t, uint32_t s, uint32_t d,
                                 bool n) override
        {
            change = c, model = m, slot = s, display = d, npc = n;
        }
    };

    struct Outline final : wxl::RenderScript
    {
        uint32_t primitives = 0;
        int32_t  base       = 0;
        float    depth      = 0.0f;
        void OnModelBatchDrawn(void*, void*, uint32_t, int32_t base_vertex, uint32_t, uint32_t, uint32_t, uint32_t count,
                               uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, float, float max_z, void*) override
        {
            primitives = count, base = base_vertex, depth = max_z;
        }
    };

    /// The gameplay families: direct parameters, enums as their C types, answers through wxl::Answer.
    struct GameplayLoot final : wxl::LootScript
    {
        wxl_loot_kind kind   = WXL_LOOT_CORPSE;
        uint32_t      money  = 0;
        int32_t       suffix = 0;
        void OnLootOpened(wxl::Object, wxl_guid, wxl_loot_kind k, uint32_t m, uint32_t) override { kind = k, money = m; }
        void OnLootSlot(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, int32_t property,
                        wxl_loot_slot_type) override
        {
            suffix = property;
        }
    };

    struct GameplayGroup final : wxl::GroupScript
    {
        std::string inviter;
        void OnGroupInvite(std::string_view who, bool canAccept, wxl::Answer accept, wxl::Answer) override
        {
            inviter = std::string(who);
            if (canAccept) accept.Set();
        }
    };

    struct GameplaySocial final : wxl::SocialScript
    {
        wxl_window_kind window = WXL_WINDOW_GOSSIP;
        void OnWindowOpened(wxl_window_kind kind, wxl::Object, wxl_guid) override { window = kind; }
        void OnResurrectRequest(wxl::Object, wxl_guid, std::string_view, bool, wxl::Answer, wxl::Answer decline) override
        {
            decline.Set();
        }
    };

    struct GameplayQuest final : wxl::QuestScript
    {
        uint32_t objective = 0, current = 0;
        void OnQuestObjectiveChanged(uint32_t, uint32_t, uint32_t which, uint32_t, uint32_t now) override
        {
            objective = which, current = now;
        }
    };

    /// Sees what an earlier handler answered.
    struct LateNetScript final : wxl::NetScript
    {
        bool sawAnswer = false;
        void OnMessageReceived(uint32_t, wxl::Bytes, wxl::Answer suppressed) override
        {
            sawAnswer = suppressed.Value();
        }
    };

    // --- a mod under test -------------------------------------------------------------------

    struct LifecycleMod final : wxl::Mod
    {
        static inline std::vector<std::string> trace;
        static inline CountingWorldScript*     script = nullptr;
        static inline std::string              throwIn; ///< the method that throws; OnStage throws an int

        static void Trace(const char* step, const char* method)
        {
            trace.push_back(step);
            if (throwIn != method) return;
            if (throwIn == "OnStage") throw 42;
            throw std::runtime_error(std::string("boom in ") + method);
        }

        wxl_result OnLoad() override
        {
            if (script) wxl::ScriptMgr::Add(script);
            Trace("load", "OnLoad");
            return WXL_OK;
        }
        void OnEarly() override { Trace("early", "OnEarly"); }
        void OnStorageReady() override { Trace("storage", "OnStorageReady"); }
        void OnGraphicsReady() override { Trace("graphics", "OnGraphicsReady"); }
        void OnScriptReady() override { Trace("script", "OnScriptReady"); }
        void OnReady() override { Trace("ready", "OnReady"); }
        void OnShutdown() override { Trace("shutdown", "OnShutdown"); }
        void OnStage(wxl_stage stage) override
        {
            Trace(("stage" + std::to_string(stage)).c_str(), "OnStage");
        }
        void OnUnload() override { Trace("unload", "OnUnload"); }
    };

    /// A script that throws from its handler, every time.
    struct ThrowingWorldScript final : wxl::WorldScript
    {
        int  calls = 0;
        void OnUpdate(float) override
        {
            ++calls;
            throw std::runtime_error("tick");
        }
    };

    /// A handler for wxl::Subscribe that throws what is not a std::exception.
    struct ThrowingHandler
    {
        int  calls = 0;
        void operator()(float)
        {
            ++calls;
            throw 7;
        }
    };

    /// Script functions as members of an object, one of them throwing.
    struct Counter
    {
        int  count = 0;
        void Next(wxl::ScriptCall& call) { call.PushNumber(++count); }
        void Peek(wxl::ScriptCall& call) const { call.PushNumber(count); }
        void Throws(wxl::ScriptCall&) { throw std::runtime_error("no counting today"); }
    };

    // --- script-side functions --------------------------------------------------------------

    void Greet(wxl::ScriptCall& call)
    {
        const std::optional<std::string_view> name = call.String(0);
        if (!name)
        {
            call.Fail("Greet(name) wants a string");
            return;
        }
        call.PushString(*name);
        call.PushNumber(static_cast<double>(call.ArgCount()));
        call.PushBool(call.Bool(1).value_or(false));
    }

    struct Adder
    {
        double total = 0.0;
        void   operator()(wxl::ScriptCall& call)
        {
            total += call.Number(0).value_or(0.0);
            call.PushNumber(total);
        }
    };

    // --- panels and hooks -------------------------------------------------------------------

    int g_panelDraws = 0;
    void DrawPanel()
    {
        ++g_panelDraws;
        wxl::UiService().Text("hello");
    }

    using ThingFn = int(WXL_CALL*)(int);
    int WXL_CALL ThingDetour(int x) { return x + 1; }
} // namespace

// The entry points the WXL_MOD macro writes, exercised in-process.
WXL_MOD(LifecycleMod, "wxl.contract", "Contract")


// --- A point service that records what is attached, for the typed facade -----------------------
namespace fakepoint
{
    struct Attached
    {
        uint32_t     point, side;
        wxl_point_fn fn;
        void*        user;
        uint32_t     hash;
        bool         live;
    };
    std::vector<Attached> g_attached;
    int                   g_originals = 0;

    uint32_t WXL_CALL List(wxl_point_info*, uint32_t) { return 0; }
    wxl_result WXL_CALL Find(const char* name, uint32_t* out)
    {
        uint32_t id = 0;
#define WXL_POINT(Ident, n, ret, params, lowered, hash, policy, thread, stability, runtimes) \
        ++id;                                                                              \
        if (std::strcmp(n, name) == 0) { *out = id; return WXL_OK; }
#include "wxl/points.def"
#undef WXL_POINT
        return WXL_E_NOT_FOUND;
    }
    uint32_t WXL_CALL Attach(uint32_t point, uint32_t side, wxl_point_fn fn, void* user, int32_t, uint32_t hash)
    {
        g_attached.push_back(Attached{point, side, fn, user, hash, true});
        return static_cast<uint32_t>(g_attached.size());
    }
    wxl_result WXL_CALL Detach(uint32_t id)
    {
        if (id == 0 || id > g_attached.size() || !g_attached[id - 1].live) return WXL_E_NOT_FOUND;
        g_attached[id - 1].live = false;
        return WXL_OK;
    }
    /// The original of the camera's point: answers data + param, as addresses.
    void WXL_CALL CallOriginal(wxl_point_call* call)
    {
        ++g_originals;
        const uintptr_t data  = reinterpret_cast<uintptr_t>(*static_cast<void**>(call->args[0]));
        const uintptr_t param = reinterpret_cast<uintptr_t>(*static_cast<void**>(call->args[1]));
        *static_cast<int32_t*>(call->result) = static_cast<int32_t>(data + param);
    }
    const wxl_point_api g_api = {sizeof(wxl_point_api), &List, &Find, &Attach, &Detach, &CallOriginal};

    /// Runs the live handlers of the camera point on a call, side by side, as a runtime would.
    int32_t Run(uintptr_t data, uintptr_t param)
    {
        void*          a = reinterpret_cast<void*>(data);
        void*          b = reinterpret_cast<void*>(param);
        void* const    args[] = {&a, &b};
        int32_t        result = 0;
        wxl_point_call call{sizeof(wxl_point_call), 1, 0, 1, 0, 2, args, &result, {0, 0, 0}};
        for (uint32_t side : {WXL_POINT_BEFORE, WXL_POINT_REPLACE, WXL_POINT_AFTER})
        {
            call.side  = side;
            bool ran   = false;
            for (const Attached& one : g_attached)
                if (one.live && one.side == side)
                {
                    one.fn(&call, one.user);
                    ran = true;
                }
            if (side == WXL_POINT_REPLACE && !ran) CallOriginal(&call);
        }
        return result;
    }
}

int main()
{
    // Unbuffered, so the last line printed before a crash is never held back in a buffer.
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    wxltest::FakeHost host;
    wxl::detail::HostRef() = host.Table();

    Section("event table");
    {
        Check(WXL_EV_COUNT == kEventRows, "the enum holds exactly the rows the table declares");
        Check(WXL_EV_Update == 0, "the first id is 0");
        Check(IdsAreUnique(), "no id is used twice");
        Check(IdsAreDense(), "the ids run 0..n-1 with no hole");
        Check(CountFamilyWorld() + CountFamilyRender() < WXL_EV_COUNT,
              "no family holds the whole table");
        Check(std::is_same_v<wxl::EventArgsOf<WXL_EV_Update>, wxl_update_args> &&
                  std::is_same_v<wxl::EventArgsOf<WXL_EV_MessageReceived>, wxl_message_args>,
              "EventArgs binds each id to its args struct");
    }

    Section("handler parameters");
    {
        Check(std::is_same_v<wxl::HandlerOf<WXL_EV_Update>, void(float)>,
              "a handler takes the members as parameters");
        Check(std::is_same_v<wxl::HandlerOf<WXL_EV_MessageReceived>,
                             void(uint32_t, wxl::Bytes, wxl::Answer)>,
              "a bytes member is a view, and the member holding its length is not a parameter");
        Check(std::is_same_v<wxl::HandlerOf<WXL_EV_FileOpen>, void(std::string_view, wxl::Buffer)>,
              "a str is a string_view, a buffer a view whose capacity is not a parameter");
        Check(std::is_same_v<wxl::HandlerOf<WXL_EV_WorldEnter>, void(uint32_t, wxl_vec3)> &&
                  std::is_same_v<wxl::HandlerOf<WXL_EV_UnitAuraChanged>,
                                 void(wxl::Object, uint32_t, bool)>,
              "values, vectors, objects and booleans keep their order");
        Check(std::is_same_v<wxl::HandlerOf<WXL_EV_ScriptVmReady>, void(wxl_script_context, void*)> &&
                  std::is_same_v<wxl::HandlerOf<WXL_EV_WorldClick>,
                                 void(wxl_vec3, wxl::Object, wxl_mouse_button)>,
              "a member holding an enumeration's values arrives as that enum");
        Check(std::is_same_v<wxl::HandlerOf<WXL_EV_WorldClickRequest>,
                             void(wxl_mouse_button, wxl_world_hit, wxl::Object, wxl_guid, wxl_vec3, bool, wxl::Answer)>,
              "a click before the game acts: its button, what it met, the object, its guid, the point, whether a spell aims, and the answer");
        Check(std::is_same_v<wxl::HandlerOf<WXL_EV_InterfaceSignature>,
                             void(std::string_view, std::string_view, wxl_interface_signature, wxl::Answer)>,
              "an interface's signature: the .toc, the file hashed with it, the client's verdict, and the answer");
        Check(std::is_same_v<wxl::HandlerOf<WXL_EV_InterfaceCleanupRequest>, void(wxl::Answer)>,
              "the clearing of loose interface folders: the answer alone");
        Check(std::is_same_v<wxl::HandlerOf<WXL_EV_ModelLoad>,
                             void(std::string_view, wxl::Bytes, const void**, uint32_t*)>,
              "a ptr member keeps its C type");

        uint8_t     raw[3] = {1, 2, 3};
        wxl::Bytes  bytes(raw, 3);
        wxl::Bytes  none(nullptr, 5);
        Check(bytes.size() == 3 && bytes[2] == 3 && !bytes.empty() && none.empty() &&
                  none.View().empty(),
              "Bytes is a range, and empty over a null pointer whatever the length says");

        wxl_bool    flag = WXL_FALSE;
        wxl::Answer answer(&flag);
        Check(!answer.Value() && answer.Set() && flag == WXL_TRUE && answer.Value(),
              "Answer reads the flag and sets it");
        Check(!wxl::Answer(nullptr).Set() && !wxl::Answer(nullptr).Value(),
              "an Answer without a flag answers nothing");

        char        storage[8] = {};
        wxl::Buffer buffer(storage, sizeof storage);
        Check(buffer.Value().empty() && buffer.Capacity() == 8, "a Buffer starts empty");
        Check(buffer.Write("1234567") && buffer.Value() == "1234567" && storage[7] == '\0',
              "a string that fits with its terminator is written");
        Check(!buffer.Write("12345678") && buffer.Value() == "1234567",
              "one that does not is refused and leaves the buffer as it was");
        Check(!wxl::Buffer(nullptr, 8).Write("") && wxl::Buffer(nullptr, 8).Capacity() == 0,
              "a Buffer without storage takes nothing");
    }

    Section("version negotiation");
    {
        const wxl_runtime_info* info = host.Table()->runtime();
        Check(info != nullptr, "the runtime describes itself");
        Check(info->abi_major == WXL_ABI_VERSION_MAJOR, "majors agree");
        Check(info->abi_minor == WXL_ABI_VERSION_MINOR, "and, under major 0, minors");
        Check(info->abi_feature >= WXL_ABI_VERSION_FEATURE,
              "the runtime is not older than what this mod was built against");
        Check(host.Table()->abi_major == info->abi_major && host.Table()->abi_minor == info->abi_minor &&
                  host.Table()->abi_feature == info->abi_feature,
              "the table and the runtime info say the same version");
        Check(info->struct_size == sizeof(wxl_runtime_info), "struct_size is filled in");
    }

    Section("subscriptions are owned");
    {
        const size_t before = host.LiveSubscriptions();
        const size_t calls  = host.UnsubscribeCalls();
        TickHandler  handler;
        {
            wxl::Subscription tick = wxl::Subscribe<WXL_EV_Update>(&handler);
            Check(static_cast<bool>(tick), "a typed subscribe takes");
            Check(host.LiveSubscriptions() == before + 1, "and is live");

            const wxl_update_args args{0.25f};
            host.Raise(WXL_EV_Update, &args);
            Check(handler.ticks == 1 && handler.last == 0.25f, "the handler sees typed args");

            wxl::Subscription moved = std::move(tick);
            Check(!tick && moved, "a move hands the token over");
            Check(host.LiveSubscriptions() == before + 1, "without releasing it");

            moved.Reset();
            moved.Reset();
            Check(host.LiveSubscriptions() == before, "Reset drops it");
            Check(host.UnsubscribeCalls() == calls + 1, "exactly once, however often Reset runs");
        }
        Check(host.UnsubscribeCalls() == calls + 1, "and the destructor adds nothing after a Reset");

        {
            wxl::Subscription scoped = wxl::Subscribe<WXL_EV_Update>(&handler);
            Check(host.LiveSubscriptions() == before + 1, "a scoped subscription is live inside");
        }
        Check(host.LiveSubscriptions() == before, "and gone at the end of its scope");
        Check(host.UnsubscribeCalls() == calls + 2, "released by the destructor, once");

        wxl_subscription raw = WXL_SUBSCRIPTION_NONE;
        {
            wxl::Subscription kept = wxl::Subscribe<WXL_EV_Update>(&handler);
            raw                    = kept.Release();
        }
        Check(raw != WXL_SUBSCRIPTION_NONE && host.LiveSubscriptions() == before + 1,
              "Release disowns: the subscription outlives the wrapper");
        host.Table()->unsubscribe(raw);

        wxl::Subscription      orphan      = wxl::Subscribe<WXL_EV_Update>(&handler);
        const wxl_subscription orphanToken = orphan.Get();
        wxl::detail::HostRef()             = nullptr;
        orphan.Reset();
        Check(host.UnsubscribeCalls() == calls + 3 && host.LiveSubscriptions() == before + 1,
              "a wrapper outliving the host does not call into it");
        wxl::detail::HostRef() = host.Table();
        host.Table()->unsubscribe(orphanToken);

        Check(!wxl::Subscribe<WXL_EV_Update>(static_cast<TickHandler*>(nullptr)),
              "a null handler yields an empty subscription");

        uint32_t seenOpcode = 0, seenSize = 0;
        auto     onMessage  = [&](uint32_t opcode, wxl::Bytes data, wxl::Answer suppressed) {
            seenOpcode = opcode;
            seenSize   = data.size();
            suppressed.Set();
        };
        wxl::Subscription message = wxl::Subscribe<WXL_EV_MessageReceived>(&onMessage);
        wxl_bool               kept    = WXL_FALSE;
        const uint8_t          body[2] = {7, 8};
        const wxl_message_args args{0x42, body, sizeof body, &kept};
        host.Raise(WXL_EV_MessageReceived, &args);
        Check(seenOpcode == 0x42 && seenSize == 2 && kept == WXL_TRUE,
              "a lambda takes the parameters and answers through them");
    }

    Section("script dispatch");
    CountingWorldScript  world;
    CountingRenderScript render;
    SecondWorldScript    secondWorld;
    CountingNetScript    net;
    CountingLuaScript    lua;
    {
        const size_t before = host.LiveSubscriptions();
        wxl::ScriptMgr::Add(&world);
        const size_t afterWorld = host.LiveSubscriptions();
        // CountingWorldScript overrides Update, WorldEnter and Input: three of its family's six.
        Check(afterWorld - before == 3 && afterWorld - before < CountFamilyWorld(),
              "a script subscribes the events it overrides, not its whole family");

        wxl::ScriptMgr::Add(&secondWorld);
        Check(host.LiveSubscriptions() == afterWorld,
              "a second script of a family costs no further subscription");

        wxl::ScriptMgr::Add(&render);
        wxl::ScriptMgr::Add(&net);
        wxl::ScriptMgr::Add(&lua);
        Check(host.LiveSubscriptions() > afterWorld, "other families subscribe on their own");

        const wxl_update_args tick{0.016f};
        host.Raise(WXL_EV_Update, &tick);

        Check(world.updates == 1, "an override runs");
        Check(secondWorld.updates == 1, "every script of the family runs");
        Check(world.lastDelta == 0.016f, "the args arrive intact");
        Check(render.frames == 0, "an event does not reach another family");

        const wxl_frame_args frame{1, 0.016f};
        host.Raise(WXL_EV_FrameBegin, &frame);
        Check(render.frames == 1, "the render family runs on its own events");
        Check(world.updates == 1, "and does not disturb the first");

        const wxl_world_args entered{571, {100.0f, 200.0f, 300.0f}};
        host.Raise(WXL_EV_WorldEnter, &entered);
        Check(world.worldEnters == 1 && world.lastMap == 571, "a second event of a family runs");

        wxl_bool               suppressed = WXL_FALSE;
        const uint8_t          body[4]    = {1, 2, 3, 4};
        const wxl_message_args message{0xF001, body, sizeof(body), &suppressed};
        host.Raise(WXL_EV_MessageReceived, &message);
        Check(net.received == 1 && net.lastOpcode == 0xF001, "the net family runs");
        Check(suppressed == WXL_TRUE, "a mod can take an opcode the client does not know");

        LateNetScript late;
        wxl::ScriptMgr::Add(&late);
        suppressed = WXL_FALSE;
        host.Raise(WXL_EV_MessageReceived, &message);
        Check(late.sawAnswer, "a later handler sees the answer an earlier one gave");
        wxl::ScriptMgr::Remove(&late);

        ParameterScript asset;
        wxl::ScriptMgr::Add(&asset);
        char                     redirect[32] = "Textures\\first.blp";
        const char*              asked        = "Textures\\asked.blp";
        const wxl_file_open_args open{wxl::Str(asked), redirect, sizeof redirect};
        host.Raise(WXL_EV_FileOpen, &open);
        Check(asset.path == asked && asset.redirectSeen == "Textures\\first.blp" &&
                  asset.capacity == sizeof redirect,
              "a str and a buffer arrive as views, with what an earlier handler wrote");
        Check(asset.wrote && std::strcmp(redirect, "Textures\\swapped.blp") == 0,
              "and a handler answers by writing into the buffer");
        const uint8_t             root[3] = {'a', 1, 12};
        const wxl_map_object_args mapObject{wxl::Str("x.wmo"), root, sizeof root};
        host.Raise(WXL_EV_MapObjectLoad, &mapObject);
        Check(asset.body == std::string("a\x01\x0c", 3) + "712", "bytes arrive with their length");
        wxl::ScriptMgr::Remove(&asset);

        const wxl_script_vm_args vm{WXL_SCRIPT_CONTEXT_GAME, nullptr};
        host.Raise(WXL_EV_ScriptVmReady, &vm);
        host.Raise(WXL_EV_ScriptVmClosing, &vm);
        Check(lua.ready == 1 && lua.closing == 1, "the script VM family sees both ends");
        Check(lua.lastContext == WXL_SCRIPT_CONTEXT_GAME, "and which context it was");

        wxl::ScriptMgr::Remove(&secondWorld);
        host.Raise(WXL_EV_Update, &tick);
        Check(world.updates == 2 && secondWorld.updates == 1, "a removed script no longer runs");

        wxl::ScriptMgr::Clear();
        Check(host.LiveSubscriptions() == before, "Clear releases every family subscription");
        host.Raise(WXL_EV_Update, &tick);
        Check(world.updates == 2, "and nothing runs afterwards");

        wxl::ScriptMgr::Add(&world);
        Check(host.LiveSubscriptions() == afterWorld, "a family subscribes again after Clear");
        wxl::ScriptMgr::Clear();
    }

    Section("the gameplay families");
    {
        GameplayLoot   loot;
        GameplayGroup  group;
        GameplaySocial social;
        GameplayQuest  quest;
        wxl::ScriptMgr::Add(&loot);
        wxl::ScriptMgr::Add(&group);
        wxl::ScriptMgr::Add(&social);
        wxl::ScriptMgr::Add(&quest);

        wxl_loot_args opened{};
        opened.kind = WXL_LOOT_SKINNING, opened.money = 7;
        host.Raise(WXL_EV_LootOpened, &opened);
        wxl_loot_slot_args slot{};
        slot.random_property_id = -40;
        host.Raise(WXL_EV_LootSlot, &slot);
        Check(loot.kind == WXL_LOOT_SKINNING && loot.money == 7 && loot.suffix == -40,
              "LootScript: the kind as wxl_loot_kind, a signed property as int32_t");

        wxl_bool              accept = WXL_FALSE, decline = WXL_FALSE;
        wxl_group_invite_args invite{wxl::Str("Jaina"), WXL_TRUE, &accept, &decline};
        host.Raise(WXL_EV_GroupInvite, &invite);
        Check(group.inviter == "Jaina" && accept == WXL_TRUE && decline == WXL_FALSE,
              "GroupScript: OnGroupInvite answers through its accept");

        wxl_window_args window{};
        window.kind = WXL_WINDOW_TRAINER;
        host.Raise(WXL_EV_WindowOpened, &window);
        accept = decline = WXL_FALSE;
        wxl_resurrect_request_args offer{};
        offer.accept = &accept, offer.decline = &decline;
        host.Raise(WXL_EV_ResurrectRequest, &offer);
        Check(social.window == WXL_WINDOW_TRAINER && decline == WXL_TRUE && accept == WXL_FALSE,
              "SocialScript: a window by kind, a resurrection declined");

        const wxl_quest_objective_args moved{783, 0, 2, 4, 5};
        host.Raise(WXL_EV_QuestObjectiveChanged, &moved);
        Check(quest.objective == 2 && quest.current == 5, "QuestScript: the objective and its count");
        wxl::ScriptMgr::Clear();
    }

    Section("the model events: the loader's stages, appearances, drawn batches");
    {
        ModelStages stages;
        Wardrobe    wardrobe;
        Outline     outline;
        wxl::ScriptMgr::Add(&stages);
        wxl::ScriptMgr::Add(&wardrobe);
        wxl::ScriptMgr::Add(&outline);

        int                         shared = 0, header = 0, skin = 0;
        const wxl_model_parsed_args parsed{wxl::Str("Creature\\Boar\\Boar.m2"), &shared, &header, 4096};
        host.Raise(WXL_EV_ModelParsed, &parsed);
        const wxl_skin_profile_parsed_args skinned{wxl::Str("Creature\\Boar\\Boar.m2"), &shared, &header, &skin};
        host.Raise(WXL_EV_SkinProfileParsed, &skinned);
        const wxl_model_sequence_args sequence{wxl::Str("Boar.m2"), &shared, 7, "anim", 4};
        host.Raise(WXL_EV_ModelSequenceLoad, &sequence);
        Check(stages.parsed == "Creature\\Boar\\Boar.m2" && stages.file == &shared && stages.bytes == 4096 &&
                  stages.skinned == stages.parsed && stages.sequenced == "Boar.m2:anim" && stages.index == 7,
              "AssetScript: the model's file and header as addresses, a sequence's bytes as wxl::Bytes");

        int                       wearer = 0, body = 0;
        const wxl_appearance_args worn{WXL_APPEARANCE_WORN, &wearer, &body, 1, 1, 4, 416036, WXL_FALSE};
        host.Raise(WXL_EV_AppearanceChanged, &worn);
        Check(wardrobe.change == WXL_APPEARANCE_WORN && wardrobe.model == &body && wardrobe.slot == 4 &&
                  wardrobe.display == 416036 && !wardrobe.npc,
              "ObjectScript: the change as wxl_appearance_change, the npc flag as bool");

        wxl_model_batch_args batch{};
        batch.base_vertex = -12, batch.primitive_count = 40, batch.viewport_max_z = 1.0f;
        host.Raise(WXL_EV_ModelBatchDrawn, &batch);
        Check(outline.primitives == 40 && outline.base == -12 && outline.depth == 1.0f,
              "RenderScript: a drawn batch with the numbers of its draw");
        wxl::ScriptMgr::Clear();
    }

    Section("what a script handles");
    {
        wxl::ScriptMgr::Clear();
        const size_t before = host.LiveSubscriptions();

        NothingWorldScript nothing;
        wxl::ScriptMgr::Add(&nothing);
        Check(host.LiveSubscriptions() == before, "a script that overrides nothing subscribes nothing");

        LeaveWorldScript leave;
        wxl::ScriptMgr::Add(&leave);
        Check(host.LiveSubscriptions() == before + 1, "one that overrides one event subscribes it");

        LeaveWorldScript secondLeave;
        wxl::ScriptMgr::Add(&secondLeave);
        Check(host.LiveSubscriptions() == before + 1,
              "a second script handling the same event subscribes nothing more");

        const wxl_world_args left{};
        host.Raise(WXL_EV_WorldLeave, &left);
        Check(leave.leaves == 1 && secondLeave.leaves == 1, "and both of them run");

        // The cases that would be a mod compiling and silently doing nothing, if the check that
        // decides what to subscribe could be fooled. Each is counted, and each is raised.
        PrivateWorldScript hidden;
        wxl::ScriptMgr::Add(&hidden);
        Check(host.LiveSubscriptions() == before + 2,
              "an override declared private is still found and subscribed");
        const wxl_world_click_args click{};
        host.Raise(WXL_EV_WorldClick, &click);
        Check(hidden.clicks == 1, "and it runs: a private override is not a silent one");

        ProtectedWorldScript guarded;
        wxl::ScriptMgr::Add(&guarded);
        Check(host.LiveSubscriptions() == before + 3, "so is one declared protected");
        const wxl_sound_args sound{};
        host.Raise(WXL_EV_SoundPlay, &sound);
        Check(guarded.sounds == 1, "and it runs too");

        LeafWorldScript leaf;
        wxl::ScriptMgr::Add(&leaf);
        Check(host.LiveSubscriptions() == before + 4,
              "an override made in a class between the script and its family is found");
        const wxl_update_args tick{0.016f};
        host.Raise(WXL_EV_Update, &tick);
        Check(leaf.updates == 1, "and runs on the script that only inherits it");

        wxl::ScriptMgr::Clear();
        Check(host.LiveSubscriptions() == before, "Clear releases what each of them subscribed");

        // A script handed over as its family hides what it overrides, so it costs the family.
        LeaveWorldScript erased;
        wxl::ScriptMgr::Add(static_cast<wxl::WorldScript*>(&erased));
        Check(host.LiveSubscriptions() == before + CountFamilyWorld(),
              "a script passed as its family subscribes every event of it, to be safe");
        host.Raise(WXL_EV_WorldLeave, &left);
        Check(erased.leaves == 1, "and its override still runs");

        wxl::ScriptMgr::Clear();
        Check(host.LiveSubscriptions() == before, "and Clear gives all of it back");
    }

    Section("a handler that changes its family");
    {
        wxl::ScriptMgr::Clear();
        const wxl_update_args tick{0.016f};

        RearrangingWorldScript first;
        SecondWorldScript      late;
        first.add        = &late;
        first.removeSelf = true;
        wxl::ScriptMgr::Add(&first);
        host.Raise(WXL_EV_Update, &tick);
        Check(first.updates == 1 && late.updates == 0,
              "a script added by a handler waits for the next event");
        host.Raise(WXL_EV_Update, &tick);
        Check(first.updates == 1 && late.updates == 1,
              "one that removed itself gets nothing more, and the one it added runs");
        wxl::ScriptMgr::Clear();

        RearrangingWorldScript remover;
        SecondWorldScript      victim;
        remover.remove = &victim;
        wxl::ScriptMgr::Add(&remover);
        wxl::ScriptMgr::Add(&victim);
        host.Raise(WXL_EV_Update, &tick);
        Check(remover.updates == 1 && victim.updates == 0,
              "a script removed by an earlier handler of the same event does not run in it");
        host.Raise(WXL_EV_Update, &tick);
        Check(remover.updates == 2 && victim.updates == 0, "nor afterwards");
        wxl::ScriptMgr::Clear();
    }

    Section("cancellation");
    {
        wxl::ScriptMgr::Add(&world);

        wxl_bool             handled = WXL_FALSE;
        const wxl_input_args input{0x100, 0, 0, &handled};
        host.Raise(WXL_EV_Input, &input);
        Check(handled == WXL_TRUE, "a handler can swallow an input");

        wxl_bool             untouched = WXL_FALSE;
        const wxl_input_args other{0x200, 0, 0, &untouched};
        host.Raise(WXL_EV_Input, &other);
        Check(untouched == WXL_FALSE, "and leaves the rest alone");

        wxl::ScriptMgr::Clear();
    }

    Section("the mod lifecycle");
    {
        const size_t before = host.LiveSubscriptions();
        LifecycleMod::script = &world;

        Check(wxl_mod_query()->struct_size == sizeof(wxl_mod_info) &&
                  wxl_mod_query()->abi_major == WXL_ABI_VERSION_MAJOR &&
                  wxl_mod_query()->abi_minor == WXL_ABI_VERSION_MINOR &&
                  wxl_mod_query()->abi_feature == WXL_ABI_VERSION_FEATURE &&
                  std::strcmp(wxl_mod_query()->id, "wxl.contract") == 0,
              "WXL_MOD reports the mod");
        Check(wxl_mod_load(host.Table()) == WXL_OK, "load runs OnLoad");
        Check(host.LiveSubscriptions() == before + 3,
              "a script added in OnLoad subscribes what it overrides");

        for (int stage = WXL_STAGE_EARLY; stage <= WXL_STAGE_SHUTDOWN; ++stage)
            wxl_mod_stage(static_cast<wxl_stage>(stage));
        wxl_mod_unload();

        const std::vector<std::string> expected = {
            "load",    "early",  "stage0", "storage",  "stage1", "graphics", "stage2", "script",
            "stage3",  "ready",  "stage4", "shutdown", "stage5", "unload"};
        Check(LifecycleMod::trace == expected,
              "each stage reaches its named virtual, then OnStage; unload comes last");
        Check(host.LiveSubscriptions() == before,
              "unload releases what ScriptMgr subscribed, so no handler outlives the mod");

        LifecycleMod::trace.clear();
        wxl_mod_stage(static_cast<wxl_stage>(99));
        Check(LifecycleMod::trace.empty(), "a stage after unload reaches nothing");
    }

    Section("a service that is not there");
    {
        Check(!wxl::ObjectService::Available() && !wxl::HasObjects(),
              "an unpublished service resolves to nothing");

        const wxl::Unit unit(wxltest::FakeObjects::kPlayer);
        Check(!unit.IsValid() && !unit, "no object is valid without the service");

        const wxl::Expected<int32_t> health = unit.Health();
        Check(!health && health.IsUnsupported() && health.Error() == WXL_E_UNSUPPORTED,
              "a read without the service is unsupported, not zero");
        Check(health.ValueOr(7) == 7, "ValueOr answers the fallback");
        Check(!health.ToOptional().has_value(), "and the optional view is empty");

        Check(!wxl::Player::Local().HasHandle(), "no local player without the service");
        Check(wxl::ObjectService().InView<wxl::Unit>().empty(), "and nothing is in view");

        Check(wxl::ScriptService().RegisterFunction<&Greet>("Greet") == WXL_E_UNSUPPORTED,
              "a facade call without its service answers unsupported");
        Check(!wxl::StorageService().Open("x"), "a File without the service is empty");
        Check(!wxl::PatchService().Apply("x", 0x1000, "a", "b", 1), "so is a Patch");
        Check(!wxl::HookService().Attach("Thing", &ThingDetour, static_cast<ThingFn*>(nullptr)),
              "so is a Hook");
        Check(wxl::NetService().Claim(0x600).IsUnsupported(), "and a claim says why");
        Check(wxl::DataService::ServeTable("Spell.dbc", "x") == WXL_E_UNSUPPORTED &&
                  !wxl::DataService::IsLoaded("Spell.dbc") && wxl::DataService::ServedFrom("Spell.dbc").empty() &&
                  wxl::StorageService::Size("x").IsUnsupported(),
              "a generated method without its service answers as the hand-written ones did");
        Check(wxl::UiService().AddPanel<&DrawPanel>("P").IsUnsupported(), "as does a panel");
    }

    Section("services");
    {
        wxl::Service<wxl_object_api> objects(WXL_OBJECT_SERVICE, WXL_OBJECT_SERVICE_VERSION);
        Check(!objects, "a raw handle to an unpublished service is empty");

        Check(host.Table()->publish_service(WXL_OBJECT_SERVICE, WXL_OBJECT_SERVICE_VERSION,
                                            wxltest::FakeObjects::Table()) == WXL_OK,
              "a service can be published");
        Check(host.Table()->has_service(WXL_OBJECT_SERVICE, WXL_OBJECT_SERVICE_VERSION) == WXL_TRUE,
              "and is then visible");
        Check(host.Table()->has_service(WXL_OBJECT_SERVICE, 99) == WXL_FALSE,
              "another version does not match");
        Check(static_cast<bool>(objects), "a raw handle resolved late picks it up");
        Check(wxl::ObjectService::Available() && wxl::HasObjects(), "so does the facade");
        Check(wxl::ObjectService::Raw() == wxltest::FakeObjects::Table(),
              "the facade reaches the table that was published");
        Check(std::strcmp(wxl::ObjectService::kName, WXL_OBJECT_SERVICE) == 0 &&
                  wxl::ObjectService::kVersion == WXL_OBJECT_SERVICE_VERSION,
              "the name and the version are sealed in the facade");

        host.Table()->publish_service(WXL_SCRIPT_SERVICE, WXL_SCRIPT_SERVICE_VERSION, wxltest::FakeScript::Table());
        host.Table()->publish_service(WXL_STORAGE_SERVICE, WXL_STORAGE_SERVICE_VERSION, wxltest::FakeStorage::Table());
        host.Table()->publish_service(WXL_NET_SERVICE, WXL_NET_SERVICE_VERSION, wxltest::FakeNet::Table());
        host.Table()->publish_service(WXL_UI_SERVICE, WXL_UI_SERVICE_VERSION, wxltest::FakeUi::Table());
        host.Table()->publish_service(WXL_HOOK_SERVICE, WXL_HOOK_SERVICE_VERSION, wxltest::FakeHooks::Table());
        host.Table()->publish_service(WXL_HOOK_RAW_SERVICE, WXL_HOOK_RAW_SERVICE_VERSION, wxltest::FakeHooks::RawTable());
        host.Table()->publish_service(WXL_PATCH_SERVICE, WXL_PATCH_SERVICE_VERSION, wxltest::FakePatch::Table());
        host.Table()->publish_service(WXL_DATA_SERVICE, WXL_DATA_SERVICE_VERSION, wxltest::FakeData::Table());
        host.Table()->publish_service(WXL_ACTION_SERVICE, WXL_ACTION_SERVICE_VERSION, wxltest::FakeActions::Table());
        host.Table()->publish_service(WXL_M2_SERVICE, WXL_M2_SERVICE_VERSION, wxltest::FakeModels::Table());
        host.Table()->publish_service(WXL_M2FILE_SERVICE, WXL_M2FILE_SERVICE_VERSION, wxltest::FakeModels::FileTable());
        host.Table()->publish_service(WXL_GX_SERVICE, WXL_GX_SERVICE_VERSION, wxltest::FakeModels::GxTable());
        host.Table()->publish_service(WXL_ENGINE_SERVICE, WXL_ENGINE_SERVICE_VERSION, wxltest::FakeEngine::Table());
        host.Table()->publish_service(WXL_CAMERA_SERVICE, WXL_CAMERA_SERVICE_VERSION, wxltest::FakeView::CameraTable());
        host.Table()->publish_service(WXL_INPUT_SERVICE, WXL_INPUT_SERVICE_VERSION, wxltest::FakeView::InputTable());
        host.Table()->publish_service(WXL_WORLD_SERVICE, WXL_WORLD_SERVICE_VERSION, wxltest::FakeView::WorldTable());
        host.Table()->publish_service(WXL_POSE_SERVICE, WXL_POSE_SERVICE_VERSION, wxltest::FakeView::PoseTable());
        host.Table()->publish_service(WXL_IMGUI_SERVICE, WXL_IMGUI_SERVICE_VERSION, wxltest::FakeView::ImGuiTable());
        host.Table()->publish_service(WXL_HIGHLIGHT_SERVICE, WXL_HIGHLIGHT_SERVICE_VERSION, wxltest::FakeView::HighlightTable());
        host.Table()->publish_service(WXL_SELECTIONCIRCLE_SERVICE, WXL_SELECTIONCIRCLE_SERVICE_VERSION,
                                      wxltest::FakeView::SelectionCircleTable());
        host.Table()->publish_service(WXL_TEXTURE_SERVICE, WXL_TEXTURE_SERVICE_VERSION, wxltest::FakeView::TextureTable());
        host.Table()->publish_service(WXL_CVAR_SERVICE, WXL_CVAR_SERVICE_VERSION, wxltest::FakeView::CVarTable());
        host.Table()->publish_service(WXL_TERRAIN_SERVICE, WXL_TERRAIN_SERVICE_VERSION, wxltest::FakeView::TerrainTable());
        host.Table()->publish_service(WXL_DRAW_SERVICE, WXL_DRAW_SERVICE_VERSION, wxltest::FakeView::DrawTable());
        host.Table()->publish_service(WXL_ANIMATION_SERVICE, WXL_ANIMATION_SERVICE_VERSION, wxltest::FakeView::AnimationTable());
        host.Table()->publish_service(WXL_RENDER_SERVICE, WXL_RENDER_SERVICE_VERSION, wxltest::FakeRender::Table());
        host.Table()->publish_service(WXL_VK_SERVICE, WXL_VK_SERVICE_VERSION, wxltest::FakeRender::VkTable());
    }

    Section("reads that can fail");
    {
        using wxltest::FakeObjects;

        const wxl::Player me = wxl::Player::Local();
        Check(me.IsValid() && me.Handle() == FakeObjects::kPlayer, "the local player resolves");
        Check(me.Type() && *me.Type() == WXL_OBJECT_PLAYER, "and reports its type");

        const wxl::Expected<int32_t> dead = me.Health();
        Check(dead && *dead == 0, "a dead unit reads as a value of zero");
        Check(me.MaxHealth().ValueOr(0) == 5000 && me.Level().ValueOr(0) == 80, "other reads arrive");
        Check(me.HealthFraction() && *me.HealthFraction() == 0.0f, "a fraction of zero is a value");

        const wxl::Unit stale(FakeObjects::kStale);
        Check(!stale.IsValid(), "a stale handle is not valid");
        const wxl::Expected<int32_t> gone = stale.Health();
        Check(!gone && gone.IsNotFound(), "a read through a stale handle is absent, not zero");
        Check(gone.Error() == WXL_E_NOT_FOUND, "and says it is not found");
        Check(stale.Name().IsNotFound() && stale.Position().IsNotFound() &&
                  stale.Guid().IsNotFound() && stale.Type().IsNotFound(),
              "every read on a stale handle says the same");

        const wxl::Unit none;
        Check(none.Health().IsNotFound() && !none.HasHandle(),
              "a null handle reads as not found without asking the runtime");

        const wxl::Unit chest(FakeObjects::kChest);
        Check(chest.IsValid(), "a game object is a valid object");
        Check(chest.Health().IsInvalid(), "but asking it for health is a mistake, not a miss");
        Check(chest.Guid() && *chest.Guid() == WXL_GUID_NONE,
              "a live object the server does not own has a guid of none, as a value");

        Check(me.Name() && *me.Name() == "Thorgrim", "a name is copied out");
        Check(me.Position() && me.Position()->x == 42.0f, "a position reads");

        const wxl::Expected<wxl::Unit> target = me.Target();
        Check(target && target->Handle() == FakeObjects::kBoar, "a target reads as a unit");
        const wxl::Unit boar(FakeObjects::kBoar);
        Check(boar.Target() && !boar.Target()->HasHandle(), "a unit with no target reads as a null unit");
        Check(boar.Health().ValueOr(0) == 90, "a live unit reads its health");
    }

    Section("objects are values");
    {
        using wxltest::FakeObjects;

        const wxl::Unit a(FakeObjects::kBoar), b(FakeObjects::kBoar), c(FakeObjects::kPlayer);
        Check(a == b && a != c && !(a < b) && (c < a) == (c.Handle() < a.Handle()),
              "objects compare by handle");
        Check(wxl::Object(a) == a, "a unit equals itself as an object");

        std::set<wxl::Unit> ordered{a, b, c};
        Check(ordered.size() == 2, "a std::set holds each object once");

        std::unordered_set<wxl::Unit> hashed{a, b, c};
        Check(hashed.size() == 2 && hashed.count(wxl::Unit(FakeObjects::kPlayer)) == 1,
              "and so does an unordered_set");

        Check(static_cast<bool>(a) && !wxl::Unit(FakeObjects::kStale) && !wxl::Unit(),
              "operator bool is IsValid");

        const wxl::Object chest(FakeObjects::kChest);
        Check(!chest.As<wxl::Unit>().has_value(), "a chest does not view as a unit");
        Check(chest.As<wxl::GameObject>().has_value(), "but does as a game object");
        Check(wxl::Object(FakeObjects::kPlayer).As<wxl::Unit>().has_value() &&
                  wxl::Object(FakeObjects::kPlayer).As<wxl::Player>().has_value() &&
                  !wxl::Object(FakeObjects::kBoar).As<wxl::Player>().has_value(),
              "a player is a unit and a player; a boar is not a player");
        Check(!wxl::Object(FakeObjects::kStale).As<wxl::Unit>().has_value(),
              "a stale object views as nothing");

        const wxl::ObjectService objects;
        Check(objects.FromGuid(0x200) == wxl::Object(FakeObjects::kBoar), "a guid finds its object");
        Check(!objects.FromGuid(0x999).HasHandle(), "an unknown guid finds nothing");
    }

    Section("iteration composes");
    {
        using wxltest::FakeObjects;
        const wxl::ObjectService objects;

        const wxl::ObjectView<wxl::Object> all = objects.InView<>();
        Check(all.size() == 3 && !all.empty(), "everything in view");

        const wxl::ObjectView<wxl::Unit> units = objects.InView<wxl::Unit>();
        Check(units.size() == 2, "units are units and players");
        Check(objects.InView<wxl::Player>().size() == 1 && objects.InView<wxl::GameObject>().size() == 1,
              "narrower classes narrow the view");
        Check(objects.InView(WXL_OBJECT_GAMEOBJECT).size() == 1, "and so does a server type");

        int seen = 0;
        for (wxl::Unit unit : units) seen += unit.IsValid() ? 1 : 0;
        Check(seen == 2, "a range-for visits each unit");

        const auto alive = std::count_if(units.begin(), units.end(),
                                         [](const wxl::Unit& u) { return u.Health().ValueOr(0) > 0; });
        Check(alive == 1, "std::count_if works over a view");

        const auto found = std::find_if(units.begin(), units.end(), [](const wxl::Unit& u) {
            return u.Name().ValueOr("") == "Boar";
        });
        Check(found != units.end() && (*found).Handle() == FakeObjects::kBoar, "std::find_if finds a unit");

        const std::vector<wxl::Unit> copied = units.ToVector();
        Check(copied.size() == 2 && copied[0] == units[0], "a view materialises to a vector");

        const wxl::Expected<std::vector<wxl_vec3>> positions = objects.Positions(units);
        Check(positions && positions->size() == 2 && (*positions)[0].x == static_cast<float>(units[0].Handle()),
              "positions read in bulk for a whole view");
    }

    Section("descriptor fields");
    {
        using wxltest::FakeObjects;
        const wxl::ObjectService objects;
        const wxl::Unit          boar(FakeObjects::kBoar);
        const wxl::Player        me(FakeObjects::kPlayer);
        const wxl::GameObject    chest(FakeObjects::kChest);

        const wxl::Expected<int32_t> health = boar.Field(wxl::field::Unit::Health);
        Check(health && *health == 90, "a field reads through its constant, as the type its row says");
        const wxl::Expected<std::array<uint8_t, 4>> bytes = me.Field(wxl::field::Unit::Bytes0);
        Check(bytes && (*bytes)[0] == 1 && (*bytes)[1] == 2 && (*bytes)[3] == 1, "a field of bytes, low first");
        const wxl::Expected<float> radius = boar.Field(wxl::field::Unit::BoundingRadius);
        Check(radius && *radius == 1.5f, "a float");
        const wxl::Expected<wxl_guid> target = me.Field(wxl::field::Unit::Target);
        Check(target && *target == 0x200, "a guid, from its two dwords");
        Check(chest.Field(wxl::field::Unit::Health).IsInvalid(), "a field of another kind is invalid");
        Check(boar.Field(wxl::field::Unit::Resistances, 7).IsInvalid(), "as is an element past the last");
        Check(wxl::Unit(FakeObjects::kStale).Field(wxl::field::Unit::Health).IsNotFound(), "a stale handle is not found");
        const wxl::Expected<std::vector<int32_t>> resistances = boar.Fields(wxl::field::Unit::Resistances);
        Check(resistances && resistances->size() == 7, "Fields reads every element");
        Check(boar.FieldCount().ValueOr(0) == 0x94 && chest.Field(wxl::field::Object::Entry).HasValue(),
              "a block's size, and the object's own fields on every kind");

        Check(boar.SetField(wxl::field::Unit::Health, 33) == WXL_OK && boar.Field(wxl::field::Unit::Health).ValueOr(0) == 33,
              "SetField writes an element and Field reads it back");
        Check(boar.SetField(wxl::field::Object::Guid, wxl_guid(1)) == WXL_E_INVALID, "the guid is not written");
        const wxl::Expected<std::vector<uint32_t>> raw = boar.RawFields(0x18, 2);
        Check(raw && (*raw)[0] == 33 && (*raw)[1] == 0, "the raw dwords by index");

        const std::vector<wxl::Expected<int32_t>> many =
            objects.Fields(objects.InView<>(), wxl::field::Unit::Health);
        Check(many.size() == 3 && many[0].ValueOr(-1) == 0 && many[1].ValueOr(-1) == 33 && many[2].IsInvalid(),
              "a field of a whole view in one call, the chest's invalid");

        FakeObjects::Watches().clear();
        Check(objects.Watch(wxl::field::Unit::Health) == WXL_OK &&
                  objects.Watch(wxl::field::Unit::Target, WXL_OBJECT_PLAYER) == WXL_OK &&
                  FakeObjects::Watches().size() == 2 && FakeObjects::Watches()[0].type == WXL_OBJECT_UNIT &&
                  FakeObjects::Watches()[1].type == WXL_OBJECT_PLAYER && FakeObjects::Watches()[1].count == 2,
              "Watch asks for the field's kind, or the type named, over the field's dwords");
        Check(objects.Watch(wxl::field::Unit::CreatedBy) == WXL_E_UNSUPPORTED, "and passes on a refusal");
        Check(objects.Unwatch(wxl::field::Unit::Target, WXL_OBJECT_PLAYER) == WXL_OK &&
                  FakeObjects::Watches().size() == 1,
              "Unwatch gives one back, by the same field and type");
        {
            wxl::FieldWatch held(wxl::field::Unit::MaxHealth);
            Check(held && held.Result() == WXL_OK && FakeObjects::Watches().size() == 2 &&
                      FakeObjects::Watches()[1].first == wxl::field::Unit::MaxHealth.index,
                  "a FieldWatch takes a watch when it is made");
            wxl::FieldWatch moved = std::move(held);
            Check(!held && moved && FakeObjects::Watches().size() == 2, "and moves without a second one");
            wxl::FieldWatch refused(wxl::field::Unit::CreatedBy);
            Check(!refused && refused.Result() == WXL_E_UNSUPPORTED, "a refused one holds nothing");
            wxl::FieldWatch kept(wxl::field::Unit::Level);
            kept.Release();
            Check(!kept && FakeObjects::Watches().size() == 3, "Release lets a watch go without giving it back");
        }
        Check(FakeObjects::Watches().size() == 2 && FakeObjects::Watches()[1].first == wxl::field::Unit::Level.index,
              "and one gives its watch back when it goes");
        objects.Unwatch(wxl::field::Unit::Level);

        struct Changes final : wxl::ObjectScript
        {
            int32_t was = 0, now = 0;
            void    OnObjectFieldChanged(wxl::Object, wxl_object_type, uint32_t field, uint32_t,
                                         wxl::FieldValue old_value, wxl::FieldValue new_value) override
            {
                if (field != wxl::field::Unit::Health.index) return;
                was = old_value.As(wxl::field::Unit::Health);
                now = new_value.As(wxl::field::Unit::Health);
            }
        } changes;
        wxl::ScriptMgr::Add(&changes);
        const wxl_object_field_args lost{FakeObjects::kBoar, WXL_OBJECT_UNIT, 0x18, 1, 33, uint64_t(uint32_t(-5))};
        host.Raise(WXL_EV_ObjectFieldChanged, &lost);
        Check(changes.was == 33 && changes.now == -5, "ObjectFieldChanged hands the values, read by the field's constant");
        Check(std::is_same_v<wxl::HandlerOf<WXL_EV_ObjectFieldChanged>,
                             void(wxl::Object, wxl_object_type, uint32_t, uint32_t, wxl::FieldValue, wxl::FieldValue)>,
              "its handler takes the object, its type, the field, its size and the two values");
        wxl::ScriptMgr::Remove(&changes);
    }

    Section("every object member: object_reads.def through the facades");
    {
        // Each row from 0.1.18 on is a method of its class, which reaches its member and answers
        // the fake's seed for the row as the type the row says: a number, a bool, an
        // enumeration, a facade class, a struct.
        using namespace wxl;
        using wxltest::FakeObjects;
        uint32_t since = 0;
#define WXL_OBJECT_SINCE(major, minor, feature) since = since ? since : __LINE__;
#define WXL_OBJECT_READ(kinds, member, Method, type, source, Cxx, cxx, doc)
#define WXL_OBJECT_READ_AT(kinds, member, Method, type, source, Cxx, cxx, Arg, arg, doc)
#include "wxl/runtime/object_reads.def"
#undef WXL_OBJECT_READ_AT
#undef WXL_OBJECT_READ
#undef WXL_OBJECT_SINCE

        std::string wrong;
        int         checked = 0;
        const auto  row     = [&](const char* member, const char* kinds, uint32_t line, auto got, auto expected) {
            ++checked;
            const bool held = std::strcmp(kinds, "Containers") != 0 && std::strcmp(kinds, "Dynamics") != 0 &&
                              std::strcmp(kinds, "Corpses") != 0;
            if (!held)
            {
                if (got || got.Error() != WXL_E_INVALID) wrong += std::string(member) + " (no such object here); ";
                return;
            }
            if (!got || !wxltest::SameSeed(*got, expected)) wrong += std::string(member) + "; ";
            (void)line;
        };
#define WXL_CONTRACT_OF_Any         Object(FakeObjects::kBoar)
#define WXL_CONTRACT_OF_Units       Unit(FakeObjects::kBoar)
#define WXL_CONTRACT_OF_Players     Player(FakeObjects::kPlayer)
#define WXL_CONTRACT_OF_Active      Player(FakeObjects::kPlayer)
#define WXL_CONTRACT_OF_Items       Item(FakeObjects::kItem)
#define WXL_CONTRACT_OF_Containers  Container(FakeObjects::kItem)
#define WXL_CONTRACT_OF_GameObjects GameObject(FakeObjects::kChest)
#define WXL_CONTRACT_OF_Dynamics    DynamicObject(FakeObjects::kChest)
#define WXL_CONTRACT_OF_Corpses     Corpse(FakeObjects::kChest)
#define WXL_CONTRACT_EXPECTED(type, cxx) wxltest::SeedAs<type, cxx>(__LINE__)
#define WXL_OBJECT_READ(kinds, member, Method, type, source, Cxx, cxx, doc)                          \
    if (__LINE__ > since)                                                                             \
        row(#member, #kinds, __LINE__, WXL_CONTRACT_OF_##kinds.Cxx(), WXL_CONTRACT_EXPECTED(type, cxx));
#define WXL_OBJECT_READ_AT(kinds, member, Method, type, source, Cxx, cxx, Arg, arg, doc)              \
    if (__LINE__ > since)                                                                             \
        row(#member, #kinds, __LINE__, WXL_CONTRACT_OF_##kinds.Cxx(std::decay_t<arg>{}),               \
            WXL_CONTRACT_EXPECTED(type, cxx));
#include "wxl/runtime/object_reads.def"
#undef WXL_OBJECT_READ_AT
#undef WXL_OBJECT_READ
        Check(checked > 60 && wrong.empty(), "every 0.1.18 row answers its seed through its class's method");
        if (!wrong.empty()) std::printf("        %s\n", wrong.c_str());

        const Player me = Player::Local();
        const Unit   boar(FakeObjects::kBoar);
        Check(me.InventoryItem(WXL_INVENTORY_MAIN_HAND) && me.InventoryItem(WXL_INVENTORY_MAIN_HAND)->Handle() == FakeObjects::kBoar,
              "a handle answer is the facade class over the object");
        Check(boar.Reaction() && *boar.Reaction() == WXL_REACTION_HOSTILE, "an enumeration answer is the C enum");
        Check(boar.IsDead() && *boar.IsDead() == true, "a wxl_bool answer is a bool");
        Check(Player(FakeObjects::kBoar).Xp().Error() == WXL_E_INVALID,
              "a unit viewed as a player, unchecked, answers no player read");
        Check(Unit(FakeObjects::kItem).Health().Error() == WXL_E_INVALID, "an item viewed as a unit no unit read");
        const auto auras = boar.Auras();
        Check(auras && !auras->empty() && (*auras)[0].stacks == 3, "Auras reads AuraCount slots");
        const auto skills = me.Skills();
        Check(skills && skills->size() == 128, "Skills keeps the lines that hold a skill");
        const auto party = me.PartyMembers();
        Check(party && party->size() == 4, "PartyMembers keeps the slots that hold someone");
        Check(!Unit(FakeObjects::kStale).IsDead() && Unit(FakeObjects::kStale).IsDead().Error() == WXL_E_NOT_FOUND,
              "a stale object is gone");
    }

    Section("wxl.m2, wxl.m2file and wxl.gx: methods generated from their tables");
    {
        using wxltest::FakeModels;
        using wxltest::FakeObjects;
        void* const body = wxl::M2Service::ObjectModel(wxl::Unit(FakeObjects::kBoar));
        Check(body == FakeModels::UnitModel() && !wxl::M2Service::ObjectModel(wxl::Object(FakeObjects::kItem)),
              "ObjectModel: a unit's model, none for an item");
        const auto row = wxl::M2Service::ItemDisplay(416036);
        Check(row && std::strcmp(row->model[0], "Buckle.mdx") == 0 && row->struct_size == sizeof(wxl_m2_item_display) &&
                  wxl::M2Service::ItemDisplay(1).Error() == WXL_E_NOT_FOUND,
              "ItemDisplay: a row, its struct_size set by the facade; not found for none");
        Check(wxl::M2Service::RacePrefix(1).ValueOr("") == "Hu" && !wxl::M2Service::RacePrefix(9),
              "RacePrefix answers a string");

        const int released = FakeModels::Released();
        {
            wxl::M2Model buckle(body, "Item\\Buckle.m2");
            Check(static_cast<bool>(buckle) && buckle.Attach(body, 99) == WXL_E_NOT_FOUND &&
                      buckle.Attach(body, 99, WXL_M2_ATTACH_ROOT | WXL_M2_ATTACH_FOLLOW_BONES) == WXL_OK &&
                      buckle.Parent() == body,
                  "M2Model: made, hung on the root where the point is missing");
            buckle.SetGeosets(1, 3, false);
            const auto* made = FakeModels::Known(buckle.Get());
            Check(made && made->first == 1 && made->last == 3 && !made->visible &&
                      buckle.ReplaceTexture(2, "missing.blp") == WXL_E_NOT_FOUND,
                  "geosets and textures reach the model");
            Check(buckle.AttachToBone(body, 53, 5, wxl_vec3{0.2f, 0.0f, 1.1f}) == WXL_OK && made->key_bone == 5 &&
                      made->offset.x == 0.2f,
                  "AttachToBone takes the offset by value");
            buckle.Detach();
            Check(!buckle.Parent(), "Detach takes it off");
            wxl::M2Model moved = std::move(buckle);
            Check(!buckle && static_cast<bool>(moved), "moving one leaves the other empty");
        }
        Check(FakeModels::Released() == released + 1, "and the one left gives the model back when it goes");
        const auto point = wxl::M2Service::AttachmentPoint(body, 32);
        Check(point && point->z == 1.5f && wxl::M2Service::AttachmentPoint(body, 1).Error() == WXL_E_NOT_FOUND,
              "AttachmentPoint answers a point");

        {
            wxl::M2Model effect = wxl::M2Model::World("Spells\\Fire.m2");
            const auto*  made   = FakeModels::Known(effect.Get());
            Check(made && made->world && effect.Place(wxl::M2Placement(wxl_vec3{1.0f, 2.0f, 3.0f}, 0.5f)) == WXL_OK &&
                      made->placed.struct_size == sizeof(wxl_m2_placement) && made->placed.position.z == 3.0f &&
                      made->placed.yaw == 0.5f && made->placed.scale == 1.0f,
                  "M2Model::World: a model of the world's scene, placed by a placement passed by address");
            Check(effect.SetTransform(wxl::M2Placement(wxl_vec3{}, 0.0f, 0.0f, 0.0f, 2.0f)) == WXL_OK &&
                      made->transform.scale == 2.0f && effect.PlayAnimation(158) == WXL_OK && made->anim == 158 &&
                      made->speed == 1.0f,
                  "SetTransform and PlayAnimation, at speed 1 unless asked");
            effect.SetAlpha(0.5f);
            effect.SetEmitting(false);
            Check(made->alpha == 0.5f && !made->emitting && effect.Loaded() && effect.AnimationLength(0).ValueOr(0) == 1333,
                  "SetAlpha, SetEmitting, Loaded, AnimationLength");
            const auto hand   = wxl::M2Service::AttachmentWorld(body, 32);
            const auto ground = wxl::M2Service::GroundHeight(wxl_vec3{1.0f, 2.0f, 10.0f}, 5.0f, 50.0f);
            Check(hand && hand->z == 31.5f && ground && *ground == 8.0f &&
                      wxl::M2Service::GroundHeight(wxl_vec3{}, 5.0f, 1.0f).Error() == WXL_E_NOT_FOUND,
                  "AttachmentWorld answers a point, GroundHeight takes one by address and answers a height");
        }
        Check(FakeModels::Released() == released + 2, "a world model is given back like any other");
        {
            const auto Accept = [](void*, wxl_str, uint32_t, void*) -> wxl_bool { return WXL_TRUE; };
            const auto Moved  = [](const wxl_m2_bones*, void*) {};
            wxl::M2BoneWatch watch(Accept, Moved);
            wxl::M2BoneWatch second(Accept, Moved);
            Check(static_cast<bool>(watch) && !second && FakeModels::Watching().token == 7,
                  "M2BoneWatch starts a watcher; one the runtime refuses is empty");
            wxl::M2BoneWatch moved = std::move(watch);
            Check(!watch && static_cast<bool>(moved) && FakeModels::Watching().token == 7, "a watcher moves");
            const auto bone = wxl::M2Service::BoneInfo(body, 1);
            Check(bone && bone->parent == 0 && bone->pivot.z == 1.0f &&
                      wxl::M2Service::BoneInfo(body, 2).Error() == WXL_E_NOT_FOUND,
                  "BoneInfo answers a bone's parent and pivot");
        }
        Check(FakeModels::Watching().token == 0, "and stops when it goes");

        uint32_t array[2]  = {0, 0};
        void*    elements  = wxl::M2FileService::AllocNear(array, 3 * sizeof(uint32_t));
        auto*    values    = static_cast<uint32_t*>(elements);
        values[0] = 10, values[1] = 20, values[2] = 30;
        Check(elements && wxl::M2FileService::SetArray(array, elements, 3) == WXL_OK &&
                  wxl::M2FileService::ArrayData(array) == elements,
              "SetArray and ArrayData agree, the runtime's way");
        uint32_t sum = 0;
        for (uint32_t value : wxl::M2FileService::Elements<uint32_t>(array)) sum += value;
        Check(sum == 60, "Elements walks a parsed array");
        const int animated = FakeModels::Animated();
        wxl::M2FileService::AnimateBones(array);
        Check(FakeModels::Animated() == animated + 1, "AnimateBones reaches the file");

        const int invalidated = FakeModels::Invalidated();
        Check(wxl::GxService::D3d9Device() == FakeModels::Device(), "D3d9Device answers the device");
        wxl::GxService::InvalidateState();
        Check(FakeModels::Invalidated() == invalidated + 1, "InvalidateState reaches the runtime");
    }

    Section("wxl.engine, and the layout check of the engine headers");
    {
        using wxltest::FakeEngine;
        using wxltest::EngineShared;
        using wxltest::EngineData;
        using wxltest::EngineAbsent;
        FakeEngine::hash      = EngineShared::kLayoutHash;
        FakeEngine::wrongHash = EngineData::kLayoutHash + 1;
        Check(wxl::EngineService::Layout("CM2Shared") && *wxl::EngineService::Layout("CM2Shared") == EngineShared::kLayoutHash,
              "Layout answers a type's hash");
        Check(!wxl::EngineService::Layout("CNoSuchType") &&
                  wxl::EngineService::Layout("CNoSuchType").Error() == WXL_E_NOT_FOUND,
              "and not found for a type it does not describe");
        Check(wxl::engine::LayoutMismatch<EngineShared>().empty(), "a type laid out alike: no mismatch");
        const std::string differs = wxl::engine::LayoutMismatch<EngineShared, EngineData>();
        Check(differs.find("M2Data") != std::string::npos && differs.find("CM2Shared") == std::string::npos &&
                  differs.find("rebuild it") != std::string::npos,
              "a type laid out otherwise is named, with what to do");
        const std::string absent = wxl::engine::LayoutMismatch<EngineAbsent>();
        Check(absent.find("CNoSuchType (this runtime does not describe it)") != std::string::npos,
              "a type the runtime does not describe is named");
    }

    Section("wxl.action, and the objects' own actions");
    {
        using wxltest::FakeActions;
        using wxltest::FakeObjects;
        FakeActions::Calls().clear();
        const wxl::Unit boar(FakeObjects::kBoar);
        const wxl::Item stone(FakeObjects::kItem);
        Check(wxl::ActionService::Target(boar) == WXL_OK && wxl::ActionService::ClearTarget() == WXL_OK &&
                  wxl::ActionService::Interact(wxl::Object(FakeObjects::kChest)) == WXL_OK &&
                  wxl::ActionService::CastSpell(133) == WXL_OK && wxl::ActionService::CastSpell(133, boar) == WXL_OK &&
                  wxl::ActionService::StopCasting() == WXL_OK && wxl::ActionService::UseItem(stone) == WXL_OK &&
                  wxl::ActionService::Follow(boar) == WXL_OK && wxl::ActionService::Assist(boar) == WXL_OK &&
                  wxl::ActionService::MoveTo(wxl_vec3{1.0f, 2.0f, 3.0f}) == WXL_OK,
              "every action of the facade reaches its member");
        const auto& calls = FakeActions::Calls();
        Check(calls.size() == 10 && calls[0].name == "target" && calls[0].object == FakeObjects::kBoar &&
                  calls[3].spell == 133 && calls[3].target == WXL_HANDLE_NONE && calls[4].target == FakeObjects::kBoar &&
                  calls[6].object == FakeObjects::kItem && calls[9].point.z == 3.0f,
              "with its arguments: no target for a cast at the current one");
        Check(wxl::ActionService::CastSpell(0) == WXL_E_FAILED, "what the client refuses answers failed");
        FakeActions::Calls().clear();
        Check(boar.Select() == WXL_OK && boar.Interact() == WXL_OK && boar.CastAt(116) == WXL_OK &&
                  boar.Follow() == WXL_OK && boar.Assist() == WXL_OK && stone.Use(boar) == WXL_OK,
              "the objects' own actions are the same calls");
        Check(calls.size() == 6 && calls[2].spell == 116 && calls[2].target == FakeObjects::kBoar &&
                  calls[5].target == FakeObjects::kBoar,
              "on the object they are called on");
        Check(wxl::Unit(FakeObjects::kStale).Select() == WXL_E_NOT_FOUND, "a stale object's action is gone");
    }

    Section("wxl.camera, wxl.input and wxl.world, and the facing");
    {
        using wxltest::FakeView;
        FakeView::Calls().clear();
        const wxl::Expected<wxl_vec3> position = wxl::CameraService::Position();
        const wxl::Expected<double>   fov      = wxl::CameraService::Fov();
        const wxl::Expected<wxl_vec3> pivot    = wxl::CameraService::Pivot();
        Check(position && position->z == 3.0f && fov && *fov == 1.5 && pivot && pivot->z == 1.75f &&
                  wxl::CameraService::Forward() && wxl::CameraService::Up() && !wxl::CameraService::Cinematic(),
              "the camera's reads answer through the facade");
        Check(wxl::CameraService::SetView(wxl_vec3{7, 8, 9}, wxl_vec3{0, 1, 0}, 1.2) == WXL_OK &&
                  wxl::CameraService::ReleaseView() == WXL_OK,
              "a view is set and released");
        Check(wxl::InputService::CaptureCursor(true) == WXL_OK && wxl::InputService::CursorCaptured() &&
                  wxl::InputService::PointerOverWorld() && wxl::InputService::CaptureCursor(false) == WXL_OK &&
                  !wxl::InputService::CursorCaptured(),
              "the pointer is captured and let go");
        const std::optional<wxl_vec3> hit   = wxl::WorldService::Trace(wxl_vec3{0, 4, 0}, wxl_vec3{20, 4, 0});
        const std::optional<wxl_vec3> clear = wxl::WorldService::Trace(wxl_vec3{0, 0, 0}, wxl_vec3{5, 0, 0},
                                                                       WXL_TRACE_LINE_OF_SIGHT);
        Check(hit && hit->x == 10.0f && hit->y == 4.0f && !clear, "a trace answers the point met, or nothing");
        const auto& calls = FakeView::Calls();
        Check(calls.size() == 6 && calls[0].name == "set_view" && calls[0].position.x == 7.0f && calls[0].fov == 1.2 &&
                  calls[1].name == "release_view" && calls[2].name == "capture" && calls[3].name == "free" &&
                  calls[4].fov == WXL_TRACE_CAMERA && calls[5].fov == WXL_TRACE_LINE_OF_SIGHT,
              "with their arguments, the camera's mask when a trace names none");
        wxltest::FakeActions::Calls().clear();
        Check(wxl::ActionService::Face(1.25) == WXL_OK && wxltest::FakeActions::Calls().size() == 1 &&
                  wxltest::FakeActions::Calls()[0].point.x == 1.25f,
              "Face reaches wxl.action with its facing");
    }

    Section("a rolled view and a pointer held off the centre");
    {
        using wxltest::FakeView;
        FakeView::Calls().clear();
        const wxl::Expected<double> roll = wxl::CameraService::Roll();
        Check(wxl::CameraService::SetViewRolled(wxl_vec3{7, 8, 9}, wxl_vec3{0, 1, 0}, 0.0, -0.3) == WXL_OK && roll &&
                  *roll == 0.25,
              "a rolled view is set and the roll read through the facade");
        Check(wxl::InputService::CaptureCursorAt(0.5, 0.35) == WXL_OK, "the pointer is held at a point");
        const wxl::Expected<wxl_vec3> point = wxl::InputService::CapturePoint();
        Check(point && point->x == 0.5f && point->y == 0.35f, "and read back where it is held");
        Check(wxl::InputService::CaptureCursorAt(1.5, 0.5) == WXL_E_INVALID, "a point off the window is refused");
        Check(wxl::InputService::CaptureCursor(false) == WXL_OK && !wxl::InputService::CapturePoint(),
              "let go, it is held nowhere");
        const auto& calls = FakeView::Calls();
        Check(calls.size() == 3 && calls[0].name == "set_view_rolled" && calls[0].roll == -0.3 &&
                  calls[0].forward.y == 1.0f && calls[1].name == "capture_at" && calls[1].position.y == 0.35f &&
                  calls[2].name == "free",
              "with their arguments");
    }

    Section("the mouse look kept out, a head turned, the runtime's ImGui");
    {
        using wxltest::FakeView;
        using wxltest::FakeObjects;
        FakeView::Calls().clear();
        Check(wxl::InputService::BlockMouseLook(true) == WXL_OK && wxl::InputService::MouseLookBlocked() &&
                  wxl::InputService::BlockMouseLook(false) == WXL_OK && !wxl::InputService::MouseLookBlocked(),
              "the mouse look is blocked and given back");
        const wxl::Unit boar(FakeObjects::kBoar);
        Check(wxl::PoseService::Head(boar, 0.5, -0.25, 0.1) == WXL_OK && wxl::PoseService::ReleaseHead(boar) == WXL_OK &&
                  wxl::PoseService::Head(wxl::Unit(FakeObjects::kStale), 0, 0, 0) == WXL_E_NOT_FOUND,
              "a head is turned and given back; a unit gone is not found");
        const auto& calls = FakeView::Calls();
        Check(calls.size() == 4 && calls[2].name == "head" && calls[2].position.x == 0.5f && calls[2].position.y == -0.25f &&
                  calls[2].fov == static_cast<double>(FakeObjects::kBoar) && calls[3].name == "release_head",
              "with the unit and its angles");
        // The facade, wxl/services/imgui.hpp, compiles Dear ImGui in: the table is read raw here.
        const wxl_imgui_api* imgui = wxl::api::Table<wxl_imgui_api>();
        void*                alloc = nullptr;
        imgui->allocators(&alloc, nullptr, nullptr);
        Check(imgui && wxl::View(imgui->version()) == "1.0;0.1" && imgui->context() == reinterpret_cast<void*>(0x1000) &&
                  imgui->plot_context() == reinterpret_cast<void*>(0x2000) && alloc == reinterpret_cast<void*>(0x3000),
              "the runtime's ImGui version, contexts and allocator reach a mod");
    }

    Section("drawing into the frame (wxl.render, wxl.vk, wxl.gx 2)");
    {
        using wxltest::FakeRender;
        FakeRender::Reset();
        static const uint32_t kSpirv[] = {0x07230203u, 0x00010500u, 0u, 1u, 0u};
        Check(wxl::GxService::Api() && *wxl::GxService::Api() == WXL_GX_BACKEND_VULKAN, "wxl.gx 2 answers the device's API");
        Check(wxl::RenderService::Supported() && wxl::RenderService::UnsupportedReason().empty(), "the render service draws");

        const wxl_handle mask = wxl::RenderService::CreateScreenImage(2, WXL_RENDER_FORMAT_RGBA8, 0);
        const wxl_handle depth = wxl::RenderService::CreateImage(64, 32, WXL_RENDER_FORMAT_DEPTH32F, 1);
        Check(mask != WXL_HANDLE_NONE && *wxl::RenderService::ImageWidth(mask) == 960 && *wxl::RenderService::ImageHeight(mask) == 540 &&
                  *wxl::RenderService::ImageSlot(mask) == mask + 100 && depth != WXL_HANDLE_NONE,
              "a screen image is the frame's size divided, a fixed one its own; each has a slot");
        const wxl_handle buffer = wxl::RenderService::CreateBuffer(16);
        const float      values[4] = {1.0f, 2.0f, 3.0f, 4.0f};
        Check(wxl::RenderService::WriteBuffer(buffer, 0, values, sizeof values) == WXL_OK &&
                  wxl::RenderService::WriteBuffer(buffer, 8, values, sizeof values) == WXL_E_INVALID &&
                  wxl::RenderService::BufferAddress(buffer).HasValue(),
              "a buffer is written inside its size, refused past it, and has an address");

        wxl::RenderPipelineDesc tint = wxl::RenderPipelineDesc::Fullscreen(kSpirv, sizeof kSpirv);
        tint.Named("tint").Blend(0, WXL_RENDER_BLEND_ALPHA);
        const wxl_handle fullscreen = wxl::RenderService::CreatePipeline(tint);
        wxl::RenderPipelineDesc silhouette = wxl::RenderPipelineDesc::Batch(kSpirv, sizeof kSpirv);
        silhouette.Colors({WXL_RENDER_FORMAT_RGBA8, WXL_RENDER_FORMAT_RGBA8}).Depth(0, WXL_RENDER_DEPTH_LESS_EQUAL, false);
        const wxl_handle batch = wxl::RenderService::CreatePipeline(silhouette);
        Check(fullscreen && batch && wxl::RenderService::PipelineReady(fullscreen) &&
                  FakeRender::Pipelines()[batch].color_count == 2 && FakeRender::Pipelines()[batch].cull == WXL_RENDER_CULL_BATCH &&
                  FakeRender::Pipelines()[fullscreen].blend[0] == WXL_RENDER_BLEND_ALPHA &&
                  FakeRender::Pipelines()[fullscreen].color_formats[0] == WXL_RENDER_FORMAT_BGRA8,
              "the desc helpers fill a pipeline: the frame's format by default, a batch culled as the batch");
        Check(wxl::RenderService::CreatePipeline(wxl::RenderPipelineDesc::Graphics(nullptr, 0, kSpirv, sizeof kSpirv)) == WXL_HANDLE_NONE,
              "a graphics pipeline without a vertex program is refused");

        wxl::RenderPass outside;
        outside.Color(WXL_RENDER_SCENE_COLOR);
        Check(wxl::RenderService::Draw(fullscreen, 3, 1, nullptr, 0) == WXL_E_INVALID, "nothing draws outside a pass");
        wxl::RenderPass pass;
        pass.Color(mask, 0.0f, 0.0f, 0.0f, 0.0f).Color(WXL_RENDER_SCENE_COLOR).Depth(WXL_RENDER_SCENE_DEPTH);
        int        kept   = 0;
        void* const held = wxl::RenderService::KeepBatch(&kept);
        Check(wxl::RenderService::Begin(pass) == WXL_OK && wxl::RenderService::Begin(pass) == WXL_E_INVALID &&
                  FakeRender::LastPass().clear == 1u && FakeRender::LastPass().colors[1] == WXL_RENDER_SCENE_COLOR,
              "a pass opens once, its first colour cleared, the frame's colour and depth named");
        const float color[4] = {1.0f, 0.5f, 0.0f, 0.25f};
        Check(wxl::RenderService::Fullscreen(fullscreen, color, sizeof color) == WXL_OK &&
                  wxl::RenderService::DrawBatch(held, batch, color, sizeof color) == WXL_OK &&
                  wxl::RenderService::DrawIndexed(fullscreen, buffer, 0, 3, 1, nullptr, 0) == WXL_OK && held != nullptr,
              "full-screen, a kept batch and indexed draws reach the device with their bytes");
        wxl::RenderService::End();
        Check(FakeRender::Bytes().size() == 3 && FakeRender::Bytes()[0].size() == sizeof color &&
                  std::memcmp(FakeRender::Bytes()[1].data(), color, sizeof color) == 0 && FakeRender::Trace().back() == "end",
              "the push constants and the batch's constants cross whole");
        Check(*wxl::RenderService::SceneColorSlot() == 1u && *wxl::RenderService::SceneDepthSlot() == 2u, "the frame's copies have slots");
        Check(wxl::RenderService::ReplaceShader("Diffuse_T1", 1, kSpirv, sizeof kSpirv) == WXL_OK &&
                  wxl::RenderService::ReplaceShader("Diffuse_T1", 2, kSpirv, sizeof kSpirv) == WXL_E_INVALID &&
                  wxl::RenderService::RestoreShader("Diffuse_T1", 1) == WXL_OK &&
                  wxl::RenderService::RestoreShader("Diffuse_T1", 1) == WXL_E_NOT_FOUND,
              "a client shader is replaced for a stage and restored once");
        wxl::RenderService::DestroyPipeline(fullscreen);
        wxl::RenderService::DestroyImage(mask);
        wxl::RenderService::DestroyBuffer(buffer);
        Check(!FakeRender::Pipelines().count(fullscreen) && !FakeRender::Images().count(mask) && !FakeRender::Buffers().count(buffer),
              "resources are given back");

        Check(wxl::VkService::Device() != nullptr && wxl::VkService::CommandBuffer() != nullptr && *wxl::VkService::QueueFamily() == 0u &&
                  wxl::VkService::Image(depth, false).HasValue() && !wxl::VkService::Image(mask, false).HasValue() &&
                  *wxl::VkService::FramesCompleted() < *wxl::VkService::Frame() && *wxl::VkService::SceneColor() == 0xA0u,
              "wxl.vk hands the device, the frame's commands and a resource's objects; none for one given back");
        FakeRender::InPoint() = false;
        Check(wxl::VkService::CommandBuffer() == nullptr && wxl::RenderService::Begin(pass) == WXL_E_INVALID,
              "outside a point there is no command buffer and no pass");
    }

    Section("a highlighted object");
    {
        using wxltest::FakeView;
        using wxltest::FakeObjects;
        FakeView::Calls().clear();
        FakeView::HighlightColors().clear();
        const wxl::Unit boar(FakeObjects::kBoar);
        Check(wxl::HighlightService::Supported() && wxl::HighlightService::UnsupportedReason().empty(),
              "the runtime says it draws highlights, with no reason not to");
        Check(wxl::HighlightService::Set(boar, 0xFFE03030u, 3.0f, 12.0f, 0.5f) == WXL_OK &&
                  wxl::HighlightService::Set(wxl::Unit(FakeObjects::kStale), 0xFFFFFFFFu, 1.0f, 0.0f, 0.0f) == WXL_E_NOT_FOUND &&
                  wxl::HighlightService::Set(boar, 0xFF00FF00u, -1.0f, 0.0f, 0.0f) == WXL_E_INVALID,
              "an object is highlighted; one gone is not found, a negative width refused");
        Check(wxl::HighlightService::Clear(boar) == WXL_OK && *wxl::HighlightService::Drawn() == 7u, "cleared, and the count drawn read");
        wxl::HighlightService::ClearAll();
        const auto& calls = FakeView::Calls();
        Check(calls.size() == 3 && calls[0].name == "highlight_set" && calls[0].position.x == 3.0f &&
                  calls[0].position.y == 12.0f && calls[0].position.z == 0.5f &&
                  calls[0].fov == static_cast<double>(FakeObjects::kBoar) && FakeView::HighlightColors().size() == 1 &&
                  FakeView::HighlightColors()[0] == 0xFFE03030u && calls[1].name == "highlight_clear" &&
                  calls[2].name == "highlight_clear_all",
              "with the object, its colour, its widths and its strength");
    }

    Section("the terrain");
    {
        using wxltest::FakeView;
        using wxl::TerrainService;
        FakeView::TerrainCalls().clear();
        Check(TerrainService::Height(10.0f, 5.0f).value_or(0.0f) == 42.0f && TerrainService::Height(10.0f, 5.0f, true).value_or(0.0f) == 40.0f &&
                  !TerrainService::Height(-10.0f, 5.0f),
              "Height answers the ground, the original on request, nothing where there is none");
        Check(TerrainService::Presence(1.0f, 1.0f) == WXL_TERRAIN_GROUND && TerrainService::Presence(-1.0f, 1.0f) == WXL_TERRAIN_NONE,
              "Presence answers the enumeration");
        const auto hit = TerrainService::Trace(wxl_vec3{1.0f, 2.0f, 50.0f}, wxl_vec3{1.0f, 2.0f, 0.0f});
        Check(hit && hit->point.z == 42.0f && hit->normal.z == 1.0f && hit->chunk_x == 511 && hit->chunk_y == 511 &&
                  !TerrainService::Trace(wxl_vec3{-1.0f, 0.0f, 0.0f}, wxl_vec3{-1.0f, 0.0f, -1.0f}),
              "Trace hands the hit with its struct_size set, and nothing for a miss");
        const auto place = TerrainService::Locate(0.0f, 0.0f);
        Check(place && place->chunk_x == 512 && place->chunk_y == 512 && place->nearest == 0 && place->loaded == WXL_TRUE,
              "Locate answers the chunk and the nearest vertex");
        const std::vector<wxl_vec3> vertices = TerrainService::Vertices(512, 512);
        Check(vertices.size() == 145 && vertices[0].x == wxl::terrain::ChunkCorner(512, 512).x && TerrainService::Vertices(-1, 0).empty(),
              "Vertices answers the 145, none off the map");
        const std::vector<wxl_terrain_vertex> brush = TerrainService::VerticesIn(0.0f, 0.0f, 3.0f);
        bool                                  near  = !brush.empty();
        for (const wxl_terrain_vertex& v : brush) near = near && v.x * v.x + v.y * v.y <= 9.001f && v.loaded == WXL_TRUE;
        Check(near && brush.front().original_z == 40.0f, "VerticesIn answers the vertices of the circle, every one counted and written");
        Check(TerrainService::SetHeights({{512, 512, 0, 41.0f}, {512, 512, 9, 41.5f}}) == WXL_OK &&
                  TerrainService::SetHeights({{512, 512, 145, 41.0f}}, 7) == WXL_E_INVALID &&
                  TerrainService::Reset(512, 512, 3) == WXL_OK && TerrainService::ResetAll() == WXL_OK,
              "SetHeights, Reset and ResetAll pass their layer, 0 unless named");
        // abi 0.1.42: a layer provider answers each layer's texture scale.
        static int seen = 0;
        Check(TerrainService::SetLayerProvider([](void* user, const wxl_terrain_layer* layer, wxl_terrain_layer_look* look) {
                  ++*static_cast<int*>(user);
                  look->texture_scale = layer->texture == 2 ? 4.0f : look->texture_scale;
              }, &seen) == WXL_OK,
              "SetLayerProvider takes a function");
        Check(FakeView::LayerAsked(2) == 4.0f && FakeView::LayerAsked(1) == 1.0f && seen == 2, "it answers each layer's look, the client's own left as it is");
        Check(TerrainService::SetLayerProvider(nullptr) == WXL_OK && FakeView::LayerAsked(2) == 1.0f, "null takes it off");
        // abi 0.1.43: the look's extra texture and numbers, and the provider's flags.
        Check(TerrainService::SetLayerProvider([](void*, const wxl_terrain_layer* layer, wxl_terrain_layer_look* look) {
                  if (look->struct_size < sizeof *look || layer->texture != 5) return;
                  look->extra_texture = "tileset\\rock_h.blp";
                  look->params[0]     = 6.0f;
                  look->params[1]     = 1.0f;
                  look->params[2]     = 1.0f;
              }, nullptr, WXL_TERRAIN_LAYER_PARAMS_EVERY_DRAW) == WXL_OK &&
                  FakeView::TerrainProvider().flags == WXL_TERRAIN_LAYER_PARAMS_EVERY_DRAW,
              "SetLayerProvider passes its flags");
        Check(FakeView::LayerAsked(5) == 1.0f && FakeView::LastLook().params[0] == 6.0f && FakeView::LastLook().extra_texture &&
                  FakeView::LayerAsked(4) == 1.0f && !FakeView::LastLook().extra_texture && FakeView::LastLook().params[2] == 0.0f,
              "a look carries the layer's extra texture and numbers, none unless answered");
        Check(TerrainService::SetLayerProvider(nullptr) == WXL_OK, "and null takes that one off too");
        // abi 0.1.44: a chunk's layers past the client's four.
        Check(TerrainService::SetLayerProvider([](void*, const wxl_terrain_layer*, wxl_terrain_layer_look*) {}, nullptr, 0,
                                               [](void*, const wxl_terrain_chunk* chunk, wxl_terrain_extra_layers* out) {
                                                   if (chunk->layers != 4) return;
                                                   out->count            = 2;
                                                   out->layer[0].texture = "tileset\\moss.blp";
                                                   out->alpha[3]         = 200;
                                               }) == WXL_OK &&
                  FakeView::TerrainProvider().chunk_fn != nullptr,
              "SetLayerProvider passes its chunk function");
        static wxl_terrain_extra_layers extras;
        extras = wxl_terrain_extra_layers{};
        extras.struct_size = sizeof extras;
        wxl_terrain_chunk chunk{};
        chunk.struct_size = sizeof chunk;
        chunk.layers      = 4;
        FakeView::TerrainProvider().chunk_fn(nullptr, &chunk, &extras);
        Check(extras.count == 2 && extras.alpha[3] == 200 && sizeof extras.alpha == 64 * 64 * 4,
              "a chunk's answer: its extra layers and their 64 x 64 alpha maps, four a texel");
        Check(TerrainService::SetLayerProvider(nullptr) == WXL_OK, "and null takes it off");
        // abi 0.1.45: a tile's split files.
        Check(TerrainService::SetTileProvider([](void*, const wxl_terrain_tile* tile, wxl_terrain_tile_files* out) {
                  static const char tex0[] = "REVM";
                  if (tile->tile_x != 32) return;
                  out->data[WXL_TERRAIN_TILE_TEX0] = tex0;
                  out->size[WXL_TERRAIN_TILE_TEX0] = 4;
              }) == WXL_OK &&
                  FakeView::TileProvider().fn != nullptr && FakeView::TileProvider().struct_size == sizeof(wxl_terrain_tile_provider),
              "SetTileProvider passes its function");
        wxl_terrain_tile tile{};
        tile.struct_size = sizeof tile;
        tile.tile_x      = 32;
        wxl_terrain_tile_files files{};
        files.struct_size = sizeof files;
        FakeView::TileProvider().fn(nullptr, &tile, &files);
        Check(files.size[WXL_TERRAIN_TILE_TEX0] == 4 && files.data[WXL_TERRAIN_TILE_OBJ0] == nullptr && WXL_TERRAIN_TILE_FILES == 3u,
              "a tile's answer: the files it has by kind, none for the others");
        Check(TerrainService::SetTileProvider(nullptr) == WXL_OK && !FakeView::TileProvider().fn, "and null takes it off");
        // abi 0.1.46: files named by FileDataID.
        Check(TerrainService::SetTileProvider([](void*, const wxl_terrain_tile*, wxl_terrain_tile_files*) {}, nullptr,
                                              [](void*, uint32_t id, char* out, uint32_t capacity) -> wxl_bool {
                                                  if (id != 189077 || capacity < 24) return WXL_FALSE;
                                                  std::strcpy(out, "tileset\\grass.blp");
                                                  return WXL_TRUE;
                                              }) == WXL_OK &&
                  FakeView::TileProvider().name_fn != nullptr,
              "SetTileProvider passes its name function");
        char named[32] = {};
        Check(FakeView::TileProvider().name_fn(nullptr, 189077, named, sizeof named) == WXL_TRUE && std::string(named) == "tileset\\grass.blp" &&
                  FakeView::TileProvider().name_fn(nullptr, 1, named, sizeof named) == WXL_FALSE,
              "a FileDataID it knows is named, one it does not is refused");
        TerrainService::SetTileProvider(nullptr);
        const auto& calls = FakeView::TerrainCalls();
        Check(calls.size() == 3 && calls[0].layer == 0 && calls[0].heights.size() == 2 && calls[0].heights[1].index == 9 &&
                  calls[1].what == "reset" && calls[1].layer == 3 && calls[1].chunk_y == 512 && calls[2].what == "reset_all",
              "every entry reaches the service as given");
        const auto stats = TerrainService::Stats();
        Check(stats && stats.Value().edited_chunks == 4 && stats.Value().last_microseconds == 12, "Stats answers the service's counts");
        const auto at = wxl::terrain::Locate(-20.0f, 33.0f);
        Check(at.on_map && wxl::terrain::Locate(-20.0f, 33.0f).chunk_x == TerrainService::Locate(-20.0f, 33.0f)->chunk_x,
              "the grid's arithmetic and the service agree");
    }

    Section("shapes in the world");
    {
        using wxltest::FakeView;
        using wxl::DrawService;
        using wxl::DrawShape;
        using wxl::DrawStyle;
        FakeView::DrawCalls().clear();
        Check(DrawService::Supported(), "Supported answers the service");
        Check(DrawService::Draw(DrawShape::Grid(wxl_vec3{1, 2, 3}, 12.0f, 8.0f, 1.0f), DrawStyle(0x40C0FF, 0.7f).Width(1.5f)) == WXL_OK,
              "Draw takes a grid and its look");
        const std::vector<wxl_vec3> path = {{0, 0, 0}, {5, 0, 0}, {5, 5, 0}};
        Check(DrawService::Draw(DrawShape::Line(path).OnGround(), DrawStyle()) == WXL_OK, "and a line on the ground");
        Check(DrawService::Draw(DrawShape::Circle(wxl_vec3{}, 500.0f), DrawStyle()) == WXL_E_INVALID, "a refusal comes back");
        const wxl_handle cone =
            DrawService::Add(DrawShape::Sector(wxl_vec3{4, 5, 6}, 15.0f, 1.0f, 1.2f), DrawStyle(0xFF4020).Fill(0.3f).Fade(0.1f, 0.4f), 3.0f);
        Check(cone != WXL_HANDLE_NONE, "Add answers a handle");
        Check(DrawService::Move(cone, wxl_vec3{7, 8, 9}, 0.5f) == WXL_OK && DrawService::SetStyle(cone, DrawStyle().OnTop()) == WXL_OK &&
                  DrawService::SetDuration(cone, 2.0f) == WXL_OK && DrawService::Attach(cone, wxl::Unit(wxltest::FakeObjects::kBoar), WXL_DRAW_ATTACH_TURN) == WXL_OK &&
                  DrawService::Detach(cone) == WXL_OK && DrawService::Update(cone, DrawShape::Ring(wxl_vec3{}, 4.0f, 3.0f)) == WXL_OK,
              "Move, SetStyle, SetDuration, Attach, Detach and Update reach the kept shape");
        DrawService::Remove(cone);
        DrawService::Clear();
        const auto& calls = FakeView::DrawCalls();
        Check(calls.size() == 11, "every call reached the service once");
        if (calls.size() == 11)
        {
            Check(calls[0].kind == WXL_DRAW_GRID && calls[0].center.z == 3.0f && calls[0].styled && calls[0].style.color == 0x40C0FF &&
                      std::fabs(calls[0].style.alpha - 0.7f) < 1e-6f && calls[0].style.width == 1.5f &&
                      calls[0].style.struct_size == sizeof(wxl_draw_style),
                  "the grid and its look as built, struct sizes set");
            Check(calls[1].kind == WXL_DRAW_LINE && calls[1].points.size() == 3 && (calls[1].flags & WXL_DRAW_ON_GROUND),
                  "the line's points, laid on the ground");
            Check(calls[2].what == "add" && calls[2].kind == WXL_DRAW_SECTOR && calls[2].duration == 3.0f && calls[2].style.fade_out == 0.4f &&
                      std::fabs(calls[2].style.fill_alpha - 0.3f) < 1e-6f,
                  "the cone kept for its duration, with its fill and fades");
            Check(calls[3].what == "move" && calls[3].center.y == 8.0f && calls[4].style.depth == WXL_DRAW_DEPTH_ON_TOP &&
                      calls[6].target == wxltest::FakeObjects::kBoar && calls[6].attach == WXL_DRAW_ATTACH_TURN && calls[8].kind == WXL_DRAW_RING &&
                      calls[9].what == "remove" && calls[10].what == "clear",
                  "each change as given");
        }
        const auto stats = DrawService::Stats();
        Check(stats && stats.Value().vertices == 1234 && stats.Value().frames == 7 && stats.Value().textured == 2,
              "Stats answers the service's counts");

        // abi 0.1.38: lines along, shadows, textured insides.
        FakeView::DrawCalls().clear();
        const std::vector<float> fades = {0.0f, 1.0f, 0.5f};
        Check(DrawService::Draw(DrawShape::Line(path).PointAlphas(fades),
                                DrawStyle(0xE0B040).Softness(1.5f).LineAlpha(0.2f, 1.0f).Shadow()) == WXL_OK,
              "a soft line fading along, with a shadow");
        Check(DrawService::Draw(DrawShape::Ring(wxl_vec3{}, 6.0f, 4.0f),
                                DrawStyle(0xFFFFFF, 0.0f).Fill(0.8f).FillColor(0x808080).Texture("SPELLS\\T_VFX_HERO_CIRCLE.BLP", 0.5f, 0.25f)
                                    .Uv(WXL_DRAW_UV_WORLD, 4.0f, 2.0f, 0.5f, 0.1f, 0.2f).Blend(WXL_DRAW_BLEND_MODULATE).Feather(0.75f).RadialFade(0.3f)) == WXL_OK,
              "a textured ring");
        const auto& more = FakeView::DrawCalls();
        Check(more.size() == 2, "both reached the service");
        if (more.size() == 2)
        {
            const wxl_draw_style& a = more[0].style;
            Check(more[0].alphas.size() == 3 && more[0].alphas[2] == 0.5f && (a.flags & WXL_DRAW_STYLE_LINE_ALPHA) && a.alpha_start == 0.2f &&
                      a.alpha_end == 1.0f && a.softness == 1.5f && a.shadow_alpha > 0.0f && a.shadow_x == 1.5f && a.shadow_y == 1.5f &&
                      a.shadow_softness == 1.0f && a.shadow_color == 0,
                  "the opacities per point, along the line and the shadow as built");
            const wxl_draw_style& b = more[1].style;
            Check(more[1].texture == "SPELLS\\T_VFX_HERO_CIRCLE.BLP" && b.texture_right == 0.5f && b.texture_bottom == 0.25f &&
                      b.uv == WXL_DRAW_UV_WORLD && b.uv_size_x == 4.0f && b.uv_size_y == 2.0f && b.uv_rotation == 0.5f && b.uv_offset_y == 0.2f &&
                      b.blend == WXL_DRAW_BLEND_MODULATE && b.feather == 0.75f && b.radial_fade == 0.3f &&
                      (b.flags & WXL_DRAW_STYLE_FILL_COLOR) && b.fill_color == 0x808080,
                  "the texture, its frame, the blend, the feather and the fill's colour as built");
        }
        Check(sizeof(wxl_draw_style) > WXL_DRAW_STYLE_SIZE_0136 && sizeof(wxl_draw_shape) > WXL_DRAW_SHAPE_SIZE_0136 &&
                  WXL_DRAW_STYLE_SIZE_0136 == 36u,
              "the styles and shapes of abi 0.1.36 are a prefix of today's");
        // abi 0.1.39: a textured box seen through the ground.
        FakeView::DrawCalls().clear();
        Check(DrawService::Draw(DrawShape::Box(wxl_vec3{1, 2, 3}, wxl_vec3{2, 2, 4}),
                                DrawStyle(0xE8C060).Fill(0.3f).Texture("SPELLS\\T_VFX_RING_FF.BLP").Uv(WXL_DRAW_UV_TILE, 0.5f, 0.5f, 0.0f, 0.25f)
                                    .Feather(0.2f).Occluded(0.35f)) == WXL_OK,
              "a textured box seen through the ground");
        Check(FakeView::DrawCalls().size() == 1 && FakeView::DrawCalls()[0].kind == WXL_DRAW_BOX &&
                  std::fabs(FakeView::DrawCalls()[0].style.occluded_alpha - 0.35f) < 1e-6f && FakeView::DrawCalls()[0].style.uv_offset_x == 0.25f,
              "its occluded opacity and its picture's frame as built");
        // abi 0.1.38's style ended at radial_fade, padded: today's is larger on both architectures.
        constexpr size_t old38 = (offsetof(wxl_draw_style, occluded_alpha) + alignof(wxl_draw_style) - 1) / alignof(wxl_draw_style) * alignof(wxl_draw_style);
        Check(WXL_DRAW_STYLE_SIZE_0138 > WXL_DRAW_STYLE_SIZE_0136 && sizeof(wxl_draw_style) > old38,
              "the styles of abi 0.1.38 are a prefix of today's, and smaller");

        // abi 0.1.40: particles.
        FakeView::DrawCalls().clear();
        using wxl::DrawEmitter;
        using wxl::DrawParticles;
        Check(DrawService::Emit(DrawEmitter::Point(wxl_vec3{1, 2, 3}).Burst(12).Cone(wxl_vec3{0, 0, 1}, 0.6f).Speed(3, 6).Gravity(19.29f)
                                    .Bounce(0.35f, 0.5f).Life(0.8f, 1.2f).Size(0.12f, 0.25f),
                                DrawParticles().Texture("SPELLS\\Dust.blp").Frames(4, 2, 12.0f).Key(0, 1, 0x806040, 1).Key(1, 0.5f, 0x806040, 0)
                                    .Softness(0.3f)) == WXL_OK,
              "Emit takes a burst of bouncing clods and their look");
        const wxl_handle puff = DrawService::AddEmitter(DrawEmitter::Disc(wxl_vec3{4, 5, 6}, 1.5f).Rate(40.0f, 0.5f).Attract(wxl_vec3{4, 5, 6}, 8.0f, 0.2f),
                                                        DrawParticles().Blend(WXL_DRAW_BLEND_ADD).Occluded(0.3f));
        Check(puff != WXL_HANDLE_NONE, "AddEmitter answers a handle");
        Check(DrawService::Burst(puff, 5) == WXL_OK && DrawService::Place(puff, wxl_vec3{7, 8, 9}, 0.5f) == WXL_OK &&
                  DrawService::Follow(puff, wxl::Unit(wxltest::FakeObjects::kBoar), WXL_DRAW_ATTACH_TURN) == WXL_OK &&
                  DrawService::UpdateEmitter(puff, DrawEmitter::Box(wxl_vec3{}, wxl_vec3{1, 1, 1})) == WXL_OK &&
                  DrawService::SetEmitterStyle(puff, DrawParticles().Stretched(0.05f)) == WXL_OK,
              "Burst, Place, Follow, UpdateEmitter and SetEmitterStyle reach the emitter");
        DrawService::Stop(puff);
        const auto& emitted = FakeView::DrawCalls();
        Check(emitted.size() == 8, "every particle call reached the service once");
        if (emitted.size() == 8)
        {
            const wxl_draw_emitter&   e = emitted[0].emitter;
            const wxl_draw_particles& p = emitted[0].particles;
            Check(e.burst == 12 && e.ground == WXL_DRAW_GROUND_BOUNCE && e.restitution == 0.35f && e.speed_max == 6.0f && e.gravity == 19.29f &&
                      e.struct_size == sizeof(wxl_draw_emitter),
                  "the burst as built");
            Check(emitted[0].texture == "SPELLS\\Dust.blp" && p.frames_x == 4 && p.frames_y == 2 && p.key_count == 2 && p.keys[1].alpha == 0.0f &&
                      p.keys[0].color == 0x806040u && p.softness == 0.3f,
                  "its look as built");
            Check(emitted[1].emitter.spawn == WXL_DRAW_SPAWN_DISC && emitted[1].emitter.rate == 40.0f && emitted[1].emitter.attraction == 8.0f &&
                      emitted[1].particles.blend == WXL_DRAW_BLEND_ADD && emitted[1].particles.occluded_alpha == 0.3f,
                  "the kept emitter as built");
            Check(emitted[2].count == 5 && emitted[3].center.y == 8.0f && emitted[4].attach == WXL_DRAW_ATTACH_TURN &&
                      emitted[5].emitter.spawn == WXL_DRAW_SPAWN_BOX && emitted[6].particles.mode == WXL_DRAW_BILLBOARD_STRETCHED &&
                      emitted[7].what == "stop",
                  "each change as given");
        }
        FakeView::DrawCalls().clear();
        const std::vector<wxl_vec3> ends = {{0, 0, 0}, {2, 3, 12}};
        Check(DrawService::Draw(DrawShape::Beam(ends, 0.8f, 2.2f), DrawStyle(0xFFFFFF, 0.0f).Fill(1.0f).Mask().TextureBlur(1.5f).Feather(1.0f)) == WXL_OK,
              "a beam, masked and blurred");
        Check(FakeView::DrawCalls().size() == 1 && FakeView::DrawCalls()[0].kind == WXL_DRAW_BEAM && FakeView::DrawCalls()[0].points.size() == 2 &&
                  (FakeView::DrawCalls()[0].style.flags & WXL_DRAW_STYLE_MASK) && FakeView::DrawCalls()[0].style.texture_blur == 1.5f,
              "its ends, its mask and its blur as built");
        Check(WXL_DRAW_STYLE_SIZE_0139 > WXL_DRAW_STYLE_SIZE_0138 && sizeof(wxl_draw_style) > WXL_DRAW_STYLE_SIZE_0139 + 4,
              "the styles of abi 0.1.39 are a prefix of today's, and smaller");
        const auto withParticles = DrawService::Stats();
        Check(withParticles && withParticles.Value().particles == 321, "Stats answers the particles alive");
    }

    Section("animations played on units");
    {
        using wxltest::FakeView;
        using wxltest::FakeObjects;
        using wxl::AnimationService;
        FakeView::AnimationCalls().clear();
        FakeView::AnimationPlays().clear();
        const wxl::Unit boar(FakeObjects::kBoar);
        Check(AnimationService::Play(boar, 62, WXL_ANIMATION_LOOP | WXL_ANIMATION_OVERRIDE, 1.5) == WXL_OK &&
                  AnimationService::Playing(boar).ValueOr(0) == 62 && AnimationService::Current(boar).ValueOr(0) == 62,
              "Play takes a unit, an id, flags and a speed; Playing and Current answer");
        Check(AnimationService::Play(boar, 600, 0, 0) == WXL_E_INVALID, "an id past the client's is refused");
        Check(AnimationService::Stop(boar) == WXL_OK && AnimationService::Stop(boar) == WXL_E_NOT_FOUND &&
                  !AnimationService::Playing(boar).HasValue(),
              "Stop ends it once");
        const auto& calls = FakeView::AnimationCalls();
        Check(calls.size() == 4 && calls[0].what == "play" && calls[0].unit == FakeObjects::kBoar && calls[0].anim == 62 &&
                  calls[0].flags == 5u && calls[0].speed == 1.5 && calls[2].what == "stop",
              "with the unit, the id, the flags and the speed as given");
    }

    Section("console variables");
    {
        using wxltest::FakeView;
        using wxl::CVarService;
        FakeView::CVars().clear();
        FakeView::CVarCalls().clear();
        Check(CVarService::Register("test_scale", "1.5", WXL_CVAR_SAVED | WXL_CVAR_LATCHED, "a size") == WXL_OK &&
                  CVarService::Register("test_bad", "0", 0x80u, "") == WXL_E_INVALID,
              "a variable is registered with its default, flags and help; a flag that names nothing is refused");
        Check(CVarService::Exists("TEST_SCALE") && !CVarService::Exists("test_none"), "a name is found without case");
        Check(CVarService::Get("test_scale").ValueOr("") == "1.5" && CVarService::GetNumber("test_scale").ValueOr(0.0) == 1.5 &&
                  CVarService::GetDefault("test_scale").ValueOr("") == "1.5",
              "its value reads as text and as a number, and its default");
        Check(CVarService::SetRange("test_scale", 0.5, 3.0) == WXL_OK && CVarService::SetNumber("test_scale", 2.0) == WXL_OK &&
                  CVarService::Set("test_scale", "9") == WXL_E_INVALID && CVarService::GetNumber("test_scale").ValueOr(0.0) == 2.0,
              "a range is kept and a set outside it refused");
        Check(CVarService::SetBool("test_scale", WXL_TRUE) == WXL_OK && CVarService::GetBool("test_scale").ValueOr(WXL_FALSE) == WXL_TRUE &&
                  CVarService::Reset("test_scale") == WXL_OK && CVarService::Get("test_scale").ValueOr("") == "1.5",
              "a switch is written and read, and a reset brings the default back");
        const auto flags = CVarService::Flags("test_scale");
        Check(flags && (*flags & WXL_CVAR_SAVED) && (*flags & WXL_CVAR_LATCHED) && (*flags & WXL_CVAR_MOD),
              "its flags answer what it is");
        Check(!CVarService::Get("test_none") && CVarService::Get("test_none").Error() == WXL_E_NOT_FOUND &&
                  CVarService::Release("test_scale") == WXL_OK,
              "a name nobody registered is not found; a variable is released");
        const auto& calls = FakeView::CVarCalls();
        Check(calls.size() == 2 && calls[0].what == "register" && calls[0].name == "test_scale" && calls[0].value == "1.5" && calls[0].help == "a size" &&
                  calls[0].flags == (WXL_CVAR_SAVED | WXL_CVAR_LATCHED) && calls[1].what == "release" && calls[1].name == "test_scale",
              "with the name, the default, the flags and the help whole");
    }

    Section("the game's selection circles");
    {
        using wxltest::FakeView;
        using wxltest::FakeObjects;
        using wxl::SelectionCircleService;
        FakeView::CircleCalls().clear();
        const wxl::Unit boar(FakeObjects::kBoar);
        Check(SelectionCircleService::Hide(WXL_SELECTIONCIRCLE_TARGET, 0) == WXL_OK &&
                  SelectionCircleService::Hide(0x8u, 0) == WXL_E_INVALID,
              "the target's circles are hidden; a bit that names no kind is refused");
        Check(SelectionCircleService::SetStyle(WXL_SELECTIONCIRCLE_TARGET, WXL_SELECTIONCIRCLE_REACTION(WXL_REACTION_HOSTILE),
                                               "Interface\\Mod\\Circle.blp", 0x80FF8000u, 1.25f) == WXL_OK &&
                  SelectionCircleService::SetStyle(0, 0, "", 0, 9.0f) == WXL_E_INVALID,
              "a style for hostile targets is set; a scale past the maximum refused");
        Check(SelectionCircleService::HideObject(boar) == WXL_OK &&
                  SelectionCircleService::SetObjectStyle(wxl::Unit(FakeObjects::kStale), "", 0xFFFFFFFFu, 1.0f) == WXL_E_NOT_FOUND &&
                  SelectionCircleService::SetObjectStyle(boar, "", 0xFF00FF00u, 0.5f) == WXL_OK &&
                  SelectionCircleService::ResetObject(boar) == WXL_OK && SelectionCircleService::Reset(0, 0) == WXL_OK,
              "an object's circles are hidden, restyled and given back; one gone is not found");
        SelectionCircleService::ResetAll();
        Check(*SelectionCircleService::Drawn() == 2u && *SelectionCircleService::Hidden() == 1u, "the counts of the last frame read");
        const auto& calls = FakeView::CircleCalls();
        Check(calls.size() == 7 && calls[0].name == "hide" && calls[0].kinds == WXL_SELECTIONCIRCLE_TARGET &&
                  calls[1].name == "set_style" && calls[1].reactions == 0x2u && calls[1].texture == "Interface\\Mod\\Circle.blp" &&
                  calls[1].color == 0x80FF8000u && calls[1].scale == 1.25f && calls[2].name == "hide_object" &&
                  calls[2].target == FakeObjects::kBoar && calls[3].name == "set_object_style" && calls[3].scale == 0.5f &&
                  calls[4].name == "reset_object" && calls[5].name == "reset" && calls[6].name == "reset_all",
              "with the masks, the texture, the colour, the scale and the object");
    }

    Section("textures made from an SVG or a PNG");
    {
        using wxltest::FakeView;
        FakeView::TextureCalls().clear();
        const wxl::TextureService textures;
        std::string               why;
        const auto made = textures.Make(wxl::TextureRequest(".\\assets\\reticle.svg")
                                            .Size(34, 21)
                                            .Color("fill", 0xFFFF4040u)
                                            .Color("outline", 0xCC000000u)
                                            .Width("outline", 2.0f)
                                            .Hide("glow")
                                            .Tint(0x80FFFFFFu),
                                        &why);
        Check(made && made->path == "Interface\\WXL\\fake\\made_34x21.blp" && made->texture_width == 64 &&
                  made->texture_height == 32 && made->right == 34.0f / 64.0f && made->bottom == 21.0f / 32.0f && !made->reused,
              "Make answers the path, the texture's size and the picture's texture coordinates");
        const auto& calls = FakeView::TextureCalls();
        Check(calls.size() == 1 && calls[0].source == ".\\assets\\reticle.svg" && calls[0].width == 34 &&
                  calls[0].height == 21 && calls[0].tint == 0x80FFFFFFu && calls[0].groups.size() == 3 &&
                  calls[0].groups[0].id == "fill" && calls[0].groups[0].set == WXL_TEXTURE_SET_COLOR &&
                  calls[0].groups[0].color == 0xFFFF4040u && calls[0].groups[1].id == "outline" &&
                  calls[0].groups[1].set == (WXL_TEXTURE_SET_COLOR | WXL_TEXTURE_SET_WIDTH) && calls[0].groups[1].width == 2.0f &&
                  calls[0].groups[2].id == "glow" && calls[0].groups[2].set == WXL_TEXTURE_SET_VISIBLE && !calls[0].groups[2].visible,
              "the request reaches the service whole: one group per id, each with what was set of it");
        const auto missing = textures.Make(wxl::TextureRequest("Mods\\x\\missing.svg"), &why);
        Check(!missing && missing.Error() == WXL_E_NOT_FOUND && why == "no file Mods\\x\\missing.svg",
              "a failure answers its result and the service's reason");
        const auto stats = textures.Stats();
        Check(stats && stats->textures == 3 && stats->byte_limit == (64u << 20) && stats->released == 1, "Stats reads");
    }

    Section("objects in handlers, and data kept per object");
    {
        using wxltest::FakeObjects;

        struct Watcher final : wxl::ObjectScript
        {
            wxl::Object created;
            bool        sawUnit = false;
            void        OnObjectCreate(wxl::Object object, wxl_guid, wxl_object_type) override
            {
                created = object;
                sawUnit = object.As<wxl::Unit>().has_value();
            }
        } watcher;
        wxl::ScriptMgr::Add(&watcher);
        const wxl_object_args boarCreated{FakeObjects::kBoar, 0x200, WXL_OBJECT_UNIT};
        host.Raise(WXL_EV_ObjectCreate, &boarCreated);
        Check(watcher.created == wxl::Object(FakeObjects::kBoar) && watcher.sawUnit,
              "a handle member reaches a handler as a wxl::Object, viewable as its class");
        Check(std::is_same_v<wxl::HandlerOf<WXL_EV_ObjectCreate>,
                             void(wxl::Object, wxl_guid, wxl_object_type)>,
              "an object event's handler takes the object, its guid and its type, as the enum");
        Check(wxl::Unit::Accepts(boarCreated.type) && !wxl::GameObject::Accepts(boarCreated.type) &&
                  wxl::Unit(watcher.created).Handle() == FakeObjects::kBoar,
              "the event's type picks the class, and an Object views as it unchecked");
        wxl::ScriptMgr::Remove(&watcher);

        wxl::ObjectMap<int> seen;
        Check(!seen.Watching() && seen.Empty(), "a map subscribes nothing until it holds something");
        seen[wxl::Unit(FakeObjects::kBoar)] = 3;
        seen.Set(wxl::Object(FakeObjects::kPlayer), 7);
        Check(seen.Watching() && seen.Size() == 2, "the first entry subscribes it to ObjectDestroy");
        Check(seen.Find(wxl::Object(FakeObjects::kBoar)) && *seen.Find(wxl::Object(FakeObjects::kBoar)) == 3,
              "a unit and the same object as an Object are one key");

        // A handler of default priority, subscribed after the map, still sees the entry.
        struct Reader
        {
            wxl::ObjectMap<int>* map;
            int                  read = -1;
            void operator()(wxl::Object object, wxl_guid, uint32_t)
            {
                const int* value = map->Find(object);
                read             = value ? *value : 0;
            }
        } reader{&seen};
        wxl::Subscription reading = wxl::Subscribe<WXL_EV_ObjectDestroy>(&reader);

        const wxl_object_args boarGone{FakeObjects::kBoar, 0x200, WXL_OBJECT_UNIT};
        host.Raise(WXL_EV_ObjectDestroy, &boarGone);
        Check(reader.read == 3, "a mod's ObjectDestroy handler runs before the map lets the entry go");
        Check(!seen.Contains(wxl::Object(FakeObjects::kBoar)) && seen.Size() == 1,
              "and the entry is gone once the event has passed");
        Check(seen.Contains(wxl::Object(FakeObjects::kPlayer)), "the other objects keep theirs");

        seen[wxl::Object(FakeObjects::kStale)] = 1;
        Check(seen.Prune() == 1 && seen.Size() == 1, "Prune drops what no longer resolves");
        reading.Reset();
    }

    Section("wxl.script");
    {
        wxl::ScriptService script;
        Check(static_cast<bool>(script) && script.Context().ValueOr(WXL_SCRIPT_CONTEXT_GLUE) == WXL_SCRIPT_CONTEXT_GAME,
              "the facade sees the context");
        Check(script.RegisterFunction<&Greet>("Greet") == WXL_OK, "a plain function registers");
        Check(script.RegisterFunction<&Greet>("Greet") == WXL_E_INVALID, "a taken name is refused");

        Adder adder;
        Check(script.RegisterFunction("Add", &adder) == WXL_OK, "a callable object registers");

        wxl_script_call call;
        call.args = {wxl::Value::String("Thorgrim"), wxl::Value::Bool(true)};
        Check(wxltest::FakeScript::Invoke("Greet", call), "the VM reaches the function");
        Check(call.results.size() == 3 && call.results[0].type == WXL_VALUE_STRING &&
                  wxl::View(call.results[0].as.string) == "Thorgrim" && call.results[1].as.number == 2.0 &&
                  call.results[2].as.boolean == WXL_TRUE,
              "arguments read and results push through the typed call");

        wxl_script_call bad;
        bad.args = {wxl::Value::Number(1.0)};
        wxltest::FakeScript::Invoke("Greet", bad);
        Check(bad.results.empty() && bad.error == "Greet(name) wants a string",
              "a wrong argument type is a miss, and Fail carries the message");

        wxl_script_call add;
        add.args = {wxl::Value::Number(2.0)};
        wxltest::FakeScript::Invoke("Add", add);
        wxltest::FakeScript::Invoke("Add", add);
        Check(adder.total == 4.0, "a callable object keeps its state between calls");

        Counter counter;
        Check(script.RegisterFunction<&Counter::Next>("Next", &counter) == WXL_OK &&
                  script.RegisterFunction<&Counter::Peek>("Peek", &counter) == WXL_OK,
              "a member function registers with its object, const or not");
        wxl_script_call next, peek;
        wxltest::FakeScript::Invoke("Next", next);
        wxltest::FakeScript::Invoke("Next", next);
        wxltest::FakeScript::Invoke("Peek", peek);
        Check(counter.count == 2 && peek.results.size() == 1 && peek.results[0].as.number == 2.0,
              "and is called on that object");
        Check(script.RegisterFunction<&Counter::Next>("Null", static_cast<Counter*>(nullptr)) ==
                  WXL_E_INVALID,
              "a null object is refused");

        Check(script.RegisterEvent("MY_EVENT") == WXL_OK, "an event registers");
        Check(script.FireEvent("MY_EVENT", {wxl::Value::Number(1), wxl::Value::String("x"), wxl::Value::Nil()}) == WXL_OK,
              "and fires with a list of values");
        Check(wxltest::FakeScript::Events().back() == "MY_EVENT n s -", "which arrive typed");
        Check(script.Run("print(1)", "chunk") == WXL_OK && wxltest::FakeScript::Chunks().back() == "chunk: print(1)",
              "a chunk runs with its name");

        const wxltest::FakeScript::Raise raise = [&](wxl_event event, const void* args) {
            host.Raise(event, args);
        };
        wxltest::FakeScript::Close(raise);
        Check(wxltest::FakeScript::Functions().empty() &&
                  script.RegisterFunction<&Greet>("Greet") == WXL_E_FAILED &&
                  script.FireEvent("MY_EVENT", {}) == WXL_E_FAILED,
              "a closed context takes its functions with it, and nothing registers until the next");
        wxltest::FakeScript::Open(WXL_SCRIPT_CONTEXT_GLUE, raise);
        Check(script.Context().ValueOr(WXL_SCRIPT_CONTEXT_GAME) == WXL_SCRIPT_CONTEXT_GLUE &&
                  script.RegisterFunction<&Greet>("Greet") == WXL_OK &&
                  script.FireEvent("MY_EVENT", {}) == WXL_E_NOT_FOUND,
              "the next starts empty: a name registers again, an event is declared again");
    }

    Section("wxl.storage");
    {
        wxl::StorageService storage;
        Check(storage.Exists("Interface\\hello.txt") && !storage.Exists("nope"), "exists answers");
        Check(storage.Size("Interface\\hello.txt").ValueOr(0) == 5, "a size reads");
        Check(storage.Size("nope").IsNotFound(), "a missing file is not found");
        Check(storage.ReadText("Interface\\hello.txt").ValueOr("") == "hello", "a file reads as text");
        Check(storage.Read("Interface\\big.bin").ValueOr(std::vector<uint8_t>{}).size() == 1000,
              "and as bytes");
        Check(storage.Read("nope").IsNotFound(), "reading a missing file is not found");

        const std::vector<std::string> found = storage.Find("Interface\\*");
        Check(found.size() == 2, "find lists matches, copied out");

        const int opens = wxltest::FakeStorage::Opens(), closes = wxltest::FakeStorage::Closes();
        {
            wxl::File file = storage.Open("Interface\\hello.txt");
            Check(static_cast<bool>(file) && wxltest::FakeStorage::Opens() == opens + 1, "a file opens");

            char                     buffer[8] = {};
            const wxl::Expected<uint64_t> read = file.ReadAt(1, buffer, sizeof(buffer));
            Check(read && *read == 4 && std::strncmp(buffer, "ello", 4) == 0, "and reads at an offset");

            wxl::File moved = std::move(file);
            Check(!file && moved, "a move hands the handle over");
            moved.Reset();
            moved.Reset();
            Check(wxltest::FakeStorage::Closes() == closes + 1, "Reset closes exactly once");
        }
        Check(wxltest::FakeStorage::Closes() == closes + 1, "and the destructor adds nothing");
        {
            wxl::File scoped = storage.Open("Interface\\hello.txt");
        }
        Check(wxltest::FakeStorage::Closes() == closes + 2, "a scoped file closes at the end of its scope");
        Check(!storage.Open("nope"), "a missing file opens as an empty File");

        Check(storage.AddSearchPath("no such folder", 1) == WXL_E_NOT_FOUND,
              "a generated method passes the runtime's answer through");
        Check(storage.MountArchive("Data\\patch-x.MPQ", 2) == WXL_OK, "and its arguments");

        const wxl::Expected<wxl::FileHash> hash = storage.HashFile("Interface\\hello.txt");
        Check(hash && hash->size == 5 && hash->layer == WXL_FILE_LAYER_ARCHIVE && hash->source == "Data\\fake.MPQ" &&
                  hash->Hex() == "050102030405060708090a0b0c0d0e0f",
              "HashFile copies the hash out, its digest as hex");
        Check(storage.HashFile("nope").IsNotFound(), "a missing file has no hash");
        int                 answered = 0;
        uint64_t            size     = 0;
        const wxl::Expected<uint64_t> request = storage.HashFileAsync("Interface\\hello.txt", [&](wxl::Expected<wxl::FileHash> got) {
            ++answered;
            size = got ? got->size : 0;
        });
        Check(request && answered == 0, "HashFileAsync answers a request, the hash to come");
        wxltest::FakeStorage::PumpHashes();
        Check(answered == 1 && size == 5, "and the function is called once with it");
        Check(storage.OverrideFile("Interface\\big.bin", "Interface\\hello.txt") == WXL_OK &&
                  storage.HashFile("Interface\\big.bin").ValueOr(wxl::FileHash{}).size == 5 &&
                  storage.RestoreFile("Interface\\big.bin") == WXL_OK && storage.RestoreFile("Interface\\big.bin") == WXL_E_NOT_FOUND,
              "OverrideFile and RestoreFile are generated from the table");
        const wxl::Expected<wxl_asset_reload> reloaded = storage.ReloadAsset("Textures\\a.blp");
        Check(reloaded && reloaded->textures == 3 && reloaded->busy == 1 && storage.ReloadAsset("World\\x.wmo").IsUnsupported(),
              "ReloadAsset answers what was done, or why nothing was");
    }

    Section("wxl.relay");
    {
        host.Table()->publish_service(WXL_RELAY_SERVICE, WXL_RELAY_SERVICE_VERSION, wxltest::FakeRelay::Table());
        const wxl::RelayService relay;
        std::string             asked;
        wxl::Expected<wxl::RelayPublication> published =
            relay.Publish("test.echo", [&](const wxl::RelayRequest& request, std::string_view method, wxl::RelayValues args) {
                asked = std::string(method) + ":" + std::string(args.String(0));
                request.Answer({wxl::Value::String("echo"), wxl::Value::Number(7)});
            });
        Check(published && relay.Published("TEST.echo"), "a service publishes, and is found in any case");
        int         replies = 0;
        wxl_result  result  = WXL_E_UNSUPPORTED;
        std::string first;
        const wxl::Expected<wxl_request> request =
            relay.Call("test.echo", "say", {wxl::Value::String("hi")}, [&](wxl_result r, wxl::RelayValues values) {
                ++replies;
                result = r;
                first  = std::string(values.String(0)) + std::to_string(static_cast<int>(values.Number(1)));
            });
        Check(request && replies == 0, "a call answers its request, the reply to come");
        wxltest::FakeRelay::Pump();
        Check(asked == "say:hi" && replies == 1 && result == WXL_OK && first == "echo7",
              "the service gets the method and arguments, the caller the answer");
        int cancelledReplies = 0;
        const wxl::Expected<wxl_request> dropped =
            relay.Call("test.nobody", "x", {}, [&](wxl_result, wxl::RelayValues) { ++cancelledReplies; });
        Check(dropped && relay.Cancel(*dropped) == WXL_OK, "a request is cancelled by its caller");
        wxltest::FakeRelay::Pump();
        Check(cancelledReplies == 0, "and its reply never called");
        published->Reset();
        Check(!relay.Published("test.echo"), "a publication taken back unpublishes");
    }

    Section("wxl.data: methods generated from data.def");
    {
        const wxl::DataService data;
        Check(data.IsLoaded("Spell.dbc") && !data.IsLoaded("DBFilesClient\\Item.dbc"),
              "IsLoaded answers a bool");
        Check(data.ServeTable("Item.dbc", "Interface\\hello.txt") == WXL_OK &&
                  data.ServeTable("item.dbc", "Interface\\hello.txt") == WXL_E_FAILED,
              "ServeTable answers the runtime's wxl_result");
        Check(data.ServedFrom("DBFilesClient\\Item.dbc") == "Interface\\hello.txt" && data.ServedFrom("x.dbc").empty(),
              "ServedFrom copies the path into a std::string, empty for the client's own file");
        Check(data.RestoreTable("Item.dbc") == WXL_OK && data.RestoreTable("Item.dbc") == WXL_E_NOT_FOUND,
              "RestoreTable gives it back once");

        // A runtime whose table stops short of a member answers for it as for no service.
        static wxl_data_api shorter = *wxltest::FakeData::Table();
        shorter.struct_size         = offsetof(wxl_data_api, served_from);
        static wxl_data_api holed   = *wxltest::FakeData::Table();
        holed.is_loaded             = nullptr;
        Check(!wxl::detail::Carries(&shorter, &wxl_data_api::served_from) &&
                  wxl::detail::Carries(&shorter, &wxl_data_api::restore_table) &&
                  !wxl::detail::Carries(&holed, &wxl_data_api::is_loaded),
              "what the generated methods check before calling: the table's size, then the member");
    }

    Section("wxl.net");
    {
        using wxltest::FakeNet;
        wxl::NetService netService;
        Check(netService.State() == WXL_NET_AUTHENTICATED, "the state reads");
        Check(netService.ReservedOpcodeLimit().ValueOr(0) == 0x520 && netService.MaxSend() == 10000,
              "the reserved limit and the largest body read");

        struct Body
        {
            uint32_t a;
            uint16_t b;
        } body{1, 2};
        Check(netService.Send(0x600, body) == WXL_E_INVALID, "a number nobody claimed is not sent");

        const int releases = FakeNet::Releases();
        {
            wxl::Expected<wxl::OpcodeClaim> claim = netService.Claim(0x601);
            Check(claim && *claim && claim->Opcode() == 0x601, "an opcode claims");
            Check(netService.Claim(0x601).IsInvalid(), "a second claim is refused");

            wxl::OpcodeClaim moved = std::move(*claim);
            Check(moved && !*claim, "a move hands the claim over");
            moved.Reset();
            moved.Reset();
            Check(FakeNet::Releases() == releases + 1, "Reset releases exactly once");
        }
        Check(FakeNet::Releases() == releases + 1, "and the destructor adds nothing");
        {
            wxl::Expected<wxl::OpcodeClaim> scoped = netService.Claim(0x602);
        }
        Check(FakeNet::Releases() == releases + 2 && !FakeNet::Claimed().count(0x602),
              "a scoped claim is given back at the end of its scope");

        // Readers and writers, on their own.
        wxl::ByteWriter out;
        out.U8(7).U16(0x1234).U32(0xDEADBEEF).F32(1.5f).PackedGuid(0x0000000100000F00ull).String("hi").Vec3({1, 2, 3});
        Check(out.Ok() && out.Size() == 1 + 2 + 4 + 4 + 3 + 3 + 12, "a writer lays its values out little-endian");
        Check(out.Data()[1] == 0x34 && out.Data()[2] == 0x12 && out.Data()[11] == 0x12 && out.Data()[12] == 0x0F &&
                  out.Data()[13] == 0x01,
              "u16 low byte first; a packed guid is its mask and its non-zero bytes");
        wxl::ByteReader in(out.Data(), out.Size());
        const uint8_t   u8   = in.U8();
        const uint16_t  u16  = in.U16();
        const uint32_t  u32  = in.U32();
        const float     f32  = in.F32();
        const uint64_t  guid = in.PackedGuid();
        const std::string text(in.String());
        const wxl_vec3  v    = in.Vec3();
        Check(u8 == 7 && u16 == 0x1234 && u32 == 0xDEADBEEF && f32 == 1.5f && guid == 0x0000000100000F00ull &&
                  text == "hi" && v.z == 3.0f && in.AtEnd(),
              "a reader reads back what the writer wrote, to the end");
        Check(in.U32() == 0 && in.Failed() && in.Remaining() == 0, "a read past the end answers 0 and fails the reader");
        wxl::ByteReader cut(out.Data(), 3);
        Check(cut.U8() == 7 && cut.U32() == 0 && cut.Failed() && cut.U8() == 0,
              "a short read fails, and every read after it answers 0");
        const uint8_t unended[3] = {'a', 'b', 'c'};
        wxl::ByteReader open(unended, sizeof unended);
        Check(open.String().empty() && open.Failed(), "a string without its zero fails the reader");
        wxl::ByteWriter small(4);
        small.U16(1).U16(2).U8(3);
        Check(small.Failed() && small.Size() == 4 && netService.Send(0x601, small) == WXL_E_INVALID,
              "a writer refuses to grow past its limit, and a failed writer is not sent");

        // NetOpcodes: claims, handlers, compressed numbers, the send gate.
        {
            wxl::NetOpcodes net;
            uint32_t        got = 0, seq = 0;
            float           height = 0;
            Check(net.Handle(0x524, [&](wxl::ByteReader& r) {
                      seq    = r.U32();
                      height = r.F32();
                      ++got;
                  }) == WXL_OK,
                  "Handle claims a number and sets its handler");
            Check(net.Handle(0x529, [&](wxl::ByteReader& r) { got += 100 * r.U8(); }, 0x52A) == WXL_OK &&
                      FakeNet::Claimed().count(0x52A),
                  "and its compressed number with it");
            Check(net.Claim(0x521) == WXL_OK, "a number to send is claimed");
            FakeNet::Connect();
            Check(net.Send(0x521, wxl::ByteWriter{}.U32(9)) == WXL_E_FAILED && !net.Heard(),
                  "nothing is sent before the server speaks on this connection");
            wxl::ByteWriter update;
            update.U32(41).F32(2.5f);
            Check(FakeNet::Deliver(0x524, std::string(reinterpret_cast<const char*>(update.Data()), update.Size())) &&
                      got == 1 && seq == 41 && height == 2.5f,
                  "a message of a claimed number reaches its handler as a reader");
            Check(net.Heard() && net.Send(0x521, wxl::ByteWriter{}.U32(9)) == WXL_OK && FakeNet::Bodies().back().size() == 4,
                  "and opens the gate: the mod sends");
            const uint8_t raw[1] = {3};
            std::string   packed(4, '\0');
            const uint32_t want = 1;
            std::memcpy(&packed[0], &want, 4);
            packed += FakeNet::DeflateStored(raw, 1);
            Check(FakeNet::Deliver(0x52A, packed) && got == 301, "a compressed message reaches the plain number's handler");
            FakeNet::Connect();
            Check(net.Send(0x521, wxl::ByteWriter{}.U32(9)) == WXL_E_FAILED, "a new connection closes the gate again");
            const auto zipped = netService.Deflate("hello", 5);
            const auto back   = zipped ? netService.Inflate(zipped->data(), static_cast<uint32_t>(zipped->size()))
                                       : wxl::Expected<std::vector<uint8_t>>(wxl::Unexpected{WXL_E_FAILED});
            Check(back && back->size() == 5 && std::memcmp(back->data(), "hello", 5) == 0, "Deflate and Inflate go round");
        }
        Check(!FakeNet::Claimed().count(0x524) && !FakeNet::Claimed().count(0x52A) && !FakeNet::Claimed().count(0x521),
              "NetOpcodes gives every number back when it goes");
    }

    Section("wxl.ui");
    {
        wxl::UiService ui;
        Check(static_cast<bool>(ui) && ui.IsOpen() && ui.CanRemovePanels(), "the facade sees the overlay");

        const int removes = wxltest::FakeUi::Removes();
        {
            wxl::Expected<wxl::Panel> panel = ui.AddPanel<&DrawPanel>("Example");
            Check(panel && *panel && panel->Title() == "Example", "a panel registers");
            Check(ui.AddPanel<&DrawPanel>("Example").IsInvalid(), "a taken title is refused");

            wxltest::FakeUi::DrawAll();
            Check(g_panelDraws == 1 && wxltest::FakeUi::Drawn().back() == "hello", "the overlay draws it");

            wxl::Panel moved = std::move(*panel);
            Check(moved && !*panel, "a move hands the panel over");
            Check(moved.Reset() == WXL_OK && moved.Reset() == WXL_OK, "Reset removes it");
            Check(wxltest::FakeUi::Removes() == removes + 1, "exactly once");
        }
        Check(wxltest::FakeUi::Removes() == removes + 1, "and the destructor adds nothing");
        {
            struct Body
            {
                int draws = 0;
                void operator()() { ++draws; }
            } body;
            wxl::Expected<wxl::Panel> scoped = ui.AddPanel("Scoped", &body);
            wxltest::FakeUi::DrawAll();
            Check(scoped && body.draws == 1, "a callable object draws");
        }
        Check(wxltest::FakeUi::Removes() == removes + 2 && wxltest::FakeUi::Panels().empty(),
              "a scoped panel is removed at the end of its scope");

        {
            wxl::Panel older(wxltest::FakeUi::OlderTable(), "Old");
            Check(older.Reset() == WXL_E_UNSUPPORTED && wxltest::FakeUi::Removes() == removes + 2,
                  "a table that predates remove_panel is left alone and says so");
        }

        bool    flag  = false;
        float   f     = 0.0f;
        int32_t i     = 0;
        float   rgba[4] = {};
        char    text[8] = {};
        const char* const items[] = {"a", "b", "c"};
        Check(ui.Checkbox("c", flag) && flag, "a checkbox binds a bool");
        Check(ui.SliderFloat("f", f, 0.0f, 2.0f) && f == 2.0f, "a float slider binds");
        Check(ui.SliderInt("i", i, 0, 9) && i == 9, "an int slider binds");
        Check(ui.ColorEdit("rgba", rgba) && rgba[3] == 0.5f, "a colour binds");
        Check(ui.Combo("combo", i, items) && i == 2, "a combo sizes its items");
        Check(ui.InputText("t", text) && std::strcmp(text, "typed") == 0, "a text field sizes its buffer");
        Check(ui.Button("go") && ui.CollapsingHeader("h"), "the rest answer");
    }

    Section("wxl.hook");
    {
        wxl::HookService hooks;
        const std::vector<wxl::HookPoint> points = hooks.Points();
        Check(points.size() == 1 && points[0].name == "Thing" && points[0].signature == "int(int)",
              "the named points list, copied out");
        Check(hooks.HasPoint("Thing") && !hooks.HasPoint("Other"), "has_point answers");

        const int detaches = wxltest::FakeHooks::Detaches();
        {
            ThingFn   original = nullptr;
            wxl::Hook hook     = hooks.Attach("Thing", &ThingDetour, &original);
            Check(static_cast<bool>(hook), "a typed detour attaches");
            Check(reinterpret_cast<void*>(original) == reinterpret_cast<void*>(&wxltest::FakeHooks::Engine),
                  "and receives the next link, typed");

            Check(!hooks.Attach("Other", &ThingDetour, &original), "an unnamed point attaches nothing");

            wxl::Hook moved = std::move(hook);
            Check(moved && !hook, "a move hands the hook over");
            moved.Reset();
            moved.Reset();
            Check(wxltest::FakeHooks::Detaches() == detaches + 1, "Reset detaches exactly once");
        }
        Check(wxltest::FakeHooks::Detaches() == detaches + 1, "and the destructor adds nothing");
        {
            ThingFn   original = nullptr;
            wxl::Hook scoped   = hooks.Attach("Thing", &ThingDetour, &original);
        }
        Check(wxltest::FakeHooks::Detaches() == detaches + 2 && wxltest::FakeHooks::Live().empty(),
              "a scoped hook detaches at the end of its scope");

        wxl_hook raw = WXL_HOOK_NONE;
        {
            ThingFn   original = nullptr;
            wxl::Hook kept     = hooks.Attach("Thing", &ThingDetour, &original);
            raw                = kept.Release();
        }
        Check(raw != WXL_HOOK_NONE && wxltest::FakeHooks::Live().count(raw) == 1,
              "Release disowns: the hook outlives the wrapper");
        wxltest::FakeHooks::Table()->detach(raw);

        wxl::RawHookService rawHooks;
        Check(rawHooks.ImageBase().ValueOr(0) == 0x400000 && rawHooks.ImageBuild().ValueOr(0) == 12340,
              "the raw service describes the image");
        {
            ThingFn   original = nullptr;
            wxl::Hook at       = rawHooks.AttachAt("Thing", 0x401000, &ThingDetour, &original);
            Check(static_cast<bool>(at) && original != nullptr, "a detour attaches at an address");
        }
        Check(wxltest::FakeHooks::Live().empty(), "and detaches with its wrapper");
    }

    Section("wxl.patch");
    {
        using wxltest::FakePatch;
        wxl::PatchService patches;

        const uint8_t nop[1] = {0x90}, ret[1] = {0xC3}, two[2] = {0x90, 0x90}, jmp[2] = {0xEB, 0x00};
        Check(patches.Matches(FakePatch::kBase, nop) && !patches.Matches(FakePatch::kBase, ret),
              "matches reads without writing");
        Check(patches.Find(two, 2) == FakePatch::kBase && patches.Find(two, 2, FakePatch::kBase) == FakePatch::kBase + 1,
              "find walks the image");
        Check(patches.FindLiteral("zz") == 0, "a literal that is not there finds nothing");

        {
            wxl::Patch patch = patches.Apply("ret", FakePatch::kBase, nop, ret);
            Check(static_cast<bool>(patch) && FakePatch::Image()[0] == 0xC3, "a patch writes");
            Check(!patches.Apply("again", FakePatch::kBase, nop, ret), "a mismatch is refused");

            wxl::Patch moved = std::move(patch);
            Check(moved && !patch, "a move hands the patch over");
            moved.Reset();
            moved.Reset();
            Check(FakePatch::Image()[0] == 0x90 && FakePatch::RestoreOrder().size() == 1,
                  "Reset restores exactly once");
        }
        Check(FakePatch::RestoreOrder().size() == 1, "and the destructor adds nothing");

        FakePatch::RestoreOrder().clear();
        {
            wxl::PatchSet set;
            Check(set.Apply("a", FakePatch::kBase, nop, ret), "a set applies one");
            Check(set.Apply("b", FakePatch::kBase + 2, two, jmp), "and another");
            Check(!set.Apply("c", FakePatch::kBase + 2, two, jmp) && set.Size() == 2,
                  "a refused one is not kept");
            Check(FakePatch::Image()[0] == 0xC3 && FakePatch::Image()[2] == 0xEB, "both are in place");
        }
        Check(FakePatch::RestoreOrder() == std::vector<wxl_patch>{3, 2} && FakePatch::Image()[0] == 0x90 &&
                  FakePatch::Image()[2] == 0x90,
              "a set restores in reverse order of apply");

        {
            wxl::PatchSet kept;
            kept.Apply("a", FakePatch::kBase, nop, ret);
            kept.Release();
        }
        Check(FakePatch::Image()[0] == 0xC3, "Release leaves the bytes in place");
        FakePatch::Image()[0] = 0x90;
        FakePatch::Live().clear();
    }

    Section("a runtime that cannot raise an event");
    {
        Check(host.Table()->subscribe(WXL_EV_Update, nullptr, nullptr, WXL_PRIORITY_DEFAULT) ==
                  WXL_SUBSCRIPTION_NONE,
              "a null handler is refused");
        host.Withhold(WXL_EV_SoundPlay);
        wxl::Subscription none = wxl::Subscribe(WXL_EV_SoundPlay, [](void*, const void*) {}, nullptr);
        Check(!none, "a withheld event yields an empty subscription, which is an answer");
    }

    Section("log");
    {
        wxl::Log(WXL_LOG_INFO, "contract", "hello");
        Check(host.LogLines().size() == 1 && host.LogLines().back() == "[contract] hello",
              "a log line crosses with its tag and its length");
        wxl::Log(WXL_LOG_INFO, "contract", std::string_view("sliced", 5));
        Check(host.LogLines().back() == "[contract] slice", "a view crosses with its own length");
        wxl::Log(WXL_LOG_INFO, "contract", "{} of 0x{:04X}, {:.1f}", 3, 0x85u, 2.3);
        Check(host.LogLines().back() == "[contract] 3 of 0x0085, 2.3",
              "a line with arguments is formatted with std::format");
        wxl::Log(WXL_LOG_INFO, "contract", "50% {kept}");
        Check(host.LogLines().back() == "[contract] 50% {kept}", "one without is written as it is");
        Check(std::string_view(wxl::ResultName(WXL_E_NOT_FOUND)) == "WXL_E_NOT_FOUND" &&
                  std::string_view(wxl::ResultName(static_cast<wxl_result>(77))) ==
                      "an unknown wxl_result",
              "ResultName names a result, and says when it does not know one");
    }

    Section("what a throw costs");
    {
        size_t     from   = host.LogLines().size();
        const auto logged = [&](const std::string& line) {
            for (size_t i = from; i < host.LogLines().size(); ++i)
                if (host.LogLines()[i] == line) return true;
            return false;
        };

        ThrowingWorldScript thrower;
        CountingWorldScript counter;
        wxl::ScriptMgr::Add(&thrower);
        wxl::ScriptMgr::Add(&counter);
        const wxl_update_args tick{0.5f};
        host.Raise(WXL_EV_Update, &tick);
        Check(thrower.calls == 1 && counter.updates == 1,
              "a script that throws loses that call, and the next script still gets it");
        Check(logged("[wxl.contract] WorldScript::OnUpdate threw an exception: tick; that call "
                     "ended there, and the mod goes on receiving events"),
              "logged under the mod's id, with the handler and what()");
        host.Raise(WXL_EV_Update, &tick);
        Check(thrower.calls == 2 && host.Disables().empty(), "it gets the next event: nothing is disabled");
        wxl::ScriptMgr::Clear();

        ThrowingHandler   handler;
        wxl::Subscription subscription = wxl::Subscribe<WXL_EV_Update>(&handler);
        host.Raise(WXL_EV_Update, &tick);
        Check(handler.calls == 1 &&
                  logged("[wxl.contract] a wxl::Subscribe handler for Update threw something that "
                         "is not a std::exception; that call ended there, and the mod goes on "
                         "receiving events"),
              "a handler given to Subscribe is caught too, and a throw of another type is named so");
        subscription.Reset();

        Counter         object;
        wxl_script_call call;
        Check(wxl::ScriptService().RegisterFunction<&Counter::Throws>("Throws", &object) == WXL_OK &&
                  wxltest::FakeScript::Invoke("Throws", call) && call.error == "no counting today" &&
                  call.results.empty(),
              "a script function that throws fails the call, with what() as the script's error");
        Check(logged("[wxl.contract] a function registered with wxl.script threw an exception: no "
                     "counting today; the call fails on the script side with that message"),
              "and is logged");

        LifecycleMod::throwIn = "OnLoad";
        LifecycleMod::trace.clear();
        const size_t before   = host.LiveSubscriptions();
        LifecycleMod::script  = &counter;
        Check(wxl_mod_load(host.Table()) == WXL_E_FAILED &&
                  logged("[wxl.contract] OnLoad threw an exception: boom in OnLoad; the mod is not "
                         "loaded"),
              "OnLoad throwing fails the load, and says why");
        Check(host.LiveSubscriptions() == before, "what it subscribed before throwing is dropped");
        LifecycleMod::script = nullptr;
        wxl_mod_stage(WXL_STAGE_EARLY);
        Check(LifecycleMod::trace.size() == 1, "and none of its code runs after");

        LifecycleMod::throwIn = "OnReady";
        LifecycleMod::trace.clear();
        Check(wxl_mod_load(host.Table()) == WXL_OK, "a mod that throws later loads");
        for (int stage = WXL_STAGE_EARLY; stage <= WXL_STAGE_SHUTDOWN; ++stage)
            wxl_mod_stage(static_cast<wxl_stage>(stage));
        wxl_mod_unload();
        const std::vector<std::string> stopped = {"load",   "early",    "stage0", "storage", "stage1",
                                                  "graphics", "stage2", "script", "stage3", "ready"};
        Check(LifecycleMod::trace == stopped,
              "a stage that throws stops the mod there: no OnStage after it, no later stage, no "
              "unload");
        Check(host.Disables().size() == 1 &&
                  host.Disables().back() == "OnReady threw an exception: boom in OnReady",
              "and asks the runtime to disable it, with the stage's virtual and what()");

        LifecycleMod::throwIn = "OnStage";
        LifecycleMod::trace.clear();
        wxl_mod_load(host.Table());
        wxl_mod_stage(WXL_STAGE_EARLY);
        wxl_mod_stage(WXL_STAGE_STORAGE);
        Check(LifecycleMod::trace == std::vector<std::string>{"load", "early", "stage0"} &&
                  host.Disables().back() ==
                      "OnStage(early) threw something that is not a std::exception",
              "OnStage is named with its stage");

        host.WithoutDisable();
        from                  = host.LogLines().size();
        LifecycleMod::throwIn = "OnUnload";
        LifecycleMod::trace.clear();
        wxl_mod_load(host.Table());
        wxl_mod_unload();
        Check(host.Disables().size() == 2 &&
                  logged("[wxl.contract] OnUnload threw an exception: boom in OnUnload; this "
                         "runtime cannot disable a mod (it predates abi 0.1.8), so the mod stops "
                         "itself: its scripts are dropped, and its remaining stages and its "
                         "unload do nothing"),
              "on a runtime without disable, the mod says so and stops itself");
        host.WithDisable();
        LifecycleMod::throwIn.clear();
    }

    Section("typed points");
    {
        host.Table()->publish_service(WXL_POINT_SERVICE, WXL_POINT_SERVICE_VERSION, &fakepoint::g_api);
        using Camera = wxl::points::CGCamera_UpdateCallback;
        Check(std::is_same_v<Camera::Signature, int32_t(void*, void*)> && Camera::kReplaceable && Camera::kStable &&
                  Camera::kThread == WXL_POINT_THREAD_MAIN && std::strcmp(Camera::kName, "CGCamera::UpdateCallback") == 0,
              "a point's class carries its row: signature, policy, stability, thread, name");
        Check(Camera::Id() == 1, "its id is found by name");

        int  seen  = 0;
        auto first = Camera::Before([&seen](void*& data, void*&) { seen = static_cast<int>(reinterpret_cast<uintptr_t>(data)); data = reinterpret_cast<void*>(uintptr_t{10}); });
        wxl::PointAttachment replace = Camera::Replace([](auto original, void* data, void* param) {
            return original(data, reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(param) + 1)) * 2;
        });
        wxl::PointAttachment after = Camera::After([](int32_t& result, void*, void*) { result += 3; });
        Check(first && replace && after && fakepoint::g_attached.size() == 3, "Before, Replace and After attach");
        Check(fakepoint::g_attached[1].hash == Camera::kLayoutHash, "and pass the point's layout hash");
        const int32_t answer = fakepoint::Run(1, 2);
        Check(seen == 1 && answer == (10 + 3) * 2 + 3 && fakepoint::g_originals == 1,
              "Before rewrote an argument, Replace called the original with its own and doubled it, After added to it");

        wxl::PointAttachment throws = Camera::Replace([](auto, void*, void*) -> int32_t { throw std::runtime_error("no"); });
        replace.Reset();
        fakepoint::g_originals = 0;
        Check(fakepoint::Run(1, 2) == 10 + 2 + 3 && fakepoint::g_originals == 1,
              "a Replace that throws before calling the original: the facade calls it, the call goes on");
        first.Reset();
        after.Reset();
        throws.Reset();
        Check(fakepoint::Run(1, 2) == 3, "Reset detaches each");
        Check(!fakepoint::g_attached[0].live && !fakepoint::g_attached[3].live, "and a reset handle detaches nothing twice");
    }

    if (g_failures == 0)
    {
        std::printf("\nall checks passed\n");
        return 0;
    }

    std::printf("\n%d check(s) failed\n", g_failures);
    return 1;
}
