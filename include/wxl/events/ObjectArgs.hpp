// SPDX-License-Identifier: GPL-3.0-or-later
// The args of the object events: server object updates, lifetimes, doodads and character equipment.

#pragma once

#include "wxl/abi/Plugin.h"

#include <cstdint>

namespace wxl::events
{
    /// OnObjectUpdate, read-only: the inbound message reader, its cursor already consumed.
    struct ObjectUpdateArgs { void* packet; int opcode; };

    /// OnObjectDestroy, read-only: the message holds the object GUID and the on-death flag.
    struct ObjectDestroyArgs { void* packet; int opcode; };

    /// OnDoodadSpawn: the transform is read through wxl::game::doodad.
    struct DoodadSpawnArgs { void* doodad; };

    /// OnItemSlotChange: a CharModelObject's model slot (one per equipment category) got an item.
    struct ItemSlotChangeArgs { void* charModelObj; uint32_t modelSlot; void* itemDataPtr; };

    /// OnItemSlotClear: `equipSlotWow` is an EQUIPMENT_SLOT_* value, 0..18.
    struct ItemSlotClearArgs { void* charModelObj; uint32_t equipSlotWow; };

    /// OnObjectAdded, OnObjectRemoved: the object, resident for the whole call.
    struct ObjectLifeArgs
    {
        WXL_Handle object;   ///< live during the call; after OnObjectRemoved it answers Expired
        uint64_t   guid;
        uint32_t   typeMask; ///< game::world::kTypeMask* bits
        void*      pointer;  ///< the client object, for the wxl::game bindings
    };
}
