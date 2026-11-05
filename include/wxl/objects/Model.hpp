// SPDX-License-Identifier: GPL-3.0-or-later
// wxl::Model: a handle over one M2 model object (a CM2Shared).

#pragma once

#include <cstdint>

#include "wxl/formats/M2Format.hpp"
#include "wxl/game/M2.hpp"

namespace wxl
{
    /// One M2 model object: the .m2 image the loader read, its parsed header, its live skin.
    class Model
    {
    public:
        Model() = default;
        /// Wraps a raw CM2Shared pointer (may be null).
        explicit Model(void* raw) : m_raw(raw) {}

        /// True when the handle holds a model.
        explicit operator bool() const { return m_raw != nullptr; }
        /// The raw engine pointer, for the bindings that take one.
        void* Raw() const { return m_raw; }

        /// The whole .m2 file as read; the parse rewrites it in place.
        void* FileBuffer() const { return game::m2::M2Model(m_raw).GetFileBuffer(); }
        /// Byte size of the model file buffer.
        uint32_t FileSize() const { return game::m2::M2Model(m_raw).GetFileSize(); }
        /// The model path stem the loader stored.
        const char* PathStem() const { return game::m2::M2Model(m_raw).GetPathStem(); }

        /// The parsed header (the buffer base); once parsed, its M2Arrays hold pointers, not offsets.
        wxl::formats::m2::M2Header* Header() const { return game::m2::M2Model(m_raw).GetHeader(); }
        /// The MD20 bounding sphere radius, once parsed.
        float BoundingSphereRadius() const { return game::m2::M2Model(m_raw).GetBoundingSphereRadius(); }
        /// Number of particle emitters in the model header.
        uint32_t ParticleEmitterCount() const { return game::m2::M2Model(m_raw).GetParticleEmitterCount(); }

        /// The engine's live skin profile (not the file layout: arrays 4 bytes higher, raw pointers,
        /// global indices); null before skin finalize.
        game::m2::M2SkinProfile* Skin() const { return game::m2::M2Model(m_raw).GetSkin(); }

        /// Points the model at another .m2 image; the parser reads it on its next call.
        void SetBuffer(void* buffer, uint32_t size) { game::m2::M2Model(m_raw).SetBuffer(buffer, size); }

        /// Rebuilds the index buffer from the skin's current triangles (apply a geoset filter just
        /// before); resets them to identity after, and leaks a little per call.
        void FinalizeSkin() { game::m2::M2Model(m_raw).FinalizeSkin(); }

        bool operator==(const Model& o) const { return m_raw == o.m_raw; }
        bool operator!=(const Model& o) const { return m_raw != o.m_raw; }

    private:
        void* m_raw = nullptr;
    };
}
