// A C++ mod that serves files the client reads as its own. Its provider holds an add-on,
// WxlProvided, that exists nowhere on the disk: the client lists it among its add-ons, reads its
// .toc and runs its Lua. A transform of .lua files rewrites that add-on's file as the client reads
// it, and one of .wxltest files upper-cases them. wxl.selftest checks what the client got.
//
// It also plays a downloader, as a mod fetching assets from a CDN would: its provider answers
// "not yet" for Interface\WxlTest\Later.blp, and its wxl.relay service wxl.test-assets.fetch
// "downloads" a file on a thread of its own (an icon the client has, copied) and serves it in the
// scope of the mod that asked, answering with the scoped name and its MD5. It keeps the last name
// the client could not find (FileMissing) for wxl.selftest to ask about.

#include <cctype>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "wxl/sdk.hpp"
#include "wxl/services/relay.hpp"
#include "wxl/services/storage.hpp"

namespace
{
    constexpr const char* kTag = "test-assets";

    /// What the downloader hands back for any name: an icon every 3.3.5a client has.
    constexpr const char* kDownloaded = "Interface\\Icons\\INV_Misc_Bag_08.blp";

    /// Names as the provider is asked them: lower case, backslashes.
    std::string Key(std::string_view name)
    {
        std::string key(name);
        for (char& c : key) c = c == '/' ? '\\' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return key;
    }

    /// The files of the add-on, a file to convert, and what the downloader fetched, in memory.
    /// Asked from the client's loader threads and written from the downloader's: under a lock.
    class Provided final : public wxl::AssetProvider
    {
    public:
        Provided()
        {
            Add("Interface\\AddOns\\WxlProvided\\WxlProvided.toc",
                "## Interface: 30300\n## Title: WxlProvided\n## Notes: served by wxl.test-assets\nWxlProvided.lua\n");
            Add("Interface\\AddOns\\WxlProvided\\WxlProvided.lua", "WXL_PROVIDED_ADDON = \"loaded\"\n");
            Add("Interface\\WxlTest\\lower.wxltest", "read through a transform");
            pending_.insert(Key("Interface\\WxlTest\\Later.blp"));
        }

        wxl::AssetState Lookup(std::string_view path) override
        {
            const std::string           key = Key(path);
            std::lock_guard<std::mutex> lock(lock_);
            if (files_.count(key)) return wxl::AssetState::Present;
            if (pending_.count(key)) return wxl::AssetState::Pending;
            return wxl::AssetState::Missing;
        }

        std::unique_ptr<wxl::AssetFile> Open(std::string_view path) override
        {
            std::lock_guard<std::mutex> lock(lock_);
            const auto found = files_.find(Key(path));
            if (found == files_.end()) return nullptr;
            return std::make_unique<wxl::MemoryAssetFile>(std::vector<uint8_t>(found->second.begin(), found->second.end()));
        }

        void List(std::string_view folder, const std::function<void(std::string_view, bool)>& entry) override
        {
            const std::string key = Key(folder);
            if (key == "interface\\addons") entry("WxlProvided", true);
            if (key == "interface\\addons\\wxlprovided")
            {
                entry("WxlProvided.toc", false);
                entry("WxlProvided.lua", false);
            }
        }

        /// A download arrived: served under its scoped name; its base name no longer pending.
        void Arrived(const std::string& base, const std::string& scoped, std::string bytes)
        {
            std::lock_guard<std::mutex> lock(lock_);
            files_[Key(scoped)] = std::move(bytes);
            pending_.erase(Key(base));
        }

    private:
        void Add(const std::string& name, const std::string& text) { files_[Key(name)] = text; }

        std::mutex                         lock_;
        std::map<std::string, std::string> files_;
        std::set<std::string>              pending_;
    };

    Provided               g_provided;
    wxl::AssetRegistration g_provider;
    wxl::AssetRegistration g_luaTransform;
    wxl::AssetRegistration g_upper;

    /// Rewrites the provided add-on's Lua as the client reads it; every other .lua is left alone.
    bool MarkAddOn(std::string_view path, const uint8_t*, size_t, const wxl::AssetOutput& out)
    {
        if (Key(path) != "interface\\addons\\wxlprovided\\wxlprovided.lua") return false;
        static const std::string kText = "WXL_PROVIDED_ADDON = \"loaded, transformed\"\n";
        return out.Replace(kText.data(), kText.size());
    }

    bool Upper(std::string_view, const uint8_t* data, size_t size, const wxl::AssetOutput& out)
    {
        uint8_t* to = out.Replace(size);
        if (!to) return false;
        for (size_t i = 0; i < size; ++i) to[i] = static_cast<uint8_t>(std::toupper(data[i]));
        return true;
    }

    /// The last name the client could not find; FileMissing may come on a loader thread.
    struct LastMissing
    {
        std::mutex  lock;
        std::string path;
        uint32_t    kind    = 0;
        bool        open    = false;
        bool        pending = false;
        uint32_t    count   = 0;
    };

    LastMissing g_missing;

    class Missing final : public wxl::AssetScript
    {
    public:
        void OnFileMissing(std::string_view path, std::string_view, std::string_view, wxl_asset_kind kind, bool open,
                           bool pending, bool) override
        {
            // The client probes many optional files; only the selftest's own names are kept.
            if (Key(path).find("wxltest") == std::string::npos) return;
            std::lock_guard<std::mutex> lock(g_missing.lock);
            g_missing.path    = std::string(path);
            g_missing.kind    = static_cast<uint32_t>(kind);
            g_missing.open    = open;
            g_missing.pending = pending;
            ++g_missing.count;
        }
    };

    class TestAssets final : public wxl::Mod
    {
    public:
        wxl_result OnLoad() override
        {
            const wxl::StorageService storage;
            auto provider = storage.AddProvider(g_provided, 5);
            auto lua      = storage.AddTransform("lua", 0, 0, &MarkAddOn);
            auto upper    = storage.AddTransform("wxltest", 0, 0, &Upper);
            if (provider) g_provider = std::move(*provider);
            if (lua) g_luaTransform = std::move(*lua);
            if (upper) g_upper = std::move(*upper);
            wxl::Log(WXL_LOG_INFO, kTag, "provider {}, transforms {} and {}", provider ? "added" : "refused",
                     lua ? "added" : "refused", upper ? "added" : "refused");

            auto published = wxl::RelayService().Publish(
                "wxl.test-assets.fetch",
                [this](const wxl::RelayRequest& request, std::string_view method, wxl::RelayValues args) {
                    Serve(request, method, args);
                },
                [this](wxl_request) { ++cancelled_; });
            if (published) publication_ = std::move(*published);
            wxl::Log(WXL_LOG_INFO, kTag, "relay service wxl.test-assets.fetch {}", published ? "published" : "refused");
            wxl::ScriptMgr::Add(&missing_);
            return WXL_OK;
        }

        void OnShutdown() override
        {
            for (std::thread& download : downloads_)
                if (download.joinable()) download.join();
            publication_.Reset();
            g_upper.Reset();
            g_luaTransform.Reset();
            g_provider.Reset();
        }

    private:
        /// The service's methods: fetch(path, scope) downloads, never() is never answered,
        /// cancelled() counts what callers took back, missing() is the last name not found.
        void Serve(const wxl::RelayRequest& request, std::string_view method, wxl::RelayValues args)
        {
            if (method == "fetch")
            {
                const std::string path(args.String(0));
                const std::string scope(args.String(1));
                if (path.empty() || scope.empty())
                {
                    request.Fail("fetch takes a path and the scope to serve it in");
                    return;
                }
                // The download, on a thread of its own: the answer goes out from there.
                downloads_.emplace_back([request, path, scope] {
                    std::this_thread::sleep_for(std::chrono::milliseconds(300));
                    const wxl::StorageService storage;
                    const std::string         scoped = storage.ScopedName(path, scope);
                    const auto                bytes  = storage.Read(kDownloaded);
                    if (scoped.empty() || !bytes)
                    {
                        request.Fail("the download failed");
                        return;
                    }
                    g_provided.Arrived(path, scoped, std::string(bytes->begin(), bytes->end()));
                    const auto        hash = storage.HashFile(scoped);
                    const std::string md5  = hash ? hash->Hex() : std::string();
                    request.Answer({wxl::Value::String(scoped), wxl::Value::String(md5),
                                    wxl::Value::Number(static_cast<double>(bytes->size()))});
                });
                return;
            }
            if (method == "never") return;
            if (method == "cancelled")
            {
                request.Answer({wxl::Value::Number(cancelled_)});
                return;
            }
            if (method == "missing")
            {
                std::lock_guard<std::mutex> lock(g_missing.lock);
                request.Answer({wxl::Value::String(g_missing.path), wxl::Value::Number(g_missing.kind),
                                wxl::Value::Bool(g_missing.open), wxl::Value::Bool(g_missing.pending),
                                wxl::Value::Number(g_missing.count)});
                return;
            }
            request.Fail("no such method");
        }

        wxl::RelayPublication    publication_;
        std::vector<std::thread> downloads_;
        Missing                  missing_;
        int                      cancelled_ = 0;
    };
}

WXL_MOD(TestAssets, "wxl.test-assets", "Test assets")
