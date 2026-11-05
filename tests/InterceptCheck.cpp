// SPDX-License-Identifier: GPL-3.0-or-later
// wxl-intercept-check.exe: Before, Replace and After on this executable's functions, one per convention.

#include "FakeCore.hpp"

#include "wxl/Intercept.hpp"

#include <cstdio>
#include <string>

namespace
{
    int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++g_failures; } } while (0)

    __declspec(noinline) int __cdecl Add(int a, int b) { return a + b; }
    __declspec(noinline) int __stdcall Scale(int a, float f) { return int(a * f); }
    __declspec(noinline) void __fastcall Note(void* self, void* edx, const char* text)
    {
        (void)edx;
        *static_cast<std::string*>(self) += text;
    }

    using AddFn   = int(__cdecl*)(int, int);
    using ScaleFn = int(__stdcall*)(int, float);
    using NoteFn  = void(__fastcall*)(void*, void*, const char*);

    /// A float result, as the client's methods return one: in ST0, whatever the extension compiles to.
    __declspec(noinline) float __fastcall Lerp(void* self, void* edx, float t)
    {
        (void)edx;
        return *static_cast<const float*>(self) * t;
    }
    using LerpFn = float(__fastcall*)(void*, void*, float);

    // What the fake core's attach saw: the detour the client would now call.
    void* g_detour[4] = {};
    int __cdecl FakeInterceptAttach(const char*, uintptr_t target, void* detour, void** original)
    {
        const int slot = target == uintptr_t(&Add) ? 0 : target == uintptr_t(&Scale) ? 1 : target == uintptr_t(&Lerp) ? 3 : 2;
        g_detour[slot] = detour;
        *original      = reinterpret_cast<void*>(target); // the next link: the function itself
        return 1;
    }
    uint32_t __cdecl FakeSelf() { return 1; }
    bool     g_disabled = false;
    int __cdecl FakeRun(uint32_t, void(__cdecl* fn)(void*), void* context)
    {
        if (g_disabled) return WXL_E_EXPIRED;
        WXL_SEH_TRY(wxl::seh::Catch::Any)
        {
            fn(context);
            return WXL_OK;
        }
        WXL_SEH_EXCEPT
        {
            g_disabled = true;
            return WXL_E_EXPIRED;
        }
    }

    int CallAdd(int a, int b) { return reinterpret_cast<AddFn>(g_detour[0])(a, b); }
    int CallScale(int a, float f) { return reinterpret_cast<ScaleFn>(g_detour[1])(a, f); }
    void CallNote(std::string& s, const char* t) { reinterpret_cast<NoteFn>(g_detour[2])(&s, nullptr, t); }

    /// Calls the detour as the client does and takes the result from ST0, not from what the compiler expects.
    __declspec(noinline) float CallLerpFromX87(void* self, float t)
    {
        void* fn = g_detour[3];
        float result;
        __asm
        {
            push t
            mov ecx, self
            xor edx, edx
            call fn
            fstp result
        }
        return result;
    }
}

int main()
{
    static WXL_Api api  = fake::MakeApi();
    api.InterceptAttach = &FakeInterceptAttach;
    api.Self            = &FakeSelf;
    api.Run             = &FakeRun;
    CHECK(wxl::Bind(&api, "intercept-check"));

    auto& add = wxl::Intercept<AddFn, &Add>();
    CHECK(!add.Attached());

    // Before: an argument changed; priority orders the handlers.
    std::string order;
    {
        auto late  = add.Before([&](auto&) { order += "2"; }, wxl::priority::Last);
        auto early = add.Before([&](auto& call) { order += "1"; call.template Arg<0>() = 10; });
        CHECK(add.Attached() && g_detour[0] && add.Count() == 2);
        CHECK(CallAdd(1, 2) == 12 && order == "12");
    }
    CHECK(add.Count() == 0 && CallAdd(1, 2) == 3);

    // Before returning early: the function and the Replace are skipped, After still runs.
    {
        bool replaced = false, after = false;
        auto skip    = add.Before([](auto& call) { if (call.template Arg<0>() < 0) call.Return(-1); });
        auto replace = add.Replace([&](auto& call) { replaced = true; return call.Original() * 2; });
        auto seen    = add.After([&](auto& call) { after = true; call.Result() += 1; });
        CHECK(CallAdd(2, 3) == 11 && replaced && after);
        replaced = after = false;
        CHECK(CallAdd(-5, 3) == 0 && !replaced && after);
    }

    // stdcall: the result changed After; a handler calling the function itself passes through.
    {
        auto& scale = wxl::Intercept<ScaleFn, &Scale>();
        int   inner = 0;
        auto  after = scale.After([&](auto& call) {
            inner = CallScale(10, 0.5f); // re-entry: no handler
            call.Result() = call.Result() + 100;
        });
        CHECK(CallScale(4, 2.0f) == 108 && inner == 5);
    }

    // fastcall (a thiscall method, as the offsets write it): void, Replace skipping the function.
    {
        auto&       note = wxl::Intercept<NoteFn, &Note>();
        std::string text;
        auto        before = note.Before([](auto& call) { call.template Arg<2>() = "changed "; });
        CallNote(text, "original ");
        CHECK(text == "changed ");
        auto replace = note.Replace([](auto& call) {
            *static_cast<std::string*>(call.template Arg<0>()) += "[";
            call.Original();
            *static_cast<std::string*>(call.template Arg<0>()) += "]";
        });
        CallNote(text, "x");
        CHECK(text == "changed [changed ]");
    }

    // A float result: the interceptor gives it back in ST0, changed or not.
    {
        auto&       lerp  = wxl::Intercept<LerpFn, &Lerp>();
        float       base  = 3.0f;
        float       seen  = 0.0f;
        auto        after = lerp.After([&](auto& call) { seen = call.Result(); call.Result() += 0.5f; });
        CHECK(CallLerpFromX87(&base, 2.0f) == 6.5f && seen == 6.0f);
    }

    // A handler that faults: the extension is disabled, the call goes on as without it.
    {
        auto fault = add.Before([](auto& call) {
            call.template Arg<0>() = 1000;
            *static_cast<volatile int*>(nullptr) = 1;
        });
        auto after = add.After([](auto& call) { call.Result() = 0; });
        CHECK(CallAdd(1, 2) == 3); // what it changed before faulting is undone; nothing else of it runs
        CHECK(g_disabled);
        CHECK(CallAdd(1, 2) == 3);
    }

    std::printf("%d failure(s)\n", g_failures);
    return g_failures ? 1 : 0;
}
