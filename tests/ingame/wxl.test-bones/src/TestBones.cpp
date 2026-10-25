// Bone watchers for wxl.selftest's phase 16. TestBonesArm() in the client's Lua starts a watcher
// whose filter takes the character model files (a path under "Character\"), on the client's
// animate threads; the watcher counts the frames it is handed, keeps the last one's bone count and
// clock, and asks BoneInfo for the root bone of the file. TestBonesCounts() answers the files
// taken, the frames, the last bone count, the root bone's parent and whether the matrices were
// there; TestBonesDisarm() stops the watcher.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "wxl/sdk.hpp"
#include "wxl/services/m2.hpp"
#include "wxl/services/script.hpp"

namespace
{
    constexpr const char* kTag = "test-bones";

    struct Counts
    {
        std::atomic<int>      files{0};
        std::atomic<int>      frames{0};
        std::atomic<uint32_t> bones{0};
        std::atomic<int>      rootParent{0};
        std::atomic<bool>     matrices{false};
    };

    /// The files of character models: what the player's model is made of.
    wxl_bool WXL_CALL TakeCharacters(void*, wxl_str path, uint32_t bone_count, void* user)
    {
        const std::string_view name   = wxl::View(path);
        constexpr std::string_view kDir = "character\\";
        bool take = bone_count > 0 && name.size() > kDir.size();
        for (size_t i = 0; take && i < kDir.size(); ++i)
            take = (name[i] >= 'A' && name[i] <= 'Z' ? char(name[i] - 'A' + 'a') : name[i]) == kDir[i];
        if (take) ++static_cast<Counts*>(user)->files;
        return take ? WXL_TRUE : WXL_FALSE;
    }

    void WXL_CALL CountFrame(const wxl_m2_bones* bones, void* user)
    {
        Counts* counts = static_cast<Counts*>(user);
        ++counts->frames;
        counts->bones    = bones->bone_count;
        counts->matrices = bones->matrices != nullptr;
        const auto root  = wxl::M2Service::BoneInfo(bones->file, 0);
        counts->rootParent = root ? root->parent : 1;
    }

    class Functions final : public wxl::LuaScript
    {
    public:
        void OnScriptVmReady(wxl_script_context, void*) override
        {
            const wxl::ScriptService script;
            if (!script) return;
            script.RegisterFunction<&Functions::Arm>("TestBonesArm", this);
            script.RegisterFunction<&Functions::Report>("TestBonesCounts", this);
            script.RegisterFunction<&Functions::Disarm>("TestBonesDisarm", this);
        }

        void Arm(wxl::ScriptCall& call)
        {
            watch_ = wxl::M2BoneWatch();
            counts_.files = counts_.frames = 0;
            counts_.bones      = 0;
            counts_.rootParent = 0;
            counts_.matrices   = false;
            watch_             = wxl::M2BoneWatch(&TakeCharacters, &CountFrame, &counts_);
            wxl::Log(WXL_LOG_INFO, kTag, "TestBonesArm: {}", watch_ ? "watching" : "refused");
            call.PushBool(static_cast<bool>(watch_));
        }

        void Report(wxl::ScriptCall& call)
        {
            call.PushString(std::to_string(counts_.files.load()) + "|" + std::to_string(counts_.frames.load()) + "|" +
                            std::to_string(counts_.bones.load()) + "|" + std::to_string(counts_.rootParent.load()) +
                            "|" + (counts_.matrices.load() ? "1" : "0"));
        }

        void Disarm(wxl::ScriptCall& call)
        {
            watch_ = wxl::M2BoneWatch();
            call.PushBool(true);
        }

    private:
        Counts           counts_;
        wxl::M2BoneWatch watch_; // stopped when the mod is destroyed, before its code is gone
    };

    class TestBones final : public wxl::Mod
    {
    public:
        wxl_result OnLoad() override
        {
            wxl::ScriptMgr::Add(&functions_);
            return WXL_OK;
        }

    private:
        Functions functions_;
    };
} // namespace

WXL_MOD(TestBones, "wxl.test-bones", "Test bones")
