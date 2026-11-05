// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>

#include "wxl/game/Binding.hpp"
#include "wxl/offsets/CGxDevice.hpp"
#include "wxl/offsets/CM2Model.hpp"
#include "wxl/offsets/CM2Scene.hpp"
#include "wxl/offsets/CM2Shared.hpp"
#include "wxl/offsets/CShaderEffect.hpp"
#include "wxl/offsets/M2Batch.hpp"
#include "wxl/offsets/SIMD.hpp"
#include "wxl/offsets/free/H.hpp"
#include "wxl/offsets/free/T.hpp"
#include "wxl/formats/M2Format.hpp"

/// Typed wrappers over the client's M2 model functions.
namespace wxl::game::m2
{
    namespace off = wxl::offsets;

    /// Resolves a texture handle to the device texture a sampler bind expects.
    inline void* ResolveTexture(void* handle)
    {
        return Native<off::TextureGetGxTexFn>(off::TextureGetGxTex)(handle, 0, 0);
    }

    inline void BindSampler(void* device, uint32_t selector, void* resolvedTexture)
    {
        Native<off::CGxDevice::RsSetFn>(off::CGxDevice::RsSet)(device, nullptr, selector, resolvedTexture);
    }

    inline void PushAlphaRef(float ref)
    {
        Native<off::CShaderEffect::SetAlphaRefFn>(off::CShaderEffect::SetAlphaRef)(ref);
    }

    /// The parsed skin profile in memory (CM2Shared::m_skinData), valid from skin finalize on.
    /// Arrays sit 4 bytes past the file layout and hold pointers; indices are global into the model's pools.
#pragma pack(push, 1)
    struct M2SkinProfile
    {
        uint32_t                          _lead;        // 0x00  added by the parse
        uint32_t                          vertexCount;  // 0x04
        uint16_t*                         vertexLookup; // 0x08
        uint32_t                          indexCount;   // 0x0C
        uint16_t*                         indices;      // 0x10
        uint32_t                          boneCount;    // 0x14
        uint8_t*                          bones;        // 0x18
        uint32_t                          submeshCount; // 0x1C
        wxl::formats::m2::M2SkinSection* submeshes;   // 0x20
        uint32_t                          batchCount;   // 0x24  (texunits)
        wxl::offsets::layout::M2Batch*    batches;      // 0x28
        uint32_t                          boneCountMax; // 0x2C  per-draw bone budget
    };
#pragma pack(pop)
    static_assert(sizeof(M2SkinProfile) == 0x30, "M2SkinProfile");
    static_assert(offsetof(M2SkinProfile, submeshes) == 0x20, "M2SkinProfile.submeshes");
    static_assert(offsetof(M2SkinProfile, batches) == 0x28, "M2SkinProfile.batches");

    /// One loaded M2 model (CM2Shared): its .m2 file buffer, parsed header and skin profile.
    class M2Model
    {
    public:
        /// Wraps a raw CM2Shared pointer (may be null).
        explicit M2Model(void* model) : m_model(model) {}

        void* raw() const { return m_model; }

        explicit operator bool() const { return m_model != nullptr; }

        /// The whole .m2 file, read into a heap buffer (CM2Shared::m_data).
        void* GetFileBuffer() const { return At<void*>(m_model, off::CM2Shared::fields::m_data); }

        uint32_t GetFileSize() const { return At<uint32_t>(m_model, off::CM2Shared::fields::m_fileSize); }

        /// The model path stem stored by the loader.
        const char* GetPathStem() const { return static_cast<const char*>(m_model) + off::CM2Shared::fields::m_fileName; }

        /// The header, parsed in place at the buffer base; once parsed, its M2Arrays hold pointers, not offsets.
        wxl::formats::m2::M2Header* GetHeader() const
        {
            return reinterpret_cast<wxl::formats::m2::M2Header*>(GetFileBuffer());
        }

        float GetBoundingSphereRadius() const { return GetHeader()->boundingSphereRadius; }

        uint32_t GetParticleEmitterCount() const { return GetHeader()->particleEmitters.count; }

        /// The parsed skin profile, or null before it is attached.
        M2SkinProfile* GetSkin() const
        {
            return At<M2SkinProfile*>(m_model, off::CM2Shared::fields::m_skinData);
        }

        /// Points the model at another .m2 image; the parser reads it on its next call.
        void SetBuffer(void* buffer, uint32_t size)
        {
            At<void*>(m_model, off::CM2Shared::fields::m_data)        = buffer;
            At<uint32_t>(m_model, off::CM2Shared::fields::m_fileSize) = size;
        }

        /// Rebuilds the GPU index buffer from skin->indices, then resets them to identity: filter them just before.
        /// Leaks a few small internal buffers per call.
        void FinalizeSkin()
        {
            Native<off::CM2Shared::InitializeSkinProfileFn>(off::CM2Shared::InitializeSkinProfile)(m_model, nullptr);
        }

    private:
        void* m_model;
    };

    /// Allocates with the .m2 load buffer's allocator, so the model destructor frees it correctly.
    /// tag is a debug label.
    inline void* AllocBuffer(uint32_t size, const char* tag)
    {
        return Native<off::SIMD::AllocA16Fn>(off::SIMD::AllocA16)(size, tag, 0);
    }

    /// Frees a buffer from AllocBuffer.
    inline void FreeBuffer(void* ptr)
    {
        Native<off::SIMD::FreeA16Fn>(off::SIMD::FreeA16)(ptr);
    }

    /// Creates a new scene model in cmo for the model path keyBuf, or null; the caller owns and releases it.
    inline void* GetRenderCtx(void* cmo, void* keyBuf)
    {
        return Native<off::CM2Scene::CreateModelFn>(off::CM2Scene::CreateModel)(cmo, nullptr, keyBuf, 0);
    }

    /// Attaches subObj to renderCtx at slot. forceAttach skips the attachment-point lookup, which otherwise
    /// silently refuses points missing from the parent's table.
    inline void AttachToScene(void* renderCtx, void* subObj, uint32_t slot, bool forceAttach = false)
    {
        Native<off::CM2Model::AttachToParentFn>(off::CM2Model::AttachToParent)(renderCtx, nullptr, subObj, slot, 0, forceAttach ? 1u : 0u);
    }

    /// Detaches and releases the models attached to subObj at slot.
    inline void DetachSlot(void* subObj, uint32_t slot)
    {
        Native<off::CM2Model::DetachAllChildrenByIdFn>(off::CM2Model::DetachAllChildrenById)(subObj, nullptr, slot);
    }

    /// Releases a model from GetRenderCtx.
    inline void ReleaseRenderCtx(void* renderCtx)
    {
        Native<off::CM2Model::ReleaseFn>(off::CM2Model::Release)(renderCtx, nullptr);
    }

    /// The replaceable-texture slot of a model's main texture.
    constexpr uint32_t kMainTextureSlot = 2;

    /// Binds modelPtr as the main texture of renderCtx.
    inline void BindTexSlot(void* renderCtx, void* modelPtr)
    {
        Native<off::CM2Model::ReplaceTextureFn>(off::CM2Model::ReplaceTexture)(renderCtx, nullptr, kMainTextureSlot, modelPtr);
    }

    /// Loads a resource by virtual path (e.g. "Item\\ObjectComponents\\Weapon\\AxeSmall.M2"); flags 0 loads
    /// synchronously. Returns null on failure; release with ReleaseResource.
    inline void* LoadResource(const char* path, uint32_t flags = 0)
    {
        int status = 0;
        return Native<off::TextureCreate_4B9760Fn>(off::TextureCreate_4B9760)(path, flags, &status, 0);
    }

    inline void ReleaseResource(void* resource)
    {
        Native<off::HandleCloseFn>(off::HandleClose)(resource);
    }
}
