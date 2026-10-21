// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The model service: what a character wears, and models a mod hangs on it.
//
// Not portable. It is published only by a runtime that sees the client's character appearance and
// its M2 models from the inside -- the recompiled client -- and a mod that needs it requires it in
// its manifest. Models are raw: a `void*` model is the client's own CM2Model, opaque to the mod,
// and every call that takes one must be given a model the mod holds a reference to (one it made
// with create_model and has not released), or one handed to it in the callback it is in.
//
// Calls are made on the client's main thread.

#ifndef WXL_SERVICE_M2_H
#define WXL_SERVICE_M2_H

#include "wxl/abi.h"

#define WXL_M2_SERVICE         "wxl.m2"
#define WXL_M2_SERVICE_VERSION 1

#ifdef __cplusplus
extern "C" {
#endif

/// What happened to a wearer, in a wxl_m2_equipment.
typedef enum wxl_m2_equipment_change
{
    WXL_M2_EQUIPPED   = 1, ///< display_id was put on item_slot, the client's own models already attached
    WXL_M2_UNEQUIPPED = 2, ///< display_id is about to come off item_slot
    WXL_M2_FREED      = 3  ///< the wearer is about to be freed: forget everything kept for it
} wxl_m2_equipment_change;

/**
 * @brief A change to what one character appearance wears.
 *
 * A wearer is the client's CCharacterComponent: every player, every humanoid NPC dressed from
 * CreatureDisplayInfoExtra, and every copy the interface makes of one (character select, the
 * dressing room). It is identified by `wearer`, stable until WXL_M2_FREED.
 */
typedef struct wxl_m2_equipment
{
    uint32_t struct_size;
    uint32_t change;     ///< a wxl_m2_equipment_change
    void*    wearer;     ///< the appearance (opaque); the same pointer for the life of it
    void*    model;      ///< its character model, what item models attach to; may be null
    uint32_t race_id;    ///< ChrRaces id
    uint32_t sex_id;     ///< 0 male, 1 female
    uint32_t item_slot;  ///< the client's item slot: 0 head, 1 shoulder, 2 shirt, 3 chest, 4 belt,
                         ///< 5 legs, 6 feet, 7 wrists, 8 hands, 9 tabard, 10 cloak, 11 quiver;
                         ///< 0 with WXL_M2_FREED
    uint32_t display_id; ///< ItemDisplayInfo id put on or taken off; 0 with WXL_M2_FREED
    wxl_bool npc;        ///< an NPC appearance: its body is one baked texture, items paint nothing
} wxl_m2_equipment;

typedef void(WXL_CALL* wxl_m2_equipment_fn)(const wxl_m2_equipment* change, void* user);

/**
 * @brief One ItemDisplayInfo row, as the client holds it.
 *
 * Strings point into the client's database and stay valid for the session; an empty column is an
 * empty string, never null.
 */
typedef struct wxl_m2_item_display
{
    uint32_t    struct_size;
    uint32_t    id;
    const char* model[2];         ///< ModelName_1, ModelName_2
    const char* model_texture[2]; ///< ModelTexture_1, ModelTexture_2
    const char* icon[2];          ///< InventoryIcon_1, InventoryIcon_2
    uint32_t    geoset_group[3];
    uint32_t    flags;
    uint32_t    spell_visual_id;
    uint32_t    group_sound_index;
    uint32_t    helmet_geoset_vis[2];
    const char* texture[8];       ///< the body texture of each component section
    int32_t     item_visual;
    uint32_t    particle_color_id;
} wxl_m2_item_display;

/// attach_model flags.
enum
{
    /// Attach even where the parent has no such attachment point: the child then sits at the
    /// parent's origin. What a model skinned to the whole body needs.
    WXL_M2_ATTACH_ROOT = 1u << 0,
    /// Every frame, pose the child's bones as the parent's bones of the same name or key bone:
    /// a piece skinned to the character's skeleton (a retail collection model) moves with the
    /// body. A bone with no counterpart follows its nearest parent that has one.
    WXL_M2_ATTACH_FOLLOW_BONES = 1u << 1,
};

typedef struct wxl_m2_api
{
    uint32_t struct_size;

    /**
     * @brief Calls @p fn for every change to what any character appearance wears, until
     *        unwatched. Runs under the crash guard; a watcher that faults disables its mod.
     */
    wxl_result(WXL_CALL* watch_equipment)(wxl_m2_equipment_fn fn, void* user, uint32_t* out_token);
    void(WXL_CALL* unwatch_equipment)(uint32_t token);

    /// The ItemDisplayInfo row @p display_id. WXL_E_NOT_FOUND for none.
    wxl_result(WXL_CALL* item_display)(uint32_t display_id, wxl_m2_item_display* out);

    /// ChrRaces' client prefix of a race ("Hu", "Or", "Sc"...): what model file names carry.
    wxl_result(WXL_CALL* race_prefix)(uint32_t race_id, wxl_str* out);

    /**
     * @brief Makes a model of @p path in the scene @p scene_model is in. The caller owns the
     *        reference returned and gives it back with release_model. Null when the scene cannot
     *        make one. A path the client cannot find still makes a model (its placeholder): check
     *        the file first.
     */
    void*(WXL_CALL* create_model)(void* scene_model, wxl_str path);
    void(WXL_CALL* release_model)(void* model);

    /**
     * @brief Attaches @p model to @p parent at attachment point @p attachment (WXL_M2_ATTACH_*
     *        flags). The parent then holds a reference of its own, and lets it go when either is
     *        detached or the parent is freed. WXL_E_NOT_FOUND when the parent, loaded, has no such
     *        point and WXL_M2_ATTACH_ROOT was not asked for.
     */
    wxl_result(WXL_CALL* attach_model)(void* model, void* parent, uint32_t attachment, uint32_t flags);

    /// Detaches @p model from whatever it is attached to; nothing when it is not.
    void(WXL_CALL* detach_model)(void* model);

    /// What @p model is attached to, null for nothing: a mod's way to tell its model was let go.
    void*(WXL_CALL* model_parent)(void* model);

    /// Shows or hides every geoset of @p model whose id is in [first, last]. Kept until the model
    /// has loaded when it has not yet.
    void(WXL_CALL* set_geosets)(void* model, uint32_t first, uint32_t last, wxl_bool visible);

    /// Replaces the texture @p model takes for texture type @p type (2 an item's skin, 3 and 4 a
    /// weapon's blade and handle, 11-13 a creature's skins...) with the file @p path.
    wxl_result(WXL_CALL* replace_texture)(void* model, uint32_t type, wxl_str path);

    /**
     * @brief Where attachment point @p attachment of @p model sits in the model's bind pose (its
     *        file's own space). WXL_E_NOT_FOUND when the model has no such point or has not loaded.
     *        @p model may be a character model handed to a watcher.
     */
    wxl_result(WXL_CALL* attachment_point)(void* model, uint32_t attachment, wxl_vec3* out);

    /**
     * @brief Hangs @p model on @p parent at a point the parent's file does not have: every frame,
     *        every bone of @p model is posed as the parent's key bone @p key_bone, moved by
     *        @p offset in the parent's bind space -- what an attachment point is, made by hand.
     *        The model is attached as @p attachment (ATTACH_ROOT), which is what copies of the
     *        parent and the client's own stripping of attachments see. WXL_E_NOT_FOUND when the
     *        parent has loaded and has no such key bone.
     */
    wxl_result(WXL_CALL* attach_to_bone)(void* model, void* parent, uint32_t attachment, uint32_t key_bone,
                                         const wxl_vec3* offset);
} wxl_m2_api;

#ifdef __cplusplus
}
#endif

#endif
