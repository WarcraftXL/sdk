// SPDX-License-Identifier: GPL-3.0-or-later
// Storage: the core's file service in C++, its providers, transforms and redirects as stateless lambdas.

#pragma once

#include "wxl/CoreServices.hpp"
#include "wxl/abi/ByteSink.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace wxl
{
    /// Where a provider or a transform writes the bytes it serves, in order.
    class ByteSink
    {
    public:
        explicit ByteSink(const WXL_ByteSink* sink) : m_sink(sink) {}

        void Write(const void* data, std::size_t size) const
        {
            if (m_sink && size) m_sink->Write(m_sink->ctx, data, static_cast<unsigned int>(size));
        }
        void Write(std::span<const uint8_t> bytes) const { Write(bytes.data(), bytes.size()); }

    private:
        const WXL_ByteSink* m_sink;
    };

    /// The core's file opens (the "wxl.storage" service): `Service<Storage> storage;`. The callbacks are
    /// lambdas without captures, called for every file the client opens: keep them cheap.
    class Storage
    {
    public:
        Storage() = default;
        explicit Storage(const WXL_StorageApi* api) : m_api(api) {}

        /// Serves a file before any archive is read: provider(name, ByteSink&) -> true when it claims it.
        template <class F>
        void AddProvider(F)
        {
            Stateless<F>();
            m_api->RegisterClientProvider([](const char* name, const WXL_ByteSink* sink) -> int {
                ByteSink out(sink);
                return F{}(name, out) ? 1 : 0;
            });
        }

        /// Reshapes every file ending in `suffix` (lowercase, ".blp"): transform(name, bytes, ByteSink&)
        /// -> true when it wrote a replacement.
        template <class F>
        void AddTransform(const char* suffix, F)
        {
            Stateless<F>();
            m_api->RegisterClientTransform(suffix, [](const char* name, const uint8_t* raw, uint32_t size,
                                                      const WXL_ByteSink* sink) -> int {
                ByteSink out(sink);
                return F{}(name, std::span<const uint8_t>(raw, size), out) ? 1 : 0;
            });
        }

        /// Opens another name: redirect(name, std::span<char> out) -> true when it wrote one in `out`.
        /// It must decline the names it produces itself.
        template <class F>
        void AddRedirect(F)
        {
            Stateless<F>();
            m_api->RegisterClientRedirect([](const char* name, char* out, uint32_t capacity) -> int {
                return F{}(name, std::span<char>(out, capacity)) ? 1 : 0;
            });
        }

    private:
        template <class F>
        static constexpr void Stateless()
        {
            static_assert(std::is_empty_v<F> && std::is_default_constructible_v<F>,
                          "Storage: the callback captures nothing (the core keeps a plain function)");
        }

        const WXL_StorageApi* m_api = nullptr;
    };

    template <>
    struct ServiceTraits<Storage>
    {
        using Table                           = WXL_StorageApi;
        static constexpr const char* kName    = ServiceTraits<WXL_StorageApi>::kName;
        static constexpr uint32_t    kVersion = ServiceTraits<WXL_StorageApi>::kVersion;
    };
}
