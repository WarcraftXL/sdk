// SPDX-License-Identifier: GPL-3.0-or-later
// wxl::Object: any resident client object, as a handle the core's objects service resolves on every call.

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#include "wxl/abi/Objects.h"
#include "wxl/Result.hpp"
#include "wxl/CoreServices.hpp"
#include "wxl/Signal.hpp"
#include "wxl/game/Pick.hpp"
#include "wxl/game/World.hpp"

namespace wxl
{
    using game::world::Vec3;

    class Unit;
    class Player;
    class GameObject;

    namespace detail
    {
        /// The core's objects service, looked up on first use.
        inline const WXL_ObjectsApi* Objects()
        {
            static const Service<WXL_ObjectsApi> service;
            return service.Get();
        }

        inline Error ToError(int status) { return static_cast<Error>(status); }

        /// The client object behind `handle`, if it is still resident and of a kind in `typeMask`.
        inline Result<void*> Resolve(WXL_Handle handle, uint32_t typeMask)
        {
            if (handle.generation == 0) return Error::NullHandle;
            const WXL_ObjectsApi* api = Objects();
            if (!api) return Error::Unsupported;
            void* object = nullptr;
            if (const int status = api->Resolve(handle, typeMask, &object)) return ToError(status);
            return object;
        }

        inline WXL_Handle HandleOf(uint64_t guid)
        {
            WXL_Handle handle{};
            if (const WXL_ObjectsApi* api = Objects(); api && guid) api->FromGuid(guid, &handle);
            return handle;
        }

        inline WXL_Handle HandleOf(void* object)
        {
            WXL_Handle handle{};
            if (const WXL_ObjectsApi* api = Objects(); api && object) api->FromPointer(object, &handle);
            return handle;
        }
    }

    /// Any resident object (creature, player, game object, item): a value to copy, keep and compare.
    /// Once the object is gone every read answers Error::Expired; back in view it is a new Object.
    class Object
    {
    public:
        /// Every category, for a lookup that accepts any object.
        static constexpr uint32_t kAnyType = 0xFFFFFFFFu;
        /// The categories this class views.
        static constexpr uint32_t kTypeMask = kAnyType;

        Object() = default;
        explicit Object(WXL_Handle handle) : m_handle(handle) {}

        /// The resident object with that GUID in `typeMask` (game::world::kTypeMask* bits); null otherwise.
        static Object FromGuid(uint64_t guid, uint32_t typeMask = kAnyType)
        {
            Object object(detail::HandleOf(guid));
            return object.Is(typeMask) ? object : Object();
        }

        /// The handle of a client object pointer: null when it is not a resident object.
        static Object FromPointer(void* object) { return Object(detail::HandleOf(object)); }

        /// The handle, for the C services.
        WXL_Handle Handle() const { return m_handle; }

        /// True for the empty handle (default, or a lookup that found nothing); asks the core nothing.
        bool IsNull() const { return m_handle.generation == 0; }

        /// True while the object is resident.
        bool Exists() const { return Resolve().HasValue(); }

        /// True while the object is resident; an expired object tests false.
        explicit operator bool() const { return Exists(); }

        /// The client object for the wxl::game bindings, valid until control returns to the client;
        /// Error::WrongKind when it is in none of `typeMask`.
        Result<void*> Resolve(uint32_t typeMask = kAnyType) const { return detail::Resolve(m_handle, typeMask); }

        /// The server's identity of the object, the same across its visits.
        Result<uint64_t> Guid() const
        {
            if (IsNull()) return Error::NullHandle;
            const WXL_ObjectsApi* api = detail::Objects();
            if (!api) return Error::Unsupported;
            uint64_t guid = 0;
            if (const int status = api->Guid(m_handle, &guid)) return detail::ToError(status);
            return guid;
        }

        /// The categories the object belongs to, game::world::kTypeMask* bits.
        Result<uint32_t> TypeMask() const
        {
            auto object = Resolve();
            if (!object) return object.Code();
            return static_cast<uint32_t>(game::world::TypeMask(*object));
        }

        /// True when the object is resident and in one of the categories of `typeMask`.
        bool Is(uint32_t typeMask) const { return (TypeMask().ValueOr(0u) & typeMask) != 0; }
        bool IsUnit() const { return Is(game::world::kTypeMaskUnit); }
        bool IsPlayer() const { return Is(game::world::kTypeMaskPlayer); }
        bool IsGameObject() const { return Is(game::world::kTypeMaskGameObject); }

        /// This object as a T (Unit, Player, GameObject); Error::WrongKind when it is not one.
        template <class T>
        Result<T> As() const
        {
            auto object = Resolve(T::kTypeMask);
            if (!object) return object.Code();
            return T(m_handle);
        }

        /// The world position.
        Result<Vec3> Position() const
        {
            auto object = Resolve();
            if (!object) return object.Code();
            float p[3];
            game::world::Position(*object, p);
            return Vec3{ p[0], p[1], p[2] };
        }

        /// The orientation, radians counter-clockwise from +X.
        Result<float> Facing() const
        {
            auto object = Resolve();
            if (!object) return object.Code();
            return game::world::Facing(*object);
        }

        /// The anchor above the object where the client hangs its name.
        Result<Vec3> NamePosition() const
        {
            auto object = Resolve();
            if (!object) return object.Code();
            float p[3];
            game::world::NamePosition(*object, p);
            return Vec3{ p[0], p[1], p[2] };
        }

        /// Once, when the object leaves the client: its handle still resolves during the call.
        Signal<events::ObjectLifeArgs> Removed() const { return SignalOf<::wxl::ObjectRemoved>(m_handle); }

        /// The same object as a Unit; null when it is not one. As<Unit>() says why.
        inline Unit AsUnit() const;
        /// The same object as a Player; null when it is not one.
        inline Player AsPlayer() const;
        /// The same object as a GameObject; null when it is not one.
        inline GameObject AsGameObject() const;

        friend bool operator==(const Object& a, const Object& b)
        {
            return a.m_handle.index == b.m_handle.index && a.m_handle.generation == b.m_handle.generation;
        }
        friend bool operator!=(const Object& a, const Object& b) { return !(a == b); }

    protected:
        WXL_Handle m_handle{};
    };
}

template <>
struct std::hash<wxl::Object>
{
    std::size_t operator()(const wxl::Object& object) const noexcept
    {
        const WXL_Handle h = object.Handle();
        return std::hash<uint64_t>()((static_cast<uint64_t>(h.generation) << 32) | h.index);
    }
};
