// SPDX-License-Identifier: GPL-3.0-or-later
// Export<MyExtension> extension;: the DLL's entry points, its name and version taken from its wxl.json.

#pragma once

#include "wxl/Core.hpp"
#include "wxl/Extension.hpp"
#include "wxl/Log.hpp"
#include "wxl/detail/Manifest.hpp"

namespace wxl
{
    namespace detail
    {
        /// The one exported extension of this DLL, set when its Export is constructed.
        struct Exported
        {
            void (*start)(void* extension) = nullptr;
            void*    extension             = nullptr;
            uint32_t exports               = 0;
            bool     started               = false;
        };
        inline Exported g_exported;

        inline Identity& ThisIdentity()
        {
            static Identity identity = ReadIdentity(&g_exported);
            return identity;
        }

        inline const WXL_PluginInfo* Query()
        {
            const Identity& id = ThisIdentity();
            static const WXL_PluginInfo info = { sizeof(WXL_PluginInfo), WXL_API_VERSION, id.name, id.packed,
                                                 WXL_CLIENT_BUILD, WXL_SDK_VERSION };
            return &info;
        }

        inline int Load(const WXL_Api* api)
        {
            const Identity& id = ThisIdentity();
            if (!Bind(api, id.name, id.version)) return 0;
            if (g_exported.exports != 1)
            {
                Log::Error("{}: one Export per DLL; this one has {}", id.name, g_exported.exports);
                return 0;
            }
            g_exported.start(g_exported.extension);
            g_exported.started = true;
            return 1;
        }
    }

    /// Makes T, a class derived from Extension, the extension of this DLL: `Export<MyPanel> extension;`
    /// at namespace scope, once per DLL. The core then starts it, and stops it when the DLL unloads.
    template <class T>
    class Export
    {
        static_assert(std::is_base_of_v<Extension, T>, "Export<T>: T derives from wxl::Extension");
        static_assert(std::is_default_constructible_v<T>, "Export<T>: T needs a default constructor");

    public:
        Export()
        {
            detail::g_exported.start     = [](void* e) { detail::Lifecycle<T>::Start(*static_cast<T*>(e)); };
            detail::g_exported.extension = &m_extension;
            ++detail::g_exported.exports;
        }

        ~Export()
        {
            if (!detail::g_exported.started || detail::g_exported.extension != &m_extension) return;
            detail::g_exported.started = false;
            detail::Lifecycle<T>::Stop(m_extension);
        }

        Export(const Export&)            = delete;
        Export& operator=(const Export&) = delete;

        T&       operator*() { return m_extension; }
        T*       operator->() { return &m_extension; }
        const T* operator->() const { return &m_extension; }

    private:
        /// Defined once per Export type: a second Export in the same file does not compile.
        friend void OneExportPerDll() {}

        T m_extension;
    };
}

extern "C" WXL_EXPORT inline const WXL_PluginInfo* WXL_CDECL WXL_Query(void)
{
    return ::wxl::detail::Query();
}

extern "C" WXL_EXPORT inline int WXL_CDECL WXL_Load(const WXL_Api* api)
{
    return ::wxl::detail::Load(api);
}
