// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The object service: reading what lives in the world.
//
// Everything is addressed by handle and read through a call. No struct layout crosses, which is
// what lets the same mod read a unit on a client that keeps it at a fixed offset and on one that
// holds a real object.

#ifndef WXL_SERVICE_OBJECT_H
#define WXL_SERVICE_OBJECT_H

#include "wxl/abi.h"
#include "wxl/events.h"

#define WXL_OBJECT_SERVICE         "wxl.object"
#define WXL_OBJECT_SERVICE_VERSION 1

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Reading world objects.
 *
 * A getter writes its out-parameter and returns WXL_OK, or leaves it untouched and returns
 * WXL_E_NOT_FOUND for a handle that no longer resolves, or WXL_E_INVALID for a handle of the
 * wrong kind -- asking a game object for its health is a mistake, not a missing value.
 *
 * Main thread only: the objects behind the handles belong to the client's object manager, which
 * lives on the thread that runs the game. A read from another thread is a data race.
 */
typedef struct wxl_object_api
{
    uint32_t struct_size;

    // --- identity ---------------------------------------------------------------------------

    /// Whether the handle still resolves. A stale handle answers WXL_FALSE; it never misreads.
    wxl_bool(WXL_CALL* valid)(wxl_handle object);

    /// The object's server identity, or WXL_GUID_NONE for something the server does not own.
    wxl_guid(WXL_CALL* guid)(wxl_handle object);

    /// A wxl_object_type value, or WXL_OBJECT_UNKNOWN.
    uint32_t(WXL_CALL* type)(wxl_handle object);

    /// The handle for a guid, or WXL_HANDLE_NONE when that object is not in view.
    wxl_handle(WXL_CALL* from_guid)(wxl_guid guid);

    /// The handle of the object the local player controls.
    wxl_handle(WXL_CALL* local_player)(void);

    // --- any object -------------------------------------------------------------------------

    wxl_result(WXL_CALL* position)(wxl_handle object, wxl_vec3* out);
    wxl_result(WXL_CALL* facing)(wxl_handle object, float* out_radians);

    /// The display name. The string points into runtime storage and is valid until the next call.
    wxl_result(WXL_CALL* name)(wxl_handle object, wxl_str* out);

    // --- units ------------------------------------------------------------------------------

    wxl_result(WXL_CALL* unit_health)(wxl_handle unit, uint32_t* out);
    wxl_result(WXL_CALL* unit_max_health)(wxl_handle unit, uint32_t* out);
    wxl_result(WXL_CALL* unit_level)(wxl_handle unit, uint32_t* out);

    /// What the unit is targeting, or WXL_HANDLE_NONE.
    wxl_result(WXL_CALL* unit_target)(wxl_handle unit, wxl_handle* out);

    // --- in bulk ----------------------------------------------------------------------------
    //
    // One call per object is fine for a hundred and wasteful for ten thousand. These exist so a
    // per-frame pass over everything in view does not have to reach past the contract.

    /**
     * @brief Lists the objects of a type currently in view.
     * @param type  a wxl_object_type value, or WXL_OBJECT_UNKNOWN for every object.
     * @param out   receives up to @p capacity handles; may be NULL to count only.
     * @return how many there are, which may exceed @p capacity.
     */
    uint32_t(WXL_CALL* enumerate)(uint32_t type, wxl_handle* out, uint32_t capacity);

    /// Positions for many handles at once. Writes WXL_HANDLE_NONE entries as the origin.
    wxl_result(WXL_CALL* positions)(const wxl_handle* objects, uint32_t count, wxl_vec3* out);

    // --- appended in 0.2: the client's own object taxonomy ------------------------------------
    //
    // The client knows eight kinds of object (wxl_object_type), and each kind carries its own
    // descriptor block. One getter per field a mod can use, gated like the unit getters above:
    // WXL_E_INVALID for a handle of another kind. A table from a runtime older than this header
    // ends before these members; the typed facades read struct_size and answer WXL_E_UNSUPPORTED.
    //
    // A field that names another object is handed over as a guid, not a handle: the object it
    // names may be out of view, and from_guid resolves it when it is not.

    // --- any object, continued --------------------------------------------------------------

    /// The server's template id: a creature, item or game-object entry; 0 for a player.
    wxl_result(WXL_CALL* entry)(wxl_handle object, uint32_t* out);
    wxl_result(WXL_CALL* scale)(wxl_handle object, float* out);

    // --- units, continued -------------------------------------------------------------------

    /// Power by index (0 mana, 1 rage, 2 focus, 3 energy, 4 happiness, 5 runes, 6 runic power).
    wxl_result(WXL_CALL* unit_power)(wxl_handle unit, uint32_t index, uint32_t* out);
    wxl_result(WXL_CALL* unit_max_power)(wxl_handle unit, uint32_t index, uint32_t* out);
    /// The index of the power the unit uses, as unit_power takes it.
    wxl_result(WXL_CALL* unit_power_type)(wxl_handle unit, uint32_t* out);
    wxl_result(WXL_CALL* unit_race)(wxl_handle unit, uint32_t* out);
    wxl_result(WXL_CALL* unit_class)(wxl_handle unit, uint32_t* out);
    /// 0 male, 1 female, 2 none.
    wxl_result(WXL_CALL* unit_sex)(wxl_handle unit, uint32_t* out);
    wxl_result(WXL_CALL* unit_display_id)(wxl_handle unit, uint32_t* out);
    wxl_result(WXL_CALL* unit_native_display_id)(wxl_handle unit, uint32_t* out);
    /// 0 while not mounted.
    wxl_result(WXL_CALL* unit_mount_display_id)(wxl_handle unit, uint32_t* out);
    wxl_result(WXL_CALL* unit_faction_template)(wxl_handle unit, uint32_t* out);
    wxl_result(WXL_CALL* unit_flags)(wxl_handle unit, uint32_t* out);
    wxl_result(WXL_CALL* unit_flags2)(wxl_handle unit, uint32_t* out);
    wxl_result(WXL_CALL* unit_dynamic_flags)(wxl_handle unit, uint32_t* out);
    wxl_result(WXL_CALL* unit_npc_flags)(wxl_handle unit, uint32_t* out);
    wxl_result(WXL_CALL* unit_bounding_radius)(wxl_handle unit, float* out);
    wxl_result(WXL_CALL* unit_combat_reach)(wxl_handle unit, float* out);
    /// Who the unit charms, summons, is charmed by, was summoned by, was created by; none as 0.
    wxl_result(WXL_CALL* unit_charm)(wxl_handle unit, wxl_guid* out);
    wxl_result(WXL_CALL* unit_summon)(wxl_handle unit, wxl_guid* out);
    wxl_result(WXL_CALL* unit_charmed_by)(wxl_handle unit, wxl_guid* out);
    wxl_result(WXL_CALL* unit_summoned_by)(wxl_handle unit, wxl_guid* out);
    wxl_result(WXL_CALL* unit_created_by)(wxl_handle unit, wxl_guid* out);

    // --- players ----------------------------------------------------------------------------

    wxl_result(WXL_CALL* player_flags)(wxl_handle player, uint32_t* out);
    /// 0 while unguilded.
    wxl_result(WXL_CALL* player_guild_id)(wxl_handle player, uint32_t* out);
    wxl_result(WXL_CALL* player_guild_rank)(wxl_handle player, uint32_t* out);
    wxl_result(WXL_CALL* player_chosen_title)(wxl_handle player, uint32_t* out);
    /// The item shown in an equipment slot (0 head .. 18 tabard); 0 when the slot is empty.
    wxl_result(WXL_CALL* player_visible_item)(wxl_handle player, uint32_t slot, uint32_t* out);

    // --- items, containers included ---------------------------------------------------------

    wxl_result(WXL_CALL* item_owner)(wxl_handle item, wxl_guid* out);
    /// The bag holding the item, or 0 when it sits in the backpack or is equipped.
    wxl_result(WXL_CALL* item_contained_in)(wxl_handle item, wxl_guid* out);
    wxl_result(WXL_CALL* item_creator)(wxl_handle item, wxl_guid* out);
    wxl_result(WXL_CALL* item_stack_count)(wxl_handle item, uint32_t* out);
    /// Seconds left before the item expires; 0 when it does not.
    wxl_result(WXL_CALL* item_duration)(wxl_handle item, uint32_t* out);
    wxl_result(WXL_CALL* item_flags)(wxl_handle item, uint32_t* out);
    wxl_result(WXL_CALL* item_durability)(wxl_handle item, uint32_t* out);
    wxl_result(WXL_CALL* item_max_durability)(wxl_handle item, uint32_t* out);

    // --- containers -------------------------------------------------------------------------

    wxl_result(WXL_CALL* container_num_slots)(wxl_handle container, uint32_t* out);
    /// The item in a slot, or 0 for an empty one; WXL_E_INVALID past the container's slots.
    wxl_result(WXL_CALL* container_slot)(wxl_handle container, uint32_t index, wxl_guid* out);

    // --- game objects -----------------------------------------------------------------------

    wxl_result(WXL_CALL* gameobject_created_by)(wxl_handle object, wxl_guid* out);
    wxl_result(WXL_CALL* gameobject_display_id)(wxl_handle object, uint32_t* out);
    wxl_result(WXL_CALL* gameobject_flags)(wxl_handle object, uint32_t* out);
    wxl_result(WXL_CALL* gameobject_dynamic_flags)(wxl_handle object, uint32_t* out);
    wxl_result(WXL_CALL* gameobject_faction)(wxl_handle object, uint32_t* out);
    wxl_result(WXL_CALL* gameobject_level)(wxl_handle object, uint32_t* out);
    /// 0 active, 1 ready, 2 destroyed, as the client's GAMEOBJECT_BYTES_1 state byte.
    wxl_result(WXL_CALL* gameobject_state)(wxl_handle object, uint32_t* out);
    /// The game-object type: door, button, chest, ... as the server's GameObjectType numbering.
    wxl_result(WXL_CALL* gameobject_type)(wxl_handle object, uint32_t* out);
    wxl_result(WXL_CALL* gameobject_art_kit)(wxl_handle object, uint32_t* out);

    // --- dynamic objects (area effects) -----------------------------------------------------

    wxl_result(WXL_CALL* dynamic_caster)(wxl_handle object, wxl_guid* out);
    wxl_result(WXL_CALL* dynamic_spell_id)(wxl_handle object, uint32_t* out);
    wxl_result(WXL_CALL* dynamic_radius)(wxl_handle object, float* out);
    wxl_result(WXL_CALL* dynamic_cast_time)(wxl_handle object, uint32_t* out);

    // --- corpses ----------------------------------------------------------------------------

    wxl_result(WXL_CALL* corpse_owner)(wxl_handle corpse, wxl_guid* out);
    wxl_result(WXL_CALL* corpse_party)(wxl_handle corpse, wxl_guid* out);
    wxl_result(WXL_CALL* corpse_display_id)(wxl_handle corpse, uint32_t* out);
    wxl_result(WXL_CALL* corpse_guild)(wxl_handle corpse, uint32_t* out);
    wxl_result(WXL_CALL* corpse_flags)(wxl_handle corpse, uint32_t* out);
    wxl_result(WXL_CALL* corpse_dynamic_flags)(wxl_handle corpse, uint32_t* out);

    // --- appended: how one unit regards another -----------------------------------------------

    /**
     * @brief How @p unit regards @p other, as the client works it out from their factions,
     *        PvP flags and charm: 0-1 hostile, 2-3 neutral (2 unfriendly), 4-7 friendly.
     *        Lua's UnitReaction answers the same scale plus one.
     */
    wxl_result(WXL_CALL* unit_reaction)(wxl_handle unit, wxl_handle other, uint32_t* out);
} wxl_object_api;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // WXL_SERVICE_OBJECT_H
