// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The members of wxl::Unit that read the object, one per row of object_reads.def: written by
// wxl-object-members (sdk/tools/objects), not by hand. Change the table and run the tool.
//
// Included inside the class, in its public part.

        /// Its health; 0 or less is a dead unit, as the client reads it.
        Expected<int32_t> Health() const { return ReadAs<int32_t>(&wxl_object_api::unit_health); }

        /// Its maximum health.
        Expected<uint32_t> MaxHealth() const { return ReadAs<uint32_t>(&wxl_object_api::unit_max_health); }

        /// Its level.
        Expected<uint32_t> Level() const { return ReadAs<uint32_t>(&wxl_object_api::unit_level); }

        /// Its power of a type (0 mana, 1 rage, 2 focus, 3 energy, 4 happiness, 5 runes, 6 runic power).
        Expected<uint32_t> PowerValue(Power power) const
        {
            return ReadAtAs<uint32_t>(&wxl_object_api::unit_power, static_cast<uint32_t>(power));
        }

        /// Its maximum power of a type.
        Expected<uint32_t> MaxPowerValue(Power power) const
        {
            return ReadAtAs<uint32_t>(&wxl_object_api::unit_max_power, static_cast<uint32_t>(power));
        }

        /// The type of power it runs on, as PowerValue takes it.
        Expected<Power> PowerType() const { return ReadAs<Power>(&wxl_object_api::unit_power_type); }

        /// Its race, as ChrRaces.dbc numbers it.
        Expected<uint32_t> Race() const { return ReadAs<uint32_t>(&wxl_object_api::unit_race); }

        /// Its class, as ChrClasses.dbc numbers it.
        Expected<uint32_t> Class() const { return ReadAs<uint32_t>(&wxl_object_api::unit_class); }

        /// 0 male, 1 female, 2 none.
        Expected<uint32_t> Sex() const { return ReadAs<uint32_t>(&wxl_object_api::unit_sex); }

        /// The model it shows now, a CreatureDisplayInfo.dbc id.
        Expected<uint32_t> DisplayId() const { return ReadAs<uint32_t>(&wxl_object_api::unit_display_id); }

        /// The model it shows when nothing changes its shape.
        Expected<uint32_t> NativeDisplayId() const { return ReadAs<uint32_t>(&wxl_object_api::unit_native_display_id); }

        /// The model of its mount; 0 while not mounted.
        Expected<uint32_t> MountDisplayId() const { return ReadAs<uint32_t>(&wxl_object_api::unit_mount_display_id); }

        /// Its FactionTemplate.dbc id.
        Expected<uint32_t> FactionTemplate() const { return ReadAs<uint32_t>(&wxl_object_api::unit_faction_template); }

        /// UNIT_FIELD_FLAGS as the server sends it.
        Expected<uint32_t> Flags() const { return ReadAs<uint32_t>(&wxl_object_api::unit_flags); }

        /// UNIT_FIELD_FLAGS_2 as the server sends it.
        Expected<uint32_t> Flags2() const { return ReadAs<uint32_t>(&wxl_object_api::unit_flags2); }

        /// UNIT_DYNAMIC_FLAGS as the server sends it to this client.
        Expected<uint32_t> DynamicFlags() const { return ReadAs<uint32_t>(&wxl_object_api::unit_dynamic_flags); }

        /// What it offers to talk about: vendor, trainer, quest giver, ... as UNIT_NPC_FLAGS.
        Expected<uint32_t> NpcFlags() const { return ReadAs<uint32_t>(&wxl_object_api::unit_npc_flags); }

        /// The radius of its body, in yards.
        Expected<float> BoundingRadius() const { return ReadAs<float>(&wxl_object_api::unit_bounding_radius); }

        /// How far it reaches in melee, in yards.
        Expected<float> CombatReach() const { return ReadAs<float>(&wxl_object_api::unit_combat_reach); }

        /// The unit it charms, or 0.
        Expected<wxl_guid> CharmGuid() const { return ReadAs<wxl_guid>(&wxl_object_api::unit_charm); }

        /// The unit it summoned, its pet among them, or 0.
        Expected<wxl_guid> SummonGuid() const { return ReadAs<wxl_guid>(&wxl_object_api::unit_summon); }

        /// Who charms it, or 0.
        Expected<wxl_guid> CharmedByGuid() const { return ReadAs<wxl_guid>(&wxl_object_api::unit_charmed_by); }

        /// Who summoned it: a pet's owner, or 0.
        Expected<wxl_guid> SummonedByGuid() const { return ReadAs<wxl_guid>(&wxl_object_api::unit_summoned_by); }

        /// Who created it: a totem's shaman, or 0.
        Expected<wxl_guid> CreatedByGuid() const { return ReadAs<wxl_guid>(&wxl_object_api::unit_created_by); }

        /// What it targets, as a guid, whether that is in view or not; 0 for nothing.
        /// Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime.
        Expected<wxl_guid> TargetGuid() const { return ReadAs<wxl_guid>(&wxl_object_api::unit_target_guid); }

        /// How it stands towards the active player, by the client's own reckoning: the colour of its name.
        /// Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime.
        Expected<wxl_reaction> Reaction() const { return ReadAs<wxl_reaction>(&wxl_object_api::unit_reaction); }

        /// How it stands towards another unit, by the client's own reckoning.
        /// Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime.
        Expected<wxl_reaction> ReactionTo(const Object& other) const
        {
            return ReadAtAs<wxl_reaction>(&wxl_object_api::unit_reaction_to, other.Handle());
        }

        /// Whether it is dead: no health left, or marked dead by the server.
        /// Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime.
        Expected<bool> IsDead() const { return ReadAs<bool>(&wxl_object_api::unit_is_dead); }

        /// Whether it is in combat, as the client's UnitAffectingCombat reads it.
        /// Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime.
        Expected<bool> InCombat() const { return ReadAs<bool>(&wxl_object_api::unit_in_combat); }

        /// Whether it is flagged for player-versus-player combat.
        /// Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime.
        Expected<bool> IsPvp() const { return ReadAs<bool>(&wxl_object_api::unit_is_pvp); }

        /// Whether someone has tapped it: its kill and loot are theirs.
        /// Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime.
        Expected<bool> IsTapped() const { return ReadAs<bool>(&wxl_object_api::unit_is_tapped); }

        /// Whether the active player, or their group, has tapped it.
        /// Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime.
        Expected<bool> IsTappedByPlayer() const { return ReadAs<bool>(&wxl_object_api::unit_is_tapped_by_player); }

        /// Whether it has loot for the active player.
        /// Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime.
        Expected<bool> IsLootable() const { return ReadAs<bool>(&wxl_object_api::unit_is_lootable); }

        /// Whether its corpse can be skinned.
        /// Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime.
        Expected<bool> IsSkinnable() const { return ReadAs<bool>(&wxl_object_api::unit_is_skinnable); }

        /// How it stands: standing, sitting, sleeping, kneeling, ...
        /// Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime.
        Expected<wxl_stand_state> StandState() const { return ReadAs<wxl_stand_state>(&wxl_object_api::unit_stand_state); }

        /// Whether it sits, on the ground or in a chair.
        /// Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime.
        Expected<bool> IsSitting() const { return ReadAs<bool>(&wxl_object_api::unit_is_sitting); }

        /// Whether it is stealthed, as the client's IsStealthed reads it.
        /// Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime.
        Expected<bool> IsStealthed() const { return ReadAs<bool>(&wxl_object_api::unit_is_stealthed); }

        /// Whether it rides a mount, as the client's IsMounted reads it.
        /// Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime.
        Expected<bool> IsMounted() const { return ReadAs<bool>(&wxl_object_api::unit_is_mounted); }

        /// The spell it casts now; 0 for none.
        /// Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime.
        Expected<uint32_t> CastingSpellId() const { return ReadAs<uint32_t>(&wxl_object_api::unit_casting_spell_id); }

        /// When the cast began, in milliseconds on the client's clock (the client's GetTime() * 1000).
        /// Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime.
        Expected<uint32_t> CastStart() const { return ReadAs<uint32_t>(&wxl_object_api::unit_cast_start); }

        /// When the cast ends, on the same clock.
        /// Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime.
        Expected<uint32_t> CastEnd() const { return ReadAs<uint32_t>(&wxl_object_api::unit_cast_end); }

        /// Whether it is casting, its cast not yet over, as the client's UnitCastingInfo reads it.
        /// Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime.
        Expected<bool> IsCasting() const { return ReadAs<bool>(&wxl_object_api::unit_is_casting); }

        /// The spell it channels now; 0 for none.
        /// Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime.
        Expected<uint32_t> ChannelSpellId() const { return ReadAs<uint32_t>(&wxl_object_api::unit_channel_spell_id); }

        /// When the channel began, on the client's clock.
        /// Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime.
        Expected<uint32_t> ChannelStart() const { return ReadAs<uint32_t>(&wxl_object_api::unit_channel_start); }

        /// When the channel ends, on the client's clock.
        /// Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime.
        Expected<uint32_t> ChannelEnd() const { return ReadAs<uint32_t>(&wxl_object_api::unit_channel_end); }

        /// Whether it is channeling, its channel not yet over.
        /// Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime.
        Expected<bool> IsChanneling() const { return ReadAs<bool>(&wxl_object_api::unit_is_channeling); }

        /// How many auras the client holds on it, buffs and debuffs alike.
        /// Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime.
        Expected<uint32_t> AuraCount() const { return ReadAs<uint32_t>(&wxl_object_api::unit_aura_count); }

        /// The aura in a slot, 0 to AuraCount - 1: its spell, caster, stacks, duration, expiration and flags.
        /// Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime.
        Expected<wxl_aura> Aura(uint32_t slot) const
        {
            return ReadAtAs<wxl_aura>(&wxl_object_api::unit_aura, static_cast<uint32_t>(slot));
        }

        /// Whether an aura of a spell is on it.
        /// Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime.
        Expected<bool> HasAura(uint32_t spell_id) const
        {
            return ReadAtAs<bool>(&wxl_object_api::unit_has_aura, static_cast<uint32_t>(spell_id));
        }

        /// What kind of creature it is, as the client works it out: beast, humanoid, undead, ...
        /// Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime.
        Expected<wxl_creature_type> CreatureType() const { return ReadAs<wxl_creature_type>(&wxl_object_api::unit_creature_type); }

        /// Its beast family, a CreatureFamily.dbc id; 0 for none.
        /// Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime.
        Expected<uint32_t> CreatureFamily() const { return ReadAs<uint32_t>(&wxl_object_api::unit_creature_family); }

        /// Its rank: normal, elite, rare, rare elite, world boss.
        /// Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime.
        Expected<wxl_classification> Classification() const { return ReadAs<wxl_classification>(&wxl_object_api::unit_classification); }

        /// Whether it knows a spell: the active player's spell book or its pet's; false for any other unit.
        /// Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime.
        Expected<bool> KnowsSpell(uint32_t spell_id) const
        {
            return ReadAtAs<bool>(&wxl_object_api::unit_knows_spell, static_cast<uint32_t>(spell_id));
        }

        /// The flags of its movement: forward 0x1, backward 0x2, ..., falling 0x1000, flying 0x2000000.
        /// Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime.
        Expected<uint32_t> MovementFlags() const { return ReadAs<uint32_t>(&wxl_object_api::unit_movement_flags); }

        /// One of its speeds, in yards a second: walk, run, swim, flight, ...; the turn and pitch rates in radians a second.
        /// Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime.
        Expected<float> Speed(wxl_move_type type) const
        {
            return ReadAtAs<float>(&wxl_object_api::unit_speed, static_cast<uint32_t>(type));
        }

        /// Whether it swims, as the client's IsSwimming reads it.
        /// Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime.
        Expected<bool> IsSwimming() const { return ReadAs<bool>(&wxl_object_api::unit_is_swimming); }

        /// Whether it flies, as the client's IsFlying reads it.
        /// Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime.
        Expected<bool> IsFlying() const { return ReadAs<bool>(&wxl_object_api::unit_is_flying); }

        /// Whether it is falling, as the client's IsFalling reads it.
        /// Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime.
        Expected<bool> IsFalling() const { return ReadAs<bool>(&wxl_object_api::unit_is_falling); }

        /// Whether it stands outdoors, as the client's IsOutdoors reads it: false in a building or a cave the world marks indoors, where a mount is refused.
        /// Came with abi 0.1.23: WXL_E_UNSUPPORTED on an older runtime.
        Expected<bool> IsOutdoors() const { return ReadAs<bool>(&wxl_object_api::unit_is_outdoors); }
