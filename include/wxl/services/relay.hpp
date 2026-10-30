// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The typed facade over wxl.relay: services mods publish for each other, answered later.

#ifndef WXL_SERVICE_RELAY_HPP
#define WXL_SERVICE_RELAY_HPP

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "wxl/sdk.hpp"
#include "wxl/services/relay.h"
#include "wxl/services/rows.hpp"
#include "wxl/services/script.hpp"

namespace wxl::api::facade
{
    template <>
    struct Methods<wxl_relay_api>
    {
#define WXL_API_TABLE "wxl/services/relay.def"
#include "wxl/services/methods.inc"
    };
} // namespace wxl::api::facade

namespace wxl
{
    /// The values of a request or of an answer, valid for the call that hands them over.
    class RelayValues
    {
    public:
        RelayValues(const wxl_value* values, uint32_t count) : values_(values), count_(values ? count : 0) {}

        uint32_t Count() const { return count_; }

        wxl_value_type Type(uint32_t index) const
        {
            return index < count_ ? static_cast<wxl_value_type>(values_[index].type) : WXL_VALUE_NIL;
        }

        /// The number at @p index, or @p fallback when it is not one.
        double Number(uint32_t index, double fallback = 0.0) const
        {
            return Type(index) == WXL_VALUE_NUMBER ? values_[index].as.number : fallback;
        }

        /// The string at @p index, empty when it is not one.
        std::string_view String(uint32_t index) const
        {
            if (Type(index) != WXL_VALUE_STRING || !values_[index].as.string.data) return {};
            return std::string_view(values_[index].as.string.data, values_[index].as.string.size);
        }

        bool Bool(uint32_t index) const { return Type(index) == WXL_VALUE_BOOL && values_[index].as.boolean; }

        const wxl_value* Data() const { return values_; }

    private:
        const wxl_value* values_;
        uint32_t         count_;
    };

    /// One request a published service received: answer it now or keep it and answer later.
    class RelayRequest
    {
    public:
        RelayRequest(const wxl_relay_api* api, wxl_request id) : api_(api), id_(id) {}

        wxl_request Id() const { return id_; }

        /// Answers with values (wxl::Value::Number, String, Bool, Nil), copied. Any thread, once.
        wxl_result Answer(std::initializer_list<wxl_value> values = {}) const
        {
            return api_ ? api_->answer(id_, values.size() ? values.begin() : nullptr, static_cast<uint32_t>(values.size()))
                        : WXL_E_UNSUPPORTED;
        }

        wxl_result Answer(const std::vector<wxl_value>& values) const
        {
            return api_ ? api_->answer(id_, values.data(), static_cast<uint32_t>(values.size())) : WXL_E_UNSUPPORTED;
        }

        /// Fails the request with a reason. Any thread, once.
        wxl_result Fail(std::string_view reason) const
        {
            return api_ ? api_->fail(id_, wxl_str{reason.data(), static_cast<uint32_t>(reason.size())})
                        : WXL_E_UNSUPPORTED;
        }

    private:
        const wxl_relay_api* api_;
        wxl_request          id_;
    };

    /// What a published service does with a request: on the main thread.
    using RelayHandler = std::function<void(const RelayRequest& request, std::string_view method, RelayValues args)>;

    /// A request no answer is wanted for any more.
    using RelayCancelled = std::function<void(wxl_request request)>;

    /// What a caller is told: WXL_OK and the answer, WXL_E_FAILED and the reason as one string,
    /// WXL_E_NOT_FOUND when the service is not there or went.
    using RelayReply = std::function<void(wxl_result result, RelayValues values)>;

    namespace detail
    {
        struct RelayEndpointBridge
        {
            const wxl_relay_api* api = nullptr;
            RelayHandler         handler;
            RelayCancelled       cancelled;
            wxl_relay_endpoint   table{};

            static void WXL_CALL Request(void* user, wxl_request request, wxl_str method, const wxl_value* args,
                                         uint32_t count)
            {
                auto& self = *static_cast<RelayEndpointBridge*>(user);
                if (!self.handler) return;
                self.handler(RelayRequest(self.api, request),
                             std::string_view(method.data ? method.data : "", method.data ? method.size : 0),
                             RelayValues(args, count));
            }

            static void WXL_CALL Cancelled(void* user, wxl_request request)
            {
                auto& self = *static_cast<RelayEndpointBridge*>(user);
                if (self.cancelled) self.cancelled(request);
            }
        };

        /// The replies a mod waits for: each is freed when its answer comes, or when the mod
        /// cancels its request.
        class RelayReplies
        {
        public:
            static RelayReplies& Instance()
            {
                static RelayReplies replies;
                return replies;
            }

            /// Keeps a reply before its call, its request not known yet.
            void Keep(RelayReply* reply)
            {
                std::lock_guard<std::mutex> lock(lock_);
                waiting_[reply] = 0;
            }

            /// Notes the request a kept reply waits for, unless its answer came already.
            void Number(RelayReply* reply, wxl_request request)
            {
                std::lock_guard<std::mutex> lock(lock_);
                const auto found = waiting_.find(reply);
                if (found != waiting_.end()) found->second = request;
            }

            /// Forgets a reply and hands it back to free; null when it is not kept.
            std::unique_ptr<RelayReply> Forget(RelayReply* reply)
            {
                std::lock_guard<std::mutex> lock(lock_);
                if (!waiting_.erase(reply)) return nullptr;
                return std::unique_ptr<RelayReply>(reply);
            }

            /// The reply waiting for a request, forgotten; null for none.
            std::unique_ptr<RelayReply> ForgetRequest(wxl_request request)
            {
                std::lock_guard<std::mutex> lock(lock_);
                for (auto it = waiting_.begin(); it != waiting_.end(); ++it)
                {
                    if (it->second != request) continue;
                    RelayReply* reply = it->first;
                    waiting_.erase(it);
                    return std::unique_ptr<RelayReply>(reply);
                }
                return nullptr;
            }

            static void WXL_CALL Answer(void* user, wxl_request, wxl_result result, const wxl_value* values,
                                        uint32_t count)
            {
                const std::unique_ptr<RelayReply> reply = Instance().Forget(static_cast<RelayReply*>(user));
                if (reply && *reply) (*reply)(result, RelayValues(values, count));
            }

        private:
            std::mutex                                   lock_;
            std::unordered_map<RelayReply*, wxl_request> waiting_;
        };
    } // namespace detail

    /**
     * @brief A service the mod publishes, taken back when this goes out of scope.
     *
     * Move-only; Reset() takes it back early.
     */
    class RelayPublication
    {
    public:
        RelayPublication() = default;
        RelayPublication(RelayPublication&&) noexcept            = default;
        RelayPublication& operator=(RelayPublication&& other) noexcept
        {
            if (this != &other)
            {
                Reset();
                name_   = std::move(other.name_);
                bridge_ = std::move(other.bridge_);
            }
            return *this;
        }
        RelayPublication(const RelayPublication&)            = delete;
        RelayPublication& operator=(const RelayPublication&) = delete;
        ~RelayPublication() { Reset(); }

        explicit operator bool() const { return bridge_ != nullptr; }
        const std::string& Name() const { return name_; }

        void Reset()
        {
            if (!bridge_) return;
            if (bridge_->api) bridge_->api->unpublish(wxl_str{name_.data(), static_cast<uint32_t>(name_.size())});
            bridge_.reset();
            name_.clear();
        }

    private:
        friend class RelayService;
        std::string                                  name_;
        std::unique_ptr<detail::RelayEndpointBridge> bridge_;
    };

    /**
     * @brief The typed facade over wxl.relay.
     *
     *   Published(name)   bool   whether some mod publishes a service under the name
     *
     * The rest are written here: Publish takes a function and gives a RelayPublication, Call
     * takes a function the answer comes to.
     */
    class RelayService : public detail::Facade<RelayService, wxl_relay_api>, public api::facade::Methods<wxl_relay_api>
    {
    public:
        static constexpr const char* kName    = WXL_RELAY_SERVICE;
        static constexpr uint32_t    kVersion = WXL_RELAY_SERVICE_VERSION;

        /// Publishes a service: @p handler gets each request, on the main thread.
        Expected<RelayPublication> Publish(std::string_view name, RelayHandler handler,
                                           RelayCancelled cancelled = {}) const
        {
            const wxl_relay_api* api = Raw();
            if (!detail::Carries(api, &wxl_relay_api::publish)) return Unexpected{WXL_E_UNSUPPORTED};
            if (!handler) return Unexpected{WXL_E_INVALID};
            auto bridge               = std::make_unique<detail::RelayEndpointBridge>();
            bridge->api               = api;
            bridge->handler           = std::move(handler);
            bridge->cancelled         = std::move(cancelled);
            bridge->table.struct_size = sizeof bridge->table;
            bridge->table.user        = bridge.get();
            bridge->table.request     = &detail::RelayEndpointBridge::Request;
            bridge->table.cancelled   = &detail::RelayEndpointBridge::Cancelled;
            const wxl_result result   = api->publish(Str(name), &bridge->table);
            if (result != WXL_OK) return Unexpected{result};
            RelayPublication publication;
            publication.name_   = std::string(name);
            publication.bridge_ = std::move(bridge);
            return publication;
        }

        /// Asks a service; @p reply is called later, on the main thread. Answers the request's
        /// number.
        Expected<wxl_request> Call(std::string_view name, std::string_view method, std::initializer_list<wxl_value> args,
                                   RelayReply reply) const
        {
            return Call(name, method, args.size() ? args.begin() : nullptr, static_cast<uint32_t>(args.size()),
                        std::move(reply));
        }

        Expected<wxl_request> Call(std::string_view name, std::string_view method, const wxl_value* args, uint32_t count,
                                   RelayReply reply) const
        {
            const wxl_relay_api* api = Raw();
            if (!detail::Carries(api, &wxl_relay_api::call)) return Unexpected{WXL_E_UNSUPPORTED};
            if (!reply) return Unexpected{WXL_E_INVALID};
            auto&       replies = detail::RelayReplies::Instance();
            RelayReply* kept    = new RelayReply(std::move(reply));
            replies.Keep(kept);
            wxl_request      request = 0;
            const wxl_result result  = api->call(Str(name), Str(method), args, count, &detail::RelayReplies::Answer,
                                                 kept, &request);
            if (result != WXL_OK)
            {
                replies.Forget(kept);
                return Unexpected{result};
            }
            replies.Number(kept, request);
            return request;
        }

        /// Answers a request this mod's service received.
        wxl_result Answer(wxl_request request, std::initializer_list<wxl_value> values = {}) const
        {
            return RelayRequest(Raw(), request).Answer(values);
        }

        /// Fails a request this mod's service received.
        wxl_result Fail(wxl_request request, std::string_view reason) const
        {
            return RelayRequest(Raw(), request).Fail(reason);
        }

        /// Takes back a request this mod made: its reply is never called.
        wxl_result Cancel(wxl_request request) const
        {
            const wxl_relay_api* api = Raw();
            if (!detail::Carries(api, &wxl_relay_api::cancel)) return WXL_E_UNSUPPORTED;
            const wxl_result result = api->cancel(request);
            if (result == WXL_OK) detail::RelayReplies::Instance().ForgetRequest(request);
            return result;
        }
    };
} // namespace wxl

#endif // WXL_SERVICE_RELAY_HPP
