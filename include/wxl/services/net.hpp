// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The typed facade over wxl.net: opcode numbers a mod claims, the messages of them it receives
// through a bounds-checked reader, and the ones it sends, written by a bounded writer.

#ifndef WXL_SERVICE_NET_HPP
#define WXL_SERVICE_NET_HPP

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "wxl/sdk.hpp"
#include "wxl/services/net.h"

namespace wxl
{
    /**
     * @brief Reads a message's body: little-endian numbers, strings, packed guids.
     *
     * A read past the end puts the reader in failure and answers zeros (an empty string, no
     * bytes) from then on, so a handler reads the whole message and checks Ok() once. The bytes
     * are the message's: valid while the handler runs.
     */
    class ByteReader
    {
    public:
        ByteReader() = default;
        ByteReader(const void* data, size_t size) : data_(static_cast<const uint8_t*>(data)), size_(data ? size : 0) {}

        uint8_t  U8() { return Read<uint8_t>(); }
        uint16_t U16() { return Read<uint16_t>(); }
        uint32_t U32() { return Read<uint32_t>(); }
        uint64_t U64() { return Read<uint64_t>(); }
        int8_t   I8() { return Read<int8_t>(); }
        int16_t  I16() { return Read<int16_t>(); }
        int32_t  I32() { return Read<int32_t>(); }
        int64_t  I64() { return Read<int64_t>(); }
        /// The four bytes as an IEEE float, bit for bit.
        float  F32() { return Read<float>(); }
        double F64() { return Read<double>(); }
        bool   Bool() { return U8() != 0; }

        /// Three floats: x, y, z.
        wxl_vec3 Vec3()
        {
            wxl_vec3 v{};
            v.x = F32();
            v.y = F32();
            v.z = F32();
            return v;
        }

        /// A guid as the server packs it: a mask byte, then the non-zero bytes it marks.
        uint64_t PackedGuid()
        {
            const uint8_t mask  = U8();
            uint64_t      value = 0;
            for (int i = 0; i < 8; ++i)
                if (mask & (1u << i)) value |= static_cast<uint64_t>(U8()) << (8 * i);
            return value;
        }

        /**
         * @brief A string ended by a zero byte, as the server writes one; at most @p max bytes
         *        before it.
         *
         * No zero within @p max bytes (or before the end) fails the reader.
         */
        std::string_view String(size_t max = 4096)
        {
            if (failed_) return {};
            const size_t left  = size_ - at_;
            const size_t limit = left < max + 1 ? left : max + 1;
            const void*  zero  = limit ? std::memchr(data_ + at_, 0, limit) : nullptr;
            if (!zero)
            {
                Fail();
                return {};
            }
            const size_t length = static_cast<size_t>(static_cast<const uint8_t*>(zero) - (data_ + at_));
            const std::string_view text(reinterpret_cast<const char*>(data_ + at_), length);
            at_ += length + 1;
            return text;
        }

        /// The next @p count bytes, or none and a failure past the end.
        std::string_view Bytes(size_t count)
        {
            if (failed_ || count > size_ - at_)
            {
                Fail();
                return {};
            }
            const std::string_view bytes(reinterpret_cast<const char*>(data_ + at_), count);
            at_ += count;
            return bytes;
        }

        /// Steps over @p count bytes.
        void Skip(size_t count) { (void)Bytes(count); }

        /// Whether every read so far stayed inside the message.
        bool Ok() const { return !failed_; }
        bool Failed() const { return failed_; }
        /// Whether every byte was read, and nothing past.
        bool           AtEnd() const { return !failed_ && at_ == size_; }
        size_t         Position() const { return at_; }
        size_t         Remaining() const { return failed_ ? 0 : size_ - at_; }
        size_t         Size() const { return size_; }
        const uint8_t* Data() const { return data_; }

    private:
        template <class T>
        T Read()
        {
            static_assert(std::is_trivially_copyable_v<T>, "a wire value is plain data");
            T value{};
            if (failed_ || sizeof(T) > size_ - at_)
            {
                Fail();
                return value;
            }
            std::memcpy(&value, data_ + at_, sizeof(T));
            at_ += sizeof(T);
            return value;
        }

        void Fail()
        {
            failed_ = true;
            at_     = size_;
        }

        const uint8_t* data_   = nullptr;
        size_t         size_   = 0;
        size_t         at_     = 0;
        bool           failed_ = false;
    };

    /**
     * @brief Writes a message's body, little-endian, up to WXL_NET_MAX_SEND bytes.
     *
     * Each call returns the writer, to chain. A write that would go past the limit is refused,
     * puts the writer in failure and writes nothing more; Send refuses a failed writer.
     */
    class ByteWriter
    {
    public:
        explicit ByteWriter(size_t limit = WXL_NET_MAX_SEND) : limit_(limit) {}

        ByteWriter& U8(uint8_t v) { return Put(v); }
        ByteWriter& U16(uint16_t v) { return Put(v); }
        ByteWriter& U32(uint32_t v) { return Put(v); }
        ByteWriter& U64(uint64_t v) { return Put(v); }
        ByteWriter& I8(int8_t v) { return Put(v); }
        ByteWriter& I16(int16_t v) { return Put(v); }
        ByteWriter& I32(int32_t v) { return Put(v); }
        ByteWriter& I64(int64_t v) { return Put(v); }
        ByteWriter& F32(float v) { return Put(v); }
        ByteWriter& F64(double v) { return Put(v); }
        ByteWriter& Bool(bool v) { return Put(static_cast<uint8_t>(v ? 1 : 0)); }
        ByteWriter& Vec3(const wxl_vec3& v) { return F32(v.x).F32(v.y).F32(v.z); }

        /// A guid packed as the server packs one: a mask byte, then its non-zero bytes.
        ByteWriter& PackedGuid(uint64_t guid)
        {
            uint8_t packed[9] = {};
            size_t  n         = 1;
            for (int i = 0; i < 8; ++i)
            {
                const uint8_t b = static_cast<uint8_t>(guid >> (8 * i));
                if (!b) continue;
                packed[0] |= static_cast<uint8_t>(1u << i);
                packed[n++] = b;
            }
            return Bytes(packed, n);
        }

        /// The text and a zero byte after it, as the server reads a string.
        ByteWriter& String(std::string_view text)
        {
            if (!Fits(text.size() + 1)) return *this;
            bytes_.insert(bytes_.end(), text.begin(), text.end());
            bytes_.push_back(0);
            return *this;
        }

        ByteWriter& Bytes(const void* data, size_t size)
        {
            if (!Fits(size)) return *this;
            const uint8_t* at = static_cast<const uint8_t*>(data);
            bytes_.insert(bytes_.end(), at, at + size);
            return *this;
        }

        bool           Ok() const { return !failed_; }
        bool           Failed() const { return failed_; }
        size_t         Size() const { return bytes_.size(); }
        const uint8_t* Data() const { return bytes_.data(); }
        void           Clear()
        {
            bytes_.clear();
            failed_ = false;
        }

    private:
        bool Fits(size_t size)
        {
            if (failed_ || size > limit_ - bytes_.size())
            {
                failed_ = true;
                return false;
            }
            return true;
        }

        template <class T>
        ByteWriter& Put(T value)
        {
            if (!Fits(sizeof(T))) return *this;
            const size_t at = bytes_.size();
            bytes_.resize(at + sizeof(T));
            std::memcpy(bytes_.data() + at, &value, sizeof(T));
            return *this;
        }

        std::vector<uint8_t> bytes_;
        size_t               limit_  = WXL_NET_MAX_SEND;
        bool                 failed_ = false;
    };

    /**
     * @brief A claimed opcode, given back when this goes out of scope.
     *
     * Move-only. Release() keeps the claim for code that gives it back by hand.
     */
    class OpcodeClaim
    {
    public:
        OpcodeClaim() = default;
        OpcodeClaim(const wxl_net_api* api, uint32_t opcode) : api_(api), opcode_(opcode), live_(true) {}

        OpcodeClaim(OpcodeClaim&& other) noexcept
            : api_(other.api_), opcode_(other.opcode_), live_(other.Release())
        {
        }
        OpcodeClaim& operator=(OpcodeClaim&& other) noexcept
        {
            if (this != &other)
            {
                Reset();
                api_    = other.api_;
                opcode_ = other.opcode_;
                live_   = other.Release();
            }
            return *this;
        }

        OpcodeClaim(const OpcodeClaim&)            = delete;
        OpcodeClaim& operator=(const OpcodeClaim&) = delete;

        ~OpcodeClaim() { Reset(); }

        explicit operator bool() const { return live_; }
        uint32_t Opcode() const { return opcode_; }

        /// Gives up ownership without releasing the opcode. True when there was a claim.
        bool Release()
        {
            const bool was = live_;
            live_          = false;
            return was;
        }

        /// Gives the opcode back now. Safe to call more than once.
        void Reset()
        {
            if (!live_) return;
            if (api_) api_->release_opcode(opcode_);
            live_ = false;
        }

    private:
        const wxl_net_api* api_    = nullptr;
        uint32_t           opcode_ = 0;
        bool               live_   = false;
    };

    /**
     * @brief The typed facade over wxl.net.
     *
     * Every call answers WXL_E_UNSUPPORTED while the runtime does not publish the service.
     */
    class NetService : public detail::Facade<NetService, wxl_net_api>
    {
    public:
        static constexpr const char* kName    = WXL_NET_SERVICE;
        static constexpr uint32_t    kVersion = WXL_NET_SERVICE_VERSION;

        /// WXL_NET_OFFLINE without the service.
        wxl_net_state State() const
        {
            const wxl_net_api* api = Raw();
            return api ? static_cast<wxl_net_state>(api->state()) : WXL_NET_OFFLINE;
        }

        /// Sends a body under a claimed opcode; the runtime frames it.
        wxl_result Send(uint32_t opcode, const void* data, uint32_t size) const
        {
            const wxl_net_api* api = Raw();
            if (!api) return WXL_E_UNSUPPORTED;
            return api->send(opcode, data, size);
        }

        /// Sends what a writer holds; a failed writer is WXL_E_INVALID.
        wxl_result Send(uint32_t opcode, const ByteWriter& body) const
        {
            if (body.Failed()) return WXL_E_INVALID;
            return Send(opcode, body.Data(), static_cast<uint32_t>(body.Size()));
        }

        /// Sends a trivially copyable struct as the body, byte for byte.
        template <class Body, class = std::enable_if_t<!std::is_same_v<Body, ByteWriter>>>
        wxl_result Send(uint32_t opcode, const Body& body) const
        {
            static_assert(std::is_trivially_copyable_v<Body>, "a wire body is plain data");
            return Send(opcode, &body, static_cast<uint32_t>(sizeof(Body)));
        }

        /// Claims an opcode the client does not handle. WXL_E_INVALID when another mod has it.
        Expected<OpcodeClaim> Claim(uint32_t opcode) const
        {
            const wxl_net_api* api = Raw();
            if (!api) return Unexpected{WXL_E_UNSUPPORTED};
            const wxl_result result = api->claim_opcode(opcode);
            if (result != WXL_OK) return Unexpected{result};
            return OpcodeClaim(api, opcode);
        }

        /// The highest opcode the client itself uses. Anything above is free to claim.
        Expected<uint32_t> ReservedOpcodeLimit() const
        {
            const wxl_net_api* api = Raw();
            if (!api) return Unexpected{WXL_E_UNSUPPORTED};
            return api->reserved_opcode_limit();
        }

        /// Sets the raw function a claimed opcode's messages go to; NetOpcodes does it for you.
        wxl_result SetHandler(uint32_t opcode, wxl_net_handler_fn handler, void* user) const
        {
            const wxl_net_api* api = Raw();
            if (!detail::Carries(api, &wxl_net_api::set_handler)) return WXL_E_UNSUPPORTED;
            return api->set_handler(opcode, handler, user);
        }

        /// Makes @p compressed the compressed form of @p opcode (u32 size and zlib); 0 undoes it.
        wxl_result SetCompressed(uint32_t opcode, uint32_t compressed) const
        {
            const wxl_net_api* api = Raw();
            if (!detail::Carries(api, &wxl_net_api::set_compressed)) return WXL_E_UNSUPPORTED;
            return api->set_compressed(opcode, compressed);
        }

        /// The largest body Send takes; WXL_NET_MAX_SEND on a runtime that does not say.
        uint32_t MaxSend() const
        {
            const wxl_net_api* api = Raw();
            return detail::Carries(api, &wxl_net_api::max_send) ? api->max_send() : WXL_NET_MAX_SEND;
        }

        /// The world connection, counted; 0 while there is none.
        uint32_t Connection() const
        {
            const wxl_net_api* api = Raw();
            return detail::Carries(api, &wxl_net_api::connection) ? api->connection() : 0u;
        }

        /// Whether the server has sent this mod one of its numbers on this connection.
        bool Heard() const
        {
            const wxl_net_api* api = Raw();
            return detail::Carries(api, &wxl_net_api::heard) && api->heard() == WXL_TRUE;
        }

        /// A zlib stream of the bytes.
        Expected<std::vector<uint8_t>> Deflate(const void* data, uint32_t size) const
        {
            return Squeeze(&wxl_net_api::deflate, data, size);
        }

        /// The bytes a zlib stream holds.
        Expected<std::vector<uint8_t>> Inflate(const void* data, uint32_t size) const
        {
            return Squeeze(&wxl_net_api::inflate, data, size);
        }

    private:
        using Zlib = wxl_result(WXL_CALL*)(const void*, uint32_t, void*, uint64_t, uint64_t*);

        Expected<std::vector<uint8_t>> Squeeze(Zlib wxl_net_api::*member, const void* data, uint32_t size) const
        {
            const wxl_net_api* api = Raw();
            if (!detail::Carries(api, member)) return Unexpected{WXL_E_UNSUPPORTED};
            std::vector<uint8_t> out;
            uint8_t              none   = 0;
            uint64_t             needed = 0;
            wxl_result           result = (api->*member)(data, size, &none, 0, &needed);
            if (result == WXL_E_INVALID && needed)
            {
                out.resize(static_cast<size_t>(needed));
                result = (api->*member)(data, size, out.data(), needed, &needed);
            }
            if (result != WXL_OK) return Unexpected{result};
            out.resize(static_cast<size_t>(needed));
            return out;
        }
    };

    /**
     * @brief The opcodes a mod speaks: claims, handlers and sends, given back at destruction.
     *
     * ```cpp
     * net_.Handle(0x524, [this](wxl::ByteReader& in) { OnUpdate(in); });
     * net_.Handle(0x529, [this](wxl::ByteReader& in) { OnSync(in); }, 0x52A); // 0x52A: compressed
     * net_.Claim(0x521);
     * net_.Send(0x521, wxl::ByteWriter{}.U32(seq).F32(height));
     * ```
     *
     * A handler that leaves its reader failed is logged once per number, "malformed 0x524 from
     * the server". One that throws is logged and the message dropped; the mod goes on. Not
     * copyable or movable: the runtime holds pointers into it.
     */
    class NetOpcodes
    {
    public:
        using Handler = std::function<void(ByteReader&)>;

        NetOpcodes() = default;
        NetOpcodes(const NetOpcodes&)            = delete;
        NetOpcodes& operator=(const NetOpcodes&) = delete;
        ~NetOpcodes() { ReleaseAll(); }

        /// Claims a number to send, or to receive once a handler is set.
        wxl_result Claim(uint32_t opcode)
        {
            const wxl_net_api* api = NetService::Raw();
            if (!api) return WXL_E_UNSUPPORTED;
            if (entries_.count(opcode)) return WXL_OK;
            const wxl_result result = api->claim_opcode(opcode);
            if (result != WXL_OK) return result;
            auto entry    = std::make_unique<Entry>();
            entry->opcode = opcode;
            entries_.emplace(opcode, std::move(entry));
            return WXL_OK;
        }

        /**
         * @brief Claims @p opcode (and @p compressed, when not 0) and hands its messages to
         *        @p handler, on the main thread.
         */
        wxl_result Handle(uint32_t opcode, Handler handler, uint32_t compressed = 0)
        {
            wxl_result result = Claim(opcode);
            if (result != WXL_OK) return result;
            Entry& entry  = *entries_.at(opcode);
            entry.handler = std::move(handler);
            result        = NetService{}.SetHandler(opcode, &Dispatch, &entry);
            if (result != WXL_OK || !compressed) return result;
            if ((result = Claim(compressed)) != WXL_OK) return result;
            return NetService{}.SetCompressed(opcode, compressed);
        }

        wxl_result Send(uint32_t opcode, const ByteWriter& body) const { return NetService{}.Send(opcode, body); }
        wxl_result Send(uint32_t opcode, const void* data, uint32_t size) const
        {
            return NetService{}.Send(opcode, data, size);
        }

        /// Whether the server has spoken to this mod on this connection: Send is refused before.
        bool Heard() const { return NetService{}.Heard(); }

        /// Gives one number back.
        void Release(uint32_t opcode)
        {
            const auto found = entries_.find(opcode);
            if (found == entries_.end()) return;
            if (const wxl_net_api* api = NetService::Raw()) api->release_opcode(opcode);
            entries_.erase(found);
        }

        /// Gives every number back.
        void ReleaseAll()
        {
            if (const wxl_net_api* api = NetService::Raw())
                for (const auto& [opcode, entry] : entries_) api->release_opcode(opcode);
            entries_.clear();
        }

    private:
        struct Entry
        {
            uint32_t opcode = 0;
            Handler  handler;
            bool     warned = false;
        };

        static void WXL_CALL Dispatch(void* user, uint32_t opcode, const void* data, uint32_t size)
        {
            Entry& entry = *static_cast<Entry*>(user);
            if (!entry.handler) return;
            ByteReader     reader(data, size);
            detail::Thrown thrown;
            if (!detail::Contain([&] { entry.handler(reader); }, thrown))
            {
                char where[64];
                std::snprintf(where, sizeof where, "the wxl.net handler of 0x%X", static_cast<unsigned>(opcode));
                detail::LogThrown(where, thrown, "; that message was dropped, and the mod goes on");
                return;
            }
            if (reader.Failed() && !entry.warned)
            {
                entry.warned = true;
                char line[128];
                std::snprintf(line, sizeof line, "malformed 0x%X from the server: its handler read past the end of %u bytes",
                              static_cast<unsigned>(opcode), static_cast<unsigned>(size));
                Log(WXL_LOG_WARN, ModId(), line);
            }
        }

        std::unordered_map<uint32_t, std::unique_ptr<Entry>> entries_;
    };
} // namespace wxl

#endif // WXL_SERVICE_NET_HPP
