// SPDX-License-Identifier: GPL-3.0-or-later
// wxl-task-check.exe: NextFrame, Delay, a signal's Next with and without timeout, cancelling a task.

#include "FakeCore.hpp"

#include "wxl/Task.hpp"

#include <cstdio>
#include <string>

namespace
{
    int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++g_failures; } } while (0)

    namespace ev = wxl::events;

    void Frame(float dt = 0.25f) { fake::Raise<ev::Event::OnUpdate>({ dt, 0 }); }

    wxl::Task Steps(std::string& log)
    {
        log += "a";
        co_await wxl::NextFrame();
        log += "b";
        co_await wxl::Delay(1.0f); // four frames of 0.25
        log += "c";
    }

    wxl::Task WaitClick(std::string& log, float timeout)
    {
        auto click = co_await wxl::SignalOf<wxl::WorldClick>().Filter([](const ev::WorldClickArgs& a) {
            return a.hitType == 3;
        }).Next(timeout);
        if (click) log += "click " + std::to_string(int(click->x));
        else log += click.Code() == wxl::Error::Timeout ? "timeout" : "expired";
    }

    wxl::Task WaitSubject(std::string& log, WXL_Handle subject)
    {
        auto e = co_await wxl::SignalOf<wxl::TargetChanged>(subject).Next();
        log += e ? "event" : (e.Code() == wxl::Error::Expired ? "expired" : "other");
    }

    wxl::Task Forever(int& frames)
    {
        for (;;)
        {
            co_await wxl::NextFrame();
            ++frames;
        }
    }
}

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    // Static: statics of the SDK (the scheduler's connection) still reach it at exit.
    static const WXL_Api api = fake::MakeApi();
    CHECK(wxl::Bind(&api, "task-check"));

    std::string log;
    wxl::Task steps = Steps(log);
    CHECK(log == "a" && !steps.Done());
    Frame();
    CHECK(log == "ab");
    Frame(); Frame(); Frame();
    CHECK(log == "ab");
    Frame();
    CHECK(log == "abc" && steps.Done());

    std::string clicks;
    wxl::Task wait = WaitClick(clicks, 0);
    fake::Raise<ev::Event::OnWorldClick>({ 0, 2, 1, 2, 3, nullptr, nullptr }); // filtered out
    CHECK(clicks.empty());
    fake::Raise<ev::Event::OnWorldClick>({ 0, 3, 42, 2, 3, nullptr, nullptr });
    CHECK(clicks == "click 42" && wait.Done());

    std::string late;
    wxl::Task timed = WaitClick(late, 0.5f);
    Frame(); Frame();
    CHECK(late == "timeout");

    std::string gone;
    const WXL_Handle wolf{ 5, 1 };
    wxl::Task subject = WaitSubject(gone, wolf);
    fake::Expire(wolf);
    Frame();
    CHECK(gone == "expired");

    int frames = 0;
    {
        wxl::Task forever = Forever(frames);
        Frame(); Frame();
        CHECK(frames == 2);
    }
    Frame();
    CHECK(frames == 2 && wxl::detail::Scheduler::Get().Waiting() == 0);

    std::printf("%d failure(s)\n", g_failures);
    return g_failures ? 1 : 0;
}
