// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The mod host checks. Run with the fixture binary's path and a scratch directory:
//
//     wxl-host-tests <fixture.dll> <scratch>
//
// Without arguments the sections that need a file on disk are skipped and say so.

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "wxl/host/admission.hpp"
#include "wxl/host/discovery.hpp"
#include "wxl/host/event_bus.hpp"
#include "wxl/host/guard.hpp"
#include "wxl/host/json.hpp"
#include "wxl/host/loader.hpp"
#include "wxl/host/log.hpp"
#include "wxl/host/manifest.hpp"
#include "wxl/host/mod_host.hpp"
#include "wxl/host/registry.hpp"
#include "wxl/reflect.hpp"

namespace fs = std::filesystem;
using namespace wxl::host;

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

    bool Contains(const std::string& text, const char* needle)
    {
        return text.find(needle) != std::string::npos;
    }

    // --- helpers ---------------------------------------------------------------------------

    /// Keeps every line, so a check can look for one.
    class CaptureSink final : public LogSink
    {
    public:
        void Write(wxl_log_level level, std::string_view tag, std::string_view message) override
        {
            lines.push_back(FormatLine(level, tag, message));
        }

        bool Has(const char* needle) const
        {
            for (const std::string& line : lines)
                if (Contains(line, needle)) return true;
            return false;
        }

        std::vector<std::string> lines;
    };

    void WriteFile(const fs::path& path, const std::string& text)
    {
        fs::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary);
        stream << text;
    }

#define WXL_TEST_STR_(x) #x
#define WXL_TEST_STR(x)  WXL_TEST_STR_(x)

    /// The ABI the host under test speaks, as a manifest spells it.
    constexpr const char* kAbi = WXL_TEST_STR(WXL_ABI_VERSION_MAJOR) "." WXL_TEST_STR(WXL_ABI_VERSION_MINOR) "."
        WXL_TEST_STR(WXL_ABI_VERSION_FEATURE);

    /// A manifest with the required fields filled and @p extra spliced in before the brace.
    std::string ManifestText(const char* id, const std::string& extra = std::string(),
                             const char* abi = kAbi, const char* binary = "mod.dll")
    {
        std::string text = "{\n  \"id\": \"";
        text += id;
        text += "\",\n  \"name\": \"Test\",\n  \"version\": \"1.0.0\",\n  \"abi\": \"";
        text += abi;
        text += "\",\n  \"binaries\": { \"";
        text += CurrentTarget();
        text += "\": \"";
        text += binary;
        text += "\" }";
        if (!extra.empty()) text += ",\n  " + extra;
        text += "\n}\n";
        return text;
    }

    bool Rejects(const std::string& text, const char* needle, std::string* message = nullptr)
    {
        Manifest    manifest;
        std::string error;
        const bool  ok = ParseManifest(text, manifest, error);
        if (message) *message = error;
        return !ok && Contains(error, needle);
    }

    bool JsonRejects(const char* text, const char* needle, int line = 0)
    {
        json::Value value;
        json::Error error;
        if (json::Parse(text, value, error)) return false;
        if (!Contains(error.message, needle)) return false;
        return line == 0 || error.line == line;
    }

    ModCandidate Candidate(const char* directory, const char* id,
                           std::vector<std::string> loadAfter = {})
    {
        ModCandidate candidate;
        candidate.directory_name      = directory;
        candidate.directory           = fs::path("mods") / directory;
        candidate.manifest.id         = id;
        candidate.manifest.name       = id;
        candidate.manifest.abi        = RuntimeAbi();
        candidate.manifest.load_after = std::move(loadAfter);
        candidate.manifest.binaries.push_back(Binary{CurrentTarget(), "mod.dll"});
        return candidate;
    }

    std::string Ids(const std::vector<ModCandidate>& mods)
    {
        std::string text;
        for (const ModCandidate& mod : mods)
        {
            if (!text.empty()) text += ' ';
            text += mod.manifest.id;
        }
        return text;
    }

    // --- event bus handlers ----------------------------------------------------------------

    std::vector<int> g_order;

    void WXL_CALL Record(void* user, const void*) { g_order.push_back(*static_cast<int*>(user)); }

    struct DropOther
    {
        EventBus*        bus;
        wxl_subscription victim;
        int              tag;
    };

    void WXL_CALL RecordAndDrop(void* user, const void*)
    {
        DropOther* self = static_cast<DropOther*>(user);
        g_order.push_back(self->tag);
        self->bus->Unsubscribe(self->victim);
    }

    struct AddOther
    {
        EventBus*        bus;
        wxl_event        event;
        int*             newTag;
        int              tag;
        wxl_subscription added;
    };

    void WXL_CALL RecordAndAdd(void* user, const void*)
    {
        AddOther* self = static_cast<AddOther*>(user);
        g_order.push_back(self->tag);
        if (self->added == WXL_SUBSCRIPTION_NONE)
            self->added = self->bus->Subscribe(self->event, &Record, self->newTag, -100);
    }

    void WXL_CALL Throws(void* user, const void*)
    {
        g_order.push_back(*static_cast<int*>(user));
        throw std::runtime_error("handler failure");
    }

    void WXL_CALL CountUp(void* user, const void*)
    {
        static_cast<std::atomic<int>*>(user)->fetch_add(1);
    }

    /// Holds its dispatch open until told to let go.
    struct Gate
    {
        std::atomic<bool> entered{false};
        std::atomic<bool> release{false};
    };

    void WXL_CALL HoldOpen(void* user, const void*)
    {
        Gate* gate = static_cast<Gate*>(user);
        gate->entered.store(true);
        while (!gate->release.load()) std::this_thread::yield();
    }

    struct SelfWait
    {
        EventBus* bus;
        bool      answered = false;
    };

    void WXL_CALL WaitInside(void* user, const void*)
    {
        SelfWait* self = static_cast<SelfWait*>(user);
        self->answered = self->bus->WaitForOtherDispatches(std::chrono::milliseconds(0));
    }

    size_t CountLines(const std::vector<std::string>& lines, const char* needle)
    {
        size_t count = 0;
        for (const std::string& line : lines)
            if (Contains(line, needle)) ++count;
        return count;
    }

    // --- in-process "binaries" for the fake loader ------------------------------------------

    std::vector<std::string> g_trace;

    wxl_mod_info g_goodInfo{sizeof(wxl_mod_info), WXL_ABI_VERSION_MAJOR, WXL_ABI_VERSION_MINOR,
                            "wxl.good", "Good", 1, WXL_ABI_VERSION_FEATURE};
    wxl_mod_info g_secondInfo{sizeof(wxl_mod_info), WXL_ABI_VERSION_MAJOR, WXL_ABI_VERSION_MINOR,
                              "wxl.second", "Second", 2, WXL_ABI_VERSION_FEATURE};
    wxl_mod_info g_nostageInfo{sizeof(wxl_mod_info), WXL_ABI_VERSION_MAJOR, WXL_ABI_VERSION_MINOR,
                               "wxl.nostage", "No stage", 3, WXL_ABI_VERSION_FEATURE};
    wxl_mod_info g_badloadInfo{sizeof(wxl_mod_info), WXL_ABI_VERSION_MAJOR, WXL_ABI_VERSION_MINOR,
                               "wxl.badload", "Bad load", 4, WXL_ABI_VERSION_FEATURE};
    wxl_mod_info g_liarInfo{sizeof(wxl_mod_info), WXL_ABI_VERSION_MAJOR, WXL_ABI_VERSION_MINOR,
                            "wxl.someone-else", "Liar", 5, WXL_ABI_VERSION_FEATURE};

    const wxl_mod_info* WXL_CALL GoodQuery(void) { return &g_goodInfo; }
    wxl_result WXL_CALL GoodLoad(const wxl_host* host)
    {
        g_trace.push_back("good:load");
        host->log(WXL_LOG_INFO, "good", wxl_str{"hello", 5});
        return WXL_OK;
    }
    void WXL_CALL GoodStage(wxl_stage stage)
    {
        g_trace.push_back("good:stage" + std::to_string(static_cast<int>(stage)));
    }
    void WXL_CALL GoodUnload(void) { g_trace.push_back("good:unload"); }

    const wxl_mod_info* WXL_CALL SecondQuery(void) { return &g_secondInfo; }
    wxl_result WXL_CALL SecondLoad(const wxl_host*)
    {
        g_trace.push_back("second:load");
        return WXL_OK;
    }
    void WXL_CALL SecondStage(wxl_stage stage)
    {
        g_trace.push_back("second:stage" + std::to_string(static_cast<int>(stage)));
    }
    void WXL_CALL SecondUnload(void) { g_trace.push_back("second:unload"); }

    const wxl_mod_info* WXL_CALL NoStageQuery(void) { return &g_nostageInfo; }
    wxl_result WXL_CALL NoStageLoad(const wxl_host*)
    {
        g_trace.push_back("nostage:load");
        return WXL_OK;
    }

    const wxl_mod_info* WXL_CALL DeclinesQuery(void) { return nullptr; }
    wxl_result WXL_CALL DeclinesLoad(const wxl_host*)
    {
        g_trace.push_back("declines:load");
        return WXL_OK;
    }

    const wxl_mod_info* WXL_CALL BadLoadQuery(void) { return &g_badloadInfo; }
    wxl_result WXL_CALL BadLoadLoad(const wxl_host*)
    {
        g_trace.push_back("badload:load");
        return WXL_E_FAILED;
    }

    const wxl_mod_info* WXL_CALL LiarQuery(void) { return &g_liarInfo; }
    wxl_result WXL_CALL LiarLoad(const wxl_host*)
    {
        g_trace.push_back("liar:load");
        return WXL_OK;
    }


    // --- a binary built against older headers -------------------------------------------------

    /// What the headers before the newest event's version carried, as wxl_mod_info reports it.
    constexpr uint32_t kOlderFeature = wxl::reflect::SinceOf(WXL_EV_ChatMessage).feature - 1;

    wxl_mod_info g_olderInfo{sizeof(wxl_mod_info), WXL_ABI_VERSION_MAJOR, WXL_ABI_VERSION_MINOR,
                             "wxl.older", "Older", 1, kOlderFeature};
    wxl_subscription g_olderNew = WXL_SUBSCRIPTION_NONE;
    wxl_subscription g_olderOld = WXL_SUBSCRIPTION_NONE;

    const wxl_mod_info* WXL_CALL OlderQuery(void) { return &g_olderInfo; }
    wxl_result WXL_CALL OlderLoad(const wxl_host* host)
    {
        g_olderNew = host->subscribe(WXL_EV_ChatMessage, &Record, nullptr, 0);
        host->subscribe(WXL_EV_ChatMessage, &Record, nullptr, 0);
        g_olderOld = host->subscribe(WXL_EV_Update, &Record, nullptr, 0);
        return WXL_OK;
    }

    // --- mods that fault, for the crash guard ----------------------------------------------

    wxl_mod_info g_steadyInfo{sizeof(wxl_mod_info), WXL_ABI_VERSION_MAJOR, WXL_ABI_VERSION_MINOR,
                              "wxl.steady", "Steady", 10, WXL_ABI_VERSION_FEATURE};
    wxl_mod_info g_faultyInfo{sizeof(wxl_mod_info), WXL_ABI_VERSION_MAJOR, WXL_ABI_VERSION_MINOR,
                              "wxl.faulty", "Faulty", 11, WXL_ABI_VERSION_FEATURE};
    wxl_mod_info g_stagecrashInfo{sizeof(wxl_mod_info), WXL_ABI_VERSION_MAJOR, WXL_ABI_VERSION_MINOR,
                                  "wxl.stagecrash", "Stage crash", 12, WXL_ABI_VERSION_FEATURE};
    wxl_mod_info g_loadcrashInfo{sizeof(wxl_mod_info), WXL_ABI_VERSION_MAJOR, WXL_ABI_VERSION_MINOR,
                                 "wxl.loadcrash", "Load crash", 13, WXL_ABI_VERSION_FEATURE};

    /// Writes through a null pointer. The pointer is volatile so the store is emitted as written.
    void Crash()
    {
        volatile int* nowhere = nullptr;
        *nowhere              = 1;
    }

    int g_steadyTag      = 100;
    int g_faultyEarlyTag = 101;
    int g_faultyLateTag  = 102;

    void WXL_CALL FaultyHandler(void* user, const void*)
    {
        g_order.push_back(*static_cast<int*>(user));
        Crash();
    }

    const wxl_mod_info* WXL_CALL SteadyQuery(void) { return &g_steadyInfo; }
    wxl_result WXL_CALL SteadyLoad(const wxl_host* host)
    {
        host->subscribe(WXL_EV_Update, &Record, &g_steadyTag, 0);
        return WXL_OK;
    }
    void WXL_CALL SteadyStage(wxl_stage stage)
    {
        g_trace.push_back("steady:stage" + std::to_string(static_cast<int>(stage)));
    }
    void WXL_CALL SteadyUnload(void) { g_trace.push_back("steady:unload"); }

    const wxl_mod_info* WXL_CALL FaultyQuery(void) { return &g_faultyInfo; }
    wxl_result WXL_CALL FaultyLoad(const wxl_host* host)
    {
        host->subscribe(WXL_EV_Update, &FaultyHandler, &g_faultyEarlyTag, -1);
        host->subscribe(WXL_EV_Update, &Record, &g_faultyLateTag, 1);
        host->subscribe(WXL_EV_FrameBegin, &Record, &g_faultyLateTag, 0);
        return WXL_OK;
    }
    void WXL_CALL FaultyStage(wxl_stage stage)
    {
        g_trace.push_back("faulty:stage" + std::to_string(static_cast<int>(stage)));
    }
    void WXL_CALL FaultyUnload(void) { g_trace.push_back("faulty:unload"); }

    const wxl_mod_info* WXL_CALL StageCrashQuery(void) { return &g_stagecrashInfo; }
    wxl_result WXL_CALL StageCrashLoad(const wxl_host*) { return WXL_OK; }
    void WXL_CALL StageCrashStage(wxl_stage stage)
    {
        g_trace.push_back("stagecrash:stage" + std::to_string(static_cast<int>(stage)));
        if (stage == WXL_STAGE_READY) Crash();
    }
    void WXL_CALL StageCrashUnload(void) { g_trace.push_back("stagecrash:unload"); }

    const wxl_mod_info* WXL_CALL LoadCrashQuery(void) { return &g_loadcrashInfo; }
    wxl_result WXL_CALL LoadCrashLoad(const wxl_host* host)
    {
        host->subscribe(WXL_EV_Update, &Record, &g_steadyTag, 0);
        Crash();
        return WXL_OK;
    }

    const wxl_mod_info* WXL_CALL QueryCrashQuery(void)
    {
        Crash();
        return nullptr;
    }

    // --- mods that disable themselves, as WXL_MOD does after a C++ exception ----------------

    const wxl_host* g_quitHost = nullptr;
    int             g_quitTag  = 200;

    wxl_mod_info g_quitterInfo{sizeof(wxl_mod_info), WXL_ABI_VERSION_MAJOR, WXL_ABI_VERSION_MINOR,
                               "wxl.quitter", "Quitter", 0, WXL_ABI_VERSION_FEATURE};
    wxl_mod_info g_loadquitInfo{sizeof(wxl_mod_info), WXL_ABI_VERSION_MAJOR, WXL_ABI_VERSION_MINOR,
                                "wxl.loadquit", "Load quit", 0, WXL_ABI_VERSION_FEATURE};
    wxl_mod_info g_unloadquitInfo{sizeof(wxl_mod_info), WXL_ABI_VERSION_MAJOR, WXL_ABI_VERSION_MINOR,
                                  "wxl.unloadquit", "Unload quit", 0, WXL_ABI_VERSION_FEATURE};

    wxl_str Text(const char* text) { return wxl_str{text, static_cast<uint32_t>(std::strlen(text))}; }

    const wxl_mod_info* WXL_CALL QuitterQuery(void) { return &g_quitterInfo; }
    wxl_result WXL_CALL QuitterLoad(const wxl_host* host)
    {
        g_quitHost = host;
        host->subscribe(WXL_EV_Update, &Record, &g_quitTag, 0);
        return WXL_OK;
    }
    void WXL_CALL QuitterStage(wxl_stage stage)
    {
        g_trace.push_back("quitter:stage" + std::to_string(static_cast<int>(stage)));
        if (stage == WXL_STAGE_READY) g_quitHost->disable(Text("OnReady threw an exception: boom"));
    }
    void WXL_CALL QuitterUnload(void) { g_trace.push_back("quitter:unload"); }

    const wxl_mod_info* WXL_CALL LoadQuitQuery(void) { return &g_loadquitInfo; }
    wxl_result WXL_CALL LoadQuitLoad(const wxl_host* host)
    {
        host->subscribe(WXL_EV_Update, &Record, &g_quitTag, 0);
        host->disable(Text("gave up"));
        return WXL_OK;
    }

    const wxl_mod_info* WXL_CALL UnloadQuitQuery(void) { return &g_unloadquitInfo; }
    wxl_result WXL_CALL UnloadQuitLoad(const wxl_host*) { return WXL_OK; }
    void WXL_CALL UnloadQuitUnload(void)
    {
        g_trace.push_back("unloadquit:unload");
        g_quitHost->disable(Text("OnUnload threw an exception: late"));
    }

    void GuardedCrash(void*) { Crash(); }
    void GuardedFine(void* context) { *static_cast<int*>(context) = 1; }

    /// Hands out in-process entry points by file name; the file itself need not exist.
    class FakeLoader final : public LibraryLoader
    {
    public:
        struct Image
        {
            std::string name;
            void*       query;
            void*       load;
            void*       stage;
            void*       unload;
        };

        void* Open(const fs::path& path, std::string& error) override
        {
            const std::string name = path.filename().string();
            for (Image& image : images)
                if (image.name == name)
                {
                    ++opened;
                    return &image;
                }
            error = "no fake binary named " + name;
            return nullptr;
        }

        void* Symbol(void* library, const char* name) override
        {
            const Image* image = static_cast<const Image*>(library);
            if (std::strcmp(name, kEntryQuery) == 0) return image->query;
            if (std::strcmp(name, kEntryLoad) == 0) return image->load;
            if (std::strcmp(name, kEntryStage) == 0) return image->stage;
            if (std::strcmp(name, kEntryUnload) == 0) return image->unload;
            return nullptr;
        }

        void Close(void*) override { ++closed; }

        std::vector<Image> images;
        int                opened = 0;
        int                closed = 0;
    };

    template <class T>
    void* Fn(T* function)
    {
        return reinterpret_cast<void*>(function);
    }

    const RefusedMod* FindRefused(const ModHost& host, const char* id)
    {
        for (const RefusedMod& mod : host.Refused())
            if (mod.id == id) return &mod;
        return nullptr;
    }

    /// The service the fixture publishes; the layout the fixture declares.
    struct FixtureApi
    {
        uint32_t struct_size;
        int32_t(WXL_CALL* answer)(void);
    };
} // namespace

int main(int argc, char** argv)
{
    // Unbuffered, so the last line printed before a crash is never held back in a buffer.
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    const fs::path fixture = argc > 1 ? fs::path(argv[1]) : fs::path();
    const fs::path scratch = argc > 2 ? fs::path(argv[2]) : fs::path();

    Section("json");
    {
        json::Value value;
        json::Error error;
        const char* text = "{\n  \"a\": [1, -2, 3.5, 1e3, true, false, null],\n"
                           "  \"s\": \"q\\\"\\\\\\/\\b\\f\\n\\r\\t\\u00e9\\ud83d\\ude00\",\n"
                           "  \"o\": { \"nested\": {} }\n}";
        Check(json::Parse(text, value, error), "a document with every value type parses");
        Check(value.Is(json::Type::Object) && value.object.size() == 3, "members are kept");
        const json::Value* a = value.Find("a");
        Check(a && a->Is(json::Type::Array) && a->array.size() == 7, "arrays are kept");
        Check(a && a->array[0].integral && a->array[0].integer == 1, "an integer is integral");
        Check(a && a->array[1].integral && a->array[1].integer == -2, "and may be negative");
        Check(a && !a->array[2].integral && a->array[2].number == 3.5, "a fraction is not");
        Check(a && !a->array[3].integral && a->array[3].number == 1000.0, "nor is an exponent");
        Check(a && a->array[4].Is(json::Type::Bool) && a->array[4].boolean, "true");
        Check(a && a->array[5].Is(json::Type::Bool) && !a->array[5].boolean, "false");
        Check(a && a->array[6].Is(json::Type::Null), "null");
        const json::Value* s = value.Find("s");
        Check(s && s->string == "q\"\\/\b\f\n\r\t\xC3\xA9\xF0\x9F\x98\x80",
              "every escape decodes, surrogate pairs included");
        Check(s && s->line == 3, "a value knows the line it started on");
        Check(value.Find("o") && value.Find("o")->Find("nested") &&
                  value.Find("o")->Find("nested")->Is(json::Type::Object),
              "nested objects parse");
        Check(value.Find("missing") == nullptr, "an absent key finds nothing");

        Check(json::Parse("\xEF\xBB\xBF{}", value, error), "a byte order mark is skipped");
        Check(json::Parse("  42  ", value, error) && value.integer == 42, "a bare value is fine");

        Check(JsonRejects("", "empty"), "an empty document is refused");
        Check(JsonRejects("{\"a\": 1,}", "quoted object key"), "a trailing comma is refused");
        Check(JsonRejects("{\"a\" 1}", "':'"), "a missing colon is refused");
        Check(JsonRejects("{\"a\": 1 \"b\": 2}", "',' or '}'"), "a missing comma is refused");
        Check(JsonRejects("[1 2]", "',' or ']'"), "a missing comma in an array is refused");
        Check(JsonRejects("\"abc", "unterminated"), "an unterminated string is refused");
        Check(JsonRejects("\"\\x\"", "unknown escape"), "an unknown escape is refused");
        Check(JsonRejects("\"a\tb\"", "control character"), "a raw control character is refused");
        Check(JsonRejects("\"\\ud83d\"", "surrogate"), "a lone surrogate is refused");
        Check(JsonRejects("\"\\u12\"", "hex"), "a short \\u escape is refused");
        Check(JsonRejects("01", "zero"), "a leading zero is refused");
        Check(JsonRejects("1.", "digit after"), "a bare decimal point is refused");
        Check(JsonRejects("1e", "exponent"), "an empty exponent is refused");
        Check(JsonRejects("-", "digit"), "a bare minus is refused");
        Check(JsonRejects("tru", "'true'"), "a cut literal is refused");
        Check(JsonRejects("{} x", "after the document"), "trailing text is refused");
        Check(JsonRejects("{\"a\": 1, \"a\": 2}", "duplicate key"), "a duplicate key is refused");
        Check(JsonRejects("{\n\n  \"a\": @\n}", "unexpected character", 3),
              "an error names the line it is on");
        Check(JsonRejects("99999999999999999999", "out of range"), "an overflowing integer is refused");
        std::string deep(100, '[');
        Check(JsonRejects(deep.c_str(), "nesting"), "runaway nesting is refused");
    }

    Section("manifest");
    {
        Manifest    manifest;
        std::string error;
        const std::string full = ManifestText(
            "wxl.example",
            "\"requires\": [ { \"service\": \"wxl.object\", \"version\": 1 } ],\n"
            "  \"optional\": [ { \"service\": \"wxl.script\", \"version\": 2 } ],\n"
            "  \"loadAfter\": [ \"wxl.base\", \"wxl.other\" ],\n"
            "  \"description\": \"An example.\",\n"
            "  \"authors\": [ \"a\", \"b\" ],\n"
            "  \"homepage\": \"https://example.test\",\n"
            "  \"$schema\": \"../schema/mod.json\"",
            kAbi, "bin/mod.dll");
        Check(ParseManifest(full, manifest, error), "a full manifest parses");
        if (!error.empty()) std::printf("        %s\n", error.c_str());
        Check(manifest.id == "wxl.example" && manifest.name == "Test", "id and name");
        Check(manifest.version.major == 1 && manifest.version.minor == 0 &&
                  manifest.version.patch == 0 && manifest.version.text == "1.0.0",
              "the version is parsed and kept as written");
        Check(manifest.abi.major == WXL_ABI_VERSION_MAJOR && manifest.abi.minor == WXL_ABI_VERSION_MINOR &&
                  manifest.abi.feature == WXL_ABI_VERSION_FEATURE,
              "the abi is parsed");
        Check(manifest.binaries.size() == 1 && manifest.BinaryFor(CurrentTarget()) &&
                  *manifest.BinaryFor(CurrentTarget()) == "bin/mod.dll",
              "the binary for this target resolves");
        Check(manifest.BinaryFor("linux-x64") == nullptr || std::string(CurrentTarget()) == "linux-x64",
              "an absent target resolves to nothing");
        Check(manifest.required.size() == 1 && manifest.required[0].service == "wxl.object" &&
                  manifest.required[0].version == 1,
              "requires is read");
        Check(manifest.optional.size() == 1 && manifest.optional[0].version == 2, "optional is read");
        Check(manifest.load_after.size() == 2 && manifest.load_after[1] == "wxl.other",
              "loadAfter is read");
        Check(manifest.lua.empty() && manifest.description == "An example." &&
                  manifest.authors.size() == 2 && manifest.homepage == "https://example.test",
              "the descriptive fields are read");

        Manifest script;
        Check(ParseManifest("{ \"id\": \"wxl.s\", \"name\": \"S\", \"version\": \"1.0.0\", \"abi\": "
                            "\"0.1.0\", \"lua\": \"init.lua\" }",
                            script, error) &&
                  script.binaries.empty() && script.lua == "init.lua",
              "a manifest with lua and no binaries is a script mod and parses");
        Check(Rejects("{ \"id\": \"wxl.s\", \"name\": \"S\", \"version\": \"1.0.0\", \"abi\": \"0.1.0\" }",
                      "'binaries' is missing"),
              "without either, binaries is still required");
        Check(Rejects(ManifestText("wxl.x", "\"lua\": \"\""), "at least 1"), "an empty lua entry is refused");
        std::string both;
        Check(Rejects(ManifestText("wxl.x", "\"lua\": \"init.lua\""), "'binaries' and 'lua' cannot both be given",
                      &both) &&
                  Contains(both, "line 6") && Contains(both, "would run twice"),
              "a manifest with both binaries and lua is refused, at the binaries' line, saying why");

        Manifest minimal;
        Check(ParseManifest(ManifestText("wxl.min"), minimal, error), "a minimal manifest parses");
        Check(minimal.required.empty() && minimal.optional.empty() && minimal.load_after.empty() &&
                  minimal.lua.empty() && minimal.authors.empty(),
              "absent optional fields take the schema's defaults");

        {
            Semver semver;
            Check(ParseSemver("2.1.3-rc.1", semver) && semver.major == 2 && semver.patch == 3 &&
                      semver.label == "-rc.1",
                  "a prerelease label is kept");
            Check(ParseSemver("1.0.0+build", semver) && semver.label == "+build",
                  "build metadata is kept");
            Check(!ParseSemver("1.0", semver), "two numbers are not a semver");
            Check(!ParseSemver("1.0.0-", semver), "an empty label is not");
            Check(!ParseSemver("1.0.0-a b", semver), "a space in the label is not");
            Check(!ParseSemver("v1.0.0", semver), "a prefix is not");
            AbiVersion abi;
            Check(ParseAbi("12.34.56", abi) && abi.major == 12 && abi.minor == 34 && abi.feature == 56,
                  "an abi parses");
            Check(AbiText(abi) == "12.34.56", "and prints as written");
            Check(!ParseAbi("1", abi) && !ParseAbi("1.2", abi) && !ParseAbi("1.2.3.4", abi) &&
                      !ParseAbi("1.2.x", abi) && !ParseAbi("1..3", abi),
                  "anything but major.minor.feature is refused");
        }

        Check(IsValidModId("wxl.example") && IsValidModId("a-b.c-1") && IsValidModId("abc"),
              "well-formed ids pass");
        Check(!IsValidModId("ab") && !IsValidModId("Wxl.example") && !IsValidModId("wxl..x") &&
                  !IsValidModId(".wxl") && !IsValidModId("wxl.") && !IsValidModId("wxl_x") &&
                  !IsValidModId(std::string(65, 'a')),
              "ill-formed ids fail");

        std::string message;
        Check(Rejects("{ \"name\": \"x\", \"version\": \"1.0.0\", \"abi\": \"0.1.0\", "
                      "\"binaries\": { \"win-x64\": \"a\" } }",
                      "'id' is missing"),
              "a missing required field is named");
        Check(Rejects(ManifestText("Bad.Id"), "'id' must be", &message), "a bad id is refused");
        Check(Contains(message, "line 2"), "and the message names the line");
        Check(Rejects(ManifestText("ab"), "at least 3"), "a short id is refused");
        std::string badVersion = ManifestText("wxl.x");
        badVersion.replace(badVersion.find("1.0.0"), 5, "1.0");
        Check(Rejects(badVersion, "semver"), "a bad version is refused");
        Check(Rejects(ManifestText("wxl.x", "", "0"), "major.minor.feature"), "a bad abi is refused");
        Check(Rejects(ManifestText("wxl.x", "", "0.5"), "two-part numbering", &message) &&
                  Contains(message, ("\"" + std::string(kAbi) + "\"").c_str()),
              "a two-part abi is refused as the old numbering, naming the one to write");
        Check(Rejects(ManifestText("wxl.x", "", "0.1.0", ""), "at least 1"),
              "an empty binary path is refused");
        Check(Rejects("{ \"id\": \"wxl.x\", \"name\": \"x\", \"version\": \"1.0.0\", \"abi\": "
                      "\"0.1.0\", \"binaries\": {} }",
                      "at least one target"),
              "empty binaries are refused");
        Check(Rejects("{ \"id\": \"wxl.x\", \"name\": \"x\", \"version\": \"1.0.0\", \"abi\": "
                      "\"0.1.0\", \"binaries\": { \"mac-arm64\": \"a\" } }",
                      "unknown target"),
              "an unknown target is refused");
        Check(Rejects("{ \"id\": \"wxl.x\", \"name\": \"x\", \"version\": \"1.0.0\", \"abi\": "
                      "\"0.1.0\", \"binaries\": { \"win-x64\": 1 } }",
                      "must be a string"),
              "a non-string binary is refused");
        Check(Rejects(ManifestText("wxl.x", "\"name\": \"\""), "duplicate"),
              "the JSON layer catches a repeated key first");
        std::string emptyName = ManifestText("wxl.x");
        emptyName.replace(emptyName.find("\"Test\""), 6, "\"\"");
        Check(Rejects(emptyName, "at least 1"), "an empty name is refused");
        Check(Rejects(ManifestText("wxl.x", "\"requires\": [ { \"service\": \"s\" } ]"),
                      "missing 'version'"),
              "a service ref without a version is refused");
        Check(Rejects(ManifestText("wxl.x", "\"requires\": [ { \"version\": 1 } ]"),
                      "missing 'service'"),
              "a service ref without a name is refused");
        Check(Rejects(ManifestText("wxl.x", "\"requires\": [ { \"service\": \"s\", \"version\": 0 } ]"),
                      "at least 1"),
              "a version below 1 is refused");
        Check(Rejects(ManifestText("wxl.x",
                                   "\"requires\": [ { \"service\": \"s\", \"version\": 1.5 } ]"),
                      "integer"),
              "a fractional version is refused");
        Check(Rejects(ManifestText("wxl.x",
                                   "\"requires\": [ { \"service\": \"s\", \"version\": 1, "
                                   "\"why\": \"x\" } ]"),
                      "unknown field 'why'"),
              "an unknown field in a service ref is refused");
        Check(Rejects(ManifestText("wxl.x", "\"requires\": { }"), "must be an array"),
              "requires must be an array");
        Check(Rejects(ManifestText("wxl.x", "\"loadAfter\": [ 1 ]"), "loadAfter[0]"),
              "a non-string loadAfter entry is refused");
        Check(Rejects(ManifestText("wxl.x", "\"description\": \"" + std::string(513, 'd') + "\""),
                      "at most 512"),
              "a long description is refused");
        Check(Rejects(ManifestText("wxl.x", "\"authors\": \"me\""), "must be an array"),
              "authors must be an array");
        Check(Rejects(ManifestText("wxl.x", "\"colour\": \"blue\""), "'colour' is not a manifest field",
                      &message),
              "an unknown field is refused");
        Check(Contains(message, "line 7"), "and its line is named");
        {
            Manifest    net;
            std::string error;
            Check(ParseManifest(ManifestText("wxl.x", "\"net\": { \"opcodes\": [ \"0x521-0x52A\", 1400, \"0x600\" ] }"),
                                net, error) &&
                      net.net.opcodes.size() == 3 && net.net.opcodes[0].first == 0x521 &&
                      net.net.opcodes[0].last == 0x52A && net.net.opcodes[1].first == 1400 &&
                      net.net.Declares(0x525) && net.net.Declares(0x600) && !net.net.Declares(0x52B) && !net.net.stock,
                  "net.opcodes reads ranges, numbers and hex strings");
            Check(ParseManifest(ManifestText("wxl.x", "\"net\": { \"stock\": true, \"opcodes\": [ \"0x12E\" ] }"), net,
                                error) &&
                      net.net.stock && net.net.Declares(0x12E),
                  "a client's own number is declared with stock");
        }
        Check(Rejects(ManifestText("wxl.x", "\"net\": { \"opcodes\": [ \"0x12E\" ] }"), "one of the client's own numbers"),
              "a client's own number without stock is refused");
        Check(Rejects(ManifestText("wxl.x", "\"net\": { \"opcodes\": [ \"0x52A-0x521\" ] }"), "whose first number is the lower"),
              "a backwards range is refused");
        Check(Rejects(ManifestText("wxl.x", "\"net\": { \"opcodes\": [ \"0x10000\" ] }"), "net.opcodes[0]"),
              "a number past 0xFFFF is refused");
        Check(Rejects(ManifestText("wxl.x", "\"net\": { \"opcodes\": [ \"0x521-0x525\", \"0x525\" ] }"), "overlaps"),
              "overlapping entries are refused");
        Check(Rejects(ManifestText("wxl.x", "\"net\": { \"port\": 1 }"), "unknown field 'port'"),
              "an unknown field of net is refused");
        Check(Rejects("[]", "must be an object"), "a non-object document is refused");
        Check(Rejects("{ \"id\": }", "line 1"), "a JSON error carries its line");
    }

    Section("load order");
    {
        std::vector<ModCandidate> mods;
        mods.push_back(Candidate("c-third", "wxl.third", {"wxl.second"}));
        mods.push_back(Candidate("b-second", "wxl.second", {"wxl.first", "wxl.absent"}));
        mods.push_back(Candidate("a-first", "wxl.first"));
        mods.push_back(Candidate("d-alone", "wxl.alone"));
        mods.push_back(Candidate("0-late", "wxl.late", {"wxl.third"}));

        const LoadOrder order = OrderByLoadAfter(mods);
        Check(order.excluded.empty(), "a well-formed graph excludes nothing");
        Check(Ids(order.ordered) == "wxl.first wxl.second wxl.third wxl.late wxl.alone",
              "loadAfter is honoured, an absent id is ignored, free mods keep name order");

        std::vector<ModCandidate> tie;
        tie.push_back(Candidate("zeta", "wxl.z"));
        tie.push_back(Candidate("alpha", "wxl.a"));
        tie.push_back(Candidate("mid", "wxl.m"));
        Check(Ids(OrderByLoadAfter(tie).ordered) == "wxl.a wxl.m wxl.z",
              "without constraints the order is the directory-name order");

        std::vector<ModCandidate> cyclic;
        cyclic.push_back(Candidate("a", "wxl.a", {"wxl.b"}));
        cyclic.push_back(Candidate("b", "wxl.b", {"wxl.a"}));
        cyclic.push_back(Candidate("c", "wxl.c", {"wxl.a"}));
        cyclic.push_back(Candidate("d", "wxl.d"));
        const LoadOrder broken = OrderByLoadAfter(cyclic);
        Check(Ids(broken.ordered) == "wxl.d", "a mod outside the cycle still loads");
        Check(broken.excluded.size() == 3, "the cycle and what waits on it are excluded");
        bool aNamed = false, bNamed = false, cBlocked = false;
        for (const DiscoveryProblem& problem : broken.excluded)
        {
            if (problem.id == "wxl.a")
                aNamed = Contains(problem.message, "cycle") && Contains(problem.message, "wxl.a") &&
                         Contains(problem.message, "wxl.b");
            if (problem.id == "wxl.b")
                bNamed = Contains(problem.message, "cycle") && Contains(problem.message, "wxl.a");
            if (problem.id == "wxl.c")
                cBlocked = Contains(problem.message, "loads after 'wxl.a'");
        }
        Check(aNamed && bNamed, "each member of the cycle is told who else is in it");
        Check(cBlocked, "a mod behind the cycle is told what it waited on");

        std::vector<ModCandidate> self;
        self.push_back(Candidate("s", "wxl.s", {"wxl.s"}));
        Check(OrderByLoadAfter(self).excluded.size() == 1, "a mod loading after itself is a cycle");

        std::vector<ModCandidate> speakers;
        speakers.push_back(Candidate("a", "wxl.first"));
        speakers.back().manifest.net.opcodes.push_back(OpcodeRange{0x521, 0x530});
        speakers.push_back(Candidate("b", "wxl.second"));
        speakers.back().manifest.net.opcodes.push_back(OpcodeRange{0x540, 0x540});
        speakers.back().manifest.net.opcodes.push_back(OpcodeRange{0x52F, 0x531});
        speakers.push_back(Candidate("c", "wxl.script"));
        speakers.back().manifest.binaries.clear();
        speakers.back().manifest.lua = "init.lua";
        speakers.back().manifest.net.opcodes.push_back(OpcodeRange{0x531, 0x532});
        LoadOrder             spoken   = OrderByLoadAfter(speakers);
        const OpcodeDeclarations declared = DeclareOpcodes(spoken);
        Check(Ids(spoken.ordered) == "wxl.first wxl.script" && spoken.excluded.size() == 1 &&
                  spoken.excluded[0].id == "wxl.second" &&
                  Contains(spoken.excluded[0].message, "declares opcode 0x52F, which wxl.first declares"),
              "a mod declaring a number an earlier one declared is taken out, naming the number and the mod");
        const DeclaredOpcodes* first  = declared.Find(0x525);
        const DeclaredOpcodes* script = declared.Find(0x532);
        Check(first && first->mod == "wxl.first" && first->served_by == "wxl.first" && script &&
                  script->mod == "wxl.script" && script->served_by == "wxl.lua" && !declared.Find(0x540),
              "the rest are kept with their mod, a script mod's served by wxl.lua; the excluded one's are not");
    }

    Section("discovery");
    if (scratch.empty())
    {
        std::printf("  (skipped: no scratch directory given)\n");
    }
    else
    {
        const fs::path mods = scratch / "discovery";
        fs::remove_all(mods);
        WriteFile(mods / "alpha" / "mod.json", ManifestText("wxl.alpha"));
        WriteFile(mods / "broken" / "mod.json", "{ \"id\": \n");
        WriteFile(mods / "dup" / "mod.json", ManifestText("wxl.alpha"));
        WriteFile(mods / "empty" / "readme.txt", "no manifest here");
        WriteFile(mods / "zulu" / "mod.json", ManifestText("wxl.zulu"));
        WriteFile(mods / "stray.json", "{}");

        const Discovery found = Discover(mods);
        Check(found.mods.size() == 2 && found.mods[0].manifest.id == "wxl.alpha" &&
                  found.mods[1].manifest.id == "wxl.zulu",
              "folders with a manifest are found in name order");
        Check(found.mods[0].directory_name == "alpha" && found.mods[0].directory == mods / "alpha",
              "a candidate knows its folder");
        Check(found.problems.size() == 2, "a broken manifest and a duplicate id are problems");
        bool brokenReported = false, dupReported = false;
        for (const DiscoveryProblem& problem : found.problems)
        {
            if (problem.directory_name == "broken")
                brokenReported = problem.id.empty() && Contains(problem.message, "line 2") &&
                                 Contains(problem.message, "mod.json");
            if (problem.directory_name == "dup")
                dupReported = problem.id == "wxl.alpha" &&
                              Contains(problem.message, "already provided by alpha");
        }
        Check(brokenReported, "a broken manifest names its file and line");
        Check(dupReported, "a duplicate id names the folder that has it");
        Check(Discover(scratch / "does-not-exist").mods.empty(), "a missing directory yields nothing");
    }

    Section("admission");
    {
        ServiceRegistry services;
        static int      iface = 0;
        services.Publish("wxl.object", 1, &iface);

        Manifest manifest = Candidate("x", "wxl.x").manifest;
        Decision decision = AdmitManifest(manifest, CurrentTarget(), services);
        Check(decision.verdict == Verdict::Load && decision.binary == "mod.dll",
              "a plain manifest is admitted with its binary");

        decision = AdmitManifest(manifest, "nothing-x64", services);
        Check(decision.verdict == Verdict::Skip && Contains(decision.reason, "no binary for nothing-x64"),
              "no binary for the target is a skip, not a refusal");

        Manifest script = manifest;
        script.binaries.clear();
        script.lua = "init.lua";
        decision   = AdmitManifest(script, CurrentTarget(), services);
        Check(decision.verdict == Verdict::Skip && Contains(decision.reason, "script mod (lua: init.lua)") &&
                  Contains(decision.reason, "wxl.lua serves it"),
              "a script mod is skipped with a reason naming wxl.lua");

        Manifest major     = manifest;
        major.abi.major    = WXL_ABI_VERSION_MAJOR + 1;
        decision           = AdmitManifest(major, CurrentTarget(), services);
        Check(decision.verdict == Verdict::Refuse && Contains(decision.reason, "major"),
              "a different abi major is refused");

        Manifest minor  = manifest;
        minor.abi.minor = WXL_ABI_VERSION_MINOR + 1;
        decision        = AdmitManifest(minor, CurrentTarget(), services);
        Check(decision.verdict == Verdict::Refuse && Contains(decision.reason, "minor"),
              "a higher abi minor is refused");

        Manifest feature    = manifest;
        feature.abi.feature = WXL_ABI_VERSION_FEATURE + 1;
        decision            = AdmitManifest(feature, CurrentTarget(), services);
        Check(decision.verdict == Verdict::Refuse && Contains(decision.reason, "newer") &&
                  Contains(decision.reason, AbiText(feature.abi).c_str()) && Contains(decision.reason, kAbi),
              "a higher abi feature is refused, naming both versions");

        Manifest older    = manifest;
        older.abi.feature = 0;
        Check(AdmitManifest(older, CurrentTarget(), services).verdict == Verdict::Load,
              "a lower abi feature is admitted");

        Manifest needs = manifest;
        needs.required.push_back(ServiceRef{"wxl.object", 1});
        Check(AdmitManifest(needs, CurrentTarget(), services).verdict == Verdict::Load,
              "a required service that exists passes");
        needs.required.push_back(ServiceRef{"wxl.script", 3});
        decision = AdmitManifest(needs, CurrentTarget(), services);
        Check(decision.verdict == Verdict::Refuse && Contains(decision.reason, "wxl.script") &&
                  Contains(decision.reason, "version 3"),
              "a missing required service is refused by name and version");
        needs.required.pop_back();
        needs.required.push_back(ServiceRef{"wxl.object", 2});
        Check(AdmitManifest(needs, CurrentTarget(), services).verdict == Verdict::Refuse,
              "another version of a present service does not satisfy it");

        Manifest escaping = manifest;
        escaping.binaries[0].path = "../outside.dll";
        Check(AdmitManifest(escaping, CurrentTarget(), services).verdict == Verdict::Refuse,
              "a binary path leaving the folder is refused");

        Check(AdmitInfo(manifest, nullptr).verdict == Verdict::Refuse &&
                  Contains(AdmitInfo(manifest, nullptr).reason, "NULL"),
              "a NULL from wxl_mod_query is refused");
        wxl_mod_info info{sizeof(wxl_mod_info), WXL_ABI_VERSION_MAJOR, WXL_ABI_VERSION_MINOR,
                          "wxl.x", "X", 1, WXL_ABI_VERSION_FEATURE};
        Check(AdmitInfo(manifest, &info).verdict == Verdict::Load, "a matching info is accepted");
        info.struct_size = static_cast<uint32_t>(offsetof(wxl_mod_info, abi_feature));
        Check(Contains(AdmitInfo(manifest, &info).reason, "two-part"),
              "an info without abi_feature is refused as the old numbering");
        info.struct_size = 8;
        Check(Contains(AdmitInfo(manifest, &info).reason, "struct_size is 8"), "a short struct_size is refused");
        info.struct_size = sizeof(wxl_mod_info) + 8;
        Check(AdmitInfo(manifest, &info).verdict == Verdict::Load,
              "a longer info, from later headers, is accepted");
        info.struct_size = sizeof(wxl_mod_info);
        info.abi_feature = WXL_ABI_VERSION_FEATURE + 1;
        Check(AdmitInfo(manifest, &info).verdict == Verdict::Load,
              "a binary built against a later feature than its manifest needs is accepted");
        info.abi_feature = 0;
        Check(Contains(AdmitInfo(manifest, &info).reason, ("asks for " + std::string(kAbi)).c_str()),
              "a binary built against less than its manifest asks for is refused");
        info.abi_feature = WXL_ABI_VERSION_FEATURE;
        info.id          = "wxl.y";
        Check(Contains(AdmitInfo(manifest, &info).reason, "'wxl.y'"), "an id mismatch is refused");
        info.id = nullptr;
        Check(Contains(AdmitInfo(manifest, &info).reason, "NULL"), "a NULL id is refused");
        info.id        = "wxl.x";
        info.abi_minor = WXL_ABI_VERSION_MINOR + 1;
        Check(Contains(AdmitInfo(manifest, &info).reason, "abi"), "an abi mismatch is refused");
    }

    Section("the abi rule");
    {
        const auto refusal = [](AbiVersion needed, AbiVersion served) { return AbiRefusal(needed, served); };
        Check(refusal({0, 1, 5}, {0, 1, 5}).empty() && refusal({0, 1, 3}, {0, 1, 5}).empty(),
              "the same or a lower feature is served");
        Check(refusal({0, 1, 7}, {0, 1, 5}) ==
                  "needs abi 0.1.7, newer than this runtime's 0.1.5: it uses what was appended after 0.1.5",
              "a higher feature is refused, saying both versions");
        Check(Contains(refusal({1, 0, 0}, {0, 1, 5}), "different major cannot be reconciled") &&
                  Contains(refusal({0, 1, 5}, {1, 0, 0}), "different major"),
              "another major is refused either way");
        Check(Contains(refusal({0, 2, 0}, {0, 1, 5}), "while the major is 0 a different minor") &&
                  Contains(refusal({0, 1, 0}, {0, 2, 0}), "while the major is 0 a different minor"),
              "under major 0, another minor is refused either way");
        Check(refusal({1, 1, 9}, {1, 2, 0}).empty(), "after major 0, an earlier stage is served");
        Check(refusal({1, 3, 0}, {1, 2, 4}) == "needs abi 1.3.0, a later stage than this runtime's 1.2.4",
              "and a later one is refused");
    }

    Section("service registry");
    {
        ServiceRegistry registry;
        static int      one = 1, two = 2;
        Check(registry.Publish("svc", 1, &one) == WXL_OK, "a service publishes");
        Check(registry.Get("svc", 1) == &one && registry.Has("svc", 1), "and resolves");
        Check(registry.Get("svc", 2) == nullptr && !registry.Has("svc", 2),
              "another version does not match");
        Check(registry.Publish("svc", 2, &two) == WXL_OK, "the same name under another version is new");
        Check(registry.Get("svc", 1) == &one && registry.Get("svc", 2) == &two,
              "both versions resolve to their own pointer");
        Check(registry.Publish("svc", 1, &two) == WXL_E_INVALID, "republishing a key is refused");
        Check(registry.Get("svc", 1) == &one, "and the first stays");
        Check(registry.Publish("", 1, &one) == WXL_E_INVALID, "an empty name is refused");
        Check(registry.Publish("x", 1, nullptr) == WXL_E_INVALID, "a null interface is refused");
        Check(registry.Entries().size() == 2, "the entries can be listed");
    }

    Section("listening");
    {
        EventBus bus({WXL_EV_Update, WXL_EV_FrameBegin}, nullptr);
        struct Seen
        {
            std::vector<std::pair<wxl_event, bool>> changes;
        } seen;
        auto record = [](void* user, wxl_event event, bool listening) {
            static_cast<Seen*>(user)->changes.emplace_back(event, listening);
        };
        int one = 1, two = 2;
        Check(!bus.Listening(WXL_EV_Update), "nobody listens to an event before a subscription");
        const wxl_subscription early = bus.Subscribe(WXL_EV_FrameBegin, &Record, &one, 0);
        bus.SetListenHandler(record, &seen);
        Check(seen.changes.size() == 1 && seen.changes[0].first == WXL_EV_FrameBegin && seen.changes[0].second,
              "the handler is told at once of an event already listened to");
        const wxl_subscription a = bus.Subscribe(WXL_EV_Update, &Record, &one, 0);
        const wxl_subscription b = bus.Subscribe(WXL_EV_Update, &Record, &two, 0);
        Check(bus.Listening(WXL_EV_Update), "a subscription makes its event listened to");
        Check(seen.changes.size() == 2 && seen.changes[1].first == WXL_EV_Update && seen.changes[1].second,
              "the first subscription is reported, the second is not");
        bus.Unsubscribe(a);
        Check(bus.Listening(WXL_EV_Update) && seen.changes.size() == 2, "one left: still listened to");
        bus.Unsubscribe(b);
        Check(!bus.Listening(WXL_EV_Update), "the last one gone: nobody listens");
        Check(seen.changes.size() == 3 && seen.changes[2].first == WXL_EV_Update && !seen.changes[2].second,
              "and the handler is told");
        bus.Subscribe(WXL_EV_Update, &Record, &one, 0, 7);
        bus.DropOwner(7);
        Check(!bus.Listening(WXL_EV_Update) && seen.changes.size() == 5, "dropping an owner is reported too");
        Check(!bus.Listening(static_cast<wxl_event>(WXL_EV_COUNT)), "an event outside the table is never listened to");
        bus.SetListenHandler(nullptr, nullptr);
        bus.Unsubscribe(early);
        Check(seen.changes.size() == 5 && !bus.Listening(WXL_EV_FrameBegin), "a null handler stops the reports");
    }

    Section("what the host tells of its events");
    {
        CaptureSink sink;
        HostConfig  config;
        config.mods_directory = scratch.empty() ? fs::path("no-mods-here") : scratch / "no-mods-here";
        config.runtime_id     = "test";
        config.log_sink       = &sink;
        config.events         = {WXL_EV_Update, WXL_EV_FrameEnd};
        ModHost host(std::move(config));
        const wxl_host* table = host.Table();
        Check(table->struct_size >= offsetof(wxl_host, raises) + sizeof(table->raises) && table->raises,
              "the table carries raises");
        Check(table->raises(WXL_EV_Update) && table->raises(WXL_EV_FrameEnd) && !table->raises(WXL_EV_SoundPlay) &&
                  !table->raises(static_cast<wxl_event>(WXL_EV_COUNT)),
              "raises answers the runtime's list, false outside it and outside the contract");

        ModHost::RecentEvent recent[ModHost::kRecentEvents];
        const wxl_update_args tick{0.1f};
        host.Raise(WXL_EV_Update, &tick);
        Check(host.Recent(recent, ModHost::kRecentEvents) == 0, "a raise nobody listens to is not remembered");
        int one = 1;
        host.Events().Subscribe(WXL_EV_Update, &Record, &one, 0);
        for (int i = 0; i < 3; ++i) host.Raise(WXL_EV_Update, &tick);
        host.Raise(WXL_EV_FrameEnd, nullptr);
        size_t count = host.Recent(recent, ModHost::kRecentEvents);
        Check(count == 3 && recent[0].event == WXL_EV_Update && recent[2].main_thread,
              "the raises that dispatched are remembered, on the main thread");
        for (int i = 0; i < 40; ++i) host.Raise(WXL_EV_Update, &tick);
        count = host.Recent(recent, 4);
        Check(count == 4 && recent[3].event == WXL_EV_Update, "the last ones, as many as asked for");
        Check(host.Recent(recent, ModHost::kRecentEvents) == ModHost::kRecentEvents, "at most kRecentEvents");
        std::thread([&] { host.Raise(WXL_EV_Update, &tick); }).join();
        count = host.Recent(recent, 1);
        Check(count == 1 && !recent[0].main_thread, "a raise on another thread says so");
        g_order.clear();
    }

    Section("event bus");
    {
        CaptureSink sink;
        Logger      logger(&sink, WXL_LOG_TRACE);
        EventBus    bus({WXL_EV_Update, WXL_EV_FrameBegin}, &logger);

        int first = 1, second = 2, third = 3, fourth = 4;
        Check(bus.Subscribe(WXL_EV_WorldEnter, &Record, &first, 0) == WXL_SUBSCRIPTION_NONE,
              "an event the runtime does not raise refuses the subscription");
        Check(bus.Subscribe(WXL_EV_Update, nullptr, &first, 0) == WXL_SUBSCRIPTION_NONE,
              "a null handler is refused");
        Check(bus.Subscribe(static_cast<wxl_event>(WXL_EV_COUNT), &Record, &first, 0) ==
                  WXL_SUBSCRIPTION_NONE,
              "an event outside the table is refused");

        const wxl_subscription s1 = bus.Subscribe(WXL_EV_Update, &Record, &first, 10);
        const wxl_subscription s2 = bus.Subscribe(WXL_EV_Update, &Record, &second, -5);
        const wxl_subscription s3 = bus.Subscribe(WXL_EV_Update, &Record, &third, 10);
        const wxl_subscription s4 = bus.Subscribe(WXL_EV_Update, &Record, &fourth, 0);
        Check(s1 && s2 && s3 && s4 && s1 != s2 && s2 != s3 && s3 != s4, "tokens are distinct");
        Check(bus.LiveCount(WXL_EV_Update) == 4 && bus.LiveCount() == 4, "four live subscriptions");

        g_order.clear();
        bus.Emit(WXL_EV_Update, nullptr);
        Check(g_order == std::vector<int>{2, 4, 1, 3},
              "dispatch runs ascending priority, ties in subscription order");

        g_order.clear();
        bus.Emit(WXL_EV_FrameBegin, nullptr);
        Check(g_order.empty(), "an event without subscribers dispatches to nobody");

        bus.Unsubscribe(s4);
        bus.Unsubscribe(s4);
        bus.Unsubscribe(WXL_SUBSCRIPTION_NONE);
        g_order.clear();
        bus.Emit(WXL_EV_Update, nullptr);
        Check(g_order == std::vector<int>{2, 1, 3} && bus.LiveCount(WXL_EV_Update) == 3,
              "an unsubscribed handler no longer runs; repeating it is harmless");

        // Unsubscribe during dispatch: the dropper runs first and removes a later handler.
        DropOther dropper{&bus, s3, 20};
        const wxl_subscription sd = bus.Subscribe(WXL_EV_Update, &RecordAndDrop, &dropper, -100);
        g_order.clear();
        bus.Emit(WXL_EV_Update, nullptr);
        Check(g_order == std::vector<int>{20, 2, 1},
              "a handler dropped during dispatch does not run in it");
        Check(bus.LiveCount(WXL_EV_Update) == 3, "and is gone afterwards");
        g_order.clear();
        bus.Emit(WXL_EV_Update, nullptr);
        Check(g_order == std::vector<int>{20, 2, 1}, "the next dispatch is consistent");

        // A handler dropping itself.
        DropOther selfDropper{&bus, WXL_SUBSCRIPTION_NONE, 30};
        selfDropper.victim = bus.Subscribe(WXL_EV_Update, &RecordAndDrop, &selfDropper, -200);
        g_order.clear();
        bus.Emit(WXL_EV_Update, nullptr);
        Check(g_order == std::vector<int>{30, 20, 2, 1}, "a handler may drop itself");
        g_order.clear();
        bus.Emit(WXL_EV_Update, nullptr);
        Check(g_order == std::vector<int>{20, 2, 1}, "and does not run again");
        bus.Unsubscribe(sd);

        // Subscribe during dispatch: the new handler, though highest priority, waits a turn.
        int      late = 40;
        AddOther adder{&bus, WXL_EV_Update, &late, 50, WXL_SUBSCRIPTION_NONE};
        bus.Subscribe(WXL_EV_Update, &RecordAndAdd, &adder, 100);
        g_order.clear();
        bus.Emit(WXL_EV_Update, nullptr);
        Check(g_order == std::vector<int>{2, 1, 50},
              "a handler subscribed during dispatch does not run in that dispatch");
        Check(adder.added != WXL_SUBSCRIPTION_NONE && bus.LiveCount(WXL_EV_Update) == 4,
              "but the subscription took");
        g_order.clear();
        bus.Emit(WXL_EV_Update, nullptr);
        Check(g_order == std::vector<int>{40, 2, 1, 50}, "and runs, in its priority, on the next");

        // A handler that throws is caught at the boundary; the rest of the dispatch continues.
        int thrower = 60;
        const wxl_subscription st = bus.Subscribe(WXL_EV_Update, &Throws, &thrower, -300);
        g_order.clear();
        bus.Emit(WXL_EV_Update, nullptr);
        Check(g_order == std::vector<int>{60, 40, 2, 1, 50},
              "an exception in a handler does not stop the dispatch");
        Check(sink.Has("handler failure") && sink.Has("Update"),
              "the exception is logged with the event's name");
        bus.Unsubscribe(st);

        Check(EventName(WXL_EV_Update) == std::string("Update") &&
                  EventName(WXL_EV_MapObjectLoad) == std::string("MapObjectLoad"),
              "events are named from the table");
        Check(AllEvents().size() == WXL_EV_COUNT, "AllEvents lists the whole table");
        EventBus all(AllEvents(), nullptr);
        Check(all.CanRaise(WXL_EV_MapObjectLoad) && !bus.CanRaise(WXL_EV_MapObjectLoad),
              "CanRaise reflects the configuration");
    }

    Section("event bus and a profiler");
    {
        // A profiler that records what it is asked: locations by name, zones as a trace.
        struct Recorder
        {
            std::vector<std::string> names;
            std::vector<std::string> trace;
        };
        static Recorder recorder;
        Profiler        profiler;
        profiler.user     = &recorder;
        profiler.location = [](void* user, std::string_view name, std::string_view, uint32_t) -> const void* {
            Recorder& r = *static_cast<Recorder*>(user);
            r.names.emplace_back(name);
            return reinterpret_cast<const void*>(r.names.size());
        };
        profiler.begin = [](void* user, const void* location) -> uint64_t {
            Recorder& r = *static_cast<Recorder*>(user);
            r.trace.push_back("+" + r.names[reinterpret_cast<uintptr_t>(location) - 1]);
            return reinterpret_cast<uintptr_t>(location);
        };
        profiler.end = [](void* user, uint64_t zone) {
            Recorder& r = *static_cast<Recorder*>(user);
            r.trace.push_back("-" + r.names[static_cast<size_t>(zone) - 1]);
        };

        EventBus bus({WXL_EV_Update}, nullptr);
        bus.SetProfiler(&profiler, [](void*, uint32_t owner) { return owner == 7 ? std::string("scout") : std::string(); },
                        nullptr);
        std::atomic<int> calls{0};
        bus.Subscribe(WXL_EV_Update, &CountUp, &calls, 0, 7);
        bus.Subscribe(WXL_EV_Update, &CountUp, &calls, 1, 0);
        bus.Emit(WXL_EV_Update, nullptr);
        Check(calls.load() == 2 && recorder.trace.empty(), "nothing is recorded while profiling is off");
        bus.SetProfiling(true);
        bus.Emit(WXL_EV_Update, nullptr);
#if WXL_HOST_PROFILE
        Check(recorder.trace.size() == 6 && recorder.trace[0] == "+OnUpdate" &&
                  recorder.trace[1] == "+scout: OnUpdate" && recorder.trace[2] == "-scout: OnUpdate" &&
                  recorder.trace[3] == "+runtime: OnUpdate" && recorder.trace[5] == "-OnUpdate",
              "a dispatch is a zone named after its event, each handler one inside it named after its owner");
        recorder.trace.clear();
        bus.Subscribe(WXL_EV_Update, &CountUp, &calls, 2, 7);
        const size_t named = recorder.names.size();
        bus.Subscribe(WXL_EV_Update, &CountUp, &calls, 3, 7);
        Check(recorder.names.size() == named, "one location per owner and event, however often it subscribes");
        bus.SetProfiling(false);
        bus.Emit(WXL_EV_Update, nullptr);
        Check(recorder.trace.empty(), "and nothing once profiling is off again");
#else
        Check(recorder.trace.empty(), "a host built without WXL_HOST_PROFILE opens no zone");
#endif
    }

    Section("event bus across threads");
    {
        EventBus bus({WXL_EV_Update}, nullptr);

        // Two threads raise while this one keeps changing the list under them.
        std::atomic<int> steady{0};
        std::atomic<int> churn{0};
        bus.Subscribe(WXL_EV_Update, &CountUp, &steady, 0);
        constexpr int kRaises = 20000;
        const auto    raise   = [&bus] {
            for (int i = 0; i < kRaises; ++i) bus.Emit(WXL_EV_Update, nullptr);
        };
        std::thread a(raise), b(raise);
        for (int i = 0; i < 2000; ++i)
            bus.Unsubscribe(bus.Subscribe(WXL_EV_Update, &CountUp, &churn, i % 3 - 1));
        a.join();
        b.join();
        Check(steady.load() == 2 * kRaises,
              "a subscription that stays is called once per raise, whatever changes around it");
        Check(bus.LiveCount(WXL_EV_Update) == 1, "and the ones that came and went are gone");

        // A dispatch on another thread holds an unload back until it is done.
        Gate gate;
        const wxl_subscription held = bus.Subscribe(WXL_EV_Update, &HoldOpen, &gate, 0);
        std::thread holder([&bus] { bus.Emit(WXL_EV_Update, nullptr); });
        while (!gate.entered.load()) std::this_thread::yield();
        bus.Unsubscribe(held);
        Check(!bus.WaitForOtherDispatches(std::chrono::milliseconds(20)),
              "a wait runs out while another thread is still inside a handler, dropped or not");
        gate.release.store(true);
        holder.join();
        Check(bus.WaitForOtherDispatches(std::chrono::milliseconds(1000)),
              "and returns once it has left");

        SelfWait self{&bus};
        const wxl_subscription inside = bus.Subscribe(WXL_EV_Update, &WaitInside, &self, 0);
        bus.Emit(WXL_EV_Update, nullptr);
        Check(self.answered, "the waiting thread's own dispatch does not count against it");
        bus.Unsubscribe(inside);
    }

    Section("log");
    {
        CaptureSink sink;
        Logger      logger(&sink, WXL_LOG_INFO);
        logger.Debug("t", "quiet");
        logger.Warn("t", "loud");
        Check(sink.lines.size() == 1 && sink.lines[0] == "[WARN ] [t] loud",
              "lines below the minimum are dropped; the rest are formatted");
        Logger silent;
        silent.Error("t", "nowhere");
        Check(!silent.Enabled(WXL_LOG_ERROR), "a logger without a sink drops everything");
        Check(std::string(LevelName(WXL_LOG_TRACE)) == "TRACE" &&
                  std::string(LevelName(WXL_LOG_ERROR)) == "ERROR",
              "levels have names");

        Check(IsLogText("plain\ttab\r\n") && IsLogText("\xC3\xA9t\xC3\xA9 \xE2\x82\xAC \xF0\x9F\x98\x80") &&
                  IsLogText(""),
              "ASCII, tabs, line breaks and two-, three- and four-byte sequences are text");
        Check(!IsLogText("\x82") && !IsLogText("a\xC3") && !IsLogText("\xC0\xAF") && !IsLogText("\xE0\x80\xAF") &&
                  !IsLogText("\xED\xA0\x80") && !IsLogText("\xF4\x90\x80\x80") && !IsLogText("\xFF") &&
                  !IsLogText("\x1B[31m") && !IsLogText(std::string_view("a\0b", 3)),
              "a stray continuation, a cut sequence, overlong forms, a surrogate, bytes past U+10FFFF, "
              "an escape and a NUL are not");
        Check(EscapeForLog("a\x82z \xC3\xA9 \xE2\x82") == "a\\x82z \xC3\xA9 \\xE2\\x82",
              "the bytes outside a sequence are escaped, the sequences kept");
        CaptureSink bytes;
        Logger      binary(&bytes, WXL_LOG_INFO);
        binary.Info("t\xFF", std::string_view("743, \"\x82\"\0end", 12));
        Check(bytes.lines.size() == 1 && bytes.lines[0] == "[INFO ] [t\\xFF] 743, \"\\x82\"\\x00end",
              "a line that is not text reaches the sink escaped, its tag included");
    }

    Section("mod host with in-process binaries");
    if (scratch.empty())
    {
        std::printf("  (skipped: no scratch directory given)\n");
    }
    else
    {
        const fs::path mods = scratch / "fake";
        fs::remove_all(mods);
        // Directory names run against the intended order: loadAfter must win.
        WriteFile(mods / "a-second" / "mod.json",
                  ManifestText("wxl.second", "\"loadAfter\": [ \"wxl.good\" ]", kAbi, "second.dll"));
        WriteFile(mods / "b-good" / "mod.json", ManifestText("wxl.good", "", kAbi, "good.dll"));
        WriteFile(mods / "c-nostage" / "mod.json",
                  ManifestText("wxl.nostage", "\"loadAfter\": [ \"wxl.second\" ]", kAbi, "nostage.dll"));
        WriteFile(mods / "declines" / "mod.json", ManifestText("wxl.declines", "", kAbi, "declines.dll"));
        WriteFile(mods / "badload" / "mod.json", ManifestText("wxl.badload", "", kAbi, "badload.dll"));
        WriteFile(mods / "liar" / "mod.json", ManifestText("wxl.liar", "", kAbi, "liar.dll"));
        WriteFile(mods / "missing" / "mod.json", ManifestText("wxl.missing", "", kAbi, "missing.dll"));
        WriteFile(mods / "needy" / "mod.json",
                  ManifestText("wxl.needy", "\"requires\": [ { \"service\": \"wxl.nothing\", \"version\": 1 } ]",
                               kAbi, "good.dll"));
        WriteFile(mods / "oldabi" / "mod.json", ManifestText("wxl.oldabi", "", "9.0.0", "good.dll"));
        WriteFile(mods / "elsewhere" / "mod.json",
                  "{ \"id\": \"wxl.elsewhere\", \"name\": \"E\", \"version\": \"1.0.0\", \"abi\": \"0.1.0\", "
                  "\"binaries\": { \"linux-x64\": \"e.so\" } }");
        WriteFile(mods / "unreadable" / "mod.json", "{ \"id\": \"wxl.unreadable\" ");
        WriteFile(mods / "noquery" / "mod.json", ManifestText("wxl.noquery", "", kAbi, "noquery.dll"));

        FakeLoader loader;
        loader.images.push_back({"good.dll", Fn(&GoodQuery), Fn(&GoodLoad), Fn(&GoodStage), Fn(&GoodUnload)});
        loader.images.push_back({"second.dll", Fn(&SecondQuery), Fn(&SecondLoad), Fn(&SecondStage), Fn(&SecondUnload)});
        loader.images.push_back({"nostage.dll", Fn(&NoStageQuery), Fn(&NoStageLoad), nullptr, nullptr});
        loader.images.push_back({"declines.dll", Fn(&DeclinesQuery), Fn(&DeclinesLoad), nullptr, nullptr});
        loader.images.push_back({"badload.dll", Fn(&BadLoadQuery), Fn(&BadLoadLoad), nullptr, nullptr});
        loader.images.push_back({"liar.dll", Fn(&LiarQuery), Fn(&LiarLoad), nullptr, nullptr});
        loader.images.push_back({"noquery.dll", nullptr, Fn(&GoodLoad), nullptr, nullptr});

        CaptureSink sink;
        static int  runtimeService = 0;
        HostConfig  config;
        config.mods_directory  = mods;
        config.runtime_id      = "test";
        config.runtime_name    = "Test runtime";
        config.runtime_version = 3;
        config.log_sink        = &sink;
        config.events          = {WXL_EV_Update};
        config.services.push_back(PublishedService{"wxl.runtime", 1, &runtimeService});
        config.loader = &loader;

        g_trace.clear();
        ModHost host(config);
        Check(host.Table()->struct_size == sizeof(wxl_host) &&
                  host.Table()->abi_major == WXL_ABI_VERSION_MAJOR &&
                  host.Table()->abi_feature == WXL_ABI_VERSION_FEATURE &&
                  host.Table()->runtime()->abi_feature == WXL_ABI_VERSION_FEATURE,
              "the table is filled before anything loads");
        Check(host.Services().Has("wxl.runtime", 1), "runtime services are published at construction");

        host.LoadAll();
        Check(host.Loaded().size() == 3, "three mods load");
        Check(host.Loaded().size() == 3 && host.Loaded()[0].id == "wxl.good" &&
                  host.Loaded()[1].id == "wxl.second" && host.Loaded()[2].id == "wxl.nostage",
              "in loadAfter order, not directory order");
        Check(g_trace == std::vector<std::string>{"good:load", "second:load", "badload:load",
                                                  "nostage:load"},
              "wxl_mod_load ran in load order; a refused query or binary never reached it");
        Check(host.Loaded().size() == 3 && host.Loaded()[0].name == "Good" &&
                  host.Loaded()[0].has_stage &&
                  host.Loaded()[0].has_unload && !host.Loaded()[2].has_stage &&
                  !host.Loaded()[2].has_unload,
              "a loaded mod reports its name and optional entry points");
        Check(host.Loaded().size() == 3 && host.Loaded()[0].directory_name == "b-good" &&
                  host.Loaded()[0].binary == mods / "b-good" / "good.dll",
              "and where it came from");

        const RefusedMod* refused = FindRefused(host, "wxl.declines");
        Check(refused && refused->verdict == Verdict::Refuse && Contains(refused->reason, "NULL"),
              "a NULL from wxl_mod_query is refused");
        refused = FindRefused(host, "wxl.badload");
        Check(refused && Contains(refused->reason, "WXL_E_FAILED"),
              "a failed wxl_mod_load is refused with the result's name");
        refused = FindRefused(host, "wxl.liar");
        Check(refused && Contains(refused->reason, "wxl.someone-else"),
              "a binary whose id disagrees with the manifest is refused");
        refused = FindRefused(host, "wxl.missing");
        Check(refused && Contains(refused->reason, "cannot load") &&
                  Contains(refused->reason, "missing.dll"),
              "a binary that cannot be opened is refused with the loader's reason");
        refused = FindRefused(host, "wxl.needy");
        Check(refused && Contains(refused->reason, "wxl.nothing"),
              "a missing required service is refused");
        refused = FindRefused(host, "wxl.oldabi");
        Check(refused && Contains(refused->reason, "9.0.0"), "a foreign abi is refused");
        refused = FindRefused(host, "wxl.elsewhere");
        Check(refused && refused->verdict == Verdict::Skip, "a mod for another target is skipped");
        refused = FindRefused(host, "wxl.noquery");
        Check(refused && Contains(refused->reason, "does not export wxl_mod_query"),
              "a binary without wxl_mod_query is refused");
        bool unreadableReported = false;
        for (const RefusedMod& mod : host.Refused())
            if (mod.directory_name == "unreadable")
                unreadableReported = mod.id.empty() && Contains(mod.reason, "mod.json");
        Check(unreadableReported, "an unreadable manifest is reported under its folder");
        Check(host.Refused().size() == 9, "every outcome is accounted for");
        Check(loader.opened == 7 && loader.closed == 4,
              "binaries that did not make it are closed again");
        Check(sink.Has("[host] loaded wxl.good 1.0.0 (Good) from good.dll"), "a load is logged");
        Check(sink.Has("[host] skipped wxl.elsewhere") && sink.Has("[host] refused wxl.liar"),
              "so are a skip and a refusal");
        Check(sink.Has("[good] hello"), "a mod's log line reaches the sink through the table");

        g_trace.clear();
        host.Stage(WXL_STAGE_EARLY);
        host.Stage(WXL_STAGE_READY);
        Check(g_trace == std::vector<std::string>{"good:stage0", "second:stage0", "good:stage4",
                                                  "second:stage4"},
              "stages run in load order, skipping mods without wxl_mod_stage");

        const wxl_runtime_info* info = host.Table()->runtime();
        Check(info && std::string(info->id.data, info->id.size) == "test" && info->version == 3 &&
                  info->struct_size == sizeof(wxl_runtime_info),
              "the runtime describes itself through the table");
        Check(info && std::string(info->mods_directory.data, info->mods_directory.size) == mods.u8string(),
              "and names the mods directory it scans");
        Check(host.Table()->has_service("wxl.runtime", 1) == WXL_TRUE &&
                  host.Table()->get_service("wxl.runtime", 1) == &runtimeService &&
                  host.Table()->has_service("wxl.runtime", 2) == WXL_FALSE,
              "services resolve through the table");
        Check(host.Table()->publish_service("wxl.runtime", 1, &runtimeService) == WXL_E_INVALID &&
                  host.Table()->publish_service(nullptr, 1, &runtimeService) == WXL_E_INVALID,
              "a taken key and a null name are refused through the table");
        Check(host.Table()->subscribe(WXL_EV_FrameBegin, &Record, nullptr, 0) == WXL_SUBSCRIPTION_NONE,
              "an event the runtime did not declare is refused through the table");
        host.Table()->subscribe(WXL_EV_FrameBegin, &Record, nullptr, 0);
        Check(CountLines(sink.lines, "subscribed to FrameBegin, which this runtime does not raise") == 1,
              "the refusal is logged as a warning, once");
        const ModHost::ModToken outside = host.SwapCurrentMod(1);
        host.Table()->subscribe(WXL_EV_WorldEnter, &Record, nullptr, 0);
        host.SwapCurrentMod(outside);
        Check(sink.Has("[WARN ] [host] mod wxl.good subscribed to WorldEnter"),
              "naming the mod whose handler will never run");
        int                    tag = 7;
        const wxl_subscription sub = host.Table()->subscribe(WXL_EV_Update, &Record, &tag, 0);
        g_order.clear();
        const wxl_update_args tick{0.5f};
        host.Raise(WXL_EV_Update, &tick);
        host.Table()->emit(WXL_EV_Update, &tick);
        Check(sub != WXL_SUBSCRIPTION_NONE && g_order == std::vector<int>{7, 7},
              "Raise and the table's emit reach the same subscribers");
        host.Table()->unsubscribe(sub);
        host.Table()->log(WXL_LOG_WARN, nullptr, wxl_str{nullptr, 0});
        Check(sink.Has("[WARN ] [?] "), "a null tag and an empty message are tolerated");

        g_trace.clear();
        host.Shutdown();
        Check(g_trace == std::vector<std::string>{"second:stage5", "good:stage5", "second:unload",
                                                  "good:unload"},
              "shutdown and unload run in reverse load order");
        Check(host.Loaded().empty() && loader.closed == 7, "everything is unloaded and closed");

        host.LoadAll();
        Check(host.Loaded().empty(), "LoadAll is a one-shot");
    }

    Section("a binary's headers bound the events it subscribes to");
    if (scratch.empty())
    {
        std::printf("  (skipped: no scratch directory given)\n");
    }
    else
    {
        const fs::path mods = scratch / "older";
        fs::remove_all(mods);
        const std::string older = std::to_string(WXL_ABI_VERSION_MAJOR) + "." +
                                  std::to_string(WXL_ABI_VERSION_MINOR) + "." + std::to_string(kOlderFeature);
        WriteFile(mods / "older" / "mod.json", ManifestText("wxl.older", "", older.c_str(), "older.dll"));

        FakeLoader loader;
        loader.images.push_back({"older.dll", Fn(&OlderQuery), Fn(&OlderLoad), nullptr, nullptr});
        CaptureSink sink;
        HostConfig  config;
        config.mods_directory = mods;
        config.runtime_id     = "test";
        config.runtime_name   = "Test runtime";
        config.log_sink       = &sink;
        config.events         = AllEvents();
        config.loader         = &loader;

        ModHost host(config);
        host.LoadAll();
        Check(host.Loaded().size() == 1 && host.Loaded()[0].built_abi.feature == kOlderFeature,
              "the binary loads, its built abi kept");
        Check(g_olderNew == WXL_SUBSCRIPTION_NONE && g_olderOld != WXL_SUBSCRIPTION_NONE,
              "an event newer than its headers is refused, an older one is not");
        const AbiVersion  since = EventSince(WXL_EV_ChatMessage);
        const std::string line  = "[WARN ] [host] mod wxl.older subscribed to ChatMessage, which came with abi " +
                                 AbiText(since) + "; its binary was built against abi " + older +
                                 ", whose headers do not have that event: the subscription is refused";
        Check(CountLines(sink.lines, line.c_str()) == 1, "the refusal is logged once, naming both versions");
        Check(AtLeast(AbiVersion{0, 1, 6}, AbiVersion{0, 1, 5}) && AtLeast(AbiVersion{0, 1, 5}, AbiVersion{0, 1, 5}) &&
                  !AtLeast(AbiVersion{0, 1, 5}, AbiVersion{0, 1, 6}) && AtLeast(AbiVersion{0, 2, 0}, AbiVersion{0, 1, 9}) &&
                  !AtLeast(AbiVersion{0, 9, 9}, AbiVersion{1, 0, 0}),
              "versions compare major, then minor, then feature");
        host.Shutdown();
    }

    Section("crash guard");
#if defined(_MSC_VER)
    {
        Fault fault;
        int   touched = 0;
        Check(CallGuarded(&GuardedFine, &touched, &fault) && touched == 1,
              "a call that returns is reported as returned");
        Check(!CallGuarded(&GuardedCrash, nullptr, &fault) && fault.code == 0xC0000005u,
              "a null write inside the guard is reported as an access violation");
        char text[192];
        DescribeFault(fault, text, sizeof text);
        Check(Contains(text, "access violation (0xC0000005) writing 0x0 at "),
              "a fault describes itself, with the address it touched");
        Check(Contains(text, ".exe+0x"), "and where it happened, as a module and an offset");
        char where[96];
        Check(std::string(DescribeAddress(0x10, where, sizeof where)) == "0x10",
              "an address outside every module is shown bare");
    }
    if (scratch.empty())
    {
        std::printf("  (skipped: no scratch directory given)\n");
    }
    else
    {
        const fs::path mods = scratch / "crash";
        fs::remove_all(mods);
        WriteFile(mods / "a-steady" / "mod.json", ManifestText("wxl.steady", "", kAbi, "steady.dll"));
        WriteFile(mods / "b-faulty" / "mod.json",
                  ManifestText("wxl.faulty", "\"loadAfter\": [ \"wxl.steady\" ]", kAbi, "faulty.dll"));
        WriteFile(mods / "c-stagecrash" / "mod.json",
                  ManifestText("wxl.stagecrash", "\"loadAfter\": [ \"wxl.faulty\" ]", kAbi, "stagecrash.dll"));
        WriteFile(mods / "loadcrash" / "mod.json", ManifestText("wxl.loadcrash", "", kAbi, "loadcrash.dll"));
        WriteFile(mods / "querycrash" / "mod.json", ManifestText("wxl.querycrash", "", kAbi, "querycrash.dll"));

        FakeLoader loader;
        loader.images.push_back({"steady.dll", Fn(&SteadyQuery), Fn(&SteadyLoad), Fn(&SteadyStage), Fn(&SteadyUnload)});
        loader.images.push_back({"faulty.dll", Fn(&FaultyQuery), Fn(&FaultyLoad), Fn(&FaultyStage), Fn(&FaultyUnload)});
        loader.images.push_back({"stagecrash.dll", Fn(&StageCrashQuery), Fn(&StageCrashLoad), Fn(&StageCrashStage), Fn(&StageCrashUnload)});
        loader.images.push_back({"loadcrash.dll", Fn(&LoadCrashQuery), Fn(&LoadCrashLoad), nullptr, nullptr});
        loader.images.push_back({"querycrash.dll", Fn(&QueryCrashQuery), Fn(&SteadyLoad), nullptr, nullptr});

        CaptureSink sink;
        HostConfig  config;
        config.mods_directory = mods;
        config.runtime_id     = "test";
        config.runtime_name   = "Test runtime";
        config.log_sink       = &sink;
        config.events         = {WXL_EV_Update, WXL_EV_FrameBegin};
        config.loader         = &loader;

        g_trace.clear();
        ModHost host(config);
        host.LoadAll();
        Check(host.Loaded().size() == 3 && host.Loaded()[0].id == "wxl.steady" &&
                  host.Loaded()[1].id == "wxl.faulty" && host.Loaded()[2].id == "wxl.stagecrash",
              "the mods that survive their entry points load; the two that fault at the door do not");
        const RefusedMod* refused = FindRefused(host, "wxl.loadcrash");
        Check(refused && Contains(refused->reason, "wxl_mod_load faulted") &&
                  Contains(refused->reason, "access violation"),
              "a load that faults is refused with the fault named");
        refused = FindRefused(host, "wxl.querycrash");
        Check(refused && Contains(refused->reason, "wxl_mod_query faulted"),
              "a query that faults is refused the same way");
        Check(loader.opened == 5 && loader.closed == 0,
              "a binary that faulted is left mapped rather than unloaded");
        Check(host.Events().LiveCount(WXL_EV_Update) == 3,
              "the subscription a faulting load made is dropped with it");

        host.Stage(WXL_STAGE_EARLY);
        Check(g_trace == std::vector<std::string>{"steady:stage0", "faulty:stage0", "stagecrash:stage0"},
              "every loaded mod sees the early stage");

        // The fault happens on another thread: what stops the mod's code happens there and then,
        // the rest of disabling it waits for this one.
        const wxl_update_args tick{0.5f};
        g_order.clear();
        std::thread([&host, &tick] { host.Raise(WXL_EV_Update, &tick); }).join();
        Check(g_order == std::vector<int>{101, 100},
              "a handler that faults ends only itself: the next mod's handler still runs, and the "
              "faulting mod's later handler for the same event does not");
        Check(host.Events().LiveCount() == 1,
              "every subscription of the faulting mod is gone at once, other events included");
        Check(!host.Loaded()[1].disabled,
              "a fault on another thread leaves the rest of the disabling to the main thread");

        g_order.clear();
        host.Raise(WXL_EV_Update, &tick);
        Check(host.Loaded()[1].disabled && Contains(host.Loaded()[1].fault, "Update"),
              "which disables the mod at its next raise, remembering what it was doing");
        Check(sink.Has("mod wxl.faulty faulted in its handler for Update") &&
                  sink.Has("access violation (0xC0000005)"),
              "the log names the mod, what it was doing and the fault");
        host.Raise(WXL_EV_FrameBegin, &tick);
        Check(g_order == std::vector<int>{100}, "the next dispatches run without it");

        g_trace.clear();
        host.Stage(WXL_STAGE_READY);
        Check(g_trace == std::vector<std::string>{"steady:stage4", "stagecrash:stage4"},
              "a disabled mod is skipped at the next stage; a stage that faults ends there");
        Check(host.Loaded()[2].disabled && Contains(host.Loaded()[2].fault, "wxl_mod_stage(ready)"),
              "the mod whose stage faulted is disabled, naming the stage");

        g_trace.clear();
        host.Shutdown();
        Check(g_trace == std::vector<std::string>{"steady:stage5", "steady:unload"},
              "shutdown and unload skip the disabled mods");
        Check(host.Loaded().empty() && loader.closed == 1,
              "the sound mod's binary is released; the disabled ones stay mapped");
        Check(sink.Has("left wxl.faulty mapped") && sink.Has("left wxl.stagecrash mapped"),
              "and the log says so");
    }
#else
    std::printf("  (skipped: off MSVC the guard is a plain call)\n");
#endif

    Section("a mod that disables itself");
    if (scratch.empty())
    {
        std::printf("  (skipped: no scratch directory given)\n");
    }
    else
    {
        const fs::path mods = scratch / "quit";
        fs::remove_all(mods);
        WriteFile(mods / "a-quitter" / "mod.json", ManifestText("wxl.quitter", "", kAbi, "quitter.dll"));
        WriteFile(mods / "b-loadquit" / "mod.json", ManifestText("wxl.loadquit", "", kAbi, "loadquit.dll"));
        WriteFile(mods / "c-unloadquit" / "mod.json",
                  ManifestText("wxl.unloadquit", "", kAbi, "unloadquit.dll"));

        FakeLoader loader;
        loader.images.push_back({"quitter.dll", Fn(&QuitterQuery), Fn(&QuitterLoad), Fn(&QuitterStage), Fn(&QuitterUnload)});
        loader.images.push_back({"loadquit.dll", Fn(&LoadQuitQuery), Fn(&LoadQuitLoad), nullptr, nullptr});
        loader.images.push_back({"unloadquit.dll", Fn(&UnloadQuitQuery), Fn(&UnloadQuitLoad), nullptr, Fn(&UnloadQuitUnload)});

        CaptureSink sink;
        HostConfig  config;
        config.mods_directory = mods;
        config.runtime_id     = "test";
        config.runtime_name   = "Test runtime";
        config.log_sink       = &sink;
        config.events         = {WXL_EV_Update};
        config.loader         = &loader;

        struct Told
        {
            std::vector<std::string> ids;
            static void Disabled(void* user, ModHost::ModToken, const char* id)
            {
                static_cast<Told*>(user)->ids.push_back(id);
            }
        } told;

        g_trace.clear();
        ModHost host(config);
        host.SetDisableHandler(&Told::Disabled, &told);
        host.LoadAll();
        Check(host.Table()->struct_size == sizeof(wxl_host) && host.Table()->disable != nullptr,
              "the table carries disable");
        const RefusedMod* refused = FindRefused(host, "wxl.loadquit");
        Check(host.Loaded().size() == 2 && refused &&
                  refused->reason == "it disabled itself in wxl_mod_load: gave up; the binary "
                                     "stays mapped and nothing of it is called again",
              "a mod that disables itself while loading is refused with its reason");
        Check(host.Events().LiveCount(WXL_EV_Update) == 1, "and what it subscribed is dropped");

        host.Stage(WXL_STAGE_EARLY);
        host.Stage(WXL_STAGE_READY);
        Check(host.Loaded()[0].disabled && host.Loaded()[0].fault == "OnReady threw an exception: boom" &&
                  told.ids == std::vector<std::string>{"wxl.quitter"},
              "one that disables itself in a stage is disabled there, and the runtime is told");
        Check(sink.Has("mod wxl.quitter disabled itself: OnReady threw an exception: boom; it is "
                       "disabled: its handlers are dropped"),
              "the log gives its reason");
        Check(host.Events().LiveCount(WXL_EV_Update) == 0, "its handlers are gone");

        host.Table()->disable(Text("nobody's"));
        Check(sink.Has("disable was called from code that is no mod's; nothing is disabled"),
              "called from no mod's code, it disables nothing and says so");

        g_trace.clear();
        host.Shutdown();
        Check(g_trace == std::vector<std::string>{"unloadquit:unload"},
              "the disabled mod's later stages and unload are skipped");
        Check(sink.Has("mod wxl.unloadquit disabled itself: OnUnload threw an exception: late") &&
                  loader.closed == 0,
              "one that disables itself in its unload stays mapped");
    }

    Section("mod host with the fixture binary");
    if (fixture.empty() || scratch.empty())
    {
        std::printf("  (skipped: no fixture binary given)\n");
    }
    else
    {
        const fs::path mods = scratch / "real";
        fs::remove_all(mods);
        fs::create_directories(mods / "fixture");
        fs::copy_file(fixture, mods / "fixture" / "fixture.dll");
        WriteFile(mods / "fixture" / "mod.json",
                  ManifestText("wxl.fixture", "", kAbi, "fixture.dll"));
        WriteFile(mods / "ghost" / "mod.json", ManifestText("wxl.ghost", "", kAbi, "ghost.dll"));

        CaptureSink sink;
        HostConfig  config;
        config.mods_directory = mods;
        config.runtime_id     = "test";
        config.runtime_name   = "Test runtime";
        config.log_sink       = &sink;
        config.events         = AllEvents();

        ModHost host(config);
        host.LoadAll();
        for (const RefusedMod& mod : host.Refused())
            std::printf("        %s: %s\n", mod.directory_name.c_str(), mod.reason.c_str());
        Check(host.Loaded().size() == 1 && host.Loaded()[0].id == "wxl.fixture" &&
                  host.Loaded()[0].name == "Fixture",
              "the fixture loads through the OS loader and reports its info");
        Check(host.Loaded().size() == 1 && host.Loaded()[0].has_stage && host.Loaded()[0].has_unload,
              "the SDK macro exports all four entry points");
        Check(sink.Has("[fixture] load"), "its wxl_mod_load ran and logged through the table");
        const FixtureApi* api =
            static_cast<const FixtureApi*>(host.Table()->get_service("fixture.api", 1));
        Check(api && api->struct_size == sizeof(FixtureApi) && api->answer() == 42,
              "a service the fixture published is callable");
        const RefusedMod* ghost = FindRefused(host, "wxl.ghost");
        Check(ghost && Contains(ghost->reason, "cannot load") && Contains(ghost->reason, "ghost.dll"),
              "a binary that is not there is refused with the OS's reason");

        host.Stage(WXL_STAGE_EARLY);
        Check(sink.Has("[fixture] stage 0"), "a stage reaches it");
        const wxl_update_args tick{0.25f};
        host.Raise(WXL_EV_Update, &tick);
        Check(sink.Has("[fixture] update 0.250"), "an event reaches its handler with its args");
        host.Shutdown();
        Check(sink.Has("[fixture] stage 5") && sink.Has("[fixture] unload") &&
                  sink.Has("[host] unloaded wxl.fixture"),
              "shutdown reaches it and the binary is released");
        Check(host.Loaded().empty(), "nothing stays loaded");
    }

    if (g_failures == 0)
    {
        std::printf("\nall checks passed\n");
        return 0;
    }

    std::printf("\n%d check(s) failed\n", g_failures);
    return 1;
}
