---@meta wxl
-- The Lua API of wxl.lua, for lua-language-server. Generated from the SDK's event
-- table (reflect.hpp) and object table (object_reads.def) by wxl-lua-types; do not
-- edit by hand.

---A guid, as "0x" and sixteen hex digits: nothing is lost and it keys a table.
---@alias wxl.Guid string

---An event's bytes, copied into a string.
---@alias wxl.Bytes string

---@class wxl.Vec3
---@field x number
---@field y number
---@field z number

---A file's content hash, as wxl.storage.HashFile answers it.
---@class wxl.FileHash
---@field md5 string thirty-two lower-case hex digits
---@field size integer bytes
---@field layer "archive"|"disk"|"folder"|"provider"|"served" where the bytes came from
---@field source string the archive, the file on the disk, or the mod that serves it

---What wxl.storage.ReloadAsset did.
---@class wxl.AssetReload
---@field textures integer textures read again in place
---@field models integer model files dropped from the model cache
---@field objects integer units and players whose model was made again
---@field busy integer textures still loading, left alone

---An element of an update field: a number, a guid's string for a guid or another
---64-bit value, a table of numbers for the bytes or the two halves of a dword.
---@alias wxl.FieldValue number|string|number[]

---A field of update_fields.def: the client's name, the kind's and the field's, or
---the field's alone where the object has one of that name.
---@alias wxl.FieldName
---| "OBJECT_FIELD_GUID"
---| "Object.Guid"
---| "OBJECT_FIELD_TYPE"
---| "Object.Type"
---| "OBJECT_FIELD_ENTRY"
---| "Object.Entry"
---| "OBJECT_FIELD_SCALE_X"
---| "Object.ScaleX"
---| "OBJECT_FIELD_PADDING"
---| "Object.Padding"
---| "ITEM_FIELD_OWNER"
---| "Item.Owner"
---| "ITEM_FIELD_CONTAINED"
---| "Item.Contained"
---| "ITEM_FIELD_CREATOR"
---| "Item.Creator"
---| "ITEM_FIELD_GIFTCREATOR"
---| "Item.GiftCreator"
---| "ITEM_FIELD_STACK_COUNT"
---| "Item.StackCount"
---| "ITEM_FIELD_DURATION"
---| "Item.Duration"
---| "ITEM_FIELD_SPELL_CHARGES"
---| "Item.SpellCharges"
---| "ITEM_FIELD_FLAGS"
---| "Item.Flags"
---| "ITEM_FIELD_ENCHANTMENT_1_1"
---| "Item.Enchantment1_1"
---| "ITEM_FIELD_ENCHANTMENT_1_3"
---| "Item.Enchantment1_3"
---| "ITEM_FIELD_ENCHANTMENT_2_1"
---| "Item.Enchantment2_1"
---| "ITEM_FIELD_ENCHANTMENT_2_3"
---| "Item.Enchantment2_3"
---| "ITEM_FIELD_ENCHANTMENT_3_1"
---| "Item.Enchantment3_1"
---| "ITEM_FIELD_ENCHANTMENT_3_3"
---| "Item.Enchantment3_3"
---| "ITEM_FIELD_ENCHANTMENT_4_1"
---| "Item.Enchantment4_1"
---| "ITEM_FIELD_ENCHANTMENT_4_3"
---| "Item.Enchantment4_3"
---| "ITEM_FIELD_ENCHANTMENT_5_1"
---| "Item.Enchantment5_1"
---| "ITEM_FIELD_ENCHANTMENT_5_3"
---| "Item.Enchantment5_3"
---| "ITEM_FIELD_ENCHANTMENT_6_1"
---| "Item.Enchantment6_1"
---| "ITEM_FIELD_ENCHANTMENT_6_3"
---| "Item.Enchantment6_3"
---| "ITEM_FIELD_ENCHANTMENT_7_1"
---| "Item.Enchantment7_1"
---| "ITEM_FIELD_ENCHANTMENT_7_3"
---| "Item.Enchantment7_3"
---| "ITEM_FIELD_ENCHANTMENT_8_1"
---| "Item.Enchantment8_1"
---| "ITEM_FIELD_ENCHANTMENT_8_3"
---| "Item.Enchantment8_3"
---| "ITEM_FIELD_ENCHANTMENT_9_1"
---| "Item.Enchantment9_1"
---| "ITEM_FIELD_ENCHANTMENT_9_3"
---| "Item.Enchantment9_3"
---| "ITEM_FIELD_ENCHANTMENT_10_1"
---| "Item.Enchantment10_1"
---| "ITEM_FIELD_ENCHANTMENT_10_3"
---| "Item.Enchantment10_3"
---| "ITEM_FIELD_ENCHANTMENT_11_1"
---| "Item.Enchantment11_1"
---| "ITEM_FIELD_ENCHANTMENT_11_3"
---| "Item.Enchantment11_3"
---| "ITEM_FIELD_ENCHANTMENT_12_1"
---| "Item.Enchantment12_1"
---| "ITEM_FIELD_ENCHANTMENT_12_3"
---| "Item.Enchantment12_3"
---| "ITEM_FIELD_PROPERTY_SEED"
---| "Item.PropertySeed"
---| "ITEM_FIELD_RANDOM_PROPERTIES_ID"
---| "Item.RandomPropertiesId"
---| "ITEM_FIELD_DURABILITY"
---| "Item.Durability"
---| "ITEM_FIELD_MAXDURABILITY"
---| "Item.MaxDurability"
---| "ITEM_FIELD_CREATE_PLAYED_TIME"
---| "Item.CreatePlayedTime"
---| "ITEM_FIELD_PAD"
---| "Item.Pad"
---| "CONTAINER_FIELD_NUM_SLOTS"
---| "Container.NumSlots"
---| "CONTAINER_ALIGN_PAD"
---| "Container.AlignPad"
---| "CONTAINER_FIELD_SLOT_1"
---| "Container.Slot1"
---| "UNIT_FIELD_CHARM"
---| "Unit.Charm"
---| "UNIT_FIELD_SUMMON"
---| "Unit.Summon"
---| "UNIT_FIELD_CRITTER"
---| "Unit.Critter"
---| "UNIT_FIELD_CHARMEDBY"
---| "Unit.CharmedBy"
---| "UNIT_FIELD_SUMMONEDBY"
---| "Unit.SummonedBy"
---| "UNIT_FIELD_CREATEDBY"
---| "Unit.CreatedBy"
---| "UNIT_FIELD_TARGET"
---| "Unit.Target"
---| "UNIT_FIELD_CHANNEL_OBJECT"
---| "Unit.ChannelObject"
---| "UNIT_CHANNEL_SPELL"
---| "Unit.ChannelSpell"
---| "UNIT_FIELD_BYTES_0"
---| "Unit.Bytes0"
---| "UNIT_FIELD_HEALTH"
---| "Unit.Health"
---| "UNIT_FIELD_POWER1"
---| "Unit.Power1"
---| "UNIT_FIELD_POWER2"
---| "Unit.Power2"
---| "UNIT_FIELD_POWER3"
---| "Unit.Power3"
---| "UNIT_FIELD_POWER4"
---| "Unit.Power4"
---| "UNIT_FIELD_POWER5"
---| "Unit.Power5"
---| "UNIT_FIELD_POWER6"
---| "Unit.Power6"
---| "UNIT_FIELD_POWER7"
---| "Unit.Power7"
---| "UNIT_FIELD_MAXHEALTH"
---| "Unit.MaxHealth"
---| "UNIT_FIELD_MAXPOWER1"
---| "Unit.MaxPower1"
---| "UNIT_FIELD_MAXPOWER2"
---| "Unit.MaxPower2"
---| "UNIT_FIELD_MAXPOWER3"
---| "Unit.MaxPower3"
---| "UNIT_FIELD_MAXPOWER4"
---| "Unit.MaxPower4"
---| "UNIT_FIELD_MAXPOWER5"
---| "Unit.MaxPower5"
---| "UNIT_FIELD_MAXPOWER6"
---| "Unit.MaxPower6"
---| "UNIT_FIELD_MAXPOWER7"
---| "Unit.MaxPower7"
---| "UNIT_FIELD_POWER_REGEN_FLAT_MODIFIER"
---| "Unit.PowerRegenFlatModifier"
---| "UNIT_FIELD_POWER_REGEN_INTERRUPTED_FLAT_MODIFIER"
---| "Unit.PowerRegenInterruptedFlatModifier"
---| "UNIT_FIELD_LEVEL"
---| "Unit.Level"
---| "UNIT_FIELD_FACTIONTEMPLATE"
---| "Unit.FactionTemplate"
---| "UNIT_VIRTUAL_ITEM_SLOT_ID"
---| "Unit.VirtualItemSlotId"
---| "UNIT_FIELD_FLAGS"
---| "Unit.Flags"
---| "UNIT_FIELD_FLAGS_2"
---| "Unit.Flags2"
---| "UNIT_FIELD_AURASTATE"
---| "Unit.AuraState"
---| "UNIT_FIELD_BASEATTACKTIME"
---| "Unit.BaseAttackTime"
---| "UNIT_FIELD_RANGEDATTACKTIME"
---| "Unit.RangedAttackTime"
---| "UNIT_FIELD_BOUNDINGRADIUS"
---| "Unit.BoundingRadius"
---| "UNIT_FIELD_COMBATREACH"
---| "Unit.CombatReach"
---| "UNIT_FIELD_DISPLAYID"
---| "Unit.DisplayId"
---| "UNIT_FIELD_NATIVEDISPLAYID"
---| "Unit.NativeDisplayId"
---| "UNIT_FIELD_MOUNTDISPLAYID"
---| "Unit.MountDisplayId"
---| "UNIT_FIELD_MINDAMAGE"
---| "Unit.MinDamage"
---| "UNIT_FIELD_MAXDAMAGE"
---| "Unit.MaxDamage"
---| "UNIT_FIELD_MINOFFHANDDAMAGE"
---| "Unit.MinOffhandDamage"
---| "UNIT_FIELD_MAXOFFHANDDAMAGE"
---| "Unit.MaxOffhandDamage"
---| "UNIT_FIELD_BYTES_1"
---| "Unit.Bytes1"
---| "UNIT_FIELD_PETNUMBER"
---| "Unit.PetNumber"
---| "UNIT_FIELD_PET_NAME_TIMESTAMP"
---| "Unit.PetNameTimestamp"
---| "UNIT_FIELD_PETEXPERIENCE"
---| "Unit.PetExperience"
---| "UNIT_FIELD_PETNEXTLEVELEXP"
---| "Unit.PetNextLevelExp"
---| "UNIT_DYNAMIC_FLAGS"
---| "Unit.DynamicFlags"
---| "UNIT_MOD_CAST_SPEED"
---| "Unit.ModCastSpeed"
---| "UNIT_CREATED_BY_SPELL"
---| "Unit.CreatedBySpell"
---| "UNIT_NPC_FLAGS"
---| "Unit.NpcFlags"
---| "UNIT_NPC_EMOTESTATE"
---| "Unit.NpcEmoteState"
---| "UNIT_FIELD_STAT0"
---| "Unit.Stat0"
---| "UNIT_FIELD_STAT1"
---| "Unit.Stat1"
---| "UNIT_FIELD_STAT2"
---| "Unit.Stat2"
---| "UNIT_FIELD_STAT3"
---| "Unit.Stat3"
---| "UNIT_FIELD_STAT4"
---| "Unit.Stat4"
---| "UNIT_FIELD_POSSTAT0"
---| "Unit.PosStat0"
---| "UNIT_FIELD_POSSTAT1"
---| "Unit.PosStat1"
---| "UNIT_FIELD_POSSTAT2"
---| "Unit.PosStat2"
---| "UNIT_FIELD_POSSTAT3"
---| "Unit.PosStat3"
---| "UNIT_FIELD_POSSTAT4"
---| "Unit.PosStat4"
---| "UNIT_FIELD_NEGSTAT0"
---| "Unit.NegStat0"
---| "UNIT_FIELD_NEGSTAT1"
---| "Unit.NegStat1"
---| "UNIT_FIELD_NEGSTAT2"
---| "Unit.NegStat2"
---| "UNIT_FIELD_NEGSTAT3"
---| "Unit.NegStat3"
---| "UNIT_FIELD_NEGSTAT4"
---| "Unit.NegStat4"
---| "UNIT_FIELD_RESISTANCES"
---| "Unit.Resistances"
---| "UNIT_FIELD_RESISTANCEBUFFMODSPOSITIVE"
---| "Unit.ResistanceBuffModsPositive"
---| "UNIT_FIELD_RESISTANCEBUFFMODSNEGATIVE"
---| "Unit.ResistanceBuffModsNegative"
---| "UNIT_FIELD_BASE_MANA"
---| "Unit.BaseMana"
---| "UNIT_FIELD_BASE_HEALTH"
---| "Unit.BaseHealth"
---| "UNIT_FIELD_BYTES_2"
---| "Unit.Bytes2"
---| "UNIT_FIELD_ATTACK_POWER"
---| "Unit.AttackPower"
---| "UNIT_FIELD_ATTACK_POWER_MODS"
---| "Unit.AttackPowerMods"
---| "UNIT_FIELD_ATTACK_POWER_MULTIPLIER"
---| "Unit.AttackPowerMultiplier"
---| "UNIT_FIELD_RANGED_ATTACK_POWER"
---| "Unit.RangedAttackPower"
---| "UNIT_FIELD_RANGED_ATTACK_POWER_MODS"
---| "Unit.RangedAttackPowerMods"
---| "UNIT_FIELD_RANGED_ATTACK_POWER_MULTIPLIER"
---| "Unit.RangedAttackPowerMultiplier"
---| "UNIT_FIELD_MINRANGEDDAMAGE"
---| "Unit.MinRangedDamage"
---| "UNIT_FIELD_MAXRANGEDDAMAGE"
---| "Unit.MaxRangedDamage"
---| "UNIT_FIELD_POWER_COST_MODIFIER"
---| "Unit.PowerCostModifier"
---| "UNIT_FIELD_POWER_COST_MULTIPLIER"
---| "Unit.PowerCostMultiplier"
---| "UNIT_FIELD_MAXHEALTHMODIFIER"
---| "Unit.MaxHealthModifier"
---| "UNIT_FIELD_HOVERHEIGHT"
---| "Unit.HoverHeight"
---| "UNIT_FIELD_PADDING"
---| "Unit.Padding"
---| "PLAYER_DUEL_ARBITER"
---| "Player.DuelArbiter"
---| "PLAYER_FLAGS"
---| "Player.Flags"
---| "PLAYER_GUILDID"
---| "Player.GuildId"
---| "PLAYER_GUILDRANK"
---| "Player.GuildRank"
---| "PLAYER_BYTES"
---| "Player.Bytes"
---| "PLAYER_BYTES_2"
---| "Player.Bytes2"
---| "PLAYER_BYTES_3"
---| "Player.Bytes3"
---| "PLAYER_DUEL_TEAM"
---| "Player.DuelTeam"
---| "PLAYER_GUILD_TIMESTAMP"
---| "Player.GuildTimestamp"
---| "PLAYER_QUEST_LOG_1_1"
---| "Player.QuestLog1_1"
---| "PLAYER_QUEST_LOG_1_2"
---| "Player.QuestLog1_2"
---| "PLAYER_QUEST_LOG_1_3"
---| "Player.QuestLog1_3"
---| "PLAYER_QUEST_LOG_1_4"
---| "Player.QuestLog1_4"
---| "PLAYER_QUEST_LOG_2_1"
---| "Player.QuestLog2_1"
---| "PLAYER_QUEST_LOG_2_2"
---| "Player.QuestLog2_2"
---| "PLAYER_QUEST_LOG_2_3"
---| "Player.QuestLog2_3"
---| "PLAYER_QUEST_LOG_2_5"
---| "Player.QuestLog2_5"
---| "PLAYER_QUEST_LOG_3_1"
---| "Player.QuestLog3_1"
---| "PLAYER_QUEST_LOG_3_2"
---| "Player.QuestLog3_2"
---| "PLAYER_QUEST_LOG_3_3"
---| "Player.QuestLog3_3"
---| "PLAYER_QUEST_LOG_3_5"
---| "Player.QuestLog3_5"
---| "PLAYER_QUEST_LOG_4_1"
---| "Player.QuestLog4_1"
---| "PLAYER_QUEST_LOG_4_2"
---| "Player.QuestLog4_2"
---| "PLAYER_QUEST_LOG_4_3"
---| "Player.QuestLog4_3"
---| "PLAYER_QUEST_LOG_4_5"
---| "Player.QuestLog4_5"
---| "PLAYER_QUEST_LOG_5_1"
---| "Player.QuestLog5_1"
---| "PLAYER_QUEST_LOG_5_2"
---| "Player.QuestLog5_2"
---| "PLAYER_QUEST_LOG_5_3"
---| "Player.QuestLog5_3"
---| "PLAYER_QUEST_LOG_5_5"
---| "Player.QuestLog5_5"
---| "PLAYER_QUEST_LOG_6_1"
---| "Player.QuestLog6_1"
---| "PLAYER_QUEST_LOG_6_2"
---| "Player.QuestLog6_2"
---| "PLAYER_QUEST_LOG_6_3"
---| "Player.QuestLog6_3"
---| "PLAYER_QUEST_LOG_6_5"
---| "Player.QuestLog6_5"
---| "PLAYER_QUEST_LOG_7_1"
---| "Player.QuestLog7_1"
---| "PLAYER_QUEST_LOG_7_2"
---| "Player.QuestLog7_2"
---| "PLAYER_QUEST_LOG_7_3"
---| "Player.QuestLog7_3"
---| "PLAYER_QUEST_LOG_7_5"
---| "Player.QuestLog7_5"
---| "PLAYER_QUEST_LOG_8_1"
---| "Player.QuestLog8_1"
---| "PLAYER_QUEST_LOG_8_2"
---| "Player.QuestLog8_2"
---| "PLAYER_QUEST_LOG_8_3"
---| "Player.QuestLog8_3"
---| "PLAYER_QUEST_LOG_8_5"
---| "Player.QuestLog8_5"
---| "PLAYER_QUEST_LOG_9_1"
---| "Player.QuestLog9_1"
---| "PLAYER_QUEST_LOG_9_2"
---| "Player.QuestLog9_2"
---| "PLAYER_QUEST_LOG_9_3"
---| "Player.QuestLog9_3"
---| "PLAYER_QUEST_LOG_9_5"
---| "Player.QuestLog9_5"
---| "PLAYER_QUEST_LOG_10_1"
---| "Player.QuestLog10_1"
---| "PLAYER_QUEST_LOG_10_2"
---| "Player.QuestLog10_2"
---| "PLAYER_QUEST_LOG_10_3"
---| "Player.QuestLog10_3"
---| "PLAYER_QUEST_LOG_10_5"
---| "Player.QuestLog10_5"
---| "PLAYER_QUEST_LOG_11_1"
---| "Player.QuestLog11_1"
---| "PLAYER_QUEST_LOG_11_2"
---| "Player.QuestLog11_2"
---| "PLAYER_QUEST_LOG_11_3"
---| "Player.QuestLog11_3"
---| "PLAYER_QUEST_LOG_11_5"
---| "Player.QuestLog11_5"
---| "PLAYER_QUEST_LOG_12_1"
---| "Player.QuestLog12_1"
---| "PLAYER_QUEST_LOG_12_2"
---| "Player.QuestLog12_2"
---| "PLAYER_QUEST_LOG_12_3"
---| "Player.QuestLog12_3"
---| "PLAYER_QUEST_LOG_12_5"
---| "Player.QuestLog12_5"
---| "PLAYER_QUEST_LOG_13_1"
---| "Player.QuestLog13_1"
---| "PLAYER_QUEST_LOG_13_2"
---| "Player.QuestLog13_2"
---| "PLAYER_QUEST_LOG_13_3"
---| "Player.QuestLog13_3"
---| "PLAYER_QUEST_LOG_13_5"
---| "Player.QuestLog13_5"
---| "PLAYER_QUEST_LOG_14_1"
---| "Player.QuestLog14_1"
---| "PLAYER_QUEST_LOG_14_2"
---| "Player.QuestLog14_2"
---| "PLAYER_QUEST_LOG_14_3"
---| "Player.QuestLog14_3"
---| "PLAYER_QUEST_LOG_14_5"
---| "Player.QuestLog14_5"
---| "PLAYER_QUEST_LOG_15_1"
---| "Player.QuestLog15_1"
---| "PLAYER_QUEST_LOG_15_2"
---| "Player.QuestLog15_2"
---| "PLAYER_QUEST_LOG_15_3"
---| "Player.QuestLog15_3"
---| "PLAYER_QUEST_LOG_15_5"
---| "Player.QuestLog15_5"
---| "PLAYER_QUEST_LOG_16_1"
---| "Player.QuestLog16_1"
---| "PLAYER_QUEST_LOG_16_2"
---| "Player.QuestLog16_2"
---| "PLAYER_QUEST_LOG_16_3"
---| "Player.QuestLog16_3"
---| "PLAYER_QUEST_LOG_16_5"
---| "Player.QuestLog16_5"
---| "PLAYER_QUEST_LOG_17_1"
---| "Player.QuestLog17_1"
---| "PLAYER_QUEST_LOG_17_2"
---| "Player.QuestLog17_2"
---| "PLAYER_QUEST_LOG_17_3"
---| "Player.QuestLog17_3"
---| "PLAYER_QUEST_LOG_17_5"
---| "Player.QuestLog17_5"
---| "PLAYER_QUEST_LOG_18_1"
---| "Player.QuestLog18_1"
---| "PLAYER_QUEST_LOG_18_2"
---| "Player.QuestLog18_2"
---| "PLAYER_QUEST_LOG_18_3"
---| "Player.QuestLog18_3"
---| "PLAYER_QUEST_LOG_18_5"
---| "Player.QuestLog18_5"
---| "PLAYER_QUEST_LOG_19_1"
---| "Player.QuestLog19_1"
---| "PLAYER_QUEST_LOG_19_2"
---| "Player.QuestLog19_2"
---| "PLAYER_QUEST_LOG_19_3"
---| "Player.QuestLog19_3"
---| "PLAYER_QUEST_LOG_19_5"
---| "Player.QuestLog19_5"
---| "PLAYER_QUEST_LOG_20_1"
---| "Player.QuestLog20_1"
---| "PLAYER_QUEST_LOG_20_2"
---| "Player.QuestLog20_2"
---| "PLAYER_QUEST_LOG_20_3"
---| "Player.QuestLog20_3"
---| "PLAYER_QUEST_LOG_20_5"
---| "Player.QuestLog20_5"
---| "PLAYER_QUEST_LOG_21_1"
---| "Player.QuestLog21_1"
---| "PLAYER_QUEST_LOG_21_2"
---| "Player.QuestLog21_2"
---| "PLAYER_QUEST_LOG_21_3"
---| "Player.QuestLog21_3"
---| "PLAYER_QUEST_LOG_21_5"
---| "Player.QuestLog21_5"
---| "PLAYER_QUEST_LOG_22_1"
---| "Player.QuestLog22_1"
---| "PLAYER_QUEST_LOG_22_2"
---| "Player.QuestLog22_2"
---| "PLAYER_QUEST_LOG_22_3"
---| "Player.QuestLog22_3"
---| "PLAYER_QUEST_LOG_22_5"
---| "Player.QuestLog22_5"
---| "PLAYER_QUEST_LOG_23_1"
---| "Player.QuestLog23_1"
---| "PLAYER_QUEST_LOG_23_2"
---| "Player.QuestLog23_2"
---| "PLAYER_QUEST_LOG_23_3"
---| "Player.QuestLog23_3"
---| "PLAYER_QUEST_LOG_23_5"
---| "Player.QuestLog23_5"
---| "PLAYER_QUEST_LOG_24_1"
---| "Player.QuestLog24_1"
---| "PLAYER_QUEST_LOG_24_2"
---| "Player.QuestLog24_2"
---| "PLAYER_QUEST_LOG_24_3"
---| "Player.QuestLog24_3"
---| "PLAYER_QUEST_LOG_24_5"
---| "Player.QuestLog24_5"
---| "PLAYER_QUEST_LOG_25_1"
---| "Player.QuestLog25_1"
---| "PLAYER_QUEST_LOG_25_2"
---| "Player.QuestLog25_2"
---| "PLAYER_QUEST_LOG_25_3"
---| "Player.QuestLog25_3"
---| "PLAYER_QUEST_LOG_25_5"
---| "Player.QuestLog25_5"
---| "PLAYER_VISIBLE_ITEM_1_ENTRYID"
---| "Player.VisibleItem1EntryId"
---| "PLAYER_VISIBLE_ITEM_1_ENCHANTMENT"
---| "Player.VisibleItem1Enchantment"
---| "PLAYER_VISIBLE_ITEM_2_ENTRYID"
---| "Player.VisibleItem2EntryId"
---| "PLAYER_VISIBLE_ITEM_2_ENCHANTMENT"
---| "Player.VisibleItem2Enchantment"
---| "PLAYER_VISIBLE_ITEM_3_ENTRYID"
---| "Player.VisibleItem3EntryId"
---| "PLAYER_VISIBLE_ITEM_3_ENCHANTMENT"
---| "Player.VisibleItem3Enchantment"
---| "PLAYER_VISIBLE_ITEM_4_ENTRYID"
---| "Player.VisibleItem4EntryId"
---| "PLAYER_VISIBLE_ITEM_4_ENCHANTMENT"
---| "Player.VisibleItem4Enchantment"
---| "PLAYER_VISIBLE_ITEM_5_ENTRYID"
---| "Player.VisibleItem5EntryId"
---| "PLAYER_VISIBLE_ITEM_5_ENCHANTMENT"
---| "Player.VisibleItem5Enchantment"
---| "PLAYER_VISIBLE_ITEM_6_ENTRYID"
---| "Player.VisibleItem6EntryId"
---| "PLAYER_VISIBLE_ITEM_6_ENCHANTMENT"
---| "Player.VisibleItem6Enchantment"
---| "PLAYER_VISIBLE_ITEM_7_ENTRYID"
---| "Player.VisibleItem7EntryId"
---| "PLAYER_VISIBLE_ITEM_7_ENCHANTMENT"
---| "Player.VisibleItem7Enchantment"
---| "PLAYER_VISIBLE_ITEM_8_ENTRYID"
---| "Player.VisibleItem8EntryId"
---| "PLAYER_VISIBLE_ITEM_8_ENCHANTMENT"
---| "Player.VisibleItem8Enchantment"
---| "PLAYER_VISIBLE_ITEM_9_ENTRYID"
---| "Player.VisibleItem9EntryId"
---| "PLAYER_VISIBLE_ITEM_9_ENCHANTMENT"
---| "Player.VisibleItem9Enchantment"
---| "PLAYER_VISIBLE_ITEM_10_ENTRYID"
---| "Player.VisibleItem10EntryId"
---| "PLAYER_VISIBLE_ITEM_10_ENCHANTMENT"
---| "Player.VisibleItem10Enchantment"
---| "PLAYER_VISIBLE_ITEM_11_ENTRYID"
---| "Player.VisibleItem11EntryId"
---| "PLAYER_VISIBLE_ITEM_11_ENCHANTMENT"
---| "Player.VisibleItem11Enchantment"
---| "PLAYER_VISIBLE_ITEM_12_ENTRYID"
---| "Player.VisibleItem12EntryId"
---| "PLAYER_VISIBLE_ITEM_12_ENCHANTMENT"
---| "Player.VisibleItem12Enchantment"
---| "PLAYER_VISIBLE_ITEM_13_ENTRYID"
---| "Player.VisibleItem13EntryId"
---| "PLAYER_VISIBLE_ITEM_13_ENCHANTMENT"
---| "Player.VisibleItem13Enchantment"
---| "PLAYER_VISIBLE_ITEM_14_ENTRYID"
---| "Player.VisibleItem14EntryId"
---| "PLAYER_VISIBLE_ITEM_14_ENCHANTMENT"
---| "Player.VisibleItem14Enchantment"
---| "PLAYER_VISIBLE_ITEM_15_ENTRYID"
---| "Player.VisibleItem15EntryId"
---| "PLAYER_VISIBLE_ITEM_15_ENCHANTMENT"
---| "Player.VisibleItem15Enchantment"
---| "PLAYER_VISIBLE_ITEM_16_ENTRYID"
---| "Player.VisibleItem16EntryId"
---| "PLAYER_VISIBLE_ITEM_16_ENCHANTMENT"
---| "Player.VisibleItem16Enchantment"
---| "PLAYER_VISIBLE_ITEM_17_ENTRYID"
---| "Player.VisibleItem17EntryId"
---| "PLAYER_VISIBLE_ITEM_17_ENCHANTMENT"
---| "Player.VisibleItem17Enchantment"
---| "PLAYER_VISIBLE_ITEM_18_ENTRYID"
---| "Player.VisibleItem18EntryId"
---| "PLAYER_VISIBLE_ITEM_18_ENCHANTMENT"
---| "Player.VisibleItem18Enchantment"
---| "PLAYER_VISIBLE_ITEM_19_ENTRYID"
---| "Player.VisibleItem19EntryId"
---| "PLAYER_VISIBLE_ITEM_19_ENCHANTMENT"
---| "Player.VisibleItem19Enchantment"
---| "PLAYER_CHOSEN_TITLE"
---| "Player.ChosenTitle"
---| "PLAYER_FAKE_INEBRIATION"
---| "Player.FakeInebriation"
---| "PLAYER_FIELD_PAD_0"
---| "Player.Pad0"
---| "PLAYER_FIELD_INV_SLOT_HEAD"
---| "Player.InvSlotHead"
---| "PLAYER_FIELD_PACK_SLOT_1"
---| "Player.PackSlot1"
---| "PLAYER_FIELD_BANK_SLOT_1"
---| "Player.BankSlot1"
---| "PLAYER_FIELD_BANKBAG_SLOT_1"
---| "Player.BankBagSlot1"
---| "PLAYER_FIELD_VENDORBUYBACK_SLOT_1"
---| "Player.VendorBuybackSlot1"
---| "PLAYER_FIELD_KEYRING_SLOT_1"
---| "Player.KeyringSlot1"
---| "PLAYER_FIELD_CURRENCYTOKEN_SLOT_1"
---| "Player.CurrencyTokenSlot1"
---| "PLAYER_FARSIGHT"
---| "Player.FarSight"
---| "PLAYER__FIELD_KNOWN_TITLES"
---| "Player.KnownTitles"
---| "PLAYER__FIELD_KNOWN_TITLES1"
---| "Player.KnownTitles1"
---| "PLAYER__FIELD_KNOWN_TITLES2"
---| "Player.KnownTitles2"
---| "PLAYER_FIELD_KNOWN_CURRENCIES"
---| "Player.KnownCurrencies"
---| "PLAYER_XP"
---| "Player.Xp"
---| "PLAYER_NEXT_LEVEL_XP"
---| "Player.NextLevelXp"
---| "PLAYER_SKILL_INFO_1_1"
---| "Player.SkillInfo1_1"
---| "PLAYER_CHARACTER_POINTS1"
---| "Player.CharacterPoints1"
---| "PLAYER_CHARACTER_POINTS2"
---| "Player.CharacterPoints2"
---| "PLAYER_TRACK_CREATURES"
---| "Player.TrackCreatures"
---| "PLAYER_TRACK_RESOURCES"
---| "Player.TrackResources"
---| "PLAYER_BLOCK_PERCENTAGE"
---| "Player.BlockPercentage"
---| "PLAYER_DODGE_PERCENTAGE"
---| "Player.DodgePercentage"
---| "PLAYER_PARRY_PERCENTAGE"
---| "Player.ParryPercentage"
---| "PLAYER_EXPERTISE"
---| "Player.Expertise"
---| "PLAYER_OFFHAND_EXPERTISE"
---| "Player.OffhandExpertise"
---| "PLAYER_CRIT_PERCENTAGE"
---| "Player.CritPercentage"
---| "PLAYER_RANGED_CRIT_PERCENTAGE"
---| "Player.RangedCritPercentage"
---| "PLAYER_OFFHAND_CRIT_PERCENTAGE"
---| "Player.OffhandCritPercentage"
---| "PLAYER_SPELL_CRIT_PERCENTAGE1"
---| "Player.SpellCritPercentage1"
---| "PLAYER_SHIELD_BLOCK"
---| "Player.ShieldBlock"
---| "PLAYER_SHIELD_BLOCK_CRIT_PERCENTAGE"
---| "Player.ShieldBlockCritPercentage"
---| "PLAYER_EXPLORED_ZONES_1"
---| "Player.ExploredZones1"
---| "PLAYER_REST_STATE_EXPERIENCE"
---| "Player.RestStateExperience"
---| "PLAYER_FIELD_COINAGE"
---| "Player.Coinage"
---| "PLAYER_FIELD_MOD_DAMAGE_DONE_POS"
---| "Player.ModDamageDonePos"
---| "PLAYER_FIELD_MOD_DAMAGE_DONE_NEG"
---| "Player.ModDamageDoneNeg"
---| "PLAYER_FIELD_MOD_DAMAGE_DONE_PCT"
---| "Player.ModDamageDonePct"
---| "PLAYER_FIELD_MOD_HEALING_DONE_POS"
---| "Player.ModHealingDonePos"
---| "PLAYER_FIELD_MOD_HEALING_PCT"
---| "Player.ModHealingPct"
---| "PLAYER_FIELD_MOD_HEALING_DONE_PCT"
---| "Player.ModHealingDonePct"
---| "PLAYER_FIELD_MOD_TARGET_RESISTANCE"
---| "Player.ModTargetResistance"
---| "PLAYER_FIELD_MOD_TARGET_PHYSICAL_RESISTANCE"
---| "Player.ModTargetPhysicalResistance"
---| "PLAYER_FIELD_BYTES"
---| "Player.FieldBytes"
---| "PLAYER_AMMO_ID"
---| "Player.AmmoId"
---| "PLAYER_SELF_RES_SPELL"
---| "Player.SelfResSpell"
---| "PLAYER_FIELD_PVP_MEDALS"
---| "Player.PvpMedals"
---| "PLAYER_FIELD_BUYBACK_PRICE_1"
---| "Player.BuybackPrice1"
---| "PLAYER_FIELD_BUYBACK_TIMESTAMP_1"
---| "Player.BuybackTimestamp1"
---| "PLAYER_FIELD_KILLS"
---| "Player.Kills"
---| "PLAYER_FIELD_TODAY_CONTRIBUTION"
---| "Player.TodayContribution"
---| "PLAYER_FIELD_YESTERDAY_CONTRIBUTION"
---| "Player.YesterdayContribution"
---| "PLAYER_FIELD_LIFETIME_HONORBALE_KILLS"
---| "Player.LifetimeHonorableKills"
---| "PLAYER_FIELD_BYTES2"
---| "Player.FieldBytes2"
---| "PLAYER_FIELD_WATCHED_FACTION_INDEX"
---| "Player.WatchedFactionIndex"
---| "PLAYER_FIELD_COMBAT_RATING_1"
---| "Player.CombatRating1"
---| "PLAYER_FIELD_ARENA_TEAM_INFO_1_1"
---| "Player.ArenaTeamInfo1_1"
---| "PLAYER_FIELD_HONOR_CURRENCY"
---| "Player.HonorCurrency"
---| "PLAYER_FIELD_ARENA_CURRENCY"
---| "Player.ArenaCurrency"
---| "PLAYER_FIELD_MAX_LEVEL"
---| "Player.MaxLevel"
---| "PLAYER_FIELD_DAILY_QUESTS_1"
---| "Player.DailyQuests1"
---| "PLAYER_RUNE_REGEN_1"
---| "Player.RuneRegen1"
---| "PLAYER_NO_REAGENT_COST_1"
---| "Player.NoReagentCost1"
---| "PLAYER_FIELD_GLYPH_SLOTS_1"
---| "Player.GlyphSlots1"
---| "PLAYER_FIELD_GLYPHS_1"
---| "Player.Glyphs1"
---| "PLAYER_GLYPHS_ENABLED"
---| "Player.GlyphsEnabled"
---| "PLAYER_PET_SPELL_POWER"
---| "Player.PetSpellPower"
---| "OBJECT_FIELD_CREATED_BY"
---| "GameObject.CreatedBy"
---| "GAMEOBJECT_DISPLAYID"
---| "GameObject.DisplayId"
---| "GAMEOBJECT_FLAGS"
---| "GameObject.Flags"
---| "GAMEOBJECT_PARENTROTATION"
---| "GameObject.ParentRotation"
---| "GAMEOBJECT_DYNAMIC"
---| "GameObject.Dynamic"
---| "GAMEOBJECT_FACTION"
---| "GameObject.Faction"
---| "GAMEOBJECT_LEVEL"
---| "GameObject.Level"
---| "GAMEOBJECT_BYTES_1"
---| "GameObject.Bytes1"
---| "DYNAMICOBJECT_CASTER"
---| "DynamicObject.Caster"
---| "DYNAMICOBJECT_BYTES"
---| "DynamicObject.Bytes"
---| "DYNAMICOBJECT_SPELLID"
---| "DynamicObject.SpellId"
---| "DYNAMICOBJECT_RADIUS"
---| "DynamicObject.Radius"
---| "DYNAMICOBJECT_CASTTIME"
---| "DynamicObject.CastTime"
---| "CORPSE_FIELD_OWNER"
---| "Corpse.Owner"
---| "CORPSE_FIELD_PARTY"
---| "Corpse.Party"
---| "CORPSE_FIELD_DISPLAY_ID"
---| "Corpse.DisplayId"
---| "CORPSE_FIELD_ITEM"
---| "Corpse.Item"
---| "CORPSE_FIELD_BYTES_1"
---| "Corpse.Bytes1"
---| "CORPSE_FIELD_BYTES_2"
---| "Corpse.Bytes2"
---| "CORPSE_FIELD_GUILD"
---| "Corpse.Guild"
---| "CORPSE_FIELD_FLAGS"
---| "Corpse.Flags"
---| "CORPSE_FIELD_DYNAMIC_FLAGS"
---| "Corpse.DynamicFlags"
---| "CORPSE_FIELD_PAD"
---| "Corpse.Pad"

---The dword index of every field of a Object.
---@class wxl.field.Object
---@field Guid integer OBJECT_FIELD_GUID
---@field Type integer OBJECT_FIELD_TYPE
---@field Entry integer OBJECT_FIELD_ENTRY
---@field ScaleX integer OBJECT_FIELD_SCALE_X
---@field Padding integer OBJECT_FIELD_PADDING

---The dword index of every field of a Item.
---@class wxl.field.Item
---@field Owner integer ITEM_FIELD_OWNER
---@field Contained integer ITEM_FIELD_CONTAINED
---@field Creator integer ITEM_FIELD_CREATOR
---@field GiftCreator integer ITEM_FIELD_GIFTCREATOR
---@field StackCount integer ITEM_FIELD_STACK_COUNT
---@field Duration integer ITEM_FIELD_DURATION
---@field SpellCharges integer ITEM_FIELD_SPELL_CHARGES
---@field Flags integer ITEM_FIELD_FLAGS
---@field Enchantment1_1 integer ITEM_FIELD_ENCHANTMENT_1_1
---@field Enchantment1_3 integer ITEM_FIELD_ENCHANTMENT_1_3
---@field Enchantment2_1 integer ITEM_FIELD_ENCHANTMENT_2_1
---@field Enchantment2_3 integer ITEM_FIELD_ENCHANTMENT_2_3
---@field Enchantment3_1 integer ITEM_FIELD_ENCHANTMENT_3_1
---@field Enchantment3_3 integer ITEM_FIELD_ENCHANTMENT_3_3
---@field Enchantment4_1 integer ITEM_FIELD_ENCHANTMENT_4_1
---@field Enchantment4_3 integer ITEM_FIELD_ENCHANTMENT_4_3
---@field Enchantment5_1 integer ITEM_FIELD_ENCHANTMENT_5_1
---@field Enchantment5_3 integer ITEM_FIELD_ENCHANTMENT_5_3
---@field Enchantment6_1 integer ITEM_FIELD_ENCHANTMENT_6_1
---@field Enchantment6_3 integer ITEM_FIELD_ENCHANTMENT_6_3
---@field Enchantment7_1 integer ITEM_FIELD_ENCHANTMENT_7_1
---@field Enchantment7_3 integer ITEM_FIELD_ENCHANTMENT_7_3
---@field Enchantment8_1 integer ITEM_FIELD_ENCHANTMENT_8_1
---@field Enchantment8_3 integer ITEM_FIELD_ENCHANTMENT_8_3
---@field Enchantment9_1 integer ITEM_FIELD_ENCHANTMENT_9_1
---@field Enchantment9_3 integer ITEM_FIELD_ENCHANTMENT_9_3
---@field Enchantment10_1 integer ITEM_FIELD_ENCHANTMENT_10_1
---@field Enchantment10_3 integer ITEM_FIELD_ENCHANTMENT_10_3
---@field Enchantment11_1 integer ITEM_FIELD_ENCHANTMENT_11_1
---@field Enchantment11_3 integer ITEM_FIELD_ENCHANTMENT_11_3
---@field Enchantment12_1 integer ITEM_FIELD_ENCHANTMENT_12_1
---@field Enchantment12_3 integer ITEM_FIELD_ENCHANTMENT_12_3
---@field PropertySeed integer ITEM_FIELD_PROPERTY_SEED
---@field RandomPropertiesId integer ITEM_FIELD_RANDOM_PROPERTIES_ID
---@field Durability integer ITEM_FIELD_DURABILITY
---@field MaxDurability integer ITEM_FIELD_MAXDURABILITY
---@field CreatePlayedTime integer ITEM_FIELD_CREATE_PLAYED_TIME
---@field Pad integer ITEM_FIELD_PAD

---The dword index of every field of a Container.
---@class wxl.field.Container
---@field NumSlots integer CONTAINER_FIELD_NUM_SLOTS
---@field AlignPad integer CONTAINER_ALIGN_PAD
---@field Slot1 integer CONTAINER_FIELD_SLOT_1

---The dword index of every field of a Unit.
---@class wxl.field.Unit
---@field Charm integer UNIT_FIELD_CHARM
---@field Summon integer UNIT_FIELD_SUMMON
---@field Critter integer UNIT_FIELD_CRITTER
---@field CharmedBy integer UNIT_FIELD_CHARMEDBY
---@field SummonedBy integer UNIT_FIELD_SUMMONEDBY
---@field CreatedBy integer UNIT_FIELD_CREATEDBY
---@field Target integer UNIT_FIELD_TARGET
---@field ChannelObject integer UNIT_FIELD_CHANNEL_OBJECT
---@field ChannelSpell integer UNIT_CHANNEL_SPELL
---@field Bytes0 integer UNIT_FIELD_BYTES_0
---@field Health integer UNIT_FIELD_HEALTH
---@field Power1 integer UNIT_FIELD_POWER1
---@field Power2 integer UNIT_FIELD_POWER2
---@field Power3 integer UNIT_FIELD_POWER3
---@field Power4 integer UNIT_FIELD_POWER4
---@field Power5 integer UNIT_FIELD_POWER5
---@field Power6 integer UNIT_FIELD_POWER6
---@field Power7 integer UNIT_FIELD_POWER7
---@field MaxHealth integer UNIT_FIELD_MAXHEALTH
---@field MaxPower1 integer UNIT_FIELD_MAXPOWER1
---@field MaxPower2 integer UNIT_FIELD_MAXPOWER2
---@field MaxPower3 integer UNIT_FIELD_MAXPOWER3
---@field MaxPower4 integer UNIT_FIELD_MAXPOWER4
---@field MaxPower5 integer UNIT_FIELD_MAXPOWER5
---@field MaxPower6 integer UNIT_FIELD_MAXPOWER6
---@field MaxPower7 integer UNIT_FIELD_MAXPOWER7
---@field PowerRegenFlatModifier integer UNIT_FIELD_POWER_REGEN_FLAT_MODIFIER
---@field PowerRegenInterruptedFlatModifier integer UNIT_FIELD_POWER_REGEN_INTERRUPTED_FLAT_MODIFIER
---@field Level integer UNIT_FIELD_LEVEL
---@field FactionTemplate integer UNIT_FIELD_FACTIONTEMPLATE
---@field VirtualItemSlotId integer UNIT_VIRTUAL_ITEM_SLOT_ID
---@field Flags integer UNIT_FIELD_FLAGS
---@field Flags2 integer UNIT_FIELD_FLAGS_2
---@field AuraState integer UNIT_FIELD_AURASTATE
---@field BaseAttackTime integer UNIT_FIELD_BASEATTACKTIME
---@field RangedAttackTime integer UNIT_FIELD_RANGEDATTACKTIME
---@field BoundingRadius integer UNIT_FIELD_BOUNDINGRADIUS
---@field CombatReach integer UNIT_FIELD_COMBATREACH
---@field DisplayId integer UNIT_FIELD_DISPLAYID
---@field NativeDisplayId integer UNIT_FIELD_NATIVEDISPLAYID
---@field MountDisplayId integer UNIT_FIELD_MOUNTDISPLAYID
---@field MinDamage integer UNIT_FIELD_MINDAMAGE
---@field MaxDamage integer UNIT_FIELD_MAXDAMAGE
---@field MinOffhandDamage integer UNIT_FIELD_MINOFFHANDDAMAGE
---@field MaxOffhandDamage integer UNIT_FIELD_MAXOFFHANDDAMAGE
---@field Bytes1 integer UNIT_FIELD_BYTES_1
---@field PetNumber integer UNIT_FIELD_PETNUMBER
---@field PetNameTimestamp integer UNIT_FIELD_PET_NAME_TIMESTAMP
---@field PetExperience integer UNIT_FIELD_PETEXPERIENCE
---@field PetNextLevelExp integer UNIT_FIELD_PETNEXTLEVELEXP
---@field DynamicFlags integer UNIT_DYNAMIC_FLAGS
---@field ModCastSpeed integer UNIT_MOD_CAST_SPEED
---@field CreatedBySpell integer UNIT_CREATED_BY_SPELL
---@field NpcFlags integer UNIT_NPC_FLAGS
---@field NpcEmoteState integer UNIT_NPC_EMOTESTATE
---@field Stat0 integer UNIT_FIELD_STAT0
---@field Stat1 integer UNIT_FIELD_STAT1
---@field Stat2 integer UNIT_FIELD_STAT2
---@field Stat3 integer UNIT_FIELD_STAT3
---@field Stat4 integer UNIT_FIELD_STAT4
---@field PosStat0 integer UNIT_FIELD_POSSTAT0
---@field PosStat1 integer UNIT_FIELD_POSSTAT1
---@field PosStat2 integer UNIT_FIELD_POSSTAT2
---@field PosStat3 integer UNIT_FIELD_POSSTAT3
---@field PosStat4 integer UNIT_FIELD_POSSTAT4
---@field NegStat0 integer UNIT_FIELD_NEGSTAT0
---@field NegStat1 integer UNIT_FIELD_NEGSTAT1
---@field NegStat2 integer UNIT_FIELD_NEGSTAT2
---@field NegStat3 integer UNIT_FIELD_NEGSTAT3
---@field NegStat4 integer UNIT_FIELD_NEGSTAT4
---@field Resistances integer UNIT_FIELD_RESISTANCES
---@field ResistanceBuffModsPositive integer UNIT_FIELD_RESISTANCEBUFFMODSPOSITIVE
---@field ResistanceBuffModsNegative integer UNIT_FIELD_RESISTANCEBUFFMODSNEGATIVE
---@field BaseMana integer UNIT_FIELD_BASE_MANA
---@field BaseHealth integer UNIT_FIELD_BASE_HEALTH
---@field Bytes2 integer UNIT_FIELD_BYTES_2
---@field AttackPower integer UNIT_FIELD_ATTACK_POWER
---@field AttackPowerMods integer UNIT_FIELD_ATTACK_POWER_MODS
---@field AttackPowerMultiplier integer UNIT_FIELD_ATTACK_POWER_MULTIPLIER
---@field RangedAttackPower integer UNIT_FIELD_RANGED_ATTACK_POWER
---@field RangedAttackPowerMods integer UNIT_FIELD_RANGED_ATTACK_POWER_MODS
---@field RangedAttackPowerMultiplier integer UNIT_FIELD_RANGED_ATTACK_POWER_MULTIPLIER
---@field MinRangedDamage integer UNIT_FIELD_MINRANGEDDAMAGE
---@field MaxRangedDamage integer UNIT_FIELD_MAXRANGEDDAMAGE
---@field PowerCostModifier integer UNIT_FIELD_POWER_COST_MODIFIER
---@field PowerCostMultiplier integer UNIT_FIELD_POWER_COST_MULTIPLIER
---@field MaxHealthModifier integer UNIT_FIELD_MAXHEALTHMODIFIER
---@field HoverHeight integer UNIT_FIELD_HOVERHEIGHT
---@field Padding integer UNIT_FIELD_PADDING

---The dword index of every field of a Player.
---@class wxl.field.Player
---@field DuelArbiter integer PLAYER_DUEL_ARBITER
---@field Flags integer PLAYER_FLAGS
---@field GuildId integer PLAYER_GUILDID
---@field GuildRank integer PLAYER_GUILDRANK
---@field Bytes integer PLAYER_BYTES
---@field Bytes2 integer PLAYER_BYTES_2
---@field Bytes3 integer PLAYER_BYTES_3
---@field DuelTeam integer PLAYER_DUEL_TEAM
---@field GuildTimestamp integer PLAYER_GUILD_TIMESTAMP
---@field QuestLog1_1 integer PLAYER_QUEST_LOG_1_1
---@field QuestLog1_2 integer PLAYER_QUEST_LOG_1_2
---@field QuestLog1_3 integer PLAYER_QUEST_LOG_1_3
---@field QuestLog1_4 integer PLAYER_QUEST_LOG_1_4
---@field QuestLog2_1 integer PLAYER_QUEST_LOG_2_1
---@field QuestLog2_2 integer PLAYER_QUEST_LOG_2_2
---@field QuestLog2_3 integer PLAYER_QUEST_LOG_2_3
---@field QuestLog2_5 integer PLAYER_QUEST_LOG_2_5
---@field QuestLog3_1 integer PLAYER_QUEST_LOG_3_1
---@field QuestLog3_2 integer PLAYER_QUEST_LOG_3_2
---@field QuestLog3_3 integer PLAYER_QUEST_LOG_3_3
---@field QuestLog3_5 integer PLAYER_QUEST_LOG_3_5
---@field QuestLog4_1 integer PLAYER_QUEST_LOG_4_1
---@field QuestLog4_2 integer PLAYER_QUEST_LOG_4_2
---@field QuestLog4_3 integer PLAYER_QUEST_LOG_4_3
---@field QuestLog4_5 integer PLAYER_QUEST_LOG_4_5
---@field QuestLog5_1 integer PLAYER_QUEST_LOG_5_1
---@field QuestLog5_2 integer PLAYER_QUEST_LOG_5_2
---@field QuestLog5_3 integer PLAYER_QUEST_LOG_5_3
---@field QuestLog5_5 integer PLAYER_QUEST_LOG_5_5
---@field QuestLog6_1 integer PLAYER_QUEST_LOG_6_1
---@field QuestLog6_2 integer PLAYER_QUEST_LOG_6_2
---@field QuestLog6_3 integer PLAYER_QUEST_LOG_6_3
---@field QuestLog6_5 integer PLAYER_QUEST_LOG_6_5
---@field QuestLog7_1 integer PLAYER_QUEST_LOG_7_1
---@field QuestLog7_2 integer PLAYER_QUEST_LOG_7_2
---@field QuestLog7_3 integer PLAYER_QUEST_LOG_7_3
---@field QuestLog7_5 integer PLAYER_QUEST_LOG_7_5
---@field QuestLog8_1 integer PLAYER_QUEST_LOG_8_1
---@field QuestLog8_2 integer PLAYER_QUEST_LOG_8_2
---@field QuestLog8_3 integer PLAYER_QUEST_LOG_8_3
---@field QuestLog8_5 integer PLAYER_QUEST_LOG_8_5
---@field QuestLog9_1 integer PLAYER_QUEST_LOG_9_1
---@field QuestLog9_2 integer PLAYER_QUEST_LOG_9_2
---@field QuestLog9_3 integer PLAYER_QUEST_LOG_9_3
---@field QuestLog9_5 integer PLAYER_QUEST_LOG_9_5
---@field QuestLog10_1 integer PLAYER_QUEST_LOG_10_1
---@field QuestLog10_2 integer PLAYER_QUEST_LOG_10_2
---@field QuestLog10_3 integer PLAYER_QUEST_LOG_10_3
---@field QuestLog10_5 integer PLAYER_QUEST_LOG_10_5
---@field QuestLog11_1 integer PLAYER_QUEST_LOG_11_1
---@field QuestLog11_2 integer PLAYER_QUEST_LOG_11_2
---@field QuestLog11_3 integer PLAYER_QUEST_LOG_11_3
---@field QuestLog11_5 integer PLAYER_QUEST_LOG_11_5
---@field QuestLog12_1 integer PLAYER_QUEST_LOG_12_1
---@field QuestLog12_2 integer PLAYER_QUEST_LOG_12_2
---@field QuestLog12_3 integer PLAYER_QUEST_LOG_12_3
---@field QuestLog12_5 integer PLAYER_QUEST_LOG_12_5
---@field QuestLog13_1 integer PLAYER_QUEST_LOG_13_1
---@field QuestLog13_2 integer PLAYER_QUEST_LOG_13_2
---@field QuestLog13_3 integer PLAYER_QUEST_LOG_13_3
---@field QuestLog13_5 integer PLAYER_QUEST_LOG_13_5
---@field QuestLog14_1 integer PLAYER_QUEST_LOG_14_1
---@field QuestLog14_2 integer PLAYER_QUEST_LOG_14_2
---@field QuestLog14_3 integer PLAYER_QUEST_LOG_14_3
---@field QuestLog14_5 integer PLAYER_QUEST_LOG_14_5
---@field QuestLog15_1 integer PLAYER_QUEST_LOG_15_1
---@field QuestLog15_2 integer PLAYER_QUEST_LOG_15_2
---@field QuestLog15_3 integer PLAYER_QUEST_LOG_15_3
---@field QuestLog15_5 integer PLAYER_QUEST_LOG_15_5
---@field QuestLog16_1 integer PLAYER_QUEST_LOG_16_1
---@field QuestLog16_2 integer PLAYER_QUEST_LOG_16_2
---@field QuestLog16_3 integer PLAYER_QUEST_LOG_16_3
---@field QuestLog16_5 integer PLAYER_QUEST_LOG_16_5
---@field QuestLog17_1 integer PLAYER_QUEST_LOG_17_1
---@field QuestLog17_2 integer PLAYER_QUEST_LOG_17_2
---@field QuestLog17_3 integer PLAYER_QUEST_LOG_17_3
---@field QuestLog17_5 integer PLAYER_QUEST_LOG_17_5
---@field QuestLog18_1 integer PLAYER_QUEST_LOG_18_1
---@field QuestLog18_2 integer PLAYER_QUEST_LOG_18_2
---@field QuestLog18_3 integer PLAYER_QUEST_LOG_18_3
---@field QuestLog18_5 integer PLAYER_QUEST_LOG_18_5
---@field QuestLog19_1 integer PLAYER_QUEST_LOG_19_1
---@field QuestLog19_2 integer PLAYER_QUEST_LOG_19_2
---@field QuestLog19_3 integer PLAYER_QUEST_LOG_19_3
---@field QuestLog19_5 integer PLAYER_QUEST_LOG_19_5
---@field QuestLog20_1 integer PLAYER_QUEST_LOG_20_1
---@field QuestLog20_2 integer PLAYER_QUEST_LOG_20_2
---@field QuestLog20_3 integer PLAYER_QUEST_LOG_20_3
---@field QuestLog20_5 integer PLAYER_QUEST_LOG_20_5
---@field QuestLog21_1 integer PLAYER_QUEST_LOG_21_1
---@field QuestLog21_2 integer PLAYER_QUEST_LOG_21_2
---@field QuestLog21_3 integer PLAYER_QUEST_LOG_21_3
---@field QuestLog21_5 integer PLAYER_QUEST_LOG_21_5
---@field QuestLog22_1 integer PLAYER_QUEST_LOG_22_1
---@field QuestLog22_2 integer PLAYER_QUEST_LOG_22_2
---@field QuestLog22_3 integer PLAYER_QUEST_LOG_22_3
---@field QuestLog22_5 integer PLAYER_QUEST_LOG_22_5
---@field QuestLog23_1 integer PLAYER_QUEST_LOG_23_1
---@field QuestLog23_2 integer PLAYER_QUEST_LOG_23_2
---@field QuestLog23_3 integer PLAYER_QUEST_LOG_23_3
---@field QuestLog23_5 integer PLAYER_QUEST_LOG_23_5
---@field QuestLog24_1 integer PLAYER_QUEST_LOG_24_1
---@field QuestLog24_2 integer PLAYER_QUEST_LOG_24_2
---@field QuestLog24_3 integer PLAYER_QUEST_LOG_24_3
---@field QuestLog24_5 integer PLAYER_QUEST_LOG_24_5
---@field QuestLog25_1 integer PLAYER_QUEST_LOG_25_1
---@field QuestLog25_2 integer PLAYER_QUEST_LOG_25_2
---@field QuestLog25_3 integer PLAYER_QUEST_LOG_25_3
---@field QuestLog25_5 integer PLAYER_QUEST_LOG_25_5
---@field VisibleItem1EntryId integer PLAYER_VISIBLE_ITEM_1_ENTRYID
---@field VisibleItem1Enchantment integer PLAYER_VISIBLE_ITEM_1_ENCHANTMENT
---@field VisibleItem2EntryId integer PLAYER_VISIBLE_ITEM_2_ENTRYID
---@field VisibleItem2Enchantment integer PLAYER_VISIBLE_ITEM_2_ENCHANTMENT
---@field VisibleItem3EntryId integer PLAYER_VISIBLE_ITEM_3_ENTRYID
---@field VisibleItem3Enchantment integer PLAYER_VISIBLE_ITEM_3_ENCHANTMENT
---@field VisibleItem4EntryId integer PLAYER_VISIBLE_ITEM_4_ENTRYID
---@field VisibleItem4Enchantment integer PLAYER_VISIBLE_ITEM_4_ENCHANTMENT
---@field VisibleItem5EntryId integer PLAYER_VISIBLE_ITEM_5_ENTRYID
---@field VisibleItem5Enchantment integer PLAYER_VISIBLE_ITEM_5_ENCHANTMENT
---@field VisibleItem6EntryId integer PLAYER_VISIBLE_ITEM_6_ENTRYID
---@field VisibleItem6Enchantment integer PLAYER_VISIBLE_ITEM_6_ENCHANTMENT
---@field VisibleItem7EntryId integer PLAYER_VISIBLE_ITEM_7_ENTRYID
---@field VisibleItem7Enchantment integer PLAYER_VISIBLE_ITEM_7_ENCHANTMENT
---@field VisibleItem8EntryId integer PLAYER_VISIBLE_ITEM_8_ENTRYID
---@field VisibleItem8Enchantment integer PLAYER_VISIBLE_ITEM_8_ENCHANTMENT
---@field VisibleItem9EntryId integer PLAYER_VISIBLE_ITEM_9_ENTRYID
---@field VisibleItem9Enchantment integer PLAYER_VISIBLE_ITEM_9_ENCHANTMENT
---@field VisibleItem10EntryId integer PLAYER_VISIBLE_ITEM_10_ENTRYID
---@field VisibleItem10Enchantment integer PLAYER_VISIBLE_ITEM_10_ENCHANTMENT
---@field VisibleItem11EntryId integer PLAYER_VISIBLE_ITEM_11_ENTRYID
---@field VisibleItem11Enchantment integer PLAYER_VISIBLE_ITEM_11_ENCHANTMENT
---@field VisibleItem12EntryId integer PLAYER_VISIBLE_ITEM_12_ENTRYID
---@field VisibleItem12Enchantment integer PLAYER_VISIBLE_ITEM_12_ENCHANTMENT
---@field VisibleItem13EntryId integer PLAYER_VISIBLE_ITEM_13_ENTRYID
---@field VisibleItem13Enchantment integer PLAYER_VISIBLE_ITEM_13_ENCHANTMENT
---@field VisibleItem14EntryId integer PLAYER_VISIBLE_ITEM_14_ENTRYID
---@field VisibleItem14Enchantment integer PLAYER_VISIBLE_ITEM_14_ENCHANTMENT
---@field VisibleItem15EntryId integer PLAYER_VISIBLE_ITEM_15_ENTRYID
---@field VisibleItem15Enchantment integer PLAYER_VISIBLE_ITEM_15_ENCHANTMENT
---@field VisibleItem16EntryId integer PLAYER_VISIBLE_ITEM_16_ENTRYID
---@field VisibleItem16Enchantment integer PLAYER_VISIBLE_ITEM_16_ENCHANTMENT
---@field VisibleItem17EntryId integer PLAYER_VISIBLE_ITEM_17_ENTRYID
---@field VisibleItem17Enchantment integer PLAYER_VISIBLE_ITEM_17_ENCHANTMENT
---@field VisibleItem18EntryId integer PLAYER_VISIBLE_ITEM_18_ENTRYID
---@field VisibleItem18Enchantment integer PLAYER_VISIBLE_ITEM_18_ENCHANTMENT
---@field VisibleItem19EntryId integer PLAYER_VISIBLE_ITEM_19_ENTRYID
---@field VisibleItem19Enchantment integer PLAYER_VISIBLE_ITEM_19_ENCHANTMENT
---@field ChosenTitle integer PLAYER_CHOSEN_TITLE
---@field FakeInebriation integer PLAYER_FAKE_INEBRIATION
---@field Pad0 integer PLAYER_FIELD_PAD_0
---@field InvSlotHead integer PLAYER_FIELD_INV_SLOT_HEAD
---@field PackSlot1 integer PLAYER_FIELD_PACK_SLOT_1
---@field BankSlot1 integer PLAYER_FIELD_BANK_SLOT_1
---@field BankBagSlot1 integer PLAYER_FIELD_BANKBAG_SLOT_1
---@field VendorBuybackSlot1 integer PLAYER_FIELD_VENDORBUYBACK_SLOT_1
---@field KeyringSlot1 integer PLAYER_FIELD_KEYRING_SLOT_1
---@field CurrencyTokenSlot1 integer PLAYER_FIELD_CURRENCYTOKEN_SLOT_1
---@field FarSight integer PLAYER_FARSIGHT
---@field KnownTitles integer PLAYER__FIELD_KNOWN_TITLES
---@field KnownTitles1 integer PLAYER__FIELD_KNOWN_TITLES1
---@field KnownTitles2 integer PLAYER__FIELD_KNOWN_TITLES2
---@field KnownCurrencies integer PLAYER_FIELD_KNOWN_CURRENCIES
---@field Xp integer PLAYER_XP
---@field NextLevelXp integer PLAYER_NEXT_LEVEL_XP
---@field SkillInfo1_1 integer PLAYER_SKILL_INFO_1_1
---@field CharacterPoints1 integer PLAYER_CHARACTER_POINTS1
---@field CharacterPoints2 integer PLAYER_CHARACTER_POINTS2
---@field TrackCreatures integer PLAYER_TRACK_CREATURES
---@field TrackResources integer PLAYER_TRACK_RESOURCES
---@field BlockPercentage integer PLAYER_BLOCK_PERCENTAGE
---@field DodgePercentage integer PLAYER_DODGE_PERCENTAGE
---@field ParryPercentage integer PLAYER_PARRY_PERCENTAGE
---@field Expertise integer PLAYER_EXPERTISE
---@field OffhandExpertise integer PLAYER_OFFHAND_EXPERTISE
---@field CritPercentage integer PLAYER_CRIT_PERCENTAGE
---@field RangedCritPercentage integer PLAYER_RANGED_CRIT_PERCENTAGE
---@field OffhandCritPercentage integer PLAYER_OFFHAND_CRIT_PERCENTAGE
---@field SpellCritPercentage1 integer PLAYER_SPELL_CRIT_PERCENTAGE1
---@field ShieldBlock integer PLAYER_SHIELD_BLOCK
---@field ShieldBlockCritPercentage integer PLAYER_SHIELD_BLOCK_CRIT_PERCENTAGE
---@field ExploredZones1 integer PLAYER_EXPLORED_ZONES_1
---@field RestStateExperience integer PLAYER_REST_STATE_EXPERIENCE
---@field Coinage integer PLAYER_FIELD_COINAGE
---@field ModDamageDonePos integer PLAYER_FIELD_MOD_DAMAGE_DONE_POS
---@field ModDamageDoneNeg integer PLAYER_FIELD_MOD_DAMAGE_DONE_NEG
---@field ModDamageDonePct integer PLAYER_FIELD_MOD_DAMAGE_DONE_PCT
---@field ModHealingDonePos integer PLAYER_FIELD_MOD_HEALING_DONE_POS
---@field ModHealingPct integer PLAYER_FIELD_MOD_HEALING_PCT
---@field ModHealingDonePct integer PLAYER_FIELD_MOD_HEALING_DONE_PCT
---@field ModTargetResistance integer PLAYER_FIELD_MOD_TARGET_RESISTANCE
---@field ModTargetPhysicalResistance integer PLAYER_FIELD_MOD_TARGET_PHYSICAL_RESISTANCE
---@field FieldBytes integer PLAYER_FIELD_BYTES
---@field AmmoId integer PLAYER_AMMO_ID
---@field SelfResSpell integer PLAYER_SELF_RES_SPELL
---@field PvpMedals integer PLAYER_FIELD_PVP_MEDALS
---@field BuybackPrice1 integer PLAYER_FIELD_BUYBACK_PRICE_1
---@field BuybackTimestamp1 integer PLAYER_FIELD_BUYBACK_TIMESTAMP_1
---@field Kills integer PLAYER_FIELD_KILLS
---@field TodayContribution integer PLAYER_FIELD_TODAY_CONTRIBUTION
---@field YesterdayContribution integer PLAYER_FIELD_YESTERDAY_CONTRIBUTION
---@field LifetimeHonorableKills integer PLAYER_FIELD_LIFETIME_HONORBALE_KILLS
---@field FieldBytes2 integer PLAYER_FIELD_BYTES2
---@field WatchedFactionIndex integer PLAYER_FIELD_WATCHED_FACTION_INDEX
---@field CombatRating1 integer PLAYER_FIELD_COMBAT_RATING_1
---@field ArenaTeamInfo1_1 integer PLAYER_FIELD_ARENA_TEAM_INFO_1_1
---@field HonorCurrency integer PLAYER_FIELD_HONOR_CURRENCY
---@field ArenaCurrency integer PLAYER_FIELD_ARENA_CURRENCY
---@field MaxLevel integer PLAYER_FIELD_MAX_LEVEL
---@field DailyQuests1 integer PLAYER_FIELD_DAILY_QUESTS_1
---@field RuneRegen1 integer PLAYER_RUNE_REGEN_1
---@field NoReagentCost1 integer PLAYER_NO_REAGENT_COST_1
---@field GlyphSlots1 integer PLAYER_FIELD_GLYPH_SLOTS_1
---@field Glyphs1 integer PLAYER_FIELD_GLYPHS_1
---@field GlyphsEnabled integer PLAYER_GLYPHS_ENABLED
---@field PetSpellPower integer PLAYER_PET_SPELL_POWER

---The dword index of every field of a GameObject.
---@class wxl.field.GameObject
---@field CreatedBy integer OBJECT_FIELD_CREATED_BY
---@field DisplayId integer GAMEOBJECT_DISPLAYID
---@field Flags integer GAMEOBJECT_FLAGS
---@field ParentRotation integer GAMEOBJECT_PARENTROTATION
---@field Dynamic integer GAMEOBJECT_DYNAMIC
---@field Faction integer GAMEOBJECT_FACTION
---@field Level integer GAMEOBJECT_LEVEL
---@field Bytes1 integer GAMEOBJECT_BYTES_1

---The dword index of every field of a DynamicObject.
---@class wxl.field.DynamicObject
---@field Caster integer DYNAMICOBJECT_CASTER
---@field Bytes integer DYNAMICOBJECT_BYTES
---@field SpellId integer DYNAMICOBJECT_SPELLID
---@field Radius integer DYNAMICOBJECT_RADIUS
---@field CastTime integer DYNAMICOBJECT_CASTTIME

---The dword index of every field of a Corpse.
---@class wxl.field.Corpse
---@field Owner integer CORPSE_FIELD_OWNER
---@field Party integer CORPSE_FIELD_PARTY
---@field DisplayId integer CORPSE_FIELD_DISPLAY_ID
---@field Item integer CORPSE_FIELD_ITEM
---@field Bytes1 integer CORPSE_FIELD_BYTES_1
---@field Bytes2 integer CORPSE_FIELD_BYTES_2
---@field Guild integer CORPSE_FIELD_GUILD
---@field Flags integer CORPSE_FIELD_FLAGS
---@field DynamicFlags integer CORPSE_FIELD_DYNAMIC_FLAGS
---@field Pad integer CORPSE_FIELD_PAD

---Every field's dword index, by kind: wxl.field.Unit.Health is UNIT_FIELD_HEALTH's.
---@class wxl.field
---@field Object wxl.field.Object
---@field Item wxl.field.Item
---@field Container wxl.field.Container
---@field Unit wxl.field.Unit
---@field Player wxl.field.Player
---@field GameObject wxl.field.GameObject
---@field DynamicObject wxl.field.DynamicObject
---@field Corpse wxl.field.Corpse

---A wxl_object_type value, by name; a newer value than these headers know
---arrives as its number.
---@alias wxl.ObjectType "Unknown"|"Item"|"Container"|"Unit"|"Player"|"GameObject"|"DynamicObject"|"Corpse"

---A wxl_script_context value, by name; a newer value than these headers know
---arrives as its number.
---@alias wxl.ScriptContext "glue"|"game"

---A wxl_mouse_button value, by name; a newer value than these headers know
---arrives as its number.
---@alias wxl.MouseButton "left"|"right"|"middle"

---A wxl_cooldown_change value, by name; a newer value than these headers know
---arrives as its number.
---@alias wxl.CooldownChange "started"|"cleared"|"shifted"

---A wxl_damage_kind value, by name; a newer value than these headers know
---arrives as its number.
---@alias wxl.DamageKind "melee"|"spell"|"periodic"|"environment"

---A wxl_environment value, by name; a newer value than these headers know
---arrives as its number.
---@alias wxl.Environment "exhaustion"|"drowning"|"fall"|"lava"|"slime"|"fire"

---A wxl_xp_source value, by name; a newer value than these headers know
---arrives as its number.
---@alias wxl.XpSource "kill"|"other"

---A wxl_loot_kind value, by name; a newer value than these headers know
---arrives as its number.
---@alias wxl.LootKind "corpse"|"pickpocket"|"fishing"|"disenchant"|"skinning"|"prospecting"|"milling"

---A wxl_loot_slot_type value, by name; a newer value than these headers know
---arrives as its number.
---@alias wxl.LootSlotType "take"|"roll"|"master"|"locked"|"owner"

---A wxl_roll_type value, by name; a newer value than these headers know
---arrives as its number.
---@alias wxl.RollType "pass"|"need"|"greed"|"disenchant"

---A wxl_loot_method value, by name; a newer value than these headers know
---arrives as its number.
---@alias wxl.LootMethod "free_for_all"|"round_robin"|"master"|"group"|"need_before_greed"

---A wxl_group_left_reason value, by name; a newer value than these headers know
---arrives as its number.
---@alias wxl.GroupLeftReason "removed"|"disbanded"

---A wxl_trade_status value, by name; a newer value than these headers know
---arrives as its number.
---@alias wxl.TradeStatus "busy"|"proposed"|"initiated"|"cancelled"|"accepted"|"already_trading"|"not_found"|"state_changed"|"complete"|"unaccepted"|"too_far"|"wrong_faction"|"failed"|"closed"|"ignored"|"stunned"|"target_stunned"|"dead"|"target_dead"|"logging_out"|"target_logging_out"|"restricted"|"wrong_realm"|"not_on_taplist"

---A wxl_window_kind value, by name; a newer value than these headers know
---arrives as its number.
---@alias wxl.WindowKind "gossip"|"merchant"|"trainer"|"bank"|"auction"|"mail"|"taxi"|"trade"|"quest"|"guild_bank"|"stable"|"petition"|"tabard"

---A wxl_texture_encoding value, by name; a newer value than these headers know
---arrives as its number.
---@alias wxl.TextureEncoding "jpeg"|"palette"|"dxt"|"raw"

---A wxl_shader_stage value, by name; a newer value than these headers know
---arrives as its number.
---@alias wxl.ShaderStage "vertex"|"hull"|"domain"|"geometry"|"pixel"|"compute"

---A wxl_power_type value, by name; a newer value than these headers know
---arrives as its number.
---@alias wxl.PowerType "mana"|"rage"|"focus"|"energy"|"happiness"|"runes"|"runic_power"

---A wxl_reaction value, by name; a newer value than these headers know
---arrives as its number.
---@alias wxl.Reaction "hated"|"hostile"|"unfriendly"|"neutral"|"friendly"|"honored"|"revered"|"exalted"

---A wxl_creature_type value, by name; a newer value than these headers know
---arrives as its number.
---@alias wxl.CreatureType "none"|"beast"|"dragonkin"|"demon"|"elemental"|"giant"|"undead"|"humanoid"|"critter"|"mechanical"|"not_specified"|"totem"|"non_combat_pet"|"gas_cloud"

---A wxl_classification value, by name; a newer value than these headers know
---arrives as its number.
---@alias wxl.Classification "normal"|"elite"|"rareelite"|"worldboss"|"rare"

---A wxl_stand_state value, by name; a newer value than these headers know
---arrives as its number.
---@alias wxl.StandState "standing"|"sitting"|"sitting_chair"|"sleeping"|"sitting_low_chair"|"sitting_medium_chair"|"sitting_high_chair"|"dead"|"kneeling"|"submerged"

---A wxl_move_type value, by name; a newer value than these headers know
---arrives as its number.
---@alias wxl.MoveType "walk"|"run"|"run_back"|"swim"|"swim_back"|"flight"|"flight_back"|"turn_rate"|"pitch_rate"

---A wxl_inventory_slot value, by name; a newer value than these headers know
---arrives as its number.
---@alias wxl.InventorySlot "head"|"neck"|"shoulder"|"shirt"|"chest"|"waist"|"legs"|"feet"|"wrist"|"hands"|"finger1"|"finger2"|"trinket1"|"trinket2"|"back"|"main_hand"|"off_hand"|"ranged"|"tabard"|"bag1"|"bag2"|"bag3"|"bag4"

---A wxl_item_quality value, by name; a newer value than these headers know
---arrives as its number.
---@alias wxl.ItemQuality "poor"|"common"|"uncommon"|"rare"|"epic"|"legendary"|"artifact"|"heirloom"

---A wxl_appearance_change value, by name; a newer value than these headers know
---arrives as its number.
---@alias wxl.AppearanceChange "worn"|"removed"|"freed"

---A wxl_asset_kind value, by name; a newer value than these headers know
---arrives as its number.
---@alias wxl.AssetKind "other"|"model"|"skin"|"animation"|"texture"|"terrain"|"map_object"|"sound"|"database"|"interface"|"shader"|"font"|"movie"

-- The script families. A function defined on one of these tables as On<Event> is
-- subscribed when the entry file has run and called with the event's parameters;
-- on an event that can be answered its return values are taken, as Override
-- takes them.

---@class WorldScript
---@field OnUpdate fun(self: WorldScript, delta: number)
---@field OnWorldEnter fun(self: WorldScript, map_id: integer, position: wxl.Vec3)
---@field OnWorldLeave fun(self: WorldScript, map_id: integer, position: wxl.Vec3)
---@field OnWorldClick fun(self: WorldScript, point: wxl.Vec3, object: wxl.Object|nil, button: wxl.MouseButton|integer)
---@field OnInput fun(self: WorldScript, message: integer, wparam: integer, lparam: integer, handled: boolean|nil): boolean|nil answers through handled
---@field OnSoundPlay fun(self: WorldScript, sound_id: integer, path: string, suppressed: boolean|nil): boolean|nil answers through suppressed
---@field OnZoneChanged fun(self: WorldScript, area_id: integer, subzone_id: integer, wmo_group_id: integer, zone_text: string, subzone_text: string, real_zone_text: string, indoors: boolean, new_area: boolean)
---@field OnMouseoverChanged fun(self: WorldScript, previous: wxl.Object|nil, previous_guid: wxl.Guid, current: wxl.Object|nil, current_guid: wxl.Guid)
---@field OnCVarChanged fun(self: WorldScript, name: string, previous: string, value: string)
---@field OnMemoryPressure fun(self: WorldScript, largest_free: integer, free: integer, committed: integer, threshold: integer)
---@field OnMouseMotion fun(self: WorldScript, dx: integer, dy: integer)
---@field OnMouseWheel fun(self: WorldScript, delta: number, handled: boolean|nil): boolean|nil answers through handled
WorldScript = {}

---@class RenderScript
---@field OnFrameBegin fun(self: RenderScript, index: integer, delta: number)
---@field OnFrameEnd fun(self: RenderScript, index: integer, delta: number)
---@field OnWorldRenderBegin fun(self: RenderScript, index: integer, delta: number)
---@field OnWorldRenderEnd fun(self: RenderScript, index: integer, delta: number)
---@field OnUiRenderEnd fun(self: RenderScript, index: integer, delta: number)
---@field OnSwapchainLost fun(self: RenderScript, width: integer, height: integer)
---@field OnSwapchainRestored fun(self: RenderScript, width: integer, height: integer)
---@field OnModelBatchDrawn fun(self: RenderScript, model: lightuserdata|nil, root_model: lightuserdata|nil, primitive_type: integer, base_vertex: integer, min_index: integer, vertex_count: integer, start_index: integer, primitive_count: integer, blend_mode: integer, viewport_x: integer, viewport_y: integer, viewport_width: integer, viewport_height: integer, viewport_min_z: number, viewport_max_z: number)
---@field OnCameraUpdate fun(self: RenderScript, index: integer, delta: number)
---@field OnImGuiFrame fun(self: RenderScript, index: integer, delta: number)
RenderScript = {}

---@class ObjectScript
---@field OnObjectCreate fun(self: ObjectScript, object: wxl.Object|nil, guid: wxl.Guid, type: wxl.ObjectType|integer)
---@field OnObjectUpdate fun(self: ObjectScript, object: wxl.Object|nil, guid: wxl.Guid, type: wxl.ObjectType|integer)
---@field OnObjectDestroy fun(self: ObjectScript, object: wxl.Object|nil, guid: wxl.Guid, type: wxl.ObjectType|integer)
---@field OnTargetChanged fun(self: ObjectScript, previous: wxl.Object|nil, current: wxl.Object|nil)
---@field OnDoodadSpawn fun(self: ObjectScript, doodad: wxl.Object|nil, path: string, position: wxl.Vec3)
---@field OnEquipmentChanged fun(self: ObjectScript, unit: wxl.Object|nil, slot: integer, item_id: integer)
---@field OnUnitDeath fun(self: ObjectScript, unit: wxl.Object|nil)
---@field OnUnitAuraChanged fun(self: ObjectScript, unit: wxl.Object|nil, spell_id: integer, removed: boolean)
---@field OnSpellCastStart fun(self: ObjectScript, caster: wxl.Object|nil, caster_guid: wxl.Guid, target: wxl.Object|nil, target_guid: wxl.Guid, cast_item: wxl.Guid, spell_id: integer, cast_count: integer, cast_flags: integer, target_flags: integer, cast_time: integer, hit_count: integer, hits: wxl.Bytes, misses: wxl.Bytes, miss_reasons: wxl.Bytes)
---@field OnSpellCastGo fun(self: ObjectScript, caster: wxl.Object|nil, caster_guid: wxl.Guid, target: wxl.Object|nil, target_guid: wxl.Guid, cast_item: wxl.Guid, spell_id: integer, cast_count: integer, cast_flags: integer, target_flags: integer, cast_time: integer, hit_count: integer, hits: wxl.Bytes, misses: wxl.Bytes, miss_reasons: wxl.Bytes)
---@field OnSpellCastFailed fun(self: ObjectScript, caster: wxl.Object|nil, caster_guid: wxl.Guid, spell_id: integer, cast_count: integer, reason: integer, custom_error: integer, custom_arg: integer)
---@field OnSpellCastInterrupted fun(self: ObjectScript, caster: wxl.Object|nil, caster_guid: wxl.Guid, spell_id: integer, cast_count: integer, reason: integer, custom_error: integer, custom_arg: integer)
---@field OnSpellCastRequest fun(self: ObjectScript, caster: wxl.Object|nil, spell_id: integer, item: wxl.Object|nil, target_guid: wxl.Guid, confirmed: boolean, refused: boolean|nil): boolean|nil answers through refused
---@field OnItemUseRequest fun(self: ObjectScript, item: wxl.Object|nil, item_guid: wxl.Guid, target_guid: wxl.Guid, confirmed: boolean, refused: boolean|nil): boolean|nil answers through refused
---@field OnSpellChannelStart fun(self: ObjectScript, caster: wxl.Object|nil, caster_guid: wxl.Guid, target: wxl.Object|nil, target_guid: wxl.Guid, spell_id: integer, duration: integer, remaining: integer)
---@field OnSpellChannelUpdate fun(self: ObjectScript, caster: wxl.Object|nil, caster_guid: wxl.Guid, target: wxl.Object|nil, target_guid: wxl.Guid, spell_id: integer, duration: integer, remaining: integer)
---@field OnSpellChannelStop fun(self: ObjectScript, caster: wxl.Object|nil, caster_guid: wxl.Guid, target: wxl.Object|nil, target_guid: wxl.Guid, spell_id: integer, duration: integer, remaining: integer)
---@field OnSpellCastResume fun(self: ObjectScript, caster: wxl.Object|nil, caster_guid: wxl.Guid, target: wxl.Object|nil, target_guid: wxl.Guid, spell_id: integer, duration: integer, remaining: integer)
---@field OnSpellCastDelayed fun(self: ObjectScript, caster: wxl.Object|nil, caster_guid: wxl.Guid, spell_id: integer, delay: integer)
---@field OnSpellCooldown fun(self: ObjectScript, unit: wxl.Object|nil, unit_guid: wxl.Guid, item: wxl.Object|nil, item_guid: wxl.Guid, spell_id: integer, duration: integer, flags: integer)
---@field OnSpellCooldownChanged fun(self: ObjectScript, unit: wxl.Object|nil, unit_guid: wxl.Guid, spell_id: integer, change: wxl.CooldownChange|integer, delay: integer)
---@field OnSpellCastCancel fun(self: ObjectScript, caster: wxl.Object|nil, caster_guid: wxl.Guid, spell_id: integer, cast_count: integer, reason: integer, notify_server: boolean)
---@field OnObjectFieldChanged fun(self: ObjectScript, object: wxl.Object|nil, type: wxl.ObjectType|integer, field: integer, count: integer, old_value: wxl.FieldValue, new_value: wxl.FieldValue)
---@field OnUnitAuraApplied fun(self: ObjectScript, unit: wxl.Object|nil, unit_guid: wxl.Guid, slot: integer, spell_id: integer, caster: wxl.Object|nil, caster_guid: wxl.Guid, stacks: integer, previous_stacks: integer, level: integer, flags: integer, harmful: boolean, duration: integer, remaining: integer)
---@field OnUnitAuraRemoved fun(self: ObjectScript, unit: wxl.Object|nil, unit_guid: wxl.Guid, slot: integer, spell_id: integer, caster: wxl.Object|nil, caster_guid: wxl.Guid, stacks: integer, previous_stacks: integer, level: integer, flags: integer, harmful: boolean, duration: integer, remaining: integer)
---@field OnUnitAuraRefreshed fun(self: ObjectScript, unit: wxl.Object|nil, unit_guid: wxl.Guid, slot: integer, spell_id: integer, caster: wxl.Object|nil, caster_guid: wxl.Guid, stacks: integer, previous_stacks: integer, level: integer, flags: integer, harmful: boolean, duration: integer, remaining: integer)
---@field OnUnitMountChanged fun(self: ObjectScript, unit: wxl.Object|nil, unit_guid: wxl.Guid, previous_display_id: integer, display_id: integer)
---@field OnAppearanceChanged fun(self: ObjectScript, change: wxl.AppearanceChange|integer, wearer: lightuserdata|nil, model: lightuserdata|nil, race_id: integer, sex_id: integer, item_slot: integer, display_id: integer, npc: boolean)
ObjectScript = {}

---@class AssetScript
---@field OnModelLoad fun(self: AssetScript, path: string, data: wxl.Bytes, replacement: lightuserdata|nil, replacement_size: lightuserdata|nil)
---@field OnTextureLoad fun(self: AssetScript, path: string)
---@field OnTerrainChunkBuild fun(self: AssetScript, map_id: integer, tile_x: integer, tile_y: integer, chunk_x: integer, chunk_y: integer)
---@field OnMapObjectLoad fun(self: AssetScript, path: string, data: wxl.Bytes)
---@field OnFileOpen fun(self: AssetScript, path: string, redirect: string|nil): string|nil answers through redirect
---@field OnFileOpened fun(self: AssetScript, path: string, found: boolean, size: integer)
---@field OnClientDbLoaded fun(self: AssetScript, name: string, records: lightuserdata|nil, record_count: integer, record_size: integer)
---@field OnArchiveMounted fun(self: AssetScript, path: string, priority: integer, hash_entries: integer, block_entries: integer, bytes: integer)
---@field OnArchiveUnmounted fun(self: AssetScript, path: string, priority: integer, hash_entries: integer, block_entries: integer, bytes: integer)
---@field OnTerrainTileLoad fun(self: AssetScript, path: string, tile_x: integer, tile_y: integer, data: wxl.Bytes)
---@field OnMapObjectGroupLoad fun(self: AssetScript, path: string, group: integer, data: wxl.Bytes)
---@field OnSkinProfileLoad fun(self: AssetScript, model: string, data: wxl.Bytes)
---@field OnTextureDecode fun(self: AssetScript, path: string, width: integer, height: integer, encoding: wxl.TextureEncoding|integer, alpha_bits: integer, alpha_kind: integer, mip_levels: integer)
---@field OnShaderLoad fun(self: AssetScript, stage: wxl.ShaderStage|integer, path: string, name: string, permutations: integer)
---@field OnSoundFileLoad fun(self: AssetScript, path: string, streaming: boolean, priority: integer)
---@field OnModelParsed fun(self: AssetScript, path: string, file: lightuserdata|nil, header: lightuserdata|nil, size: integer)
---@field OnSkinProfileParsed fun(self: AssetScript, path: string, file: lightuserdata|nil, header: lightuserdata|nil, skin: lightuserdata|nil)
---@field OnModelSequenceLoad fun(self: AssetScript, path: string, file: lightuserdata|nil, sequence_index: integer, data: wxl.Bytes)
---@field OnFileMissing fun(self: AssetScript, path: string, kind: wxl.AssetKind|integer, open: boolean, pending: boolean, loader: boolean)
AssetScript = {}

---@class NetScript
---@field OnMessageReceived fun(self: NetScript, opcode: integer, data: wxl.Bytes, suppressed: boolean|nil): boolean|nil answers through suppressed
---@field OnMessageSent fun(self: NetScript, opcode: integer, data: wxl.Bytes, suppressed: boolean|nil): boolean|nil answers through suppressed
---@field OnConnected fun(self: NetScript, address: string)
---@field OnDisconnected fun(self: NetScript, address: string)
NetScript = {}

---@class LuaScript
---@field OnScriptVmReady fun(self: LuaScript, context: wxl.ScriptContext|integer, native_state: lightuserdata|nil)
---@field OnScriptVmClosing fun(self: LuaScript, context: wxl.ScriptContext|integer, native_state: lightuserdata|nil)
---@field OnUiEvent fun(self: LuaScript, index: integer, name: string, format: string, values: wxl.Bytes, value_count: integer)
---@field OnScriptError fun(self: LuaScript, context: wxl.ScriptContext|integer, message: string)
LuaScript = {}

---@class CombatScript
---@field OnCombatAttackStart fun(self: CombatScript, attacker: wxl.Object|nil, attacker_guid: wxl.Guid, victim: wxl.Object|nil, victim_guid: wxl.Guid)
---@field OnCombatAttackStop fun(self: CombatScript, attacker: wxl.Object|nil, attacker_guid: wxl.Guid, victim: wxl.Object|nil, victim_guid: wxl.Guid)
---@field OnCombatDamage fun(self: CombatScript, source: wxl.Object|nil, source_guid: wxl.Guid, target: wxl.Object|nil, target_guid: wxl.Guid, spell_id: integer, kind: wxl.DamageKind|integer, environment: wxl.Environment|integer, amount: integer, overkill: integer, school: integer, absorbed: integer, resisted: integer, blocked: integer, crit: boolean, hit_info: integer)
---@field OnCombatHeal fun(self: CombatScript, source: wxl.Object|nil, source_guid: wxl.Guid, target: wxl.Object|nil, target_guid: wxl.Guid, spell_id: integer, amount: integer, overheal: integer, absorbed: integer, crit: boolean, periodic: boolean)
---@field OnCombatMiss fun(self: CombatScript, source: wxl.Object|nil, source_guid: wxl.Guid, target: wxl.Object|nil, target_guid: wxl.Guid, spell_id: integer, melee: boolean, reason: integer, hit_info: integer)
---@field OnCombatEnergize fun(self: CombatScript, source: wxl.Object|nil, source_guid: wxl.Guid, target: wxl.Object|nil, target_guid: wxl.Guid, spell_id: integer, power_type: integer, amount: integer, periodic: boolean, drain: boolean, multiplier: number)
CombatScript = {}

---@class PlayerScript
---@field OnPlayerMoveStart fun(self: PlayerScript, unit: wxl.Object|nil, unit_guid: wxl.Guid, opcode: integer, flags: integer, position: wxl.Vec3, facing: number)
---@field OnPlayerMoveStop fun(self: PlayerScript, unit: wxl.Object|nil, unit_guid: wxl.Guid, opcode: integer, flags: integer, position: wxl.Vec3, facing: number)
---@field OnPlayerLevelUp fun(self: PlayerScript, level: integer, health: integer, mana: integer, rage: integer, focus: integer, energy: integer, happiness: integer, runes: integer, runic_power: integer, strength: integer, agility: integer, stamina: integer, intellect: integer, spirit: integer)
---@field OnPlayerXpGain fun(self: PlayerScript, source: wxl.Object|nil, source_guid: wxl.Guid, amount: integer, kind: wxl.XpSource|integer, bonus: integer, group_multiplier: number, refer_a_friend: boolean)
---@field OnPlayerMoneyChanged fun(self: PlayerScript, player: wxl.Object|nil, previous: integer, current: integer)
---@field OnPlayerDied fun(self: PlayerScript, unit: wxl.Object|nil)
---@field OnPlayerReleased fun(self: PlayerScript, unit: wxl.Object|nil)
---@field OnPlayerResurrected fun(self: PlayerScript, unit: wxl.Object|nil)
---@field OnReputationChanged fun(self: PlayerScript, faction_id: integer, previous: integer, current: integer, bonus: number)
---@field OnAchievementEarned fun(self: PlayerScript, player: wxl.Object|nil, player_guid: wxl.Guid, achievement_id: integer, date: integer, silent: boolean)
PlayerScript = {}

---@class GlueScript
---@field OnGlueScreenChanged fun(self: GlueScript, screen: string, previous: string)
---@field OnGlueCharacterList fun(self: GlueScript, count: integer, valid: boolean)
---@field OnGlueCharacter fun(self: GlueScript, index: integer, count: integer, guid: wxl.Guid, name: string, race: integer, class_id: integer, sex: integer, level: integer, zone_id: integer, map_id: integer, position: wxl.Vec3, guild_id: integer, flags: integer, customize_flags: integer, first_login: boolean)
---@field OnGlueRealmList fun(self: GlueScript, count: integer)
---@field OnGlueRealm fun(self: GlueScript, index: integer, count: integer, name: string, address: string, type: integer, flags: integer, locked: boolean, population: number, characters: integer, category: integer, realm_id: integer, version_major: integer, version_minor: integer, version_revision: integer, version_build: integer, region_id: integer, battlegroup_id: integer, build: integer)
---@field OnGlueAuthResult fun(self: GlueScript, result: integer, queue_position: integer, billing_time_remaining: integer, billing_flags: integer, billing_time_rested: integer, expansion: integer, free_character_migration: boolean)
---@field OnGlueEnterWorld fun(self: GlueScript, character_guid: wxl.Guid)
GlueScript = {}

---@class ChatScript
---@field OnChatMessage fun(self: ChatScript, type: integer, language: integer, sender: wxl.Object|nil, sender_guid: wxl.Guid, sender_name: string, target_guid: wxl.Guid, target_name: string, channel: string, text: string, tag: integer, achievement_id: integer, gm: boolean, line_id: integer, shown_text: string)
---@field OnChatSend fun(self: ChatScript, text: string, type: string, language: string, target: string, prefix: string, addon: boolean, suppressed: boolean|nil): boolean|nil answers through suppressed
---@field OnChatAddonMessage fun(self: ChatScript, type: integer, sender: wxl.Object|nil, sender_guid: wxl.Guid, sender_name: string, prefix: string, text: string, channel: string, gm: boolean)
ChatScript = {}

---@class LootScript
---@field OnLootOpened fun(self: LootScript, source: wxl.Object|nil, source_guid: wxl.Guid, kind: wxl.LootKind|integer, money: integer, count: integer)
---@field OnLootSlot fun(self: LootScript, index: integer, slot: integer, item_id: integer, quantity: integer, display_id: integer, random_suffix: integer, random_property_id: integer, slot_type: wxl.LootSlotType|integer)
---@field OnLootSlotCleared fun(self: LootScript, index: integer, slot: integer, item_id: integer, quantity: integer, display_id: integer, random_suffix: integer, random_property_id: integer, slot_type: wxl.LootSlotType|integer)
---@field OnLootClosed fun(self: LootScript, source: wxl.Object|nil, source_guid: wxl.Guid, kind: wxl.LootKind|integer, money: integer, count: integer)
---@field OnLootMoney fun(self: LootScript, amount: integer, alone: boolean)
---@field OnLootRollStart fun(self: LootScript, source_guid: wxl.Guid, slot: integer, item_id: integer, random_suffix: integer, random_property_id: integer, quantity: integer, duration: integer, vote_mask: integer, roll_id: integer)
---@field OnLootRoll fun(self: LootScript, source_guid: wxl.Guid, slot: integer, item_id: integer, random_suffix: integer, random_property_id: integer, player_guid: wxl.Guid, roll: integer, roll_type: wxl.RollType|integer, auto_pass: boolean)
---@field OnLootRollWon fun(self: LootScript, source_guid: wxl.Guid, slot: integer, item_id: integer, random_suffix: integer, random_property_id: integer, player_guid: wxl.Guid, roll: integer, roll_type: wxl.RollType|integer, auto_pass: boolean)
---@field OnLootAllPassed fun(self: LootScript, source_guid: wxl.Guid, slot: integer, item_id: integer, random_suffix: integer, random_property_id: integer, player_guid: wxl.Guid, roll: integer, roll_type: wxl.RollType|integer, auto_pass: boolean)
LootScript = {}

---@class ItemScript
---@field OnItemPushed fun(self: ItemScript, player_guid: wxl.Guid, bag: integer, slot: integer, item_id: integer, suffix_factor: integer, random_property_id: integer, count: integer, total: integer, received: boolean, created: boolean, announced: boolean)
---@field OnInventorySlotChanged fun(self: ItemScript, container: wxl.Object|nil, container_guid: wxl.Guid, slot: integer, previous_guid: wxl.Guid, item: wxl.Object|nil, item_guid: wxl.Guid)
---@field OnItemDurabilityChanged fun(self: ItemScript, item: wxl.Object|nil, item_guid: wxl.Guid, previous: integer, current: integer, maximum: integer)
---@field OnItemStackChanged fun(self: ItemScript, item: wxl.Object|nil, item_guid: wxl.Guid, previous: integer, current: integer)
---@field OnInventoryError fun(self: ItemScript, result: integer, item_guid: wxl.Guid, other_guid: wxl.Guid, required_level: integer)
ItemScript = {}

---@class QuestScript
---@field OnQuestAccepted fun(self: QuestScript, quest_id: integer, log_slot: integer)
---@field OnQuestRemoved fun(self: QuestScript, quest_id: integer, log_slot: integer)
---@field OnQuestObjectiveChanged fun(self: QuestScript, quest_id: integer, log_slot: integer, objective: integer, previous: integer, current: integer)
---@field OnQuestCompleted fun(self: QuestScript, quest_id: integer, log_slot: integer)
---@field OnQuestFailed fun(self: QuestScript, quest_id: integer, log_slot: integer)
---@field OnQuestTurnedIn fun(self: QuestScript, quest_id: integer, xp: integer, money: integer, honor: integer, talents: integer, arena_points: integer)
---@field OnQuestGiverGreeting fun(self: QuestScript, giver: wxl.Object|nil, giver_guid: wxl.Guid, quest_id: integer, title: string, text: string, count: integer, shared_by: wxl.Guid)
---@field OnQuestGiverDetail fun(self: QuestScript, giver: wxl.Object|nil, giver_guid: wxl.Guid, quest_id: integer, title: string, text: string, count: integer, shared_by: wxl.Guid)
---@field OnQuestGiverProgress fun(self: QuestScript, giver: wxl.Object|nil, giver_guid: wxl.Guid, quest_id: integer, title: string, text: string, count: integer, shared_by: wxl.Guid)
---@field OnQuestGiverReward fun(self: QuestScript, giver: wxl.Object|nil, giver_guid: wxl.Guid, quest_id: integer, title: string, text: string, count: integer, shared_by: wxl.Guid)
QuestScript = {}

---@class GroupScript
---@field OnGroupInvite fun(self: GroupScript, inviter: string, can_accept: boolean, accept: boolean|nil, decline: boolean|nil): boolean|nil, boolean|nil answers through accept, then decline
---@field OnGroupUpdated fun(self: GroupScript, group_guid: wxl.Guid, raid: boolean, battleground: boolean, count: integer, subgroup: integer, leader_guid: wxl.Guid, loot_method: wxl.LootMethod|integer, looter_guid: wxl.Guid, loot_threshold: integer, dungeon_difficulty: integer, raid_difficulty: integer)
---@field OnGroupMember fun(self: GroupScript, index: integer, count: integer, member: wxl.Object|nil, member_guid: wxl.Guid, name: string, online: boolean, subgroup: integer, flags: integer, roles: integer)
---@field OnGroupLeft fun(self: GroupScript, reason: wxl.GroupLeftReason|integer)
---@field OnGroupLootMethodChanged fun(self: GroupScript, loot_method: wxl.LootMethod|integer, looter_guid: wxl.Guid, loot_threshold: integer)
---@field OnReadyCheck fun(self: GroupScript, initiator: wxl.Object|nil, initiator_guid: wxl.Guid, duration: integer)
---@field OnReadyCheckResponse fun(self: GroupScript, member: wxl.Object|nil, member_guid: wxl.Guid, ready: boolean)
---@field OnReadyCheckFinished fun(self: GroupScript, initiator_guid: wxl.Guid)
GroupScript = {}

---@class SocialScript
---@field OnDuelRequested fun(self: SocialScript, challenger: wxl.Object|nil, challenger_guid: wxl.Guid, arbiter_guid: wxl.Guid, accept: boolean|nil, decline: boolean|nil): boolean|nil, boolean|nil answers through accept, then decline
---@field OnDuelCountdown fun(self: SocialScript, duration: integer)
---@field OnDuelWon fun(self: SocialScript, winner: string, loser: string, fled: boolean)
---@field OnDuelFinished fun(self: SocialScript, completed: boolean)
---@field OnTradeStatus fun(self: SocialScript, status: wxl.TradeStatus|integer, partner_guid: wxl.Guid)
---@field OnEmote fun(self: SocialScript, unit: wxl.Object|nil, unit_guid: wxl.Guid, emote_id: integer)
---@field OnTextEmote fun(self: SocialScript, sender: wxl.Object|nil, sender_guid: wxl.Guid, text_emote: integer, emote_number: integer, target_name: string)
---@field OnResurrectRequest fun(self: SocialScript, offerer: wxl.Object|nil, offerer_guid: wxl.Guid, name: string, sickness: boolean, accept: boolean|nil, decline: boolean|nil): boolean|nil, boolean|nil answers through accept, then decline
---@field OnWindowOpened fun(self: SocialScript, kind: wxl.WindowKind|integer, npc: wxl.Object|nil, npc_guid: wxl.Guid)
---@field OnWindowClosed fun(self: SocialScript, kind: wxl.WindowKind|integer, npc: wxl.Object|nil, npc_guid: wxl.Guid)
SocialScript = {}

---@alias wxl.Family "WorldScript"|"RenderScript"|"ObjectScript"|"AssetScript"|"NetScript"|"LuaScript"|"CombatScript"|"PlayerScript"|"GlueScript"|"ChatScript"|"LootScript"|"ItemScript"|"QuestScript"|"GroupScript"|"SocialScript"

---Calls `handler` with the event's parameters whenever it is raised. Returns the
---handler's id.
---@overload fun(family: "WorldScript", method: "OnUpdate", handler: fun(delta: number)): integer
---@overload fun(family: "WorldScript", method: "OnWorldEnter", handler: fun(map_id: integer, position: wxl.Vec3)): integer
---@overload fun(family: "WorldScript", method: "OnWorldLeave", handler: fun(map_id: integer, position: wxl.Vec3)): integer
---@overload fun(family: "WorldScript", method: "OnWorldClick", handler: fun(point: wxl.Vec3, object: wxl.Object|nil, button: wxl.MouseButton|integer)): integer
---@overload fun(family: "WorldScript", method: "OnInput", handler: fun(message: integer, wparam: integer, lparam: integer, handled: boolean|nil)): integer
---@overload fun(family: "WorldScript", method: "OnSoundPlay", handler: fun(sound_id: integer, path: string, suppressed: boolean|nil)): integer
---@overload fun(family: "RenderScript", method: "OnFrameBegin", handler: fun(index: integer, delta: number)): integer
---@overload fun(family: "RenderScript", method: "OnFrameEnd", handler: fun(index: integer, delta: number)): integer
---@overload fun(family: "RenderScript", method: "OnWorldRenderBegin", handler: fun(index: integer, delta: number)): integer
---@overload fun(family: "RenderScript", method: "OnWorldRenderEnd", handler: fun(index: integer, delta: number)): integer
---@overload fun(family: "RenderScript", method: "OnUiRenderEnd", handler: fun(index: integer, delta: number)): integer
---@overload fun(family: "RenderScript", method: "OnSwapchainLost", handler: fun(width: integer, height: integer)): integer
---@overload fun(family: "RenderScript", method: "OnSwapchainRestored", handler: fun(width: integer, height: integer)): integer
---@overload fun(family: "ObjectScript", method: "OnObjectCreate", handler: fun(object: wxl.Object|nil, guid: wxl.Guid, type: wxl.ObjectType|integer)): integer
---@overload fun(family: "ObjectScript", method: "OnObjectUpdate", handler: fun(object: wxl.Object|nil, guid: wxl.Guid, type: wxl.ObjectType|integer)): integer
---@overload fun(family: "ObjectScript", method: "OnObjectDestroy", handler: fun(object: wxl.Object|nil, guid: wxl.Guid, type: wxl.ObjectType|integer)): integer
---@overload fun(family: "ObjectScript", method: "OnTargetChanged", handler: fun(previous: wxl.Object|nil, current: wxl.Object|nil)): integer
---@overload fun(family: "ObjectScript", method: "OnDoodadSpawn", handler: fun(doodad: wxl.Object|nil, path: string, position: wxl.Vec3)): integer
---@overload fun(family: "ObjectScript", method: "OnEquipmentChanged", handler: fun(unit: wxl.Object|nil, slot: integer, item_id: integer)): integer
---@overload fun(family: "AssetScript", method: "OnModelLoad", handler: fun(path: string, data: wxl.Bytes, replacement: lightuserdata|nil, replacement_size: lightuserdata|nil)): integer
---@overload fun(family: "AssetScript", method: "OnTextureLoad", handler: fun(path: string)): integer
---@overload fun(family: "AssetScript", method: "OnTerrainChunkBuild", handler: fun(map_id: integer, tile_x: integer, tile_y: integer, chunk_x: integer, chunk_y: integer)): integer
---@overload fun(family: "AssetScript", method: "OnMapObjectLoad", handler: fun(path: string, data: wxl.Bytes)): integer
---@overload fun(family: "AssetScript", method: "OnFileOpen", handler: fun(path: string, redirect: string|nil)): integer
---@overload fun(family: "AssetScript", method: "OnFileOpened", handler: fun(path: string, found: boolean, size: integer)): integer
---@overload fun(family: "AssetScript", method: "OnClientDbLoaded", handler: fun(name: string, records: lightuserdata|nil, record_count: integer, record_size: integer)): integer
---@overload fun(family: "NetScript", method: "OnMessageReceived", handler: fun(opcode: integer, data: wxl.Bytes, suppressed: boolean|nil)): integer
---@overload fun(family: "NetScript", method: "OnMessageSent", handler: fun(opcode: integer, data: wxl.Bytes, suppressed: boolean|nil)): integer
---@overload fun(family: "NetScript", method: "OnConnected", handler: fun(address: string)): integer
---@overload fun(family: "NetScript", method: "OnDisconnected", handler: fun(address: string)): integer
---@overload fun(family: "LuaScript", method: "OnScriptVmReady", handler: fun(context: wxl.ScriptContext|integer, native_state: lightuserdata|nil)): integer
---@overload fun(family: "LuaScript", method: "OnScriptVmClosing", handler: fun(context: wxl.ScriptContext|integer, native_state: lightuserdata|nil)): integer
---@overload fun(family: "ObjectScript", method: "OnUnitDeath", handler: fun(unit: wxl.Object|nil)): integer
---@overload fun(family: "ObjectScript", method: "OnUnitAuraChanged", handler: fun(unit: wxl.Object|nil, spell_id: integer, removed: boolean)): integer
---@overload fun(family: "ObjectScript", method: "OnSpellCastStart", handler: fun(caster: wxl.Object|nil, caster_guid: wxl.Guid, target: wxl.Object|nil, target_guid: wxl.Guid, cast_item: wxl.Guid, spell_id: integer, cast_count: integer, cast_flags: integer, target_flags: integer, cast_time: integer, hit_count: integer, hits: wxl.Bytes, misses: wxl.Bytes, miss_reasons: wxl.Bytes)): integer
---@overload fun(family: "ObjectScript", method: "OnSpellCastGo", handler: fun(caster: wxl.Object|nil, caster_guid: wxl.Guid, target: wxl.Object|nil, target_guid: wxl.Guid, cast_item: wxl.Guid, spell_id: integer, cast_count: integer, cast_flags: integer, target_flags: integer, cast_time: integer, hit_count: integer, hits: wxl.Bytes, misses: wxl.Bytes, miss_reasons: wxl.Bytes)): integer
---@overload fun(family: "ObjectScript", method: "OnSpellCastFailed", handler: fun(caster: wxl.Object|nil, caster_guid: wxl.Guid, spell_id: integer, cast_count: integer, reason: integer, custom_error: integer, custom_arg: integer)): integer
---@overload fun(family: "ObjectScript", method: "OnSpellCastInterrupted", handler: fun(caster: wxl.Object|nil, caster_guid: wxl.Guid, spell_id: integer, cast_count: integer, reason: integer, custom_error: integer, custom_arg: integer)): integer
---@overload fun(family: "ObjectScript", method: "OnSpellCastRequest", handler: fun(caster: wxl.Object|nil, spell_id: integer, item: wxl.Object|nil, target_guid: wxl.Guid, confirmed: boolean, refused: boolean|nil)): integer
---@overload fun(family: "ObjectScript", method: "OnItemUseRequest", handler: fun(item: wxl.Object|nil, item_guid: wxl.Guid, target_guid: wxl.Guid, confirmed: boolean, refused: boolean|nil)): integer
---@overload fun(family: "LuaScript", method: "OnUiEvent", handler: fun(index: integer, name: string, format: string, values: wxl.Bytes, value_count: integer)): integer
---@overload fun(family: "LuaScript", method: "OnScriptError", handler: fun(context: wxl.ScriptContext|integer, message: string)): integer
---@overload fun(family: "ObjectScript", method: "OnSpellChannelStart", handler: fun(caster: wxl.Object|nil, caster_guid: wxl.Guid, target: wxl.Object|nil, target_guid: wxl.Guid, spell_id: integer, duration: integer, remaining: integer)): integer
---@overload fun(family: "ObjectScript", method: "OnSpellChannelUpdate", handler: fun(caster: wxl.Object|nil, caster_guid: wxl.Guid, target: wxl.Object|nil, target_guid: wxl.Guid, spell_id: integer, duration: integer, remaining: integer)): integer
---@overload fun(family: "ObjectScript", method: "OnSpellChannelStop", handler: fun(caster: wxl.Object|nil, caster_guid: wxl.Guid, target: wxl.Object|nil, target_guid: wxl.Guid, spell_id: integer, duration: integer, remaining: integer)): integer
---@overload fun(family: "ObjectScript", method: "OnSpellCastResume", handler: fun(caster: wxl.Object|nil, caster_guid: wxl.Guid, target: wxl.Object|nil, target_guid: wxl.Guid, spell_id: integer, duration: integer, remaining: integer)): integer
---@overload fun(family: "ObjectScript", method: "OnSpellCastDelayed", handler: fun(caster: wxl.Object|nil, caster_guid: wxl.Guid, spell_id: integer, delay: integer)): integer
---@overload fun(family: "ObjectScript", method: "OnSpellCooldown", handler: fun(unit: wxl.Object|nil, unit_guid: wxl.Guid, item: wxl.Object|nil, item_guid: wxl.Guid, spell_id: integer, duration: integer, flags: integer)): integer
---@overload fun(family: "ObjectScript", method: "OnSpellCooldownChanged", handler: fun(unit: wxl.Object|nil, unit_guid: wxl.Guid, spell_id: integer, change: wxl.CooldownChange|integer, delay: integer)): integer
---@overload fun(family: "ObjectScript", method: "OnSpellCastCancel", handler: fun(caster: wxl.Object|nil, caster_guid: wxl.Guid, spell_id: integer, cast_count: integer, reason: integer, notify_server: boolean)): integer
---@overload fun(family: "CombatScript", method: "OnCombatAttackStart", handler: fun(attacker: wxl.Object|nil, attacker_guid: wxl.Guid, victim: wxl.Object|nil, victim_guid: wxl.Guid)): integer
---@overload fun(family: "CombatScript", method: "OnCombatAttackStop", handler: fun(attacker: wxl.Object|nil, attacker_guid: wxl.Guid, victim: wxl.Object|nil, victim_guid: wxl.Guid)): integer
---@overload fun(family: "CombatScript", method: "OnCombatDamage", handler: fun(source: wxl.Object|nil, source_guid: wxl.Guid, target: wxl.Object|nil, target_guid: wxl.Guid, spell_id: integer, kind: wxl.DamageKind|integer, environment: wxl.Environment|integer, amount: integer, overkill: integer, school: integer, absorbed: integer, resisted: integer, blocked: integer, crit: boolean, hit_info: integer)): integer
---@overload fun(family: "CombatScript", method: "OnCombatHeal", handler: fun(source: wxl.Object|nil, source_guid: wxl.Guid, target: wxl.Object|nil, target_guid: wxl.Guid, spell_id: integer, amount: integer, overheal: integer, absorbed: integer, crit: boolean, periodic: boolean)): integer
---@overload fun(family: "CombatScript", method: "OnCombatMiss", handler: fun(source: wxl.Object|nil, source_guid: wxl.Guid, target: wxl.Object|nil, target_guid: wxl.Guid, spell_id: integer, melee: boolean, reason: integer, hit_info: integer)): integer
---@overload fun(family: "WorldScript", method: "OnZoneChanged", handler: fun(area_id: integer, subzone_id: integer, wmo_group_id: integer, zone_text: string, subzone_text: string, real_zone_text: string, indoors: boolean, new_area: boolean)): integer
---@overload fun(family: "WorldScript", method: "OnMouseoverChanged", handler: fun(previous: wxl.Object|nil, previous_guid: wxl.Guid, current: wxl.Object|nil, current_guid: wxl.Guid)): integer
---@overload fun(family: "WorldScript", method: "OnCVarChanged", handler: fun(name: string, previous: string, value: string)): integer
---@overload fun(family: "PlayerScript", method: "OnPlayerMoveStart", handler: fun(unit: wxl.Object|nil, unit_guid: wxl.Guid, opcode: integer, flags: integer, position: wxl.Vec3, facing: number)): integer
---@overload fun(family: "PlayerScript", method: "OnPlayerMoveStop", handler: fun(unit: wxl.Object|nil, unit_guid: wxl.Guid, opcode: integer, flags: integer, position: wxl.Vec3, facing: number)): integer
---@overload fun(family: "PlayerScript", method: "OnPlayerLevelUp", handler: fun(level: integer, health: integer, mana: integer, rage: integer, focus: integer, energy: integer, happiness: integer, runes: integer, runic_power: integer, strength: integer, agility: integer, stamina: integer, intellect: integer, spirit: integer)): integer
---@overload fun(family: "PlayerScript", method: "OnPlayerXpGain", handler: fun(source: wxl.Object|nil, source_guid: wxl.Guid, amount: integer, kind: wxl.XpSource|integer, bonus: integer, group_multiplier: number, refer_a_friend: boolean)): integer
---@overload fun(family: "PlayerScript", method: "OnPlayerMoneyChanged", handler: fun(player: wxl.Object|nil, previous: integer, current: integer)): integer
---@overload fun(family: "GlueScript", method: "OnGlueScreenChanged", handler: fun(screen: string, previous: string)): integer
---@overload fun(family: "GlueScript", method: "OnGlueCharacterList", handler: fun(count: integer, valid: boolean)): integer
---@overload fun(family: "GlueScript", method: "OnGlueCharacter", handler: fun(index: integer, count: integer, guid: wxl.Guid, name: string, race: integer, class_id: integer, sex: integer, level: integer, zone_id: integer, map_id: integer, position: wxl.Vec3, guild_id: integer, flags: integer, customize_flags: integer, first_login: boolean)): integer
---@overload fun(family: "GlueScript", method: "OnGlueRealmList", handler: fun(count: integer)): integer
---@overload fun(family: "GlueScript", method: "OnGlueRealm", handler: fun(index: integer, count: integer, name: string, address: string, type: integer, flags: integer, locked: boolean, population: number, characters: integer, category: integer, realm_id: integer, version_major: integer, version_minor: integer, version_revision: integer, version_build: integer, region_id: integer, battlegroup_id: integer, build: integer)): integer
---@overload fun(family: "GlueScript", method: "OnGlueAuthResult", handler: fun(result: integer, queue_position: integer, billing_time_remaining: integer, billing_flags: integer, billing_time_rested: integer, expansion: integer, free_character_migration: boolean)): integer
---@overload fun(family: "GlueScript", method: "OnGlueEnterWorld", handler: fun(character_guid: wxl.Guid)): integer
---@overload fun(family: "ChatScript", method: "OnChatMessage", handler: fun(type: integer, language: integer, sender: wxl.Object|nil, sender_guid: wxl.Guid, sender_name: string, target_guid: wxl.Guid, target_name: string, channel: string, text: string, tag: integer, achievement_id: integer, gm: boolean, line_id: integer, shown_text: string)): integer
---@overload fun(family: "ChatScript", method: "OnChatSend", handler: fun(text: string, type: string, language: string, target: string, prefix: string, addon: boolean, suppressed: boolean|nil)): integer
---@overload fun(family: "CombatScript", method: "OnCombatEnergize", handler: fun(source: wxl.Object|nil, source_guid: wxl.Guid, target: wxl.Object|nil, target_guid: wxl.Guid, spell_id: integer, power_type: integer, amount: integer, periodic: boolean, drain: boolean, multiplier: number)): integer
---@overload fun(family: "ChatScript", method: "OnChatAddonMessage", handler: fun(type: integer, sender: wxl.Object|nil, sender_guid: wxl.Guid, sender_name: string, prefix: string, text: string, channel: string, gm: boolean)): integer
---@overload fun(family: "ObjectScript", method: "OnObjectFieldChanged", handler: fun(object: wxl.Object|nil, type: wxl.ObjectType|integer, field: integer, count: integer, old_value: wxl.FieldValue, new_value: wxl.FieldValue)): integer
---@overload fun(family: "AssetScript", method: "OnArchiveMounted", handler: fun(path: string, priority: integer, hash_entries: integer, block_entries: integer, bytes: integer)): integer
---@overload fun(family: "AssetScript", method: "OnArchiveUnmounted", handler: fun(path: string, priority: integer, hash_entries: integer, block_entries: integer, bytes: integer)): integer
---@overload fun(family: "WorldScript", method: "OnMemoryPressure", handler: fun(largest_free: integer, free: integer, committed: integer, threshold: integer)): integer
---@overload fun(family: "ObjectScript", method: "OnUnitAuraApplied", handler: fun(unit: wxl.Object|nil, unit_guid: wxl.Guid, slot: integer, spell_id: integer, caster: wxl.Object|nil, caster_guid: wxl.Guid, stacks: integer, previous_stacks: integer, level: integer, flags: integer, harmful: boolean, duration: integer, remaining: integer)): integer
---@overload fun(family: "ObjectScript", method: "OnUnitAuraRemoved", handler: fun(unit: wxl.Object|nil, unit_guid: wxl.Guid, slot: integer, spell_id: integer, caster: wxl.Object|nil, caster_guid: wxl.Guid, stacks: integer, previous_stacks: integer, level: integer, flags: integer, harmful: boolean, duration: integer, remaining: integer)): integer
---@overload fun(family: "ObjectScript", method: "OnUnitAuraRefreshed", handler: fun(unit: wxl.Object|nil, unit_guid: wxl.Guid, slot: integer, spell_id: integer, caster: wxl.Object|nil, caster_guid: wxl.Guid, stacks: integer, previous_stacks: integer, level: integer, flags: integer, harmful: boolean, duration: integer, remaining: integer)): integer
---@overload fun(family: "ObjectScript", method: "OnUnitMountChanged", handler: fun(unit: wxl.Object|nil, unit_guid: wxl.Guid, previous_display_id: integer, display_id: integer)): integer
---@overload fun(family: "LootScript", method: "OnLootOpened", handler: fun(source: wxl.Object|nil, source_guid: wxl.Guid, kind: wxl.LootKind|integer, money: integer, count: integer)): integer
---@overload fun(family: "LootScript", method: "OnLootSlot", handler: fun(index: integer, slot: integer, item_id: integer, quantity: integer, display_id: integer, random_suffix: integer, random_property_id: integer, slot_type: wxl.LootSlotType|integer)): integer
---@overload fun(family: "LootScript", method: "OnLootSlotCleared", handler: fun(index: integer, slot: integer, item_id: integer, quantity: integer, display_id: integer, random_suffix: integer, random_property_id: integer, slot_type: wxl.LootSlotType|integer)): integer
---@overload fun(family: "LootScript", method: "OnLootClosed", handler: fun(source: wxl.Object|nil, source_guid: wxl.Guid, kind: wxl.LootKind|integer, money: integer, count: integer)): integer
---@overload fun(family: "LootScript", method: "OnLootMoney", handler: fun(amount: integer, alone: boolean)): integer
---@overload fun(family: "LootScript", method: "OnLootRollStart", handler: fun(source_guid: wxl.Guid, slot: integer, item_id: integer, random_suffix: integer, random_property_id: integer, quantity: integer, duration: integer, vote_mask: integer, roll_id: integer)): integer
---@overload fun(family: "LootScript", method: "OnLootRoll", handler: fun(source_guid: wxl.Guid, slot: integer, item_id: integer, random_suffix: integer, random_property_id: integer, player_guid: wxl.Guid, roll: integer, roll_type: wxl.RollType|integer, auto_pass: boolean)): integer
---@overload fun(family: "LootScript", method: "OnLootRollWon", handler: fun(source_guid: wxl.Guid, slot: integer, item_id: integer, random_suffix: integer, random_property_id: integer, player_guid: wxl.Guid, roll: integer, roll_type: wxl.RollType|integer, auto_pass: boolean)): integer
---@overload fun(family: "LootScript", method: "OnLootAllPassed", handler: fun(source_guid: wxl.Guid, slot: integer, item_id: integer, random_suffix: integer, random_property_id: integer, player_guid: wxl.Guid, roll: integer, roll_type: wxl.RollType|integer, auto_pass: boolean)): integer
---@overload fun(family: "ItemScript", method: "OnItemPushed", handler: fun(player_guid: wxl.Guid, bag: integer, slot: integer, item_id: integer, suffix_factor: integer, random_property_id: integer, count: integer, total: integer, received: boolean, created: boolean, announced: boolean)): integer
---@overload fun(family: "ItemScript", method: "OnInventorySlotChanged", handler: fun(container: wxl.Object|nil, container_guid: wxl.Guid, slot: integer, previous_guid: wxl.Guid, item: wxl.Object|nil, item_guid: wxl.Guid)): integer
---@overload fun(family: "ItemScript", method: "OnItemDurabilityChanged", handler: fun(item: wxl.Object|nil, item_guid: wxl.Guid, previous: integer, current: integer, maximum: integer)): integer
---@overload fun(family: "ItemScript", method: "OnItemStackChanged", handler: fun(item: wxl.Object|nil, item_guid: wxl.Guid, previous: integer, current: integer)): integer
---@overload fun(family: "ItemScript", method: "OnInventoryError", handler: fun(result: integer, item_guid: wxl.Guid, other_guid: wxl.Guid, required_level: integer)): integer
---@overload fun(family: "QuestScript", method: "OnQuestAccepted", handler: fun(quest_id: integer, log_slot: integer)): integer
---@overload fun(family: "QuestScript", method: "OnQuestRemoved", handler: fun(quest_id: integer, log_slot: integer)): integer
---@overload fun(family: "QuestScript", method: "OnQuestObjectiveChanged", handler: fun(quest_id: integer, log_slot: integer, objective: integer, previous: integer, current: integer)): integer
---@overload fun(family: "QuestScript", method: "OnQuestCompleted", handler: fun(quest_id: integer, log_slot: integer)): integer
---@overload fun(family: "QuestScript", method: "OnQuestFailed", handler: fun(quest_id: integer, log_slot: integer)): integer
---@overload fun(family: "QuestScript", method: "OnQuestTurnedIn", handler: fun(quest_id: integer, xp: integer, money: integer, honor: integer, talents: integer, arena_points: integer)): integer
---@overload fun(family: "QuestScript", method: "OnQuestGiverGreeting", handler: fun(giver: wxl.Object|nil, giver_guid: wxl.Guid, quest_id: integer, title: string, text: string, count: integer, shared_by: wxl.Guid)): integer
---@overload fun(family: "QuestScript", method: "OnQuestGiverDetail", handler: fun(giver: wxl.Object|nil, giver_guid: wxl.Guid, quest_id: integer, title: string, text: string, count: integer, shared_by: wxl.Guid)): integer
---@overload fun(family: "QuestScript", method: "OnQuestGiverProgress", handler: fun(giver: wxl.Object|nil, giver_guid: wxl.Guid, quest_id: integer, title: string, text: string, count: integer, shared_by: wxl.Guid)): integer
---@overload fun(family: "QuestScript", method: "OnQuestGiverReward", handler: fun(giver: wxl.Object|nil, giver_guid: wxl.Guid, quest_id: integer, title: string, text: string, count: integer, shared_by: wxl.Guid)): integer
---@overload fun(family: "GroupScript", method: "OnGroupInvite", handler: fun(inviter: string, can_accept: boolean, accept: boolean|nil, decline: boolean|nil)): integer
---@overload fun(family: "GroupScript", method: "OnGroupUpdated", handler: fun(group_guid: wxl.Guid, raid: boolean, battleground: boolean, count: integer, subgroup: integer, leader_guid: wxl.Guid, loot_method: wxl.LootMethod|integer, looter_guid: wxl.Guid, loot_threshold: integer, dungeon_difficulty: integer, raid_difficulty: integer)): integer
---@overload fun(family: "GroupScript", method: "OnGroupMember", handler: fun(index: integer, count: integer, member: wxl.Object|nil, member_guid: wxl.Guid, name: string, online: boolean, subgroup: integer, flags: integer, roles: integer)): integer
---@overload fun(family: "GroupScript", method: "OnGroupLeft", handler: fun(reason: wxl.GroupLeftReason|integer)): integer
---@overload fun(family: "GroupScript", method: "OnGroupLootMethodChanged", handler: fun(loot_method: wxl.LootMethod|integer, looter_guid: wxl.Guid, loot_threshold: integer)): integer
---@overload fun(family: "GroupScript", method: "OnReadyCheck", handler: fun(initiator: wxl.Object|nil, initiator_guid: wxl.Guid, duration: integer)): integer
---@overload fun(family: "GroupScript", method: "OnReadyCheckResponse", handler: fun(member: wxl.Object|nil, member_guid: wxl.Guid, ready: boolean)): integer
---@overload fun(family: "GroupScript", method: "OnReadyCheckFinished", handler: fun(initiator_guid: wxl.Guid)): integer
---@overload fun(family: "SocialScript", method: "OnDuelRequested", handler: fun(challenger: wxl.Object|nil, challenger_guid: wxl.Guid, arbiter_guid: wxl.Guid, accept: boolean|nil, decline: boolean|nil)): integer
---@overload fun(family: "SocialScript", method: "OnDuelCountdown", handler: fun(duration: integer)): integer
---@overload fun(family: "SocialScript", method: "OnDuelWon", handler: fun(winner: string, loser: string, fled: boolean)): integer
---@overload fun(family: "SocialScript", method: "OnDuelFinished", handler: fun(completed: boolean)): integer
---@overload fun(family: "SocialScript", method: "OnTradeStatus", handler: fun(status: wxl.TradeStatus|integer, partner_guid: wxl.Guid)): integer
---@overload fun(family: "SocialScript", method: "OnEmote", handler: fun(unit: wxl.Object|nil, unit_guid: wxl.Guid, emote_id: integer)): integer
---@overload fun(family: "SocialScript", method: "OnTextEmote", handler: fun(sender: wxl.Object|nil, sender_guid: wxl.Guid, text_emote: integer, emote_number: integer, target_name: string)): integer
---@overload fun(family: "SocialScript", method: "OnResurrectRequest", handler: fun(offerer: wxl.Object|nil, offerer_guid: wxl.Guid, name: string, sickness: boolean, accept: boolean|nil, decline: boolean|nil)): integer
---@overload fun(family: "SocialScript", method: "OnWindowOpened", handler: fun(kind: wxl.WindowKind|integer, npc: wxl.Object|nil, npc_guid: wxl.Guid)): integer
---@overload fun(family: "SocialScript", method: "OnWindowClosed", handler: fun(kind: wxl.WindowKind|integer, npc: wxl.Object|nil, npc_guid: wxl.Guid)): integer
---@overload fun(family: "PlayerScript", method: "OnPlayerDied", handler: fun(unit: wxl.Object|nil)): integer
---@overload fun(family: "PlayerScript", method: "OnPlayerReleased", handler: fun(unit: wxl.Object|nil)): integer
---@overload fun(family: "PlayerScript", method: "OnPlayerResurrected", handler: fun(unit: wxl.Object|nil)): integer
---@overload fun(family: "PlayerScript", method: "OnReputationChanged", handler: fun(faction_id: integer, previous: integer, current: integer, bonus: number)): integer
---@overload fun(family: "PlayerScript", method: "OnAchievementEarned", handler: fun(player: wxl.Object|nil, player_guid: wxl.Guid, achievement_id: integer, date: integer, silent: boolean)): integer
---@overload fun(family: "AssetScript", method: "OnTerrainTileLoad", handler: fun(path: string, tile_x: integer, tile_y: integer, data: wxl.Bytes)): integer
---@overload fun(family: "AssetScript", method: "OnMapObjectGroupLoad", handler: fun(path: string, group: integer, data: wxl.Bytes)): integer
---@overload fun(family: "AssetScript", method: "OnSkinProfileLoad", handler: fun(model: string, data: wxl.Bytes)): integer
---@overload fun(family: "AssetScript", method: "OnTextureDecode", handler: fun(path: string, width: integer, height: integer, encoding: wxl.TextureEncoding|integer, alpha_bits: integer, alpha_kind: integer, mip_levels: integer)): integer
---@overload fun(family: "AssetScript", method: "OnShaderLoad", handler: fun(stage: wxl.ShaderStage|integer, path: string, name: string, permutations: integer)): integer
---@overload fun(family: "AssetScript", method: "OnSoundFileLoad", handler: fun(path: string, streaming: boolean, priority: integer)): integer
---@overload fun(family: "AssetScript", method: "OnModelParsed", handler: fun(path: string, file: lightuserdata|nil, header: lightuserdata|nil, size: integer)): integer
---@overload fun(family: "AssetScript", method: "OnSkinProfileParsed", handler: fun(path: string, file: lightuserdata|nil, header: lightuserdata|nil, skin: lightuserdata|nil)): integer
---@overload fun(family: "AssetScript", method: "OnModelSequenceLoad", handler: fun(path: string, file: lightuserdata|nil, sequence_index: integer, data: wxl.Bytes)): integer
---@overload fun(family: "ObjectScript", method: "OnAppearanceChanged", handler: fun(change: wxl.AppearanceChange|integer, wearer: lightuserdata|nil, model: lightuserdata|nil, race_id: integer, sex_id: integer, item_slot: integer, display_id: integer, npc: boolean)): integer
---@overload fun(family: "RenderScript", method: "OnModelBatchDrawn", handler: fun(model: lightuserdata|nil, root_model: lightuserdata|nil, primitive_type: integer, base_vertex: integer, min_index: integer, vertex_count: integer, start_index: integer, primitive_count: integer, blend_mode: integer, viewport_x: integer, viewport_y: integer, viewport_width: integer, viewport_height: integer, viewport_min_z: number, viewport_max_z: number)): integer
---@overload fun(family: "AssetScript", method: "OnFileMissing", handler: fun(path: string, kind: wxl.AssetKind|integer, open: boolean, pending: boolean, loader: boolean)): integer
---@overload fun(family: "WorldScript", method: "OnMouseMotion", handler: fun(dx: integer, dy: integer)): integer
---@overload fun(family: "WorldScript", method: "OnMouseWheel", handler: fun(delta: number, handled: boolean|nil)): integer
---@overload fun(family: "RenderScript", method: "OnCameraUpdate", handler: fun(index: integer, delta: number)): integer
---@overload fun(family: "RenderScript", method: "OnImGuiFrame", handler: fun(index: integer, delta: number)): integer
---@param family wxl.Family
---@param method string
---@param handler function
---@return integer id
function Observe(family, method, handler) end

---Like Observe, on an event that can be answered. The handler returns one value per
---member it answers through, in member order: true sets an answer, a string fills a
---buffer, nil leaves the member as it is. Taken on the main thread only.
---@overload fun(family: "WorldScript", method: "OnInput", handler: fun(message: integer, wparam: integer, lparam: integer, handled: boolean|nil): boolean|nil): integer
---@overload fun(family: "WorldScript", method: "OnSoundPlay", handler: fun(sound_id: integer, path: string, suppressed: boolean|nil): boolean|nil): integer
---@overload fun(family: "AssetScript", method: "OnFileOpen", handler: fun(path: string, redirect: string|nil): string|nil): integer
---@overload fun(family: "NetScript", method: "OnMessageReceived", handler: fun(opcode: integer, data: wxl.Bytes, suppressed: boolean|nil): boolean|nil): integer
---@overload fun(family: "NetScript", method: "OnMessageSent", handler: fun(opcode: integer, data: wxl.Bytes, suppressed: boolean|nil): boolean|nil): integer
---@overload fun(family: "ObjectScript", method: "OnSpellCastRequest", handler: fun(caster: wxl.Object|nil, spell_id: integer, item: wxl.Object|nil, target_guid: wxl.Guid, confirmed: boolean, refused: boolean|nil): boolean|nil): integer
---@overload fun(family: "ObjectScript", method: "OnItemUseRequest", handler: fun(item: wxl.Object|nil, item_guid: wxl.Guid, target_guid: wxl.Guid, confirmed: boolean, refused: boolean|nil): boolean|nil): integer
---@overload fun(family: "ChatScript", method: "OnChatSend", handler: fun(text: string, type: string, language: string, target: string, prefix: string, addon: boolean, suppressed: boolean|nil): boolean|nil): integer
---@overload fun(family: "GroupScript", method: "OnGroupInvite", handler: fun(inviter: string, can_accept: boolean, accept: boolean|nil, decline: boolean|nil): boolean|nil, boolean|nil): integer
---@overload fun(family: "SocialScript", method: "OnDuelRequested", handler: fun(challenger: wxl.Object|nil, challenger_guid: wxl.Guid, arbiter_guid: wxl.Guid, accept: boolean|nil, decline: boolean|nil): boolean|nil, boolean|nil): integer
---@overload fun(family: "SocialScript", method: "OnResurrectRequest", handler: fun(offerer: wxl.Object|nil, offerer_guid: wxl.Guid, name: string, sickness: boolean, accept: boolean|nil, decline: boolean|nil): boolean|nil, boolean|nil): integer
---@overload fun(family: "WorldScript", method: "OnMouseWheel", handler: fun(delta: number, handled: boolean|nil): boolean|nil): integer
---@param family wxl.Family
---@param method string
---@param handler function
---@return integer id
function Override(family, method, handler) end

---Drops a handler by the id Observe or Override returned.
---@param id integer
---@return boolean dropped
function Unobserve(id) end

---@alias wxl.CallbackName "onInit"|"onUpdate"|"onDraw"|"onShutdown"

---Registers a callback: onInit at WXL_STAGE_READY (or right after a reload),
---onUpdate(delta) at every World.Update, onDraw at every Render.UiRenderEnd,
---onShutdown at WXL_STAGE_SHUTDOWN and before a reload.
---@overload fun(event: "onInit", fn: fun())
---@overload fun(event: "onUpdate", fn: fun(delta: number))
---@overload fun(event: "onDraw", fn: fun())
---@overload fun(event: "onShutdown", fn: fun())
---@param event wxl.CallbackName
---@param fn function
function registerForEvent(event, fn) end

---A world object, by handle. Every getter answers the value, or nil and why:
---"gone" for an object that left, "unsupported" for a runtime without the read, or
---what the object is not ("a GameObject has no Health").
---
---One object is one Lua value for as long as it stays in view: the same at every event
---and every lookup, a table key, equal only to itself. A field set on it that is not a
---method's name (`obj.seen = true`) is the mod's own and is kept until the object leaves.
---@class wxl.Object
---@field [string] any what the mod stored on the object
local Object = {}

---Whether the object is still in view.
---@return boolean|nil valid, string|nil reason
function Object:Valid() end

---The server's id for the object, as "0x" and sixteen hex digits.
---@return string|nil guid, string|nil reason
function Object:Guid() end

---The kind's name, "Unit", "GameObject"...
---@return wxl.ObjectType|nil type, string|nil reason
function Object:Type() end

---The kind as a number.
---@return integer|nil type, string|nil reason
function Object:TypeId() end

---@return string|nil name, string|nil reason
function Object:Name() end

---What a unit targets; nil when nothing in view.
---@return wxl.Object|nil target, string|nil reason
function Object:Target() end

---The handle itself, as "0x" and sixteen hex digits: for logs.
---@return string
function Object:Handle() end

---The size of the object's descriptor block, in dwords.
---@return integer|nil count, string|nil reason
function Object:FieldCount() end

---One element of a descriptor field: a number, a guid's string, or the four bytes or two
---halves of a dword as that many numbers.
---@param name wxl.FieldName|string
---@param element? integer 0 for the first
---@return wxl.FieldValue|nil ..., string|nil reason
function Object:Field(name, element) end

---Every element of a descriptor field, in order; the bytes and halves as tables.
---@param name wxl.FieldName|string
---@return wxl.FieldValue[]|nil values, string|nil reason
function Object:Fields(name) end

---Writes one element of a descriptor field in the client, until the server's next update
---of it replaces it: a number, a guid, or a table of the bytes or the halves.
---@param name wxl.FieldName|string
---@param value wxl.FieldValue
---@param element? integer
---@return boolean|nil written, string|nil reason
function Object:SetField(name, value, element) end

---Where it stands in the world, in yards; an item has no place.
---For any object (position).
---@return wxl.Vec3|nil value, string|nil reason
function Object:Position() end

---The way it faces, in radians.
---For any object (facing).
---@return number|nil value, string|nil reason
function Object:Facing() end

---The server's template id: a creature, item or game-object entry; 0 for a player.
---For any object (entry).
---@return integer|nil value, string|nil reason
function Object:Entry() end

---Its size, 1 being the model's own.
---For any object (scale).
---@return number|nil value, string|nil reason
function Object:Scale() end

---Its health; 0 or less is a dead unit, as the client reads it.
---For Unit, Player (unit_health).
---@return integer|nil value, string|nil reason
function Object:Health() end

---Its maximum health.
---For Unit, Player (unit_max_health).
---@return integer|nil value, string|nil reason
function Object:MaxHealth() end

---Its level.
---For Unit, Player; GameObject; Item, Container (unit_level, gameobject_level, item_level).
---@return integer|nil value, string|nil reason
function Object:Level() end

---Its power of a type (0 mana, 1 rage, 2 focus, 3 energy, 4 happiness, 5 runes, 6 runic power).
---For Unit, Player (unit_power).
---@param index integer
---@return integer|nil value, string|nil reason
function Object:Power(index) end

---Its maximum power of a type.
---For Unit, Player (unit_max_power).
---@param index integer
---@return integer|nil value, string|nil reason
function Object:MaxPower(index) end

---The type of power it runs on, as PowerValue takes it.
---For Unit, Player (unit_power_type).
---@return integer|nil value, string|nil reason
function Object:PowerType() end

---Its race, as ChrRaces.dbc numbers it.
---For Unit, Player (unit_race).
---@return integer|nil value, string|nil reason
function Object:Race() end

---Its class, as ChrClasses.dbc numbers it.
---For Unit, Player; Item, Container (unit_class, item_class).
---@return integer|nil value, string|nil reason
function Object:Class() end

---0 male, 1 female, 2 none.
---For Unit, Player (unit_sex).
---@return integer|nil value, string|nil reason
function Object:Sex() end

---The model it shows now, a CreatureDisplayInfo.dbc id.
---For Unit, Player; GameObject; Corpse (unit_display_id, gameobject_display_id, corpse_display_id).
---@return integer|nil value, string|nil reason
function Object:DisplayId() end

---The model it shows when nothing changes its shape.
---For Unit, Player (unit_native_display_id).
---@return integer|nil value, string|nil reason
function Object:NativeDisplayId() end

---The model of its mount; 0 while not mounted.
---For Unit, Player (unit_mount_display_id).
---@return integer|nil value, string|nil reason
function Object:MountDisplayId() end

---Its FactionTemplate.dbc id.
---For Unit, Player (unit_faction_template).
---@return integer|nil value, string|nil reason
function Object:FactionTemplate() end

---UNIT_FIELD_FLAGS as the server sends it.
---For Unit, Player; Item, Container; GameObject; Corpse (unit_flags, item_flags, gameobject_flags, corpse_flags).
---@return integer|nil value, string|nil reason
function Object:Flags() end

---UNIT_FIELD_FLAGS_2 as the server sends it.
---For Unit, Player (unit_flags2).
---@return integer|nil value, string|nil reason
function Object:Flags2() end

---UNIT_DYNAMIC_FLAGS as the server sends it to this client.
---For Unit, Player; GameObject; Corpse (unit_dynamic_flags, gameobject_dynamic_flags, corpse_dynamic_flags).
---@return integer|nil value, string|nil reason
function Object:DynamicFlags() end

---What it offers to talk about: vendor, trainer, quest giver, ... as UNIT_NPC_FLAGS.
---For Unit, Player (unit_npc_flags).
---@return integer|nil value, string|nil reason
function Object:NpcFlags() end

---The radius of its body, in yards.
---For Unit, Player (unit_bounding_radius).
---@return number|nil value, string|nil reason
function Object:BoundingRadius() end

---How far it reaches in melee, in yards.
---For Unit, Player (unit_combat_reach).
---@return number|nil value, string|nil reason
function Object:CombatReach() end

---The unit it charms, or 0.
---For Unit, Player (unit_charm).
---@return wxl.Guid|nil value, string|nil reason
function Object:Charm() end

---The unit it summoned, its pet among them, or 0.
---For Unit, Player (unit_summon).
---@return wxl.Guid|nil value, string|nil reason
function Object:Summon() end

---Who charms it, or 0.
---For Unit, Player (unit_charmed_by).
---@return wxl.Guid|nil value, string|nil reason
function Object:CharmedBy() end

---Who summoned it: a pet's owner, or 0.
---For Unit, Player (unit_summoned_by).
---@return wxl.Guid|nil value, string|nil reason
function Object:SummonedBy() end

---Who created it: a totem's shaman, or 0.
---For Unit, Player; GameObject (unit_created_by, gameobject_created_by).
---@return wxl.Guid|nil value, string|nil reason
function Object:CreatedBy() end

---PLAYER_FLAGS as the server sends it.
---For Player (player_flags).
---@return integer|nil value, string|nil reason
function Object:PlayerFlags() end

---Its guild's id; 0 while unguilded.
---For Player (player_guild_id).
---@return integer|nil value, string|nil reason
function Object:GuildId() end

---Its rank in the guild, 0 for the guild master.
---For Player (player_guild_rank).
---@return integer|nil value, string|nil reason
function Object:GuildRank() end

---The title it shows, a CharTitles.dbc bit index; 0 for none.
---For Player (player_chosen_title).
---@return integer|nil value, string|nil reason
function Object:ChosenTitle() end

---The item id an equipment slot shows (0 head .. 18 tabard); 0 when the slot is empty.
---For Player (player_visible_item).
---@param index integer
---@return integer|nil value, string|nil reason
function Object:VisibleItem(index) end

---The player who owns it.
---For Item, Container; Corpse (item_owner, corpse_owner).
---@return wxl.Guid|nil value, string|nil reason
function Object:Owner() end

---The bag holding it, or 0 in the backpack or when equipped.
---For Item, Container (item_contained_in).
---@return wxl.Guid|nil value, string|nil reason
function Object:ContainedIn() end

---Who crafted it, or 0.
---For Item, Container (item_creator).
---@return wxl.Guid|nil value, string|nil reason
function Object:Creator() end

---How many the stack holds.
---For Item, Container (item_stack_count).
---@return integer|nil value, string|nil reason
function Object:StackCount() end

---Seconds left before it expires; 0 when it does not.
---For Item, Container (item_duration).
---@return integer|nil value, string|nil reason
function Object:Duration() end

---Its durability now.
---For Item, Container (item_durability).
---@return integer|nil value, string|nil reason
function Object:Durability() end

---Its durability when repaired; 0 for an item that does not wear.
---For Item, Container (item_max_durability).
---@return integer|nil value, string|nil reason
function Object:MaxDurability() end

---How many slots the bag has.
---For Container (container_num_slots).
---@return integer|nil value, string|nil reason
function Object:NumSlots() end

---The item in a slot, as a guid; 0 for an empty one, invalid past NumSlots.
---For Container (container_slot).
---@param index integer
---@return wxl.Guid|nil value, string|nil reason
function Object:Slot(index) end

---Its FactionTemplate.dbc id.
---For GameObject (gameobject_faction).
---@return integer|nil value, string|nil reason
function Object:Faction() end

---0 active, 1 ready, 2 destroyed.
---For GameObject (gameobject_state).
---@return integer|nil value, string|nil reason
function Object:State() end

---The game-object type, in the server's numbering (door 0, button 1, chest 3, ...).
---For GameObject (gameobject_type).
---@return integer|nil value, string|nil reason
function Object:GameObjectType() end

---The art kit it shows.
---For GameObject (gameobject_art_kit).
---@return integer|nil value, string|nil reason
function Object:ArtKit() end

---The unit that cast it.
---For DynamicObject (dynamic_caster).
---@return wxl.Guid|nil value, string|nil reason
function Object:Caster() end

---The spell it is the area of.
---For DynamicObject (dynamic_spell_id).
---@return integer|nil value, string|nil reason
function Object:SpellId() end

---Its radius, in yards.
---For DynamicObject (dynamic_radius).
---@return number|nil value, string|nil reason
function Object:Radius() end

---The client time it was cast at, in milliseconds.
---For DynamicObject (dynamic_cast_time).
---@return integer|nil value, string|nil reason
function Object:CastTime() end

---The party the owner was in when they died, or 0.
---For Corpse (corpse_party).
---@return wxl.Guid|nil value, string|nil reason
function Object:Party() end

---The owner's guild id.
---For Corpse (corpse_guild).
---@return integer|nil value, string|nil reason
function Object:Guild() end

---Whether it is the unit the client controls.
---For any object (is_active_player).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@return boolean|nil value, string|nil reason
function Object:IsActivePlayer() end

---Yards from it to another object, centre to centre; invalid when either has no place in the world.
---For any object (distance).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@param other wxl.Object
---@return number|nil value, string|nil reason
function Object:Distance(other) end

---Yards from it to a point; invalid for an object with no place in the world.
---For any object (distance_to).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@param point wxl.Vec3
---@return number|nil value, string|nil reason
function Object:DistanceTo(point) end

---What it targets, as a guid, whether that is in view or not; 0 for nothing.
---For Unit, Player (unit_target_guid).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@return wxl.Guid|nil value, string|nil reason
function Object:TargetGuid() end

---How it stands towards the active player, by the client's own reckoning: the colour of its name.
---For Unit, Player (unit_reaction).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@return wxl.Reaction|nil value, string|nil reason
function Object:Reaction() end

---How it stands towards another unit, by the client's own reckoning.
---For Unit, Player (unit_reaction_to).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@param other wxl.Object
---@return wxl.Reaction|nil value, string|nil reason
function Object:ReactionTo(other) end

---Whether it is dead: no health left, or marked dead by the server.
---For Unit, Player (unit_is_dead).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@return boolean|nil value, string|nil reason
function Object:IsDead() end

---Whether it is in combat, as the client's UnitAffectingCombat reads it.
---For Unit, Player (unit_in_combat).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@return boolean|nil value, string|nil reason
function Object:InCombat() end

---Whether it is flagged for player-versus-player combat.
---For Unit, Player (unit_is_pvp).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@return boolean|nil value, string|nil reason
function Object:IsPvp() end

---Whether someone has tapped it: its kill and loot are theirs.
---For Unit, Player (unit_is_tapped).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@return boolean|nil value, string|nil reason
function Object:IsTapped() end

---Whether the active player, or their group, has tapped it.
---For Unit, Player (unit_is_tapped_by_player).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@return boolean|nil value, string|nil reason
function Object:IsTappedByPlayer() end

---Whether it has loot for the active player.
---For Unit, Player (unit_is_lootable).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@return boolean|nil value, string|nil reason
function Object:IsLootable() end

---Whether its corpse can be skinned.
---For Unit, Player (unit_is_skinnable).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@return boolean|nil value, string|nil reason
function Object:IsSkinnable() end

---How it stands: standing, sitting, sleeping, kneeling, ...
---For Unit, Player (unit_stand_state).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@return wxl.StandState|nil value, string|nil reason
function Object:StandState() end

---Whether it sits, on the ground or in a chair.
---For Unit, Player (unit_is_sitting).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@return boolean|nil value, string|nil reason
function Object:IsSitting() end

---Whether it is stealthed, as the client's IsStealthed reads it.
---For Unit, Player (unit_is_stealthed).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@return boolean|nil value, string|nil reason
function Object:IsStealthed() end

---Whether it rides a mount, as the client's IsMounted reads it.
---For Unit, Player (unit_is_mounted).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@return boolean|nil value, string|nil reason
function Object:IsMounted() end

---The spell it casts now; 0 for none.
---For Unit, Player (unit_casting_spell_id).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@return integer|nil value, string|nil reason
function Object:CastingSpellId() end

---When the cast began, in milliseconds on the client's clock (the client's GetTime() * 1000).
---For Unit, Player (unit_cast_start).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@return integer|nil value, string|nil reason
function Object:CastStart() end

---When the cast ends, on the same clock.
---For Unit, Player (unit_cast_end).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@return integer|nil value, string|nil reason
function Object:CastEnd() end

---Whether it is casting, its cast not yet over, as the client's UnitCastingInfo reads it.
---For Unit, Player (unit_is_casting).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@return boolean|nil value, string|nil reason
function Object:IsCasting() end

---The spell it channels now; 0 for none.
---For Unit, Player (unit_channel_spell_id).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@return integer|nil value, string|nil reason
function Object:ChannelSpellId() end

---When the channel began, on the client's clock.
---For Unit, Player (unit_channel_start).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@return integer|nil value, string|nil reason
function Object:ChannelStart() end

---When the channel ends, on the client's clock.
---For Unit, Player (unit_channel_end).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@return integer|nil value, string|nil reason
function Object:ChannelEnd() end

---Whether it is channeling, its channel not yet over.
---For Unit, Player (unit_is_channeling).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@return boolean|nil value, string|nil reason
function Object:IsChanneling() end

---How many auras the client holds on it, buffs and debuffs alike.
---For Unit, Player (unit_aura_count).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@return integer|nil value, string|nil reason
function Object:AuraCount() end

---The aura in a slot, 0 to AuraCount - 1: its spell, caster, stacks, duration, expiration and flags.
---For Unit, Player (unit_aura).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@param index integer
---@return wxl.Aura|nil value, string|nil reason
function Object:Aura(index) end

---Whether an aura of a spell is on it.
---For Unit, Player (unit_has_aura).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@param index integer
---@return boolean|nil value, string|nil reason
function Object:HasAura(index) end

---What kind of creature it is, as the client works it out: beast, humanoid, undead, ...
---For Unit, Player (unit_creature_type).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@return wxl.CreatureType|nil value, string|nil reason
function Object:CreatureType() end

---Its beast family, a CreatureFamily.dbc id; 0 for none.
---For Unit, Player (unit_creature_family).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@return integer|nil value, string|nil reason
function Object:CreatureFamily() end

---Its rank: normal, elite, rare, rare elite, world boss.
---For Unit, Player (unit_classification).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@return wxl.Classification|nil value, string|nil reason
function Object:Classification() end

---Whether it knows a spell: the active player's spell book or its pet's; false for any other unit.
---For Unit, Player (unit_knows_spell).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@param index integer
---@return boolean|nil value, string|nil reason
function Object:KnowsSpell(index) end

---The flags of its movement: forward 0x1, backward 0x2, ..., falling 0x1000, flying 0x2000000.
---For Unit, Player (unit_movement_flags).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@return integer|nil value, string|nil reason
function Object:MovementFlags() end

---One of its speeds, in yards a second: walk, run, swim, flight, ...; the turn and pitch rates in radians a second.
---For Unit, Player (unit_speed).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@param which wxl.MoveType|integer
---@return number|nil value, string|nil reason
function Object:Speed(which) end

---Whether it swims, as the client's IsSwimming reads it.
---For Unit, Player (unit_is_swimming).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@return boolean|nil value, string|nil reason
function Object:IsSwimming() end

---Whether it flies, as the client's IsFlying reads it.
---For Unit, Player (unit_is_flying).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@return boolean|nil value, string|nil reason
function Object:IsFlying() end

---Whether it is falling, as the client's IsFalling reads it.
---For Unit, Player (unit_is_falling).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@return boolean|nil value, string|nil reason
function Object:IsFalling() end

---Whether it is a ghost, released from its corpse.
---For Player (player_is_ghost).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@return boolean|nil value, string|nil reason
function Object:IsGhost() end

---Whether it is away from the keyboard.
---For Player (player_is_afk).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@return boolean|nil value, string|nil reason
function Object:IsAfk() end

---Whether it asked not to be disturbed.
---For Player (player_is_dnd).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@return boolean|nil value, string|nil reason
function Object:IsDnd() end

---Whether it rests in an inn or a city, gaining rested experience.
---For Player (player_is_resting).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@return boolean|nil value, string|nil reason
function Object:IsResting() end

---Its money, in copper.
---For the active player (player_money).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@return integer|nil value, string|nil reason
function Object:Money() end

---Its experience in this level.
---For the active player (player_xp).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@return integer|nil value, string|nil reason
function Object:Xp() end

---The experience this level takes.
---For the active player (player_next_level_xp).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@return integer|nil value, string|nil reason
function Object:NextLevelXp() end

---Its rested experience: kills give twice as much until it is spent.
---For the active player (player_rested_xp).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@return integer|nil value, string|nil reason
function Object:RestedXp() end

---Its honor points.
---For the active player (player_honor_points).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@return integer|nil value, string|nil reason
function Object:HonorPoints() end

---Its arena points.
---For the active player (player_arena_points).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@return integer|nil value, string|nil reason
function Object:ArenaPoints() end

---The item in an equipment slot, or the bag in a bag slot (19 to 22); none for an empty slot.
---For the active player (player_inventory_item).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@param which wxl.InventorySlot|integer
---@return wxl.Object|nil value, string|nil reason
function Object:InventoryItem(which) end

---The item in a slot of the backpack, 0 to 15; none for an empty slot.
---For the active player (player_backpack_item).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@param index integer
---@return wxl.Object|nil value, string|nil reason
function Object:BackpackItem(index) end

---The skill on a line of the skill list, 0 to 127: its id, value and maximum; an id of 0 for an empty line.
---For the active player (player_skill).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@param index integer
---@return wxl.Skill|nil value, string|nil reason
function Object:Skill(index) end

---A skill by its SkillLine.dbc id; invalid for a skill the player does not have.
---For the active player (player_skill_of).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@param index integer
---@return wxl.Skill|nil value, string|nil reason
function Object:SkillOf(index) end

---How many others are in its party, 0 to 4.
---For the active player (player_party_size).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@return integer|nil value, string|nil reason
function Object:PartySize() end

---The other party member in a party slot, 0 to 3, whether in view or not; 0 for an empty slot.
---For the active player (player_party_member_guid).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@param index integer
---@return wxl.Guid|nil value, string|nil reason
function Object:PartyMemberGuid(index) end

---How many are in its raid, itself included; 0 outside a raid.
---For the active player (player_raid_size).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@return integer|nil value, string|nil reason
function Object:RaidSize() end

---The raid member in a raid slot, 0 to 39, whether in view or not; 0 for an empty slot.
---For the active player (player_raid_member_guid).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@param index integer
---@return wxl.Guid|nil value, string|nil reason
function Object:RaidMemberGuid(index) end

---The enchantment in a slot, 0 to 11 (0 permanent, 1 temporary, 2 to 4 sockets, ...), a SpellItemEnchantment.dbc id; 0 for none.
---For Item, Container (item_enchant).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@param index integer
---@return integer|nil value, string|nil reason
function Object:Enchant(index) end

---Its quality: poor, common, uncommon, rare, epic, legendary, artifact, heirloom.
---For Item, Container (item_quality).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@return wxl.ItemQuality|nil value, string|nil reason
function Object:Quality() end

---The level it takes to use.
---For Item, Container (item_required_level).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@return integer|nil value, string|nil reason
function Object:RequiredLevel() end

---Its subclass within the class: a sword, a cloth robe, ...
---For Item, Container (item_subclass).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@return integer|nil value, string|nil reason
function Object:Subclass() end

---Where it is worn: head 1, neck 2, ...; 0 for an item that is not worn.
---For Item, Container (item_inventory_type).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@return integer|nil value, string|nil reason
function Object:InventoryType() end

---How many a stack of it holds at most.
---For Item, Container (item_max_stack).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@return integer|nil value, string|nil reason
function Object:MaxStack() end

---What a vendor pays for one, in copper.
---For Item, Container (item_sell_price).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@return integer|nil value, string|nil reason
function Object:SellPrice() end

---The item in a slot of the bag; none for an empty one, invalid past NumSlots.
---For Container (container_slot_item).
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@param index integer
---@return wxl.Object|nil value, string|nil reason
function Object:SlotItem(index) end

---Whether it stands outdoors, as the client's IsOutdoors reads it: false in a building or a cave the world marks indoors, where a mount is refused.
---For Unit, Player (unit_is_outdoors).
---Came with abi 0.1.23: a mod whose mod.json asks for an older abi cannot use it.
---@return boolean|nil value, string|nil reason
function Object:IsOutdoors() end

---Every aura on a unit, in slot order, as Aura(slot) answers each. Came with abi 0.1.18.
---@return wxl.Aura[]|nil auras, string|nil reason
function Object:Auras() end

---The lines of the active player's skill list that hold a skill. Came with abi 0.1.18.
---@return wxl.Skill[]|nil skills, string|nil reason
function Object:Skills() end

---The guids of the active player's other party members. Came with abi 0.1.18.
---@return wxl.Guid[]|nil members, string|nil reason
function Object:PartyMembers() end

---The guids of the active player's raid, itself included. Came with abi 0.1.18.
---@return wxl.Guid[]|nil members, string|nil reason
function Object:RaidMembers() end

---Targets the object, as a left click on it does (wxl.action). Came with abi 0.1.18.
---@return boolean|nil done, string|nil reason
function Object:Select() end

---Right-clicks the object: talks, loots, opens, uses (wxl.action). Came with abi 0.1.18.
---@return boolean|nil done, string|nil reason
function Object:Interact() end

---Casts a spell the player knows at this unit (wxl.action). Came with abi 0.1.18.
---@param spell_id integer
---@return boolean|nil done, string|nil reason
function Object:CastAt(spell_id) end

---Follows this unit, as /follow does (wxl.action). Came with abi 0.1.18.
---@return boolean|nil done, string|nil reason
function Object:Follow() end

---Targets what this unit targets, as /assist does (wxl.action). Came with abi 0.1.18.
---@return boolean|nil done, string|nil reason
function Object:Assist() end

---Uses this item, on a unit, or on what it picks itself (wxl.action). Came with abi 0.1.18.
---@param target? wxl.Object
---@return boolean|nil done, string|nil reason
function Object:Use(target) end

---An aura on a unit, as Aura(slot) answers it.
---@class wxl.Aura
---@field spell_id integer
---@field caster wxl.Guid
---@field stacks integer
---@field level integer
---@field flags integer 0x10 helpful, 0x20 with a duration, 0x80 harmful
---@field duration integer milliseconds
---@field expiration integer when it ends, on the client's clock in milliseconds; 0 for never

---A line of the skill list, as Skill(line) answers it.
---@class wxl.Skill
---@field id integer
---@field value integer
---@field max integer
---@field step integer
---@field temporary_bonus integer
---@field permanent_bonus integer

-- The service modules. Each function answers its value, or nil and the reason: "unsupported"
-- for a runtime without the service or the member, otherwise the result's name ("invalid",
-- "not_found", "failed"). A number left out is 0.

---@class wxl.HookPoint
---@field name string
---@field signature string

---@class wxl.FootprintCategory
---@field name string
---@field owner string
---@field bytes integer
---@field count integer
---@field peak integer

---@class wxl.FootprintSource
---@field file string
---@field bytes integer
---@field count integer
---@field peak integer
---@field allocations integer

---@class wxl.FootprintArchive
---@field path string
---@field priority integer
---@field hash_entries integer
---@field block_entries integer
---@field bytes integer

---@class wxl.FootprintTable
---@field name string
---@field records integer
---@field record_size integer
---@field bytes integer

---@class wxl.FootprintRegion
---@field state string
---@field type string
---@field bytes integer
---@field regions integer
---@field largest integer

---@class wxl.PointInfo
---@field point integer
---@field name string
---@field signature string
---@field thread integer
---@field flags integer
---@field layout_hash integer
---@field arg_count integer
---@field absent string

---A File of wxl.storage.
---@class wxl.File
local File = {}

---Reads at an offset: as many bytes as asked, or what is left.
---@param offset integer
---@param count integer how many bytes, at most
---@return string|nil bytes
---@return string|nil reason
function File:ReadAt(offset, count) end

---Closes the file; one the mod lets go of is closed for it.
function File:Close() end

---A Patch of wxl.patch.
---@class wxl.Patch
local Patch = {}

---Puts back the bytes the patch replaced; a mod's are put back, newest first, when it unloads.
---@return true|nil ok
---@return string|nil reason
function Patch:Restore() end

---A Model of wxl.m2.
---@class wxl.Model
local Model = {}

---Takes the model off what it hangs on and gives it back; the mod's models go when it unloads too.
function Model:Release() end

---Hangs the model on a model at an attachment point; flags 1 attaches to the root where the point is missing, 2 makes it follow the parent's bones.
---@param parent lightuserdata|wxl.Model an address an event or a call handed you
---@param attachment integer
---@param flags integer
---@return true|nil ok
---@return string|nil reason
function Model:Attach(parent, attachment, flags) end

---Takes the model off what it hangs on.
function Model:Detach() end

---What the model hangs on; nil, "not_found" for nothing, which is how a mod tells its model was let go.
---@return lightuserdata|nil address
---@return string|nil reason
function Model:Parent() end

---Shows or hides every geoset of the model whose id is in [first, last].
---@param first integer
---@param last integer
---@param visible boolean
function Model:SetGeosets(first, last, visible) end

---Replaces the texture the model takes for a texture type (2 an item's skin, 11-13 a creature's) with a file.
---@param type integer
---@param path string
---@return true|nil ok
---@return string|nil reason
function Model:ReplaceTexture(type, path) end

---Hangs the model on one key bone of a model, moved by an offset: an attachment point the parent's file does not have.
---@param parent lightuserdata|wxl.Model an address an event or a call handed you
---@param attachment integer
---@param key_bone integer
---@param offset wxl.Vec3
---@return true|nil ok
---@return string|nil reason
function Model:AttachToBone(parent, attachment, key_bone, offset) end

---wxl.script. The client's own script: functions and events added to it, text run in it.
---@class wxl.script
local script = {}

---Which script context is live: the glue's (login, characters) or the game's.
---@return wxl.ScriptContext|integer|nil
---@return string|nil reason
function script.Context() end

---Makes a Lua function callable from the client's own script, under a global name: in every
---script context the client makes from now on, or only in `context`. Kept until the mod
---unloads or reloads; its arguments and returns are numbers, strings, booleans and nil.
---@param name string the global's name
---@param fn function
---@param context? wxl.ScriptContext
---@return true|nil ok
---@return string|nil reason
function script.RegisterFunction(name, fn, context) end

---Declares an event the client's frames can listen to, in every script context the client
---makes from now on, or only in `context`. FireEvent raises it.
---@param name string
---@param context? wxl.ScriptContext
---@return true|nil ok
---@return string|nil reason
function script.RegisterEvent(name, context) end

---Raises a declared event, with its values, to every frame listening.
---@param name string
---@param ... nil|number|string|boolean the values it carries
---@return true|nil ok
---@return string|nil reason
function script.FireEvent(name, ...) end

---Runs script text in the live context; the name labels it in errors.
---@param chunk string
---@param name string
---@return true|nil ok
---@return string|nil reason
function script.Run(chunk, name) end

---Came with abi 0.1.15: a mod whose mod.json asks for an older abi cannot use it.
---Whether a script context is live: false before the first one and between two.
---@return boolean|nil
---@return string|nil reason
function script.Live() end

---wxl.storage. The client's files, whatever they are packed in: named as the client names them.
---@class wxl.storage
local storage = {}

---Whether a name resolves to a file, in an archive or in a folder.
---@param path string
---@return boolean|nil
---@return string|nil reason
function storage.Exists(path) end

---A file's length in bytes, without reading it.
---@param path string
---@return integer|nil
---@return string|nil reason
function storage.Size(path) end

---A whole file.
---@param path string
---@return string|nil bytes
---@return string|nil reason
function storage.Read(path) end

---Opens a file to read it a piece at a time.
---@param path string
---@return wxl.File|nil
---@return string|nil reason
function storage.Open(path) end

---Adds a folder inside the client's folder to the files searched, for this mod: the higher
---priority wins a name, the client's own data is 0. It stays until RemoveSearchPath, or until
---the mod is disabled, reloads or unloads; adding it again changes its priority.
---@param directory string
---@param priority integer
---@return true|nil ok
---@return string|nil reason
function storage.AddSearchPath(directory, priority) end

---Adds an archive to the files searched, at a priority as AddSearchPath's.
---@param path string
---@param priority integer
---@return true|nil ok
---@return string|nil reason
function storage.MountArchive(path, priority) end

---The names matching a pattern, * standing for any run of characters, each once.
---@param pattern string
---@return string[]|nil
---@return string|nil reason
function storage.Find(pattern) end

---Came with abi 0.1.10: a mod whose mod.json asks for an older abi cannot use it.
---Takes a folder this mod added back out of the files searched; nil, "not_found" for one
---it did not add.
---@param directory string
---@return true|nil ok
---@return string|nil reason
function storage.RemoveSearchPath(directory) end

---Came with abi 0.1.15: a mod whose mod.json asks for an older abi cannot use it.
---Makes every open of `from` open `to` instead, on whichever thread the client opens it, for
---this mod: names compared in any case, with either slash; `to` is found as any name is. It
---stays until Unredirect, or until the mod is disabled, reloads or unloads; when two mods
---redirect one name, the later wins. An OnFileOpen handler that answers still has the last word.
---@param from string
---@param to string
---@return true|nil ok
---@return string|nil reason
function storage.Redirect(from, to) end

---Came with abi 0.1.15: a mod whose mod.json asks for an older abi cannot use it.
---Takes this mod's redirect of a name back; nil, "not_found" for one it did not redirect.
---@param from string
---@return true|nil ok
---@return string|nil reason
function storage.Unredirect(from) end

---Came with abi 0.1.19: a mod whose mod.json asks for an older abi cannot use it.
---Serves `data` under a name, for this mod, on every thread: the client opens and reads it as
---one of its own files, and so do wxl.storage's Read and Open. The bytes are copied. Above 0,
---the name wins over the client's archives; 0 or below, it only fills a name they lack. Serving
---a name again replaces this mod's bytes. It stays until Unserve, or until the mod is disabled,
---reloads or unloads; when two mods serve one name, the higher priority wins, then the later.
---@param path string
---@param data string
---@param priority integer
---@return true|nil ok
---@return string|nil reason
function storage.Serve(path, data, priority) end

---Came with abi 0.1.19: a mod whose mod.json asks for an older abi cannot use it.
---Takes back what this mod serves under a name; nil, "not_found" for one it does not serve.
---@param path string
---@return true|nil ok
---@return string|nil reason
function storage.Unserve(path) end

---Came with abi 0.1.22: a mod whose mod.json asks for an older abi cannot use it.
---The content hash of a file as the client would read it, from whichever layer holds it: an
---archive, the disk, a folder of the search path, a provider, served bytes. The bytes are hashed
---before any transform, by the system's MD5, and the answer is kept: `fresh` reads it again.
---Reading a large file takes time on the main thread; HashFileAsync does not.
---@param path string
---@param fresh? boolean
---@return wxl.FileHash|nil hash
---@return string|nil reason
function storage.HashFile(path, fresh) end

---Came with abi 0.1.22: a mod whose mod.json asks for an older abi cannot use it.
---HashFile on a thread of the runtime's: `fn(hash)` is called later, on the main thread, with the
---table HashFile answers, or nil and the reason. Answers the request's number. A request of a
---mod that is disabled, reloads or unloads is dropped.
---@param path string
---@param fn fun(hash: wxl.FileHash|nil, reason: string|nil)
---@param fresh? boolean
---@return integer|nil request
---@return string|nil reason
function storage.HashFileAsync(path, fn, fresh) end

---Came with abi 0.1.22: a mod whose mod.json asks for an older abi cannot use it.
---Replaces a file of the client with another name, for this mod: every open of `path`, and every
---check that it exists, gets `with` instead, on every thread, above every layer. The way to
---put a file of the mod's own in place of the client's file. It stays until RestoreFile,
---or until the mod is disabled, reloads or unloads; when two mods override one name, the later
---wins. What the client already loaded stays: ReloadAsset makes it read the file again.
---@param path string
---@param with string
---@return true|nil ok
---@return string|nil reason
function storage.OverrideFile(path, with) end

---Came with abi 0.1.22: a mod whose mod.json asks for an older abi cannot use it.
---Takes this mod's override of a name back; nil, "not_found" for one it did not override.
---@param path string
---@return true|nil ok
---@return string|nil reason
function storage.RestoreFile(path) end

---Came with abi 0.1.22: a mod whose mod.json asks for an older abi cannot use it.
---Makes the client read again a model or a texture it holds, so a file that changed shows: a
---texture is read again in place, a model is dropped from the model cache and the units drawn
---with it made again. Other kinds answer nil, "unsupported"; nil, "pending" when what the
---client holds is still loading.
---@param path string
---@return wxl.AssetReload|nil done
---@return string|nil reason
function storage.ReloadAsset(path) end

---wxl.data. The client's databases, served from a mod's own files before the client reads them.
---@class wxl.data
local data = {}

---Serves a table from a file the client can read instead of its own: before the client reads it.
---@param table string
---@param path string
---@return true|nil ok
---@return string|nil reason
function data.ServeTable(table, path) end

---Gives a table back, so the client's own file is used again.
---@param table string
---@return true|nil ok
---@return string|nil reason
function data.RestoreTable(table) end

---What a table is served from, empty when it is the client's own file.
---@param table string
---@return string|nil
---@return string|nil reason
function data.ServedFrom(table) end

---Whether the client has read a table already, in which case serving it comes too late.
---@param table string
---@return boolean|nil
---@return string|nil reason
function data.IsLoaded(table) end

---wxl.hook. The points a runtime names for a native detour.
---@class wxl.hook
local hook = {}

---The points this runtime names, each with its detour's C signature.
---@return wxl.HookPoint[]|nil
---@return string|nil reason
function hook.Points() end

---Whether this runtime names a point.
---@param name string
---@return boolean|nil
---@return string|nil reason
function hook.HasPoint(name) end

---wxl.patch. Bytes written into the client's image over the bytes expected there, journaled.
---Raw: present only when the mod's mod.json declares it, in "requires" or "optional".
---@class wxl.patch
local patch = {}

---Writes the replacement bytes at an address that holds the expected ones; refused otherwise.
---@param name string
---@param address integer
---@param expected string the bytes, as a string
---@param replacement string the bytes, as a string
---@return wxl.Patch|nil
---@return string|nil reason
function patch.Apply(name, address, expected, replacement) end

---Whether an address holds the expected bytes right now.
---@param address integer
---@param expected string the bytes, as a string
---@return boolean|nil
---@return string|nil reason
function patch.Matches(address, expected) end

---The address of a byte pattern in the image's read-only data, after an earlier one or, `after` left out (0), from the start.
---@param pattern string the bytes, as a string
---@param after? integer 0 when left out
---@return integer|nil address
---@return string|nil reason
function patch.Find(pattern, after) end

---wxl.footprint. Where the client's memory goes: Storm's allocations by source file, the big consumers, the address space.
---Came with abi 0.1.11: a mod whose mod.json asks for an older abi cannot use it.
---@class wxl.footprint
local footprint = {}

---Starts counting Storm's allocations from zero: only what is allocated from now on is followed.
---@return true|nil ok
---@return string|nil reason
function footprint.Start() end

---Stops counting; the last counts stay in the next snapshot.
function footprint.Stop() end

---Whether accounting runs.
---@return boolean|nil
---@return string|nil reason
function footprint.Running() end

---Takes the picture the lists read: Storm's counts, the client's consumers, the address space,
---and the heap of every Lua mod, each shown as its own category. On the main thread.
---@return true|nil ok
---@return string|nil reason
function footprint.Snapshot() end

---The consumers of the last snapshot, largest first: name, owner, bytes, count, peak.
---@return wxl.FootprintCategory[]|nil
---@return string|nil reason
function footprint.Categories() end

---Storm's allocations of the last snapshot by source file, largest first: file, bytes, count, peak, allocations.
---@return wxl.FootprintSource[]|nil
---@return string|nil reason
function footprint.Sources() end

---The mounted archives of the last snapshot: path, priority, hash_entries, block_entries, bytes.
---@return wxl.FootprintArchive[]|nil
---@return string|nil reason
function footprint.Archives() end

---The client databases of the last snapshot, by name: name, records, record_size, bytes.
---@return wxl.FootprintTable[]|nil
---@return string|nil reason
function footprint.Tables() end

---The address space of the last snapshot: state, type, bytes, regions, largest.
---@return wxl.FootprintRegion[]|nil
---@return string|nil reason
function footprint.Regions() end

---States one consumer of this mod's own, shown in Categories() as "<mod id>/<name>" until the
---mod reports it again or goes; 0 bytes and 0 count remove it.
---@param name string
---@param bytes integer
---@param count integer
---@return true|nil ok
---@return string|nil reason
function footprint.Report(name, bytes, count) end

---Raises MemoryPressure when the largest free block of the address space falls under this
---many bytes, and again only once it has risen back above; 0 stops. Kept for this mod until it
---goes; the highest threshold any mod holds is the one the runtime watches.
---@param bytes integer
---@return true|nil ok
---@return string|nil reason
function footprint.WatchPressure(bytes) end

---wxl.profile. Zones, graphs and messages on the profiler's timeline, beside the client's frames and the mods' handlers.
---Came with abi 0.1.12: a mod whose mod.json asks for an older abi cannot use it.
---@class wxl.profile
local profile = {}

---Whether a profiler viewer is connected: nothing is recorded while none is.
---@return boolean|nil
---@return string|nil reason
function profile.Connected() end

---Opens a zone named `name` on the profiler's timeline, inside the handler or callback running
---now; End closes it. Zones nest. Whatever this call left open is closed when it returns or
---errors. Nothing is recorded while no viewer is connected.
---@param name string
function profile.Begin(name) end

---Closes the innermost zone this mod opened in the call running now; nothing when none is open.
function profile.End() end

---Adds a line of text to the innermost zone this mod opened: the item, the unit, the file.
---@param text string
function profile.Text(text) end

---Puts a value on the graph of that name, drawn under the timeline.
---@param name string
---@param value number
function profile.Plot(name, value) end

---A line of text on the timeline, now.
---@param text string
function profile.Message(text) end

---Ends a frame of a frame set of the mod's own, drawn beside the client's frames.
---@param name string
function profile.FrameMark(name) end

---wxl.action. What the player can do, through the client's own functions: target, interact, cast, use, follow, move. Main thread only.
---Came with abi 0.1.18: a mod whose mod.json asks for an older abi cannot use it.
---@class wxl.action
local action = {}

---Targets an object, as a left click on it does.
---@param target wxl.Object
---@return true|nil ok
---@return string|nil reason
function action.Target(target) end

---Clears the target, as Escape does.
---@return true|nil ok
---@return string|nil reason
function action.ClearTarget() end

---Right-clicks an object: talks to an NPC, loots a corpse, opens a chest, uses a game object; it becomes the target.
---@param target wxl.Object
---@return true|nil ok
---@return string|nil reason
function action.Interact(target) end

---Casts a spell the player knows at a unit, or at the current target when target is left out, as CastSpellByID does.
---@param spell_id integer
---@param target? wxl.Object none when left out
---@return true|nil ok
---@return string|nil reason
function action.CastSpell(spell_id, target) end

---Stops the cast, the channel or the repeating spell in progress, as SpellStopCasting does.
---@return true|nil ok
---@return string|nil reason
function action.StopCasting() end

---Uses an item the player holds, on a unit, or on what the item picks itself when target is left out.
---@param item wxl.Object
---@param target? wxl.Object none when left out
---@return true|nil ok
---@return string|nil reason
function action.UseItem(item, target) end

---Follows a unit, as /follow does.
---@param unit wxl.Object
---@return true|nil ok
---@return string|nil reason
function action.Follow(unit) end

---Targets what a unit targets, as /assist does.
---@param unit wxl.Object
---@return true|nil ok
---@return string|nil reason
function action.Assist(unit) end

---Walks the player to a point, as a click-to-move click on the ground does.
---@param point wxl.Vec3
---@return true|nil ok
---@return string|nil reason
function action.MoveTo(point) end

---Came with abi 0.1.23: a mod whose mod.json asks for an older abi cannot use it.
---Turns the player to a facing in radians (0 north, growing towards the west), as steering with the mouse does, and tells the server.
---@param facing number
---@return true|nil ok
---@return string|nil reason
function action.Face(facing) end

---wxl.point. Functions of the engine a mod runs code before, instead of or after.
---Came with abi 0.1.16: a mod whose mod.json asks for an older abi cannot use it.
---@class wxl.point
local point = {}

---Every point of the table, with its signature, thread, flags and, when this runtime does not have it, why.
---@return wxl.PointInfo[]|nil
---@return string|nil reason
function point.Points() end

---The id of a point this runtime has, by name.
---@param name string
---@return integer|nil
---@return string|nil reason
function point.Find(name) end

---wxl.m2. The models objects draw with, what a character wears, and models a mod makes and hangs on them. Main thread only.
---Came with abi 0.1.20: a mod whose mod.json asks for an older abi cannot use it.
---Raw: present only when the mod's mod.json declares it, in "requires" or "optional".
---@class wxl.m2
local m2 = {}

---The model an object draws with, followed up what it hangs on (a mounted player answers its mount); nil, "not_found" for none.
---@param object wxl.Object
---@return lightuserdata|nil address
---@return string|nil reason
function m2.ObjectModel(object) end

---A race's prefix in model file names ("Hu", "Or", "Sc"), from ChrRaces.dbc.
---@param race_id integer
---@return string|nil
---@return string|nil reason
function m2.RacePrefix(race_id) end

---Makes a model of a file in the scene of the model given, for this mod: hang it on something with Attach.
---@param scene_model lightuserdata|wxl.Model an address an event or a call handed you
---@param path string
---@return wxl.Model|nil
---@return string|nil reason
function m2.CreateModel(scene_model, path) end

---Where an attachment point of a loaded model sits in its bind pose, as {x, y, z}.
---@param model lightuserdata|wxl.Model an address an event or a call handed you
---@param attachment integer
---@return wxl.Vec3|nil
---@return string|nil reason
function m2.AttachmentPoint(model, attachment) end

---wxl.engine. The client's own structures as this runtime lays them out.
---Came with abi 0.1.21: a mod whose mod.json asks for an older abi cannot use it.
---@class wxl.engine
local engine = {}

---The layout hash of an engine type (sdk/engine) on this runtime, by the fork's name of it.
---@param name string
---@return integer|nil
---@return string|nil reason
function engine.Layout(name) end

---wxl.relay. Services mods publish for each other: a call now, its answer later, on the main thread.
---Came with abi 0.1.22: a mod whose mod.json asks for an older abi cannot use it.
---@class wxl.relay
local relay = {}

---Publishes a service under a name, for this mod. `fn(request, method, ...)` runs on the main
---thread for each call: what it returns is the answer; when it returns nothing, it answers later
---with Answer or Fail. `cancelled(request)`, when given, is told of a request whose caller
---took it back or went. It stays until Unpublish, or until the mod is disabled, reloads or
---unloads; its unanswered requests then fail with "not_found".
---@param name string
---@param fn fun(request: integer, method: string, ...): ...
---@param cancelled? fun(request: integer)
---@return true|nil ok
---@return string|nil reason
function relay.Publish(name, fn, cancelled) end

---Takes this mod's service back; nil, "not_found" for one it does not publish.
---@param name string
---@return true|nil ok
---@return string|nil reason
function relay.Unpublish(name) end

---Whether some mod publishes a service under the name.
---@param name string
---@return boolean|nil
---@return string|nil reason
function relay.Published(name) end

---Asks a service: `fn(true, ...)` is called later, on the main thread, with the answer, or
---`fn(false, reason)` ("not_found" when nobody publishes it or it went, else the reason the
---service gave). `args` is a list of numbers, strings and booleans, or nil. Answers the
---request's number. Dropped, never answered, if this mod is disabled, reloads or unloads.
---@param name string
---@param method string
---@param args? any[]
---@param fn fun(ok: boolean, ...)
---@return integer|nil request
---@return string|nil reason
function relay.Call(name, method, args, fn) end

---Answers a request this mod's service received, with values: numbers, strings, booleans, nil.
---@param request integer
---@param ... any
---@return true|nil ok
---@return string|nil reason
function relay.Answer(request, ...) end

---Fails a request this mod's service received: its caller gets false and `reason`.
---@param request integer
---@param reason string
---@return true|nil ok
---@return string|nil reason
function relay.Fail(request, reason) end

---Takes back a request this mod made: its fn is never called, and the service is told.
---@param request integer
---@return true|nil ok
---@return string|nil reason
function relay.Cancel(request) end

---wxl.camera. Where the world camera is, and placing it instead of the client. Main thread only.
---Came with abi 0.1.23: a mod whose mod.json asks for an older abi cannot use it.
---@class wxl.camera
local camera = {}

---Where the world camera is this frame, in world coordinates.
---@return wxl.Vec3|nil
---@return string|nil reason
function camera.Position() end

---The direction the world camera looks, of unit length.
---@return wxl.Vec3|nil
---@return string|nil reason
function camera.Forward() end

---The direction the top of the screen is, of unit length.
---@return wxl.Vec3|nil
---@return string|nil reason
function camera.Up() end

---The field of view the world camera draws with, in radians.
---@return number|nil
---@return string|nil reason
function camera.Fov() end

---The point the client's camera turns around: the followed object's position at the height the client looks from.
---@return wxl.Vec3|nil
---@return string|nil reason
function camera.Pivot() end

---Whether a cinematic or a model camera of the client's has the view; a view set by a mod waits for it to end.
---@return boolean|nil
---@return string|nil reason
function camera.Cinematic() end

---Places the world camera from now on, for this mod: where it is, the direction it looks, and
---the field of view in radians, 0 to keep the client's. Set again each frame from OnCameraUpdate
---to move it with the frame. It stays until ReleaseView, or until the mod is disabled, reloads
---or unloads; when two mods set one, the later is applied and the earlier comes back after it.
---@param position wxl.Vec3
---@param forward wxl.Vec3
---@param fov number
---@return true|nil ok
---@return string|nil reason
function camera.SetView(position, forward, fov) end

---Gives the world camera this mod placed back to the client; nil, "not_found" when it holds none.
---@return true|nil ok
---@return string|nil reason
function camera.ReleaseView() end

---wxl.input. The pointer, for a mod that steers with the mouse: captured at the centre of the window, or free. Main thread only.
---Came with abi 0.1.23: a mod whose mod.json asks for an older abi cannot use it.
---@class wxl.input
local input = {}

---Hides the pointer and holds it at the centre of the window (true), where the game still picks
---and clicks, or lets this mod's hold go (false). Held while any mod holds it, and let go when
---the mod is disabled, reloads or unloads. The motion arrives as OnMouseMotion.
---@param captured boolean
---@return true|nil ok
---@return string|nil reason
function input.CaptureCursor(captured) end

---Whether some mod holds the pointer.
---@return boolean|nil
---@return string|nil reason
function input.CursorCaptured() end

---Whether the pointer is over the world rather than a frame of the interface.
---@return boolean|nil
---@return string|nil reason
function input.PointerOverWorld() end

---Came with abi 0.1.24: a mod whose mod.json asks for an older abi cannot use it.
---Keeps the game out of its own mouse look (true): a held mouse button turns nothing and hides
---nothing, a click still selects or acts; false gives this mod's block back. Kept while any mod
---blocks it, and given back when the mod is disabled, reloads or unloads.
---@param blocked boolean
---@return true|nil ok
---@return string|nil reason
function input.BlockMouseLook(blocked) end

---Came with abi 0.1.24: a mod whose mod.json asks for an older abi cannot use it.
---Whether some mod keeps the game out of its mouse look.
---@return boolean|nil
---@return string|nil reason
function input.MouseLookBlocked() end

---wxl.world. Questions about the world's geometry. Main thread only.
---Came with abi 0.1.23: a mod whose mod.json asks for an older abi cannot use it.
---@class wxl.world
local world = {}

---The first point of the world's geometry (ground, buildings, doodads) between two points, or nil when the way is clear; flags is the client's hit-test mask, the camera's when left out.
---@param start wxl.Vec3
---@param finish wxl.Vec3
---@param flags? integer 0 when left out
---@return wxl.Vec3|nil
---@return string|nil reason
function world.Trace(start, finish, flags) end

---wxl.pose. A unit's head turned by a mod, over its animation. Main thread only.
---Came with abi 0.1.24: a mod whose mod.json asks for an older abi cannot use it.
---@class wxl.pose
local pose = {}

---Turns a unit's head from where its body faces, in radians (yaw positive to its left, pitch
---positive up), reached over `smoothing` seconds for half the way, and holds it until
---ReleaseHead, or until the mod is disabled, reloads or unloads.
---@param unit wxl.Object
---@param yaw number
---@param pitch number
---@param smoothing number
---@return true|nil ok
---@return string|nil reason
function pose.Head(unit, yaw, pitch, smoothing) end

---Gives a unit's head this mod turned back to its animation; nil, "not_found" for one it does not hold.
---@param unit wxl.Object
---@return true|nil ok
---@return string|nil reason
function pose.ReleaseHead(unit) end

---@class wxl.ModInfo
---@field id string
---@field name string
---@field version string
---@field directory string

---@class wxl.RuntimeInfo
---@field id string
---@field name string
---@field version integer
---@field abi string the contract this mod is served, "major.minor.feature"
---@field abi_major integer
---@field abi_minor integer
---@field abi_feature integer
---Whether the runtime raises an event at all, so that subscribing to it can work: false
---for one it never raises; nil and "unsupported" on a runtime older than abi 0.1.13.
---@field Raises fun(family: wxl.Family, event: string): boolean|nil, string|nil

---@class wxl.object
local object = {}

---The object the local player controls, or nil while out of the world.
---@return wxl.Object|nil
function object.LocalPlayer() end

---The object carrying a guid, or nil when it is not in view.
---@param guid string|integer "0x" and up to sixteen hex digits, or a whole number
---@return wxl.Object|nil
function object.FromGuid(guid) end

---The objects of a type in view, or every object.
---@param type? wxl.ObjectType|integer
---@return wxl.Object[]
function object.InView(type) end

---One element of a field of every object of a list, in its order, in one call; false
---for an object that has none.
---@param objects wxl.Object[]
---@param name wxl.FieldName|string
---@param element? integer
---@return (wxl.FieldValue|false)[]
function object.Fields(objects, name, element) end

---Asks for ObjectScript's OnObjectFieldChanged whenever the field changes on an object of
---the type (nil for every object), for this mod, until Unwatch or until the mod closes.
---@param type wxl.ObjectType|"Object"|nil
---@param name wxl.FieldName|string
---@return boolean|nil watched, string|nil reason
function object.Watch(type, name) end

---Gives back what Watch(type, name) took: this mod's handlers no longer receive the
---field's changes. True also for a field the mod did not watch.
---@param type wxl.ObjectType|"Object"|nil
---@param name wxl.FieldName|string
---@return boolean|nil done, string|nil reason
function object.Unwatch(type, name) end

---@class wxl
---@field mod wxl.ModInfo this mod, from its manifest or its folder
---@field runtime wxl.RuntimeInfo the runtime that loaded wxl.lua
---@field object wxl.object
---@field field wxl.field every update field's dword index, by kind
---@field script wxl.script
---@field storage wxl.storage
---@field data wxl.data
---@field hook wxl.hook
---@field patch wxl.patch
---@field footprint wxl.footprint
---@field profile wxl.profile
---@field action wxl.action
---@field point wxl.point
---@field m2 wxl.m2
---@field engine wxl.engine
---@field relay wxl.relay
---@field camera wxl.camera
---@field input wxl.input
---@field world wxl.world
---@field pose wxl.pose
---@field events table<wxl.Family, string[]> every family's events, as On<Event>
wxl = {}

---Writes a line to the runtime's log, tagged with the mod's id.
---@param level "trace"|"debug"|"info"|"warn"|"error"
---@param ... any
function wxl.log(level, ...) end
