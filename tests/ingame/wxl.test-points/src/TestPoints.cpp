// Points on the model loader for wxl.selftest's phase 12. TestPointsArm() in the client's Lua
// attaches Before and After handlers to CM2Shared::FinishLoading, a function of both clients, and
// a Before handler to M2Init(uint8_t*,uint32_t,M2Data&), which the stock client inlines into
// FinishLoading and FinishLoadingLowPrioritySequence and its runtime makes a function again (a
// de-inlined point: sdk/include/wxl/deinlined.def). Each handler counts its calls and reads the
// model through the engine headers: the file's name from the CM2Shared, the version from the
// M2Data header. M2Init's calls are counted by where they come from: inside FinishLoading (between
// its Before and its After) or elsewhere, a sequence read later. TestPointsCounts() answers the
// counts, the last name and version; TestPointsDisarm() detaches them. At load the mod checks that
// the runtime lays out the two types as its headers do, and stops otherwise.

#include <atomic>
#include <cstring>
#include <mutex>
#include <string>

#include "wxl/engine/check.hpp"
#include "wxl/engine/m2.hpp"
#include "wxl/sdk.hpp"
#include "wxl/services/point.hpp"
#include "wxl/services/script.hpp"

namespace
{
    constexpr const char* kTag = "test-points";

    using FinishLoading = wxl::points::CM2Shared_FinishLoading;
    using M2Init        = wxl::points::M2Init_uint8_t_uint32_t_M2Data;

    class Functions final : public wxl::LuaScript
    {
    public:
        void OnScriptVmReady(wxl_script_context, void*) override
        {
            const wxl::ScriptService script;
            if (!script) return;
            script.RegisterFunction<&Functions::Arm>("TestPointsArm", this);
            script.RegisterFunction<&Functions::Counts>("TestPointsCounts", this);
            script.RegisterFunction<&Functions::Disarm>("TestPointsDisarm", this);
        }

        void Arm(wxl::ScriptCall& call)
        {
            finishes_ = inits_ = initsInFinish_ = 0;
            inFinish_ = false;
            finish_   = FinishLoading::Before([this](void*& self) {
                ++finishes_;
                inFinish_          = true;
                const auto* shared = static_cast<const wxl::engine::CM2Shared*>(self);
                const std::lock_guard<std::mutex> lock(mutex_);
                name_.assign(shared->m_fileName, strnlen(shared->m_fileName, sizeof shared->m_fileName));
            });
            finished_ = FinishLoading::After([this](int32_t&, void*&) { inFinish_ = false; });
            init_     = M2Init::Before([this](void*&, uint32_t& size, void*& data) {
                ++inits_;
                if (inFinish_) ++initsInFinish_;
                version_ = static_cast<const wxl::engine::M2Data*>(data)->m_version;
                size_    = size;
            });
            std::string why;
            if (!finish_ || !finished_) why = "CM2Shared::FinishLoading: " + FinishLoading::Absent();
            if (!init_) why += (why.empty() ? "" : "; ") + std::string("M2Init: ") + M2Init::Absent();
            wxl::Log(WXL_LOG_INFO, kTag, "TestPointsArm: {}", why.empty() ? std::string("both attached") : why);
            call.PushBool(why.empty());
            call.PushString(why);
        }

        void Counts(wxl::ScriptCall& call)
        {
            call.PushNumber(finishes_.load());
            call.PushNumber(inits_.load());
            const std::lock_guard<std::mutex> lock(mutex_);
            call.PushString(name_);
            call.PushNumber(version_.load());
            call.PushNumber(size_.load());
            call.PushNumber(initsInFinish_.load());
            call.PushNumber(inits_.load() - initsInFinish_.load());
        }

        void Disarm(wxl::ScriptCall& call)
        {
            finish_   = wxl::PointAttachment();
            finished_ = wxl::PointAttachment();
            init_     = wxl::PointAttachment();
            call.PushBool(true);
        }

    private:
        std::atomic<int>      finishes_{0}, inits_{0}, initsInFinish_{0};
        std::atomic<bool>     inFinish_{false}; // between FinishLoading's Before and After (main thread)
        std::atomic<uint32_t> version_{0}, size_{0};
        std::mutex            mutex_;
        std::string           name_;
        wxl::PointAttachment  finish_; // detached when the mod is destroyed, before its code is gone
        wxl::PointAttachment  finished_;
        wxl::PointAttachment  init_;
    };

    class TestPoints final : public wxl::Mod
    {
    public:
        wxl_result OnLoad() override
        {
            // The handlers read both types through the engine headers: the runtime must lay them
            // out as the headers this mod was built with do.
            if (!wxl::engine::RequireLayouts<wxl::engine::CM2Shared, wxl::engine::M2Data>()) return WXL_OK;
            wxl::ScriptMgr::Add(&functions_);
            return WXL_OK;
        }

    private:
        Functions functions_;
    };
} // namespace

WXL_MOD(TestPoints, "wxl.test-points", "Test points")
