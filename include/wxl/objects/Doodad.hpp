// SPDX-License-Identifier: GPL-3.0-or-later
// wxl::Doodad: a handle over one placed map doodad, read and written through wxl::game::doodad.

#pragma once

#include <cstddef>

#include "wxl/game/Doodad.hpp"
#include "wxl/objects/Object.hpp"

namespace wxl
{
    /// A placed map doodad (an entry of a terrain chunk's doodad list): a model, a world transform, bounds.
    class Doodad
    {
    public:
        /// A model file name in storage of its own.
        struct Name
        {
            /// Bytes of storage, terminator included; a longer name is truncated.
            static constexpr size_t kCapacity = 128;

            char text[kCapacity] = { '\0' };

            /// The bare model file name; empty when it could not be read.
            const char* Text() const { return text; }

            /// True when the name is not empty.
            explicit operator bool() const { return text[0] != '\0'; }
        };

        Doodad() = default;
        /// Wraps a raw doodad instance pointer (may be null).
        explicit Doodad(void* raw) : m_raw(raw) {}

        /// True when the position block is mapped and reads as a sane world location.
        explicit operator bool() const { return game::doodad::IsValid(m_raw); }

        /// The raw engine pointer, for the bindings that take one.
        void* Raw() const { return m_raw; }

        /// The render instance holding the live world matrix; null while the model loads.
        void* Instance() const { return game::doodad::Instance(m_raw); }

        /// The bare model file name into `out` (always terminated); false while the model loads.
        bool ModelName(char* out, size_t cap) const { return game::doodad::ModelName(m_raw, out, cap); }

        /// The bare model file name; falsy while the model loads.
        Name ModelName() const
        {
            Name n;
            game::doodad::ModelName(m_raw, n.text, Name::kCapacity);
            return n;
        }

        /// The world position; the origin when the block is not mapped.
        Vec3 Position() const
        {
            float p[3];
            game::doodad::Position(m_raw, p);
            return { p[0], p[1], p[2] };
        }

        /// The uniform scale; 1 when not mapped.
        float Scale() const { return game::doodad::Scale(m_raw); }

        /// The pick target, the world bounding-sphere center; the position when that reads implausible.
        Vec3 Center() const
        {
            float c[3];
            game::doodad::Center(m_raw, c);
            return { c[0], c[1], c[2] };
        }

        /// The world-space box set once at spawn (a degenerate point); false when not mapped.
        bool BBox(Vec3& min, Vec3& max) const
        {
            float mn[3], mx[3];
            if (!game::doodad::BBox(m_raw, mn, mx)) return false;
            min = { mn[0], mn[1], mn[2] };
            max = { mx[0], mx[1], mx[2] };
            return true;
        }

        /// The model-local box from the model header; transform its corners by WorldMatrix for the
        /// world box. False while the model loads or when the box is degenerate.
        bool LocalBounds(Vec3& lo, Vec3& hi) const
        {
            float l[3], h[3];
            if (!game::doodad::LocalBounds(m_raw, l, h)) return false;
            lo = { l[0], l[1], l[2] };
            hi = { h[0], h[1], h[2] };
            return true;
        }

        /// The live world matrix, 16 floats row-major (translation in m[12..14]); false when not loaded.
        bool WorldMatrix(float m[16]) const { return game::doodad::WorldMatrix(m_raw, m); }

        /// Replaces the world transform: the instance matrix, then the doodad's staging copies.
        void SetWorldMatrix(const float m[16]) const
        {
            if (m_raw) game::doodad::SetWorldMatrix(m_raw, m);
        }

        /// Moves the doodad (the translation row of the instance matrix).
        void SetPosition(const Vec3& position) const
        {
            const float p[3] = { position.x, position.y, position.z };
            if (m_raw) game::doodad::SetPosition(m_raw, p);
        }

        /// Moves the doodad to x, y, z.
        void SetPosition(const float p[3]) const
        {
            if (m_raw) game::doodad::SetPosition(m_raw, p);
        }

        bool operator==(const Doodad& d) const { return m_raw == d.m_raw; }
        bool operator!=(const Doodad& d) const { return m_raw != d.m_raw; }

    protected:
        void* m_raw = nullptr;
    };
}
