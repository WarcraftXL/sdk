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

/// write_fields' flags: none is defined yet, so 0 is the only value a runtime accepts.
#define WXL_FIELD_WRITE_PLAIN 0u

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief One aura the client holds on a unit, as unit_aura writes it.
 *
 * The caller sets struct_size to the size it knows; the runtime writes no more than that and
 * sets struct_size to what it wrote.
 */
typedef struct wxl_aura
{
    uint32_t struct_size;
    uint32_t spell_id;
    wxl_guid caster;     ///< who cast it; 0 for an aura with no caster the client knows
    uint32_t stacks;     ///< how many times it is applied; 0 for one that does not stack
    uint32_t level;      ///< the caster's level when it was cast
    uint32_t flags;      ///< as the server sends them: 0x8 cast by the unit itself, 0x10 helpful, 0x20 with a duration, 0x80 harmful
    int32_t  duration;   ///< its whole duration in milliseconds; 0 or less for none
    uint32_t expiration; ///< when it ends, in milliseconds on the client's clock; 0 for never
} wxl_aura;

/**
 * @brief One line of the active player's skill list, as player_skill writes it.
 *
 * The caller sets struct_size to the size it knows; the runtime writes no more than that and
 * sets struct_size to what it wrote.
 */
typedef struct wxl_skill
{
    uint32_t struct_size;
    uint32_t id;              ///< a SkillLine.dbc id; 0 for an empty line
    uint32_t value;           ///< the skill now, without bonuses
    uint32_t max;             ///< the most it can reach at this rank
    uint32_t step;            ///< the rank: apprentice 1, journeyman 2, ...
    int32_t  temporary_bonus; ///< from what the player wears or drinks
    int32_t  permanent_bonus; ///< from racials and talents
} wxl_skill;

/**
 * @brief Reading world objects.
 *
 * A getter writes its out-parameter and returns WXL_OK, or leaves it untouched and returns
 * WXL_E_NOT_FOUND for a handle that no longer resolves, or WXL_E_INVALID for a handle of the
 * wrong kind -- asking a game object for its health is a mistake, not a missing value.
 *
 * A handle names one visibility episode of an object: from the ObjectCreate that announces it to
 * the ObjectDestroy that sees it leave. An object that leaves view and comes back -- even the same
 * client object, revived before the client freed it -- is announced again with a new handle, and
 * the old one stays stale. The guid is what lasts across episodes.
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

    /// The handle for a guid, or WXL_HANDLE_NONE when that object is not in view. Any object the
    /// client holds resolves, announced or not.
    wxl_handle(WXL_CALL* from_guid)(wxl_guid guid);

    /// The handle of the object the local player controls.
    wxl_handle(WXL_CALL* local_player)(void);

    // --- any object -------------------------------------------------------------------------

    wxl_result(WXL_CALL* position)(wxl_handle object, wxl_vec3* out);
    wxl_result(WXL_CALL* facing)(wxl_handle object, float* out_radians);

    /// The display name. The string points into runtime storage and is valid until the next call.
    wxl_result(WXL_CALL* name)(wxl_handle object, wxl_str* out);

    // --- units ------------------------------------------------------------------------------

    /// Signed, as the client holds UNIT_FIELD_HEALTH and tests it (`<= 0` is dead).
    wxl_result(WXL_CALL* unit_health)(wxl_handle unit, int32_t* out);
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

    // --- appended in 0.1.2: the client's own object taxonomy----------------------------------
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
    /// The item shown in an equipment slot (0 head .. 18 tabard); 0 when the slot is empty. The
    /// client keeps a disarmed weapon's id negated in PLAYER_VISIBLE_ITEM_n_ENTRYID; this answers
    /// the item, without the sign, which the field itself still carries.
    wxl_result(WXL_CALL* player_visible_item)(wxl_handle player, uint32_t slot, uint32_t* out);

    // --- items, containers included ---------------------------------------------------------

    wxl_result(WXL_CALL* item_owner)(wxl_handle item, wxl_guid* out);
    /// The bag holding the item, or 0 when it sits in the backpack or is equipped.
    wxl_result(WXL_CALL* item_contained_in)(wxl_handle item, wxl_guid* out);
    wxl_result(WXL_CALL* item_creator)(wxl_handle item, wxl_guid* out);
    /// Signed, as the client holds ITEM_FIELD_STACK_COUNT.
    wxl_result(WXL_CALL* item_stack_count)(wxl_handle item, int32_t* out);
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

    // --- appended in 0.1.9: the descriptor fields, by dword index ---------------------------
    //
    // Every object carries a block of dwords the server fills through update packets: its
    // descriptor fields, OBJECT_FIELD_GUID at dword 0 and each kind's own fields after its
    // parent's. wxl/update_fields.def names every one of them with its dword index, its size and
    // what it holds; the getters above read a few of them by name, these reach all of them by
    // index. A table from a runtime older than this header ends before these members: the typed
    // facades read struct_size and answer WXL_E_UNSUPPORTED.
    //
    // Each of them refuses what it cannot do with WXL_E_INVALID -- a range past the object's
    // block, a null pointer, a flag it does not know -- and says why in the runtime's log
    // ("write_fields: dword 0x95 is past a Unit's 0x94").

    /**
     * @brief The size of the object's descriptor block, in dwords; 0 for a handle that does not
     *        resolve.
     *
     * Each kind's end in update_fields.def (a Unit's 0x94), except for a player other than the
     * active one: the client keeps the fields past PLAYER_FIELD_PAD_0 -- inventory, skills,
     * money, experience -- for the player it controls only, so another player's block ends at
     * 0x144.
     */
    uint32_t(WXL_CALL* field_count)(wxl_handle object);

    /// Dwords [first, first + count) of the object's block into @p out, as the client holds them.
    wxl_result(WXL_CALL* read_fields)(wxl_handle object, uint32_t first, uint32_t count, uint32_t* out);

    /**
     * @brief The same dwords of many objects at once.
     *
     * Writes @p dwords dwords from @p field for each of @p count objects, one after the other,
     * into @p out, which holds count * dwords. An object that does not answer -- a stale handle,
     * a block too short for the range -- is written as zeros, and @p results, when not null,
     * receives each object's result in order: what read_fields would have answered for it.
     * Returns WXL_OK when the call itself was well formed, whatever each object answered.
     */
    wxl_result(WXL_CALL* read_field_many)(const wxl_handle* objects, uint32_t count, uint32_t field,
                                          uint32_t dwords, uint32_t* out, wxl_result* results);

    /**
     * @brief Writes dwords [first, first + count) of the object's block in the client.
     *
     * A client-side write: the client's copy of the field holds the value from here on, and the
     * next update of the server that carries the field replaces it, as it replaces anything the
     * client holds. Nothing else happens -- no handler of the client runs, no model is rebuilt,
     * the server is not told, and no ObjectFieldChanged is raised -- so a field the client reads
     * each frame shows the value at once and one it reads on change shows it when it next looks.
     * OBJECT_FIELD_GUID and OBJECT_FIELD_TYPE, dwords 0 to 2, are the keys the client finds the
     * object by and are refused.
     *
     * @param flags  WXL_FIELD_WRITE_PLAIN. A flag this runtime does not know is refused, so that
     *               a later one -- a write the runtime keeps re-applying over the server's -- is
     *               never taken for a plain write.
     */
    wxl_result(WXL_CALL* write_fields)(wxl_handle object, uint32_t first, uint32_t count,
                                       const uint32_t* in, uint32_t flags);

    /**
     * @brief Asks for ObjectFieldChanged when a field of objects of a type changes.
     *
     * Nothing is raised for a field nobody asked for. From this call on, each update of the
     * server that changes an element of dwords [first, first + count) on an object of @p type --
     * a wxl_object_type, or WXL_OBJECT_UNKNOWN for every object -- raises one ObjectFieldChanged
     * per element that changed, with the element's old and new value, once the client has
     * applied the update: where the client runs its own reactions to a field's change. A field
     * of a parent kind is watched on the type asked for only: watching UNIT_FIELD_HEALTH for
     * WXL_OBJECT_PLAYER raises nothing for a creature.
     *
     * The range starts and ends on elements (a guid is one element of two dwords) of fields the
     * client sees change: update_fields.def's watch column. The client finds a change by
     * comparing an element with the copy it keeps from before the update, and keeps one only for
     * some fields; the others are refused with WXL_E_UNSUPPORTED and named in the log. The
     * player's fields past PLAYER_FIELD_PAD_0 (scope local) are seen on the active player only.
     *
     * A watch belongs to the mod that asked for it, and the event goes to that mod's handlers
     * only: a mod receives the changes it watches and no other mod's, whatever it subscribed
     * to. A watch lasts, across worlds, until the mod gives it back with unwatch_field or goes
     * -- disabled or unloaded -- which gives back all of its watches. Each call is one watch:
     * two parts of a mod that each ask for a field each give it back, and the mod receives the
     * field's changes, once each, until the last one does.
     */
    wxl_result(WXL_CALL* watch_field)(uint32_t type, uint32_t first, uint32_t count);

    // --- appended in 0.1.10 -----------------------------------------------------------------

    /**
     * @brief Gives back one of the calling mod's watches of dwords [first, first + count) on
     *        objects of @p type, as watch_field took it.
     *
     * For each element of the range, one watch the mod holds for that type goes; with its last
     * one the mod stops receiving the element's changes. An element it does not watch is left
     * as it is, so giving back more than was asked for is not a mistake. Another mod's watch of
     * the same field goes on. The range is checked as watch_field checks it -- elements, the
     * kind's block -- and refused with WXL_E_INVALID when it is wrong.
     */
    wxl_result(WXL_CALL* unwatch_field)(uint32_t type, uint32_t first, uint32_t count);

    // --- appended in 0.1.18: the typed reads ------------------------------------------------
    //
    // One getter per thing a mod asks of an object, each described by a row of
    // object_reads.def, which the facades and wxl.lua are made from. They answer as the getters
    // above: WXL_OK, WXL_E_NOT_FOUND for a stale handle, WXL_E_INVALID for a handle of another
    // kind or an argument out of range. Three more answers:
    //
    //   - a uint32_t answer described with an enumeration holds a value of it: unit_reaction a
    //     wxl_reaction, unit_creature_type a wxl_creature_type, ...;
    //   - a wxl_handle answer is the object in view, or WXL_HANDLE_NONE for nothing there;
    //   - the item cache's reads (item_quality, item_level, ...) answer WXL_E_FAILED while the
    //     client has not received the item's record from the server: ask again later.
    //
    // The player getters marked as the active player's answer WXL_E_INVALID for any other player:
    // the client keeps money, experience, inventory and skills for the player it controls only.

    /// Whether it is the unit the client controls.
    wxl_result(WXL_CALL* is_active_player)(wxl_handle object, wxl_bool* out);
    /// Yards from it to another object, centre to centre; invalid when either has no place in the world.
    wxl_result(WXL_CALL* distance)(wxl_handle object, wxl_handle other, float* out);
    /// Yards from it to a point; invalid for an object with no place in the world.
    wxl_result(WXL_CALL* distance_to)(wxl_handle object, wxl_vec3 point, float* out);
    /// What it targets, as a guid, whether that is in view or not; 0 for nothing.
    wxl_result(WXL_CALL* unit_target_guid)(wxl_handle unit, wxl_guid* out);
    /// How it stands towards the active player, by the client's own reckoning: the colour of its name.
    wxl_result(WXL_CALL* unit_reaction)(wxl_handle unit, uint32_t* out);
    /// How it stands towards another unit, by the client's own reckoning.
    wxl_result(WXL_CALL* unit_reaction_to)(wxl_handle unit, wxl_handle other, uint32_t* out);
    /// Whether it is dead: no health left, or marked dead by the server.
    wxl_result(WXL_CALL* unit_is_dead)(wxl_handle unit, wxl_bool* out);
    /// Whether it is in combat, as the client's UnitAffectingCombat reads it.
    wxl_result(WXL_CALL* unit_in_combat)(wxl_handle unit, wxl_bool* out);
    /// Whether it is flagged for player-versus-player combat.
    wxl_result(WXL_CALL* unit_is_pvp)(wxl_handle unit, wxl_bool* out);
    /// Whether someone has tapped it: its kill and loot are theirs.
    wxl_result(WXL_CALL* unit_is_tapped)(wxl_handle unit, wxl_bool* out);
    /// Whether the active player, or their group, has tapped it.
    wxl_result(WXL_CALL* unit_is_tapped_by_player)(wxl_handle unit, wxl_bool* out);
    /// Whether it has loot for the active player.
    wxl_result(WXL_CALL* unit_is_lootable)(wxl_handle unit, wxl_bool* out);
    /// Whether its corpse can be skinned.
    wxl_result(WXL_CALL* unit_is_skinnable)(wxl_handle unit, wxl_bool* out);
    /// How it stands: standing, sitting, sleeping, kneeling, ...
    wxl_result(WXL_CALL* unit_stand_state)(wxl_handle unit, uint32_t* out);
    /// Whether it sits, on the ground or in a chair.
    wxl_result(WXL_CALL* unit_is_sitting)(wxl_handle unit, wxl_bool* out);
    /// Whether it is stealthed, as the client's IsStealthed reads it.
    wxl_result(WXL_CALL* unit_is_stealthed)(wxl_handle unit, wxl_bool* out);
    /// Whether it rides a mount, as the client's IsMounted reads it.
    wxl_result(WXL_CALL* unit_is_mounted)(wxl_handle unit, wxl_bool* out);
    /// The spell it casts now; 0 for none.
    wxl_result(WXL_CALL* unit_casting_spell_id)(wxl_handle unit, uint32_t* out);
    /// When the cast began, in milliseconds on the client's clock (the client's GetTime() * 1000).
    wxl_result(WXL_CALL* unit_cast_start)(wxl_handle unit, uint32_t* out);
    /// When the cast ends, on the same clock.
    wxl_result(WXL_CALL* unit_cast_end)(wxl_handle unit, uint32_t* out);
    /// Whether it is casting, its cast not yet over, as the client's UnitCastingInfo reads it.
    wxl_result(WXL_CALL* unit_is_casting)(wxl_handle unit, wxl_bool* out);
    /// The spell it channels now; 0 for none.
    wxl_result(WXL_CALL* unit_channel_spell_id)(wxl_handle unit, uint32_t* out);
    /// When the channel began, on the client's clock.
    wxl_result(WXL_CALL* unit_channel_start)(wxl_handle unit, uint32_t* out);
    /// When the channel ends, on the client's clock.
    wxl_result(WXL_CALL* unit_channel_end)(wxl_handle unit, uint32_t* out);
    /// Whether it is channeling, its channel not yet over.
    wxl_result(WXL_CALL* unit_is_channeling)(wxl_handle unit, wxl_bool* out);
    /// How many auras the client holds on it, buffs and debuffs alike.
    wxl_result(WXL_CALL* unit_aura_count)(wxl_handle unit, uint32_t* out);
    /// The aura in a slot, 0 to AuraCount - 1: its spell, caster, stacks, duration, expiration and flags.
    wxl_result(WXL_CALL* unit_aura)(wxl_handle unit, uint32_t slot, wxl_aura* out);
    /// Whether an aura of a spell is on it.
    wxl_result(WXL_CALL* unit_has_aura)(wxl_handle unit, uint32_t spell_id, wxl_bool* out);
    /// What kind of creature it is, as the client works it out: beast, humanoid, undead, ...
    wxl_result(WXL_CALL* unit_creature_type)(wxl_handle unit, uint32_t* out);
    /// Its beast family, a CreatureFamily.dbc id; 0 for none.
    wxl_result(WXL_CALL* unit_creature_family)(wxl_handle unit, uint32_t* out);
    /// Its rank: normal, elite, rare, rare elite, world boss.
    wxl_result(WXL_CALL* unit_classification)(wxl_handle unit, uint32_t* out);
    /// Whether it knows a spell: the active player's spell book or its pet's; false for any other unit.
    wxl_result(WXL_CALL* unit_knows_spell)(wxl_handle unit, uint32_t spell_id, wxl_bool* out);
    /// The flags of its movement: forward 0x1, backward 0x2, ..., falling 0x1000, flying 0x2000000.
    wxl_result(WXL_CALL* unit_movement_flags)(wxl_handle unit, uint32_t* out);
    /// One of its speeds, in yards a second: walk, run, swim, flight, ...; the turn and pitch rates in radians a second.
    wxl_result(WXL_CALL* unit_speed)(wxl_handle unit, uint32_t move_type, float* out);
    /// Whether it swims, as the client's IsSwimming reads it.
    wxl_result(WXL_CALL* unit_is_swimming)(wxl_handle unit, wxl_bool* out);
    /// Whether it flies, as the client's IsFlying reads it.
    wxl_result(WXL_CALL* unit_is_flying)(wxl_handle unit, wxl_bool* out);
    /// Whether it is falling, as the client's IsFalling reads it.
    wxl_result(WXL_CALL* unit_is_falling)(wxl_handle unit, wxl_bool* out);
    /// Whether it is a ghost, released from its corpse.
    wxl_result(WXL_CALL* player_is_ghost)(wxl_handle player, wxl_bool* out);
    /// Whether it is away from the keyboard.
    wxl_result(WXL_CALL* player_is_afk)(wxl_handle player, wxl_bool* out);
    /// Whether it asked not to be disturbed.
    wxl_result(WXL_CALL* player_is_dnd)(wxl_handle player, wxl_bool* out);
    /// Whether it rests in an inn or a city, gaining rested experience.
    wxl_result(WXL_CALL* player_is_resting)(wxl_handle player, wxl_bool* out);
    /// Its money, in copper.
    wxl_result(WXL_CALL* player_money)(wxl_handle player, uint32_t* out);
    /// Its experience in this level.
    wxl_result(WXL_CALL* player_xp)(wxl_handle player, uint32_t* out);
    /// The experience this level takes.
    wxl_result(WXL_CALL* player_next_level_xp)(wxl_handle player, uint32_t* out);
    /// Its rested experience: kills give twice as much until it is spent.
    wxl_result(WXL_CALL* player_rested_xp)(wxl_handle player, uint32_t* out);
    /// Its honor points.
    wxl_result(WXL_CALL* player_honor_points)(wxl_handle player, uint32_t* out);
    /// Its arena points.
    wxl_result(WXL_CALL* player_arena_points)(wxl_handle player, uint32_t* out);
    /// The item in an equipment slot, or the bag in a bag slot (19 to 22); none for an empty slot.
    wxl_result(WXL_CALL* player_inventory_item)(wxl_handle player, uint32_t slot, wxl_handle* out);
    /// The item in a slot of the backpack, 0 to 15; none for an empty slot.
    wxl_result(WXL_CALL* player_backpack_item)(wxl_handle player, uint32_t slot, wxl_handle* out);
    /// The skill on a line of the skill list, 0 to 127: its id, value and maximum; an id of 0 for an empty line.
    wxl_result(WXL_CALL* player_skill)(wxl_handle player, uint32_t line, wxl_skill* out);
    /// A skill by its SkillLine.dbc id; invalid for a skill the player does not have.
    wxl_result(WXL_CALL* player_skill_of)(wxl_handle player, uint32_t skill_id, wxl_skill* out);
    /// How many others are in its party, 0 to 4.
    wxl_result(WXL_CALL* player_party_size)(wxl_handle player, uint32_t* out);
    /// The other party member in a party slot, 0 to 3, whether in view or not; 0 for an empty slot.
    wxl_result(WXL_CALL* player_party_member_guid)(wxl_handle player, uint32_t slot, wxl_guid* out);
    /// How many are in its raid, itself included; 0 outside a raid.
    wxl_result(WXL_CALL* player_raid_size)(wxl_handle player, uint32_t* out);
    /// The raid member in a raid slot, 0 to 39, whether in view or not; 0 for an empty slot.
    wxl_result(WXL_CALL* player_raid_member_guid)(wxl_handle player, uint32_t slot, wxl_guid* out);
    /// The enchantment in a slot, 0 to 11 (0 permanent, 1 temporary, 2 to 4 sockets, ...), a SpellItemEnchantment.dbc id; 0 for none.
    wxl_result(WXL_CALL* item_enchant)(wxl_handle item, uint32_t slot, uint32_t* out);
    /// Its quality: poor, common, uncommon, rare, epic, legendary, artifact, heirloom.
    wxl_result(WXL_CALL* item_quality)(wxl_handle item, uint32_t* out);
    /// Its item level.
    wxl_result(WXL_CALL* item_level)(wxl_handle item, uint32_t* out);
    /// The level it takes to use.
    wxl_result(WXL_CALL* item_required_level)(wxl_handle item, uint32_t* out);
    /// Its class, an ItemClass.dbc id: weapon 2, armor 4, ...
    wxl_result(WXL_CALL* item_class)(wxl_handle item, uint32_t* out);
    /// Its subclass within the class: a sword, a cloth robe, ...
    wxl_result(WXL_CALL* item_subclass)(wxl_handle item, uint32_t* out);
    /// Where it is worn: head 1, neck 2, ...; 0 for an item that is not worn.
    wxl_result(WXL_CALL* item_inventory_type)(wxl_handle item, uint32_t* out);
    /// How many a stack of it holds at most.
    wxl_result(WXL_CALL* item_max_stack)(wxl_handle item, uint32_t* out);
    /// What a vendor pays for one, in copper.
    wxl_result(WXL_CALL* item_sell_price)(wxl_handle item, uint32_t* out);
    /// The item in a slot of the bag; none for an empty one, invalid past NumSlots.
    wxl_result(WXL_CALL* container_slot_item)(wxl_handle container, uint32_t slot, wxl_handle* out);
    /// Whether it stands outdoors, as the client's IsOutdoors reads it: false in a building or a cave the world marks indoors, where a mount is refused.
    wxl_result(WXL_CALL* unit_is_outdoors)(wxl_handle unit, wxl_bool* out);
    /// Whether the active player may attack it, by the client's own reckoning (faction, duel, PvP, sanctuary): what turns the cursor into a sword.
    wxl_result(WXL_CALL* unit_can_attack)(wxl_handle unit, wxl_bool* out);
    /// Whether the active player can interact with it now: an NPC offering something (gossip, quests, trade, training, a flight), alive, out of combat, of a reaction that lets it; out of reach counts.
    wxl_result(WXL_CALL* unit_can_interact)(wxl_handle unit, wxl_bool* out);
    /// Whether the active player can use it now, as the client decides the cursor over it: its kind lets it be used, its quest is in the log, its lock opens; out of reach counts.
    wxl_result(WXL_CALL* gameobject_can_use)(wxl_handle gameobject, wxl_bool* out);
} wxl_object_api;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // WXL_SERVICE_OBJECT_H
