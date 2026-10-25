// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The typed facade over wxl.storage: the client's files, whatever they are packed in.

#ifndef WXL_SERVICE_STORAGE_HPP
#define WXL_SERVICE_STORAGE_HPP

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "wxl/scoped_name.hpp"
#include "wxl/sdk.hpp"
#include "wxl/services/rows.hpp"
#include "wxl/services/storage.h"

namespace wxl::api::facade
{
    template <>
    struct Methods<wxl_storage_api>
    {
#define WXL_API_TABLE "wxl/services/storage.def"
#include "wxl/services/methods.inc"
    };
} // namespace wxl::api::facade

namespace wxl
{
    /**
     * @brief An open file, closed when this goes out of scope.
     *
     * Move-only. Release() hands the handle back for code that closes it by hand.
     */
    class File
    {
    public:
        File() = default;
        File(const wxl_storage_api* api, wxl_handle handle) : api_(api), handle_(handle) {}

        File(File&& other) noexcept : api_(other.api_), handle_(other.Release()) {}
        File& operator=(File&& other) noexcept
        {
            if (this != &other)
            {
                Reset();
                api_    = other.api_;
                handle_ = other.Release();
            }
            return *this;
        }

        File(const File&)            = delete;
        File& operator=(const File&) = delete;

        ~File() { Reset(); }

        explicit operator bool() const { return handle_ != WXL_HANDLE_NONE; }
        wxl_handle Get() const { return handle_; }

        /// Reads up to @p size bytes at @p offset; the value is how many were read.
        Expected<uint64_t> ReadAt(uint64_t offset, void* buffer, uint64_t size) const
        {
            if (!detail::Carries(api_, &wxl_storage_api::read_at))
                return Unexpected{WXL_E_UNSUPPORTED};
            if (handle_ == WXL_HANDLE_NONE) return Unexpected{WXL_E_NOT_FOUND};
            uint64_t         read   = 0;
            const wxl_result result = api_->read_at(handle_, offset, buffer, size, &read);
            if (result != WXL_OK) return Unexpected{result};
            return read;
        }

        /// Gives up ownership without closing.
        wxl_handle Release()
        {
            const wxl_handle handle = handle_;
            handle_                 = WXL_HANDLE_NONE;
            return handle;
        }

        /// Closes now. Safe to call more than once.
        void Reset()
        {
            if (handle_ == WXL_HANDLE_NONE) return;
            if (detail::Carries(api_, &wxl_storage_api::close)) api_->close(handle_);
            handle_ = WXL_HANDLE_NONE;
        }

    private:
        const wxl_storage_api* api_    = nullptr;
        wxl_handle             handle_ = WXL_HANDLE_NONE;
    };

    /// A file kind's first four bytes as a transform filters on them: Magic("MD20").
    constexpr uint32_t Magic(const char (&text)[5])
    {
        return static_cast<uint32_t>(static_cast<uint8_t>(text[0])) |
               static_cast<uint32_t>(static_cast<uint8_t>(text[1])) << 8 |
               static_cast<uint32_t>(static_cast<uint8_t>(text[2])) << 16 |
               static_cast<uint32_t>(static_cast<uint8_t>(text[3])) << 24;
    }

    /**
     * @brief One file a provider opened, read by the client a piece at a time.
     *
     * Calls on one file come one at a time, on whichever thread is reading it; it is destroyed
     * when the client closes it, or when its provider is taken back.
     */
    class AssetFile
    {
    public:
        virtual ~AssetFile() = default;

        /// Its length in bytes, asked once when it is opened.
        virtual uint64_t Size() const = 0;

        /// Copies up to @p size bytes at @p offset into @p buffer and answers how many: fewer
        /// only at the end. An unexpected value is a read error the client is told of.
        virtual Expected<uint32_t> ReadAt(uint64_t offset, void* buffer, uint32_t size) = 0;
    };

    /// A file held in memory, for a provider that has the bytes at hand.
    class MemoryAssetFile final : public AssetFile
    {
    public:
        explicit MemoryAssetFile(std::vector<uint8_t> bytes) : bytes_(std::move(bytes)) {}

        uint64_t Size() const override { return bytes_.size(); }

        Expected<uint32_t> ReadAt(uint64_t offset, void* buffer, uint32_t size) override
        {
            if (offset >= bytes_.size()) return uint32_t{0};
            const uint64_t left = bytes_.size() - offset;
            const uint32_t n    = left < size ? static_cast<uint32_t>(left) : size;
            std::memcpy(buffer, bytes_.data() + offset, n);
            return n;
        }

    private:
        std::vector<uint8_t> bytes_;
    };

    /// What a provider says of a name.
    enum class AssetState
    {
        Missing, ///< it has no such file
        Present, ///< it has it
        Pending, ///< it will, not yet: looked for further down meanwhile; a mod's read is told so
    };

    /**
     * @brief A source of files the client reads as it reads its archives.
     *
     * Asked on every thread that opens files, often several at once: every method must be safe
     * to call concurrently, and quick. Lookup is asked at each open and existence check the
     * provider may win, so it is a lookup; Open does the work. The object must outlive its
     * registration. An exception thrown out of a method is caught at the boundary and answers
     * "no such file" or a failed read.
     */
    class AssetProvider
    {
    public:
        virtual ~AssetProvider() = default;

        /// Whether a name is one of the provider's files. @p path has backslashes, any case.
        virtual AssetState Lookup(std::string_view path) = 0;

        /// Opens one of them; null when it cannot after all.
        virtual std::unique_ptr<AssetFile> Open(std::string_view path) = 0;

        /// The entries of a folder ("Interface\\AddOns", "" for the top): names alone, with
        /// whether each is a folder. The default lists nothing.
        virtual void List(std::string_view folder, const std::function<void(std::string_view name, bool folder)>& entry)
        {
            (void)folder;
            (void)entry;
        }
    };

    /// Where a transform writes the bytes it answers with.
    class AssetOutput
    {
    public:
        explicit AssetOutput(const wxl_asset_output* out) : out_(out) {}

        /// A buffer of @p size bytes that becomes the file once the transform returns true;
        /// null when that much memory cannot be had.
        uint8_t* Replace(size_t size) const
        {
            if (!out_ || !out_->replace) return nullptr;
            return static_cast<uint8_t*>(out_->replace(out_->context, size));
        }

        /// Replaces the file by a copy of @p bytes; false when the memory cannot be had.
        bool Replace(const void* bytes, size_t size) const
        {
            uint8_t* to = Replace(size);
            if (!to) return false;
            if (size) std::memcpy(to, bytes, size);
            return true;
        }

    private:
        const wxl_asset_output* out_;
    };

    /**
     * @brief A conversion of one kind of file: called with the bytes as read, before the client
     *        parses them.
     *
     * Called on the thread opening the file, possibly several at once: Apply must be safe to
     * call concurrently. Answer true after writing through @p out to replace the bytes, false
     * to leave them as they are. The object must outlive its registration.
     */
    class AssetTransform
    {
    public:
        virtual ~AssetTransform() = default;
        virtual bool Apply(std::string_view path, const uint8_t* data, size_t size, const AssetOutput& out) = 0;
    };

    /// A transform as a function, for AddTransform's short form.
    using TransformFn = std::function<bool(std::string_view path, const uint8_t* data, size_t size, const AssetOutput& out)>;

    namespace detail
    {
        /// The C side of a provider or a transform: the table the runtime calls, over the object.
        struct AssetBridge
        {
            AssetProvider*      provider  = nullptr;
            AssetTransform*     transform = nullptr;
            TransformFn         fn;
            wxl_asset_provider  provider_table{};
            wxl_asset_transform transform_table{};
            std::string         extension;

            static wxl_result WXL_CALL Lookup(void* user, wxl_str path)
            {
                try
                {
                    switch (static_cast<AssetBridge*>(user)->provider->Lookup(View(path)))
                    {
                    case AssetState::Present: return WXL_OK;
                    case AssetState::Pending: return WXL_E_PENDING;
                    case AssetState::Missing: break;
                    }
                    return WXL_E_NOT_FOUND;
                }
                catch (...)
                {
                    return WXL_E_NOT_FOUND;
                }
            }

            static void* WXL_CALL Open(void* user, wxl_str path, uint64_t* out_size)
            {
                try
                {
                    std::unique_ptr<AssetFile> file = static_cast<AssetBridge*>(user)->provider->Open(View(path));
                    if (!file) return nullptr;
                    if (out_size) *out_size = file->Size();
                    return file.release();
                }
                catch (...)
                {
                    return nullptr;
                }
            }

            static wxl_result WXL_CALL ReadAt(void*, void* file, uint64_t offset, void* buffer, uint32_t size,
                                              uint32_t* out_read)
            {
                try
                {
                    const Expected<uint32_t> read = static_cast<AssetFile*>(file)->ReadAt(offset, buffer, size);
                    if (!read) return read.Error();
                    if (out_read) *out_read = *read;
                    return WXL_OK;
                }
                catch (...)
                {
                    return WXL_E_FAILED;
                }
            }

            static void WXL_CALL Close(void*, void* file) { delete static_cast<AssetFile*>(file); }

            struct ListContext
            {
                wxl_asset_entry_fn fn;
                void*              context;
            };

            static void WXL_CALL List(void* user, wxl_str folder, wxl_asset_entry_fn fn, void* context)
            {
                try
                {
                    static_cast<AssetBridge*>(user)->provider->List(View(folder), [&](std::string_view name, bool is) {
                        fn(context, Str(name), is ? WXL_TRUE : WXL_FALSE);
                    });
                }
                catch (...)
                {
                }
            }

            static wxl_result WXL_CALL Apply(void* user, wxl_str path, const void* data, uint64_t size,
                                             const wxl_asset_output* out)
            {
                try
                {
                    AssetBridge* bridge = static_cast<AssetBridge*>(user);
                    const auto*  bytes  = static_cast<const uint8_t*>(data);
                    const bool   done   = bridge->transform
                                              ? bridge->transform->Apply(View(path), bytes, static_cast<size_t>(size), AssetOutput(out))
                                              : bridge->fn(View(path), bytes, static_cast<size_t>(size), AssetOutput(out));
                    return done ? WXL_OK : WXL_E_NOT_FOUND;
                }
                catch (...)
                {
                    return WXL_E_FAILED;
                }
            }
        };
    } // namespace detail

    /**
     * @brief A provider or a transform registered with wxl.storage, taken back when this goes.
     *
     * Move-only. A mod that goes takes its registrations with it anyway; holding one is how a
     * mod takes one back sooner.
     */
    class AssetRegistration
    {
    public:
        AssetRegistration() = default;
        AssetRegistration(AssetRegistration&& other) noexcept
            : api_(other.api_), handle_(other.handle_), kind_(other.kind_), bridge_(std::move(other.bridge_))
        {
            other.handle_ = 0;
        }
        AssetRegistration& operator=(AssetRegistration&& other) noexcept
        {
            if (this != &other)
            {
                Reset();
                api_    = other.api_;
                handle_ = other.handle_;
                kind_   = other.kind_;
                bridge_ = std::move(other.bridge_);
                other.handle_ = 0;
            }
            return *this;
        }
        AssetRegistration(const AssetRegistration&)            = delete;
        AssetRegistration& operator=(const AssetRegistration&) = delete;
        ~AssetRegistration() { Reset(); }

        explicit operator bool() const { return handle_ != 0; }
        uint64_t Get() const { return handle_; }

        /// Takes the registration back now; safe to call more than once.
        void Reset()
        {
            if (handle_ && api_)
            {
                if (kind_ == Kind::Provider && detail::Carries(api_, &wxl_storage_api::remove_provider))
                    api_->remove_provider(handle_);
                if (kind_ == Kind::Transform && detail::Carries(api_, &wxl_storage_api::remove_transform))
                    api_->remove_transform(handle_);
            }
            handle_ = 0;
            bridge_.reset();
        }

    private:
        friend class StorageService;
        enum class Kind
        {
            Provider,
            Transform
        };

        const wxl_storage_api*               api_    = nullptr;
        uint64_t                             handle_ = 0;
        Kind                                 kind_   = Kind::Provider;
        std::unique_ptr<detail::AssetBridge> bridge_;
    };

    /// A file's content hash, as StorageService::HashFile answers it.
    struct FileHash
    {
        uint8_t        md5[16] = {};
        uint64_t       size    = 0;
        wxl_file_layer layer   = WXL_FILE_LAYER_NONE;
        std::string    source; ///< the archive, the file on the disk, or the mod that serves it

        /// The digest as thirty-two lower-case hex digits, the way a CDN writes a content key.
        std::string Hex() const
        {
            static const char kDigits[] = "0123456789abcdef";
            std::string text(32, '0');
            for (size_t i = 0; i < 16; ++i)
            {
                text[2 * i]     = kDigits[md5[i] >> 4];
                text[2 * i + 1] = kDigits[md5[i] & 0xF];
            }
            return text;
        }

        static FileHash From(const wxl_file_hash& hash)
        {
            FileHash out;
            std::memcpy(out.md5, hash.md5, sizeof out.md5);
            out.size   = hash.size;
            out.layer  = static_cast<wxl_file_layer>(hash.layer);
            out.source = hash.source.data ? std::string(hash.source.data, hash.source.size) : std::string();
            return out;
        }
    };

    namespace detail
    {
        /// The function a HashFileAsync request answers, kept until it does.
        struct HashCallback
        {
            std::function<void(Expected<FileHash>)> fn;

            static void WXL_CALL Answer(void* user, uint64_t, wxl_result result, const wxl_file_hash* hash)
            {
                std::unique_ptr<HashCallback> self(static_cast<HashCallback*>(user));
                if (!self->fn) return;
                if (result != WXL_OK || !hash) self->fn(Unexpected{result == WXL_OK ? WXL_E_FAILED : result});
                else self->fn(FileHash::From(*hash));
            }
        };
    } // namespace detail

    /**
     * @brief The typed facade over wxl.storage.
     *
     * Paths are the client's own: backslash-separated, case-insensitive, relative to the data
     * root. Every call answers WXL_E_UNSUPPORTED while the runtime does not publish the service.
     *
     * The plain methods are rows of storage.def, and answer as storage.h says:
     *
     *   Exists(path)                        bool                whether the name resolves
     *   Size(path)                          Expected<uint64_t>  its length, without reading it
     *   AddSearchPath(directory, priority)  wxl_result          a folder joins the search: the
     *                                                           higher priority wins a name, the
     *                                                           client's own data is 0
     *   MountArchive(path, priority)        wxl_result          an archive joins the search
     *   OverrideFile(path, with)            wxl_result          `with` replaces the client's
     *                                                           `path`, for opens and existence
     *                                                           checks, until RestoreFile
     *   RestoreFile(path)                   wxl_result          the override taken back
     *
     * The rest are written here, for what C++ does better than the raw table: a whole file into
     * a vector, a File that closes itself, a list of names.
     */
    class StorageService : public detail::Facade<StorageService, wxl_storage_api>,
                           public api::facade::Methods<wxl_storage_api>
    {
    public:
        static constexpr const char* kName    = WXL_STORAGE_SERVICE;
        static constexpr uint32_t    kVersion = WXL_STORAGE_SERVICE_VERSION;

        /// A whole file.
        Expected<std::vector<uint8_t>> Read(std::string_view path) const
        {
            const wxl_storage_api* api = Raw();
            if (!detail::Carries(api, &wxl_storage_api::read))
                return Unexpected{WXL_E_UNSUPPORTED};

            const Expected<uint64_t> size = Size(path);
            if (!size) return Unexpected{size.Error()};

            std::vector<uint8_t> bytes(static_cast<size_t>(*size));
            uint64_t             read   = 0;
            wxl_result           result = api->read(Str(path), bytes.data(), bytes.size(), &read);
            if (result == WXL_E_INVALID && read > bytes.size())
            {
                // The file grew between the two calls: once more, at the size it now reports.
                bytes.resize(static_cast<size_t>(read));
                result = api->read(Str(path), bytes.data(), bytes.size(), &read);
            }
            if (result != WXL_OK) return Unexpected{result};
            bytes.resize(static_cast<size_t>(read));
            return bytes;
        }

        /// A whole file, as text.
        Expected<std::string> ReadText(std::string_view path) const
        {
            Expected<std::vector<uint8_t>> bytes = Read(path);
            if (!bytes) return Unexpected{bytes.Error()};
            return std::string(bytes->begin(), bytes->end());
        }

        /// Opens a file for streaming; an empty File means it was not found.
        File Open(std::string_view path) const
        {
            const wxl_storage_api* api = Raw();
            if (!detail::Carries(api, &wxl_storage_api::open)) return File();
            return File(api, api->open(Str(path)));
        }

        /// The paths matching a pattern, `*` standing for any run of characters.
        std::vector<std::string> Find(std::string_view pattern) const
        {
            std::vector<std::string> found;
            const wxl_storage_api*   api = Raw();
            if (!detail::Carries(api, &wxl_storage_api::find)) return found;

            const uint32_t count = api->find(Str(pattern), nullptr, 0);
            if (!count) return found;

            std::vector<wxl_str> slices(count);
            const uint32_t       written = api->find(Str(pattern), slices.data(), count);
            found.reserve(written < count ? written : count);
            for (uint32_t i = 0; i < written && i < count; ++i) found.emplace_back(View(slices[i]));
            return found;
        }

        /**
         * @brief Adds a provider at a priority (above 0: before the client's archives; 0 or
         *        below: only names the client does not have). Taken back when the registration
         *        goes, or with the mod.
         */
        Expected<AssetRegistration> AddProvider(AssetProvider& provider, int32_t priority) const
        {
            const wxl_storage_api* api = Raw();
            if (!detail::Carries(api, &wxl_storage_api::add_provider)) return Unexpected{WXL_E_UNSUPPORTED};
            auto bridge      = std::make_unique<detail::AssetBridge>();
            bridge->provider = &provider;
            wxl_asset_provider& table = bridge->provider_table;
            table.struct_size         = sizeof table;
            table.user                = bridge.get();
            table.lookup              = &detail::AssetBridge::Lookup;
            table.open                = &detail::AssetBridge::Open;
            table.read_at             = &detail::AssetBridge::ReadAt;
            table.close               = &detail::AssetBridge::Close;
            table.list                = &detail::AssetBridge::List;
            uint64_t         handle   = 0;
            const wxl_result result   = api->add_provider(&table, priority, &handle);
            if (result != WXL_OK) return Unexpected{result};
            AssetRegistration registration;
            registration.api_    = api;
            registration.handle_ = handle;
            registration.kind_   = AssetRegistration::Kind::Provider;
            registration.bridge_ = std::move(bridge);
            return registration;
        }

        /**
         * @brief Adds a transform of the files ending in @p extension ("m2", "blp"...), only
         *        those starting with @p magic unless it is 0, at a priority (highest runs first).
         */
        Expected<AssetRegistration> AddTransform(std::string_view extension, uint32_t magic, int32_t priority,
                                                 AssetTransform& transform) const
        {
            auto bridge       = std::make_unique<detail::AssetBridge>();
            bridge->transform = &transform;
            return Transform(extension, magic, priority, std::move(bridge));
        }

        /// The same with a function: `[](std::string_view path, const uint8_t* data, size_t size,
        /// const wxl::AssetOutput& out) { ...; return true; }`.
        Expected<AssetRegistration> AddTransform(std::string_view extension, uint32_t magic, int32_t priority,
                                                 TransformFn fn) const
        {
            if (!fn) return Unexpected{WXL_E_INVALID};
            auto bridge = std::make_unique<detail::AssetBridge>();
            bridge->fn  = std::move(fn);
            return Transform(extension, magic, priority, std::move(bridge));
        }

        /// Serves @p size bytes under a name, on every thread, until Unserve or the mod goes.
        wxl_result Serve(std::string_view path, const void* data, uint32_t size, int32_t priority) const
        {
            const wxl_storage_api* api = Raw();
            if (!detail::Carries(api, &wxl_storage_api::serve)) return WXL_E_UNSUPPORTED;
            return api->serve(Str(path), data, size, priority);
        }

        /// Serves text under a name.
        wxl_result Serve(std::string_view path, std::string_view text, int32_t priority) const
        {
            return Serve(path, text.data(), static_cast<uint32_t>(text.size()), priority);
        }

        /// The MD5, length and layer of a file as the client would read it, before any
        /// transform. @p fresh reads it again rather than answer from what the runtime kept.
        Expected<FileHash> HashFile(std::string_view path, bool fresh = false) const
        {
            const wxl_storage_api* api = Raw();
            if (!detail::Carries(api, &wxl_storage_api::hash_file)) return Unexpected{WXL_E_UNSUPPORTED};
            wxl_file_hash    hash{};
            hash.struct_size        = sizeof hash;
            const wxl_result result = api->hash_file(Str(path), fresh ? WXL_HASH_FRESH : 0u, &hash);
            if (result != WXL_OK) return Unexpected{result};
            return FileHash::From(hash);
        }

        /**
         * @brief HashFile on a thread of the runtime's: @p fn is called later, on the main
         *        thread, with the hash or why there is none. Answers the request's number.
         *
         * Never called if the mod goes first.
         */
        Expected<uint64_t> HashFileAsync(std::string_view path, std::function<void(Expected<FileHash>)> fn,
                                         bool fresh = false) const
        {
            const wxl_storage_api* api = Raw();
            if (!detail::Carries(api, &wxl_storage_api::hash_file_async)) return Unexpected{WXL_E_UNSUPPORTED};
            if (!fn) return Unexpected{WXL_E_INVALID};
            auto     callback = std::make_unique<detail::HashCallback>();
            callback->fn      = std::move(fn);
            uint64_t request  = 0;
            const wxl_result result = api->hash_file_async(Str(path), fresh ? WXL_HASH_FRESH : 0u,
                                                           &detail::HashCallback::Answer, callback.get(), &request);
            if (result != WXL_OK) return Unexpected{result};
            callback.release(); // the answer frees it
            return request;
        }

        /// The name of @p path in a mod's scope -- this mod's when @p mod is empty --, which the
        /// client reads as the mod's own copy when one is served, else as its own file. Empty
        /// when there is none (see wxl::ScopedName).
        std::string ScopedName(std::string_view path, std::string_view mod = {}) const
        {
            const wxl_storage_api* api = Raw();
            if (!detail::Carries(api, &wxl_storage_api::scoped_name)) return std::string();
            const wxl_str scoped = api->scoped_name(Str(mod), Str(path));
            return scoped.data ? std::string(scoped.data, scoped.size) : std::string();
        }

        /// Makes the client read again a model or a texture it holds; what it did.
        Expected<wxl_asset_reload> ReloadAsset(std::string_view path) const
        {
            const wxl_storage_api* api = Raw();
            if (!detail::Carries(api, &wxl_storage_api::reload_asset)) return Unexpected{WXL_E_UNSUPPORTED};
            wxl_asset_reload done{};
            done.struct_size        = sizeof done;
            const wxl_result result = api->reload_asset(Str(path), &done);
            if (result != WXL_OK) return Unexpected{result};
            return done;
        }

    private:
        Expected<AssetRegistration> Transform(std::string_view extension, uint32_t magic, int32_t priority,
                                              std::unique_ptr<detail::AssetBridge> bridge) const
        {
            const wxl_storage_api* api = Raw();
            if (!detail::Carries(api, &wxl_storage_api::add_transform)) return Unexpected{WXL_E_UNSUPPORTED};
            bridge->extension          = std::string(extension);
            wxl_asset_transform& table = bridge->transform_table;
            table.struct_size          = sizeof table;
            table.user                 = bridge.get();
            table.extension            = Str(bridge->extension);
            table.magic                = magic;
            table.apply                = &detail::AssetBridge::Apply;
            uint64_t         handle    = 0;
            const wxl_result result    = api->add_transform(&table, priority, &handle);
            if (result != WXL_OK) return Unexpected{result};
            AssetRegistration registration;
            registration.api_    = api;
            registration.handle_ = handle;
            registration.kind_   = AssetRegistration::Kind::Transform;
            registration.bridge_ = std::move(bridge);
            return registration;
        }
    };
} // namespace wxl

#endif // WXL_SERVICE_STORAGE_HPP
