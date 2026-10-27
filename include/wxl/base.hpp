// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// What every C++ header of the SDK rests on: the runtime table, strings across the boundary, the
// log, the services resolved through the runtime, and the line no exception crosses.
//
// Kept apart from sdk.hpp so that the object classes, which an event handler takes as parameters,
// can be defined before sdk.hpp declares the handlers.

#ifndef WXL_BASE_HPP
#define WXL_BASE_HPP

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <type_traits>
#include <utility>

#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
    /// Whether the mod is compiled with C++ exceptions, which is the default of every toolchain.
    #define WXL_SDK_EXCEPTIONS 1
    #include <exception>
#else
    #define WXL_SDK_EXCEPTIONS 0
#endif

#if __has_include(<version>)
    #include <version>
#endif
#if defined(__cpp_lib_format) && __cpp_lib_format >= 202207L
    #include <format>
    #include <string>
#endif

#include "wxl/abi.h"
#include "wxl/host.h"

namespace wxl
{
    namespace detail
    {
        /// The runtime table, set once by WXL_MOD before any of the mod's own code runs.
        inline const wxl_host*& HostRef()
        {
            static const wxl_host* host = nullptr;
            return host;
        }

        /// The id WXL_MOD declares, set with the table.
        inline const char*& ModIdRef()
        {
            static const char* id = nullptr;
            return id;
        }
    } // namespace detail

    /// The runtime that loaded this mod.
    inline const wxl_host* Host() { return detail::HostRef(); }

    /// The mod's id, as WXL_MOD declares it and the manifest says it; "?" before the mod loads.
    inline const char* ModId()
    {
        const char* id = detail::ModIdRef();
        return id ? id : "?";
    }

    /// "WXL_OK", "WXL_E_NOT_FOUND", ...: a result as the log should show it.
    constexpr const char* ResultName(wxl_result result)
    {
        switch (result)
        {
        case WXL_OK: return "WXL_OK";
        case WXL_E_UNSUPPORTED: return "WXL_E_UNSUPPORTED";
        case WXL_E_PENDING: return "WXL_E_PENDING";
        case WXL_E_BUSY: return "WXL_E_BUSY";
        case WXL_E_VERSION: return "WXL_E_VERSION";
        case WXL_E_INVALID: return "WXL_E_INVALID";
        case WXL_E_NOT_FOUND: return "WXL_E_NOT_FOUND";
        case WXL_E_FAILED: return "WXL_E_FAILED";
        }
        return "an unknown wxl_result";
    }

    /// Wraps a NUL-terminated string for the boundary.
    inline wxl_str Str(const char* text)
    {
        return wxl_str{text, text ? static_cast<uint32_t>(std::strlen(text)) : 0u};
    }

    /// Wraps a view for the boundary. The view's storage must outlive the call it is given to.
    inline wxl_str Str(std::string_view text)
    {
        return wxl_str{text.data(), static_cast<uint32_t>(text.size())};
    }

    /// The view over a boundary string. Empty when the runtime answered with nothing.
    inline std::string_view View(wxl_str text)
    {
        return text.data ? std::string_view(text.data, text.size) : std::string_view();
    }

    /// Writes one line to the runtime's log.
    inline void Log(wxl_log_level level, const char* tag, std::string_view message)
    {
        if (const wxl_host* host = Host()) host->log(level, tag, Str(message));
    }

    /// The same for a NUL-terminated string; a null message logs an empty line.
    inline void Log(wxl_log_level level, const char* tag, const char* message)
    {
        if (const wxl_host* host = Host()) host->log(level, tag, Str(message));
    }

#if defined(__cpp_lib_format) && __cpp_lib_format >= 202207L
    /**
     * @brief Formats a line with std::format, then writes it.
     *
     *     wxl::Log(WXL_LOG_INFO, "scout", "entered map {} at {:.0f}, {:.0f}", map, x, y);
     *
     * The format is checked against the arguments when the mod compiles. A line below the
     * runtime's level is still formatted: keep a per-frame line out of a per-frame handler.
     */
    template <class First, class... Rest>
    void Log(wxl_log_level level, const char* tag, std::format_string<First, Rest...> format,
             First&& first, Rest&&... rest)
    {
        if (!Host()) return;
        const std::string line =
            std::format(format, std::forward<First>(first), std::forward<Rest>(rest)...);
        Log(level, tag, std::string_view(line));
    }
#else
    namespace detail
    {
        template <class...>
        constexpr bool kNeedsFormat = false;
    }

    /// Formats a line: available from C++20, which brings std::format.
    template <class First, class... Rest>
    void Log(wxl_log_level, const char*, std::string_view, First&&, Rest&&...)
    {
        static_assert(detail::kNeedsFormat<First, Rest...>,
                      "wxl::Log with arguments formats them with std::format, which this "
                      "standard library does not have: build the mod as C++20 or later "
                      "(wxl_add_mod does; a target of your own sets CXX_STANDARD 20)");
    }
#endif

    // --- Exceptions ----------------------------------------------------------------------
    //
    // No C++ exception crosses into the runtime (abi.h). Every place the SDK calls the mod's
    // code from the runtime's -- WXL_MOD's entry points, a script's handlers, a function
    // registered with a service -- catches what that code throws, logs it under the mod's id
    // with the exception's what(), and goes on as sdk.hpp's "What a throw costs" says.

    namespace detail
    {
        /// What a call threw, copied out so that nothing of it outlives the catch.
        struct Thrown
        {
            char what[256] = {};
            bool standard  = false; ///< a std::exception, whose what() is in `what`

            /// "threw an exception: <what>", or that it was not a std::exception.
            const char* Describe(char* buffer, size_t capacity) const
            {
                if (standard)
                    std::snprintf(buffer, capacity, "threw an exception: %s", what);
                else
                    std::snprintf(buffer, capacity, "threw something that is not a std::exception");
                return buffer;
            }
        };

        /// Calls @p f and answers whether it returned rather than threw; @p thrown says what.
        template <class F>
        bool Contain(F&& f, Thrown& thrown) noexcept
        {
#if WXL_SDK_EXCEPTIONS
            try
            {
                std::forward<F>(f)();
                return true;
            }
            catch (const std::exception& e)
            {
                thrown.standard = true;
                std::snprintf(thrown.what, sizeof thrown.what, "%s", e.what() ? e.what() : "");
            }
            catch (...)
            {
                thrown.standard = false;
            }
            return false;
#else
            (void)thrown;
            std::forward<F>(f)();
            return true;
#endif
        }

        /// Logs "<where> <what it threw><then>" as an error under the mod's id.
        inline void LogThrown(const char* where, const Thrown& thrown, const char* then)
        {
            char described[320];
            char line[640];
            std::snprintf(line, sizeof line, "%s %s%s", where,
                          thrown.Describe(described, sizeof described), then);
            Log(WXL_LOG_ERROR, ModId(), line);
        }
    } // namespace detail

    // --- Services ------------------------------------------------------------------------

    /**
     * @brief A raw service table, resolved on first use.
     *
     * Load order is not something a mod can rely on, so nothing is looked up until it is needed,
     * and a lookup that finds nothing is retried on the next use. The typed facades in
     * wxl/services/<service>.hpp wrap one of these with the name and version sealed in; this is for a
     * table those do not cover, or for reaching a raw table directly.
     *
     *     static wxl::Service<my_api> g_mine("my.service", 1);
     *     if (g_mine) g_mine->call();
     */
    template <class T>
    class Service
    {
    public:
        constexpr Service(const char* name, uint32_t version) : name_(name), version_(version) {}

        /// The table, or nullptr while nobody has published it under that name and version.
        /// Safe from any thread.
        const T* Get() const
        {
            const T* iface = iface_.load(std::memory_order_acquire);
            if (!iface)
            {
                if (const wxl_host* host = Host())
                {
                    iface = static_cast<const T*>(host->get_service(name_, version_));
                    if (iface) iface_.store(iface, std::memory_order_release);
                }
            }
            return iface;
        }

        const T* operator->() const { return Get(); }
        explicit operator bool() const { return Get() != nullptr; }

        constexpr const char* Name() const { return name_; }
        constexpr uint32_t    Version() const { return version_; }

    private:
        const char*      name_;
        uint32_t         version_;
        mutable std::atomic<const T*> iface_{nullptr};
    };

    namespace detail
    {
        /**
         * @brief Shared shape of every typed facade: a stateless object over one cached table.
         *
         * Derived names the table type and carries `kName` and `kVersion`. The table is resolved
         * on first use and shared by every instance, so a facade can be a temporary.
         */
        template <class Derived, class Api>
        class Facade
        {
        public:
            using api_type = Api;

            /// The raw table, or nullptr while the runtime does not publish the service.
            static const Api* Raw()
            {
                static Service<Api> service(Derived::kName, Derived::kVersion);
                return service.Get();
            }

            /// Whether the runtime publishes the service right now.
            static bool Available() { return Raw() != nullptr; }

            explicit operator bool() const { return Available(); }
        };
    } // namespace detail

    namespace detail
    {
        /**
         * @brief Whether a service table really answers for one of its members.
         *
         * Two ways it may not. The runtime's table can be older than this header, and stop
         * short of the member: struct_size says how far it reaches. Or the runtime can reach
         * the member and have nothing true to say through it, and leave it null -- which the
         * contract allows, and which is how a runtime tells a mod that one row of a service is
         * beyond it without refusing the whole service.
         *
         * Either way the answer is the same to a mod: unsupported.
         */
        template <class Api, class Member>
        bool Carries(const Api* api, Member Api::*member)
        {
            if (!api) return false;
            const char* base = reinterpret_cast<const char*>(api);
            const char* end  = reinterpret_cast<const char*>(&(api->*member)) + sizeof(Member);
            if (static_cast<size_t>(end - base) > api->struct_size) return false;
            return (api->*member) != nullptr;
        }
    } // namespace detail
} // namespace wxl

#endif // WXL_BASE_HPP
