// SPDX-License-Identifier: GPL-3.0-or-later
// wxl-hot-path-check.exe: no heap allocation per call in overrides, On, Log, signals, interceptors, hooks, watches, tasks.

#include "FakeCore.hpp"

#include "wxl/Hook.hpp"
#include "wxl/Intercept.hpp"
#include "wxl/Extension.hpp"
#include "wxl/Log.hpp"
#include "wxl/Signal.hpp"
#include "wxl/Task.hpp"
#include "wxl/objects/Observable.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace
{
    int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++g_failures; } } while (0)

    long g_allocations = 0;

    constexpr int kWarmUp = 4;
    constexpr int kCalls  = 1000;

    namespace ev = wxl::events;

    /// Calls the handlers of `event` as the core's bus does, without the fake bus's own allocations.
    template <ev::Event E>
    void Dispatch(const ev::Args<E>& args)
    {
        for (const fake::Entry* e : fake::g_entries)
            if (e && !e->ended && e->event == uint32_t(E)) e->fn(e->user, &args);
    }

    /// The allocations `call` makes per run, once warmed up.
    template <class F>
    long AllocationsPerRun(const char* what, F&& call)
    {
        for (int i = 0; i < kWarmUp; ++i) call();
        const long before = g_allocations;
        for (int i = 0; i < kCalls; ++i) call();
        const long made = g_allocations - before;
        std::printf("%-34s %ld allocation(s) in %d calls\n", what, made, kCalls);
        return made;
    }

    __declspec(noinline) int __cdecl Add(int a, int b) { return a + b; }
    using AddFn = int(__cdecl*)(int, int);
    void* g_detour = nullptr;
    int __cdecl FakeInterceptAttach(const char*, uintptr_t target, void* detour, void** original)
    {
        g_detour  = detour;
        *original = reinterpret_cast<void*>(target);
        return 1;
    }

    class Ticker final : public wxl::Extension
    {
    public:
        void OnUpdate(float, uint32_t) override { ++ticks; }
        int ticks = 0;
    };

    wxl::Task EveryFrame(int& frames)
    {
        for (;;)
        {
            co_await wxl::NextFrame();
            ++frames;
        }
    }

    /// A log sink that keeps the last line in place: the fake core's own allocates.
    char g_lastLine[2048];
    long g_lines = 0;
    void __cdecl KeepLine(int, const char* tag, const char* fmt, ...)
    {
        char    line[1024];
        va_list args;
        va_start(args, fmt);
        std::vsnprintf(line, sizeof line, fmt, args);
        va_end(args);
        std::snprintf(g_lastLine, sizeof g_lastLine, "%s: %s", tag, line);
        ++g_lines;
    }

    wxl::Task Sleeper()
    {
        co_await wxl::Delay(1.0e9f);
    }
}

void* operator new(std::size_t size)
{
    ++g_allocations;
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    static WXL_Api api  = fake::MakeApi();
    api.InterceptAttach = &FakeInterceptAttach;
    CHECK(wxl::Bind(&api, "hot-path-check"));

    const ev::UpdateArgs tick{ 0.016f, 0 };

    int  plain = 0, filtered = 0;
    auto c1 = wxl::SignalOf<wxl::Update>().Connect([&](const ev::UpdateArgs&) { ++plain; });
    auto c2 = wxl::SignalOf<wxl::Update>()
                  .Filter([](const ev::UpdateArgs& a) { return a.dt > 0; })
                  .Connect([&](const ev::UpdateArgs&) { ++filtered; });
    CHECK(c1 && c2);
    CHECK(AllocationsPerRun("signal dispatch", [&] { Dispatch<ev::Event::OnUpdate>(tick); }) == 0);
    CHECK(plain == kWarmUp + kCalls && filtered == plain);

    static Ticker ticker;
    wxl::detail::Lifecycle<Ticker>::Start(ticker);
    CHECK(AllocationsPerRun("override dispatch", [&] { Dispatch<ev::Event::OnUpdate>(tick); }) == 0);
    CHECK(ticker.ticks == kWarmUp + kCalls);

    int heard = 0;
    wxl::On<wxl::Update>([&](const wxl::UpdateArgs& a) { heard += a.dt > 0; });
    CHECK(AllocationsPerRun("On dispatch", [&] { Dispatch<ev::Event::OnUpdate>(tick); }) == 0);
    CHECK(heard == kWarmUp + kCalls);

    api.Log = &KeepLine;
    const char* name = "hot";
    CHECK(AllocationsPerRun("Log::Info format", [&] { wxl::Log::Info("{} {:08X} {:.2f} {}", 42, 0xBEEFu, 0.5, name); }) == 0);
    CHECK(g_lines == kWarmUp + kCalls && std::strcmp(g_lastLine, "hot-path-check: 42 0000BEEF 0.50 hot") == 0);
    wxl::detail::g_link.level = WXL_LOG_ERROR;
    CHECK(AllocationsPerRun("Log::Info below the threshold", [&] { wxl::Log::Info("{}", 42); }) == 0);
    CHECK(g_lines == kWarmUp + kCalls);
    wxl::detail::g_link.level = WXL_LOG_INFO;
    api.Log = &fake::Log;

    int  value = 0, changes = 0;
    auto watch = wxl::detail::Watch<int>("value", [&] { return wxl::Result<int>(value); })
                     .Connect([&](const wxl::Change<int>&) { ++changes; });
    CHECK(watch.HasValue());
    CHECK(AllocationsPerRun("observable watch", [&] { ++value; Dispatch<ev::Event::OnUpdate>(tick); }) == 0);
    CHECK(changes == kWarmUp + kCalls);

    auto& add    = wxl::Intercept<AddFn, &Add>();
    auto  before = add.Before([](auto& call) { call.template Arg<0>() += 1; });
    auto  swap   = add.Replace([](auto& call) { return call.Original() * 2; });
    auto  after  = add.After([](auto& call) { call.Result() += 1; });
    CHECK(add.Attached() && g_detour);
    const auto detour = reinterpret_cast<AddFn>(g_detour);
    int        sum    = 0;
    CHECK(AllocationsPerRun("interceptor call", [&] { sum = detour(1, 2); }) == 0);
    CHECK(sum == (1 + 1 + 2) * 2 + 1);

    static wxl::Hook<AddFn> hook;
    CHECK(AllocationsPerRun("hook next link", [&] { sum = hook.Original() ? hook(1, 2) : Add(1, 2); }) == 0);

    int        frames = 0;
    wxl::Task  loop   = EveryFrame(frames);
    wxl::Task  sleeps[8];
    for (auto& s : sleeps) s = Sleeper();
    CHECK(AllocationsPerRun("task scheduler tick", [&] { Dispatch<ev::Event::OnUpdate>(tick); }) == 0);
    CHECK(frames == kWarmUp + kCalls);

    std::printf("%d failure(s)\n", g_failures);
    ExitProcess(g_failures ? 1 : 0); // the SDK's statics still reach the fake core at exit
}
