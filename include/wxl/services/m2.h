// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The model service: the models objects draw with, what a character wears, and models a mod
// makes and hangs on them.
//
// A model is the client's own CM2Model, opaque to the mod. One the mod makes with create_model is
// its own until it gives it back with release_model, or until the mod unloads or is disabled:
// then it is taken off whatever it hangs on and let go. Every other model (what object_model
// answers, the `model` of an AppearanceChanged event) is the client's: a mod reads it and hangs
// its own models on it, during the call or the event it was handed in, and never keeps it.
//
// What a character wears comes as the event AppearanceChanged (Object family): an item display
// put on or taken off a character appearance, and the appearance going away. A model hung on an
// appearance's points that the client strips from its interface copies (the character panel,
// portraits, character select) is hung back on the copy after the strip.
//
// Raw: a model pointer the client freed is not detected. Calls are made on the client's
// main thread.

#ifndef WXL_SERVICE_M2_H
#define WXL_SERVICE_M2_H

#include "wxl/abi.h"

#define WXL_M2_SERVICE         "wxl.m2"
#define WXL_M2_SERVICE_VERSION 1

#ifdef __cplusplus
extern "C" {
#endif

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
    /// Every frame, pose the child's bones as the parent's bones of the same key bone or name:
    /// a piece skinned to the character's skeleton (a later expansion's collection model) moves
    /// with the body. A bone with no counterpart follows the nearest bone of the parent.
    WXL_M2_ATTACH_FOLLOW_BONES = 1u << 1,
};

/**
 * @brief Where a model is put: a position, a turn and a scale.
 *
 * The turn is applied as the client turns its spell effects: @p yaw around the up axis (a unit's
 * facing), then @p pitch around the model's side axis, then @p roll around its forward axis, all
 * in radians, in the model's own frame.
 */
typedef struct wxl_m2_placement
{
    uint32_t struct_size;
    wxl_vec3 position;
    float    yaw;
    float    pitch;
    float    roll;
    float    scale; ///< 0 reads as 1
} wxl_m2_placement;

/**
 * @brief One model's bones, the frame the client posed them.
 *
 * Matrices are the client's own: 16 floats each, row vectors (translation in 12..14), taking a
 * point of the model's file into the scene's camera-relative space. Valid for the callback only.
 */
typedef struct wxl_m2_bones
{
    uint32_t    struct_size;
    void*       model;             ///< the client's CM2Model
    void*       file;              ///< its model file, the same for every model of it while loaded
    wxl_str     path;              ///< the file's path, as the client asked for it
    uint32_t    bone_count;
    float*      matrices;          ///< bone_count matrices; a watcher may rewrite them
    float       model_matrix[16];  ///< the model's own matrix: its file's space to camera-relative
    float       view_to_world[16]; ///< the scene's camera-relative space to the world
    uint32_t    time_ms;           ///< the scene's clock
} wxl_m2_bones;

/// Whether a model file's models should be handed to a bone watcher. Asked once per file.
typedef wxl_bool(WXL_CALL* wxl_m2_bones_filter_fn)(void* file, wxl_str path, uint32_t bone_count, void* user);
typedef void(WXL_CALL* wxl_m2_bones_fn)(const wxl_m2_bones* bones, void* user);

typedef struct wxl_m2_api
{
    uint32_t struct_size;

    /**
     * @brief The model an object draws with, followed up what it is attached to: a mounted
     *        player answers its mount. Null for an object with no model, and for a handle that
     *        is not in view.
     */
    void*(WXL_CALL* object_model)(wxl_handle object);

    /// The ItemDisplayInfo row @p display_id. WXL_E_NOT_FOUND for none.
    wxl_result(WXL_CALL* item_display)(uint32_t display_id, wxl_m2_item_display* out);

    /// ChrRaces' client prefix of a race ("Hu", "Or", "Sc"...): what model file names carry.
    /// WXL_E_NOT_FOUND for a race the client does not have.
    wxl_result(WXL_CALL* race_prefix)(uint32_t race_id, wxl_str* out);

    /**
     * @brief Makes a model of @p path in the scene @p scene_model is in. The mod owns the model
     *        and gives it back with release_model. Null when the scene cannot make one. A path
     *        the client cannot find still makes a model (its placeholder): check the file first.
     */
    void*(WXL_CALL* create_model)(void* scene_model, wxl_str path);

    /// Takes the model off what it hangs on and gives it back. A model this mod did not make is
    /// left alone.
    void(WXL_CALL* release_model)(void* model);

    /**
     * @brief Hangs @p model on @p parent at attachment point @p attachment (WXL_M2_ATTACH_*
     *        flags). The parent holds a reference of its own, and lets it go when either is
     *        detached or the parent is freed. WXL_E_NOT_FOUND when the parent, loaded, has no
     *        such point and WXL_M2_ATTACH_ROOT was not asked for; WXL_E_INVALID for a model this
     *        mod did not make; WXL_E_UNSUPPORTED for WXL_M2_ATTACH_FOLLOW_BONES on a runtime that
     *        does not pose bones.
     */
    wxl_result(WXL_CALL* attach_model)(void* model, void* parent, uint32_t attachment, uint32_t flags);

    /// Takes @p model off whatever it hangs on; nothing when it hangs on nothing.
    void(WXL_CALL* detach_model)(void* model);

    /// What @p model hangs on, null for nothing: how a mod tells its model was let go.
    void*(WXL_CALL* model_parent)(void* model);

    /// Shows or hides every geoset of @p model whose id is in [first, last]. Kept until the
    /// model has loaded when it has not yet.
    void(WXL_CALL* set_geosets)(void* model, uint32_t first, uint32_t last, wxl_bool visible);

    /// Replaces the texture @p model takes for texture type @p type (2 an item's skin, 3 and 4 a
    /// weapon's blade and handle, 11-13 a creature's skins...) with the file @p path.
    /// WXL_E_NOT_FOUND when the file makes no texture.
    wxl_result(WXL_CALL* replace_texture)(void* model, uint32_t type, wxl_str path);

    /**
     * @brief Where attachment point @p attachment of @p model sits in the model's bind pose (its
     *        file's own space). WXL_E_NOT_FOUND when the model has no such point or has not
     *        loaded. Any model: one this mod made, or the client's.
     */
    wxl_result(WXL_CALL* attachment_point)(const void* model, uint32_t attachment, wxl_vec3* out);

    /**
     * @brief Hangs @p model on @p parent at a point the parent's file does not have: every frame,
     *        every bone of @p model is posed as the parent's key bone @p key_bone, moved by
     *        @p offset in the parent's bind space -- an attachment point, made by hand. The model
     *        hangs as @p attachment, attached to the root, which is what interface copies of the
     *        parent and the client's own stripping of attachments see. WXL_E_NOT_FOUND when the
     *        parent has loaded and has no such key bone; WXL_E_UNSUPPORTED on a runtime that does
     *        not pose bones.
     */
    wxl_result(WXL_CALL* attach_to_bone)(void* model, void* parent, uint32_t attachment, uint32_t key_bone,
                                         wxl_vec3 offset);

    // --- Models of the world: spell effects, missiles, anything placed by hand -------------------
    // Members from here on are newer than the first table: check struct_size reaches one before
    // calling it (offsetof(wxl_m2_api, member) < struct_size).

    /**
     * @brief Makes a model of @p path in the world's scene, the one units and spell effects are
     *        in. It shows nowhere until it is placed with place_model or attached to a unit's
     *        model. The caller owns the reference, as with create_model. Null outside the world.
     */
    void*(WXL_CALL* create_world_model)(wxl_str path);

    /**
     * @brief Puts a world model at @p placement in the world (position in world coordinates),
     *        or moves it there. Detaches it first when it hangs on something. Call it every frame
     *        for a model that moves.
     */
    wxl_result(WXL_CALL* place_model)(void* model, const wxl_m2_placement* placement);

    /**
     * @brief How a model hanging on a point sits on it: @p placement's position is the offset
     *        from the point, in the point's own frame, its turn and scale the model's. Kept across
     *        attach_model; identity until set.
     */
    wxl_result(WXL_CALL* set_model_transform)(void* model, const wxl_m2_placement* placement);

    /**
     * @brief Plays animation @p anim_id on the whole model from its start, at @p speed (1 normal),
     *        repeating as its file says. A model without it plays what the file falls back to, or
     *        Stand. Kept until the model has loaded when it has not yet.
     */
    wxl_result(WXL_CALL* play_animation)(void* model, uint32_t anim_id, float speed);

    /// Whether @p model's file has loaded: what animation_length and attachment_world need.
    wxl_bool(WXL_CALL* model_loaded)(void* model);

    /// How long one play of animation @p anim_id lasts, in milliseconds; 0 for a model without it.
    /// WXL_E_FAILED while the model has not loaded.
    wxl_result(WXL_CALL* animation_length)(void* model, uint32_t anim_id, uint32_t* out_ms);

    /// The model's own alpha, 0 to 1: a fade.
    void(WXL_CALL* set_alpha)(void* model, float alpha);

    /// Whether the model's particle emitters and ribbons make anything new; those already made
    /// play out. What a spell effect does as it ends.
    void(WXL_CALL* set_emitting)(void* model, wxl_bool emitting);


    /**
     * @brief Where attachment point @p attachment of a loaded @p model is in the world now.
     *        WXL_E_NOT_FOUND when the model has no such point, WXL_E_FAILED while it has not
     *        loaded. @p model may be one object_model returned.
     */
    wxl_result(WXL_CALL* attachment_world)(void* model, uint32_t attachment, wxl_vec3* out);

    /**
     * @brief The height of the ground under @p at: the first terrain, building or doodad surface a
     *        ray meets from @p up yards above it to @p down yards below, as the client finds the
     *        ground for its own spell effects. WXL_E_NOT_FOUND when the ray meets nothing.
     */
    wxl_result(WXL_CALL* ground_height)(const wxl_vec3* at, float up, float down, float* out_z);

    // --- Bones, as the client has just posed them: what a mod simulating cloth or chains needs ---

    /**
     * @brief Calls @p fn every frame for every model whose file @p filter accepted, right after
     *        the client posed its bones (and after wxl.m2 posed the models it attached), until
     *        unwatched. @p filter is asked once per model file the first time a loaded model of
     *        it animates. Both run on the client's animate threads, more than one at a time:
     *        they must be thread-safe and must not call this service. A watcher that faults is
     *        dropped.
     */
    wxl_result(WXL_CALL* watch_bones)(wxl_m2_bones_filter_fn filter, wxl_m2_bones_fn fn, void* user,
                                      uint32_t* out_token);
    void(WXL_CALL* unwatch_bones)(uint32_t token);

    /**
     * @brief Bone @p index of model file @p file (a wxl_m2_bones' file): its parent bone (-1 for
     *        none) and its pivot in the file's own space. WXL_E_NOT_FOUND past the last bone.
     *        Safe on the animate threads, inside a bone watcher.
     */
    wxl_result(WXL_CALL* bone_info)(void* file, uint32_t index, int16_t* out_parent, wxl_vec3* out_pivot);
} wxl_m2_api;

#ifdef __cplusplus
}
#endif

#endif
