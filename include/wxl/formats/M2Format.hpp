// SPDX-License-Identifier: GPL-3.0-or-later
// The M2 (MD20) records the client's model loader reads.

#pragma once

#include <cstddef>
#include <cstdint>

/// The model records the client's M2 loader reads, as stored in a .m2 file.
namespace wxl::formats::m2
{
    constexpr uint32_t kMagicMD20 = 0x3032444D; // 'MD20', the only magic the loader accepts

    constexpr uint32_t kClientVersion = 264;    // the header version the loader reads

    constexpr uint32_t kFlagUseTextureCombinerCombos = 0x8; // global_flags: header carries that trailing array

#pragma pack(push, 1)

    /// A count and an offset into the model buffer (a pointer once parsed).
    struct M2Array { uint32_t count; uint32_t offset; };

    /// The MD20 header: the exact on-disk layout the loader reads.
    struct M2Header
    {
        uint32_t magic;                  // 0x00
        uint32_t version;                // 0x04
        M2Array  name;                   // 0x08
        uint32_t globalFlags;            // 0x10
        M2Array  globalLoops;            // 0x14
        M2Array  sequences;              // 0x1C
        M2Array  sequenceLookup;         // 0x24
        M2Array  bones;                  // 0x2C
        M2Array  boneLookup;             // 0x34
        M2Array  vertices;               // 0x3C
        uint32_t numSkinProfiles;        // 0x44
        M2Array  colors;                 // 0x48
        M2Array  textures;               // 0x50
        M2Array  textureWeights;         // 0x58
        M2Array  textureTransforms;      // 0x60
        M2Array  textureReplacements;    // 0x68
        M2Array  materials;              // 0x70
        M2Array  boneCombos;             // 0x78
        M2Array  textureCombos;          // 0x80
        M2Array  textureUnitLookup;      // 0x88
        M2Array  textureWeightCombos;    // 0x90
        M2Array  textureTransformCombos; // 0x98
        float    boundingBox[6];         // 0xA0
        float    boundingSphereRadius;   // 0xB8
        float    collisionBox[6];        // 0xBC
        float    collisionSphereRadius;  // 0xD4
        M2Array  collisionIndices;       // 0xD8
        M2Array  collisionPositions;     // 0xE0
        M2Array  collisionNormals;       // 0xE8
        M2Array  attachments;            // 0xF0
        M2Array  attachmentLookup;       // 0xF8
        M2Array  events;                 // 0x100
        M2Array  lights;                 // 0x108
        M2Array  cameras;                // 0x110
        M2Array  cameraLookup;           // 0x118
        M2Array  ribbonEmitters;         // 0x120
        M2Array  particleEmitters;       // 0x128
        M2Array  textureCombinerCombos;  // 0x130  (only if globalFlags & kFlagUseTextureCombinerCombos)

        /// The header base as bytes.
        uint8_t*       base()       { return reinterpret_cast<uint8_t*>(this); }
        const uint8_t* base() const { return reinterpret_cast<const uint8_t*>(this); }
    };

    /// One animation sequence. flags 0x20: keys embedded (else streamed from a .anim file); 0x40: no keys of
    /// its own, aliasNext is the index of the next sequence of a ring sharing one key set.
    struct M2Sequence
    {
        uint16_t id;             // 0x00
        uint16_t variationIndex; // 0x02
        uint32_t duration;       // 0x04
        float    movespeed;      // 0x08
        uint32_t flags;          // 0x0C  bit 0x20 = embedded
        int16_t  frequency;      // 0x10
        uint16_t _pad12;         // 0x12
        uint32_t replayMin;      // 0x14
        uint32_t replayMax;      // 0x18
        uint32_t blendTime;      // 0x1C
        uint8_t  _bounds[0x1C];  // 0x20
        int16_t  variationNext;  // 0x3C  index of the next variation of this id, -1 = none
        uint16_t aliasNext;      // 0x3E  index of the next ring member (see flags bit 0x40)
    };

    // M2Texture type values.
    constexpr uint32_t kTexTypeHardcoded = 0;
    constexpr uint32_t kTexTypeObjectSkin = 2;
    constexpr uint32_t kTexTypeWeaponBlade = 3;
    // Hair and facial hair: the character code writes the handle at every texture of this type.
    constexpr uint32_t kTexTypeCharHair = 6;

    /// One skin submesh; character and component skins may use level as indexStart's high bits
    /// ((level << 16) | indexStart), others as a LOD marker.
    struct M2SkinSection
    {
        uint16_t skinSectionId;     // 0x00
        uint16_t level;             // 0x02
        uint16_t vertexStart;       // 0x04
        uint16_t vertexCount;       // 0x06
        uint16_t indexStart;        // 0x08
        uint16_t indexCount;        // 0x0A
        uint16_t boneCount;         // 0x0C
        uint16_t boneComboIndex;    // 0x0E
        uint16_t boneInfluences;    // 0x10
        uint16_t centerBoneIndex;   // 0x12
        float    centerPosition[3]; // 0x14
        float    sortCenterPos[3];  // 0x20
        float    sortRadius;        // 0x2C
    };

    constexpr uint32_t kBoneFlagTransformed = 0x200u; // M2CompBone flags: a non-identity local transform

#pragma pack(pop)

    static_assert(sizeof(M2Array) == 8, "M2Array");
    static_assert(offsetof(M2Header, vertices) == 0x3C, "vertices");
    static_assert(offsetof(M2Header, cameras) == 0x110, "cameras");
    static_assert(offsetof(M2Header, particleEmitters) == 0x128, "particleEmitters");
    static_assert(offsetof(M2Header, textureCombinerCombos) == 0x130, "textureCombinerCombos");
    static_assert(sizeof(M2Sequence) == 0x40, "M2Sequence");
    static_assert(offsetof(M2Sequence, blendTime) == 0x1C, "blendTime");
    static_assert(sizeof(M2SkinSection) == 0x30, "M2SkinSection");
    static_assert(offsetof(M2SkinSection, level) == 0x02, "M2SkinSection.level");
}
