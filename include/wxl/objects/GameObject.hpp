// SPDX-License-Identifier: GPL-3.0-or-later
// wxl::GameObject: a door, a chest, a chair, a light. The Object surface under its own name.

#pragma once

#include <cstdint>

#include "wxl/game/World.hpp"
#include "wxl/objects/Object.hpp"

namespace wxl
{
    /// A game object, read through the Object surface.
    class GameObject : public Object
    {
    public:
        static constexpr uint32_t kTypeMask = game::world::kTypeMaskGameObject;

        GameObject() = default;
        explicit GameObject(WXL_Handle handle) : Object(handle) {}

        /// The resident game object with that GUID; null when not resident or not one.
        static GameObject FromGuid(uint64_t guid) { return Object::FromGuid(guid, kTypeMask).AsGameObject(); }
    };

    inline GameObject Object::AsGameObject() const { return IsGameObject() ? GameObject(m_handle) : GameObject(); }
}
