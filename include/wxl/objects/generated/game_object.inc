// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The members of wxl::GameObject that read the object, one per row of object_reads.def: written by
// wxl-object-members (sdk/tools/objects), not by hand. Change the table and run the tool.
//
// Included inside the class, in its public part.

        /// Who summoned or spawned it, or 0 for one placed in the world.
        Expected<wxl_guid> CreatedByGuid() const { return ReadAs<wxl_guid>(&wxl_object_api::gameobject_created_by); }

        /// Its model, a GameObjectDisplayInfo.dbc id.
        Expected<uint32_t> DisplayId() const { return ReadAs<uint32_t>(&wxl_object_api::gameobject_display_id); }

        /// GAMEOBJECT_FLAGS: in use 0x1, locked 0x2, ...
        Expected<uint32_t> Flags() const { return ReadAs<uint32_t>(&wxl_object_api::gameobject_flags); }

        /// GAMEOBJECT_DYNAMIC's flags, as the server sends them to this client.
        Expected<uint32_t> DynamicFlags() const { return ReadAs<uint32_t>(&wxl_object_api::gameobject_dynamic_flags); }

        /// Its FactionTemplate.dbc id.
        Expected<uint32_t> Faction() const { return ReadAs<uint32_t>(&wxl_object_api::gameobject_faction); }

        /// Its level.
        Expected<uint32_t> Level() const { return ReadAs<uint32_t>(&wxl_object_api::gameobject_level); }

        /// 0 active, 1 ready, 2 destroyed.
        Expected<uint32_t> State() const { return ReadAs<uint32_t>(&wxl_object_api::gameobject_state); }

        /// The game-object type, in the server's numbering (door 0, button 1, chest 3, ...).
        Expected<uint32_t> GoType() const { return ReadAs<uint32_t>(&wxl_object_api::gameobject_type); }

        /// The art kit it shows.
        Expected<uint32_t> ArtKit() const { return ReadAs<uint32_t>(&wxl_object_api::gameobject_art_kit); }

        /// Whether the active player can use it now, as the client decides the cursor over it: its kind lets it be used, its quest is in the log, its lock opens; out of reach counts.
        /// Came with abi 0.1.26: WXL_E_UNSUPPORTED on an older runtime.
        Expected<bool> CanUse() const { return ReadAs<bool>(&wxl_object_api::gameobject_can_use); }
