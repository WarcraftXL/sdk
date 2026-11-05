// SPDX-License-Identifier: GPL-3.0-or-later
// Services: tables of functions the core or an extension publishes, found by type through ServiceTraits.

#pragma once

#include "wxl/Core.hpp"
#include "wxl/Listener.hpp"

#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>

namespace wxl
{
    /// What the header of a service declares for its C++ type T: the C table behind it, its name and
    /// version.
    ///     template <> struct wxl::ServiceTraits<Overlay>
    ///     { using Table = WXL_OverlayApi; static constexpr const char* kName = "wxl.overlay"; static constexpr uint32_t kVersion = 1; };
    /// T is the table itself, or a class constructible from `const Table*` that wraps it.
    template <class T>
    struct ServiceTraits;

    namespace detail
    {
        template <class T>
        inline constexpr bool kWrapped = !std::is_same_v<T, typename ServiceTraits<T>::Table>;
    }

    /// A service, looked up on first use and kept once found: the highest published version at or above
    /// the traits' one. `Service<Overlay> overlay;` then overlay->AddPanel(...), or OnReady.
    template <class T>
    class Service
    {
    public:
        using Traits = ServiceTraits<T>;
        using Table  = typename Traits::Table;
        /// What Get() points at: the wrapper, or the const table.
        using Value = std::conditional_t<detail::kWrapped<T>, T, const Table>;

        constexpr Service() {}
        Service(const Service&)            = delete;
        Service& operator=(const Service&) = delete;
        ~Service() { Forget(); }

        /// The service; null while nobody has published it.
        Value* Get() const
        {
            if (!m_table && Api()) m_table = static_cast<const Table*>(Api()->GetInterface(Traits::kName, Traits::kVersion));
            if (!m_table) return nullptr;
            if constexpr (detail::kWrapped<T>)
            {
                if (!m_bound)
                {
                    m_wrapper = T(m_table);
                    m_bound   = true;
                }
                return &m_wrapper;
            }
            else return m_table;
        }

        explicit operator bool() const { return Get() != nullptr; }
        Value* operator->() const { return Get(); }

        static constexpr const char* Name() { return Traits::kName; }
        static constexpr uint32_t    Version() { return Traits::kVersion; }

        /// Calls ready(service) once it is available: now, or when it is published (checked when every
        /// extension has loaded, then each frame). One callback per Service; call it from OnStart.
        template <class F>
        void OnReady(F&& ready)
        {
            using Fn = std::decay_t<F>;
            static_assert(std::is_invocable_v<Fn&, Value&>, "OnReady: the callback takes the service, `auto&`");
            static_assert(sizeof(Fn) <= detail::kCaptureSize,
                          "OnReady: the callback's captures take more than 32 bytes; capture a pointer to the state instead");
            static_assert(alignof(Fn) <= 8, "OnReady: the callback's captures need more than 8-byte alignment");
            if (Value* service = Get())
            {
                ready(*service);
                return;
            }
            Forget();
            ::new (static_cast<void*>(m_ready)) Fn(std::forward<F>(ready));
            m_call    = [](void* f, Value& service) { (*std::launder(static_cast<Fn*>(f)))(service); };
            m_destroy = [](void* f) { std::launder(static_cast<Fn*>(f))->~Fn(); };
            m_loaded  = On<ExtensionsLoaded>([this] { Poll(); });
            m_tick    = On<Update>([this] { Poll(); });
        }

    private:
        void Poll()
        {
            Value* service = Get();
            if (!service || !m_call) return;
            auto call = std::exchange(m_call, nullptr);
            m_loaded.Stop();
            m_tick.Stop();
            call(m_ready, *service);
            Forget();
        }

        void Forget()
        {
            m_loaded.Stop();
            m_tick.Stop();
            m_call = nullptr;
            if (auto destroy = std::exchange(m_destroy, nullptr)) destroy(m_ready);
        }

        using Wrapper = std::conditional_t<detail::kWrapped<T>, T, char>;

        mutable const Table* m_table = nullptr;
        mutable Wrapper      m_wrapper{};
        mutable bool         m_bound = false;
        alignas(8) unsigned char m_ready[detail::kCaptureSize];
        void (*m_call)(void*, Value&) = nullptr;
        void (*m_destroy)(void*)      = nullptr;
        Listener m_loaded, m_tick;
    };

    /// Offers `table` (which must outlive every user) as the service T; false before the extension starts.
    template <class T>
    bool Publish(const typename ServiceTraits<T>::Table* table)
    {
        if (!Api() || !table) return false;
        Api()->PublishInterface(ServiceTraits<T>::kName, ServiceTraits<T>::kVersion,
                                const_cast<typename ServiceTraits<T>::Table*>(table));
        return true;
    }
}
