// Model Swap: the engine's model cache, changed through a point.
//
// CM2Cache::CreateShared is the function every model of the client goes through, by file name,
// before the cache is searched. This mod attaches to it twice: a Replace that hands the function
// another file name for one model, and an After that logs each model the first time it loads.

#include <cstdio>
#include <cstring>
#include <set>
#include <string>

#include "wxl/sdk.hpp"
#include "wxl/services/point.hpp"

namespace
{
    constexpr const char* kTag = "model-swap";

    using CreateShared = wxl::points::CM2Cache_CreateShared;

    /// Whether a model path names the murloc: the client asks for it as .mdx, .mdl or .m2, in any case.
    bool IsMurloc(const char* name)
    {
        char lower[260];
        size_t i = 0;
        for (; name[i] && i + 1 < sizeof lower; ++i)
            lower[i] = static_cast<char>(name[i] >= 'A' && name[i] <= 'Z' ? name[i] + 32 : name[i]);
        lower[i] = '\0';
        return std::strstr(lower, "creature\\murloc\\murloc.") != nullptr;
    }

    class ModelSwap final : public wxl::Mod
    {
    public:
        wxl_result OnLoad() override
        {
            // Replace: runs instead of the function. `original` is the function itself (and any
            // other mod's Replace below this one); called with another name, it loads that model.
            swap_ = CreateShared::Replace([](auto original, void* cache, void* name, uint32_t flags) {
                if (name && IsMurloc(static_cast<const char*>(name)))
                    return original(cache, const_cast<char*>("Creature\\Gnoll\\Gnoll.m2"), flags);
                return original(cache, name, flags);
            });

            // After: runs once the function has returned; `model` is what it answered.
            log_ = CreateShared::After([this](void*& model, void*, void* name, uint32_t) {
                if (!name || !seen_.insert(static_cast<const char*>(name)).second) return;
                char line[320];
                std::snprintf(line, sizeof line, "model %s%s", static_cast<const char*>(name),
                              model ? "" : " (not found)");
                wxl::Log(WXL_LOG_INFO, kTag, line);
            });

            if (!swap_ || !log_)
            {
                wxl::Log(WXL_LOG_WARN, kTag, "the model cache's point is not here; the mod does nothing");
                return WXL_OK;
            }

            // A point the stock client inlined: its runtime makes it a function again, and says why
            // when it cannot (a runtime older than abi 0.1.21 does not list it).
            const std::string absent = wxl::points::M2Init_uint8_t_uint32_t_M2Data::Absent();
            wxl::Log(WXL_LOG_INFO, kTag, absent.empty() ? std::string("M2Init, a model file's check, is a point here")
                                                        : "M2Init is not a point here: " + absent);
            return WXL_OK;
        }

    private:
        std::set<std::string> seen_;
        wxl::PointAttachment  swap_; // detached when the mod is destroyed, before its code is gone
        wxl::PointAttachment  log_;
    };
} // namespace

WXL_MOD(ModelSwap, "wxl.model-swap", "Model Swap")
