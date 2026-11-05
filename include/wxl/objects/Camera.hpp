// SPDX-License-Identifier: GPL-3.0-or-later
// wxl::Camera: a handle over the engine's active camera, and the global render state the world is drawn with.

#pragma once

#include "wxl/game/Camera.hpp"
#include "wxl/game/Pick.hpp"

namespace wxl
{
    using game::world::Vec3;

    /// The engine's active camera (its field of view), plus the scene's view, projection and eye as statics.
    /// Not wxl::game::camera::Camera, the CPU-side struct Aim() fills.
    class Camera
    {
    public:
        Camera() = default;
        /// Wraps a raw engine camera pointer (may be null).
        explicit Camera(void* raw) : m_raw(raw) {}

        /// The camera the world render reads; null outside a world frame.
        static Camera Active() { return Camera(game::camera::GetActiveCamera()); }

        /// True when the handle holds a camera.
        explicit operator bool() const { return m_raw != nullptr; }
        /// The raw engine pointer, for the bindings that take one.
        void* Raw() const { return m_raw; }

        /// The full vertical field of view in radians; about 70 degrees for a null handle.
        float Fov() const { return game::camera::GetFov(m_raw); }
        /// Sets it for this frame (in world the engine sets it again each frame); ignored when null.
        void SetFov(float fovRad) const { game::camera::SetFov(m_raw, fovRad); }

        /// The world-to-view matrix, row-major float[16] aliasing the engine global.
        static const float* View() { return game::camera::GetView(); }
        /// The projection matrix, row-major float[16] aliasing the engine global.
        static const float* Projection() { return game::camera::GetProjection(); }
        /// View * Projection, row-major float[16] aliasing the engine global.
        static const float* ViewProj() { return game::camera::GetViewProj(); }

        /// The eye position of the scene.
        static Vec3 Position()
        {
            float p[3];
            game::camera::GetPosition(p);
            return { p[0], p[1], p[2] };
        }

        /// Writes the view matrix: in world the engine rewrites it every frame, on glue screens it stays.
        static void SetView(const float m[16]) { game::camera::SetView(m); }
        /// Writes the projection matrix (same per-frame rewrite as SetView).
        static void SetProjection(const float m[16]) { game::camera::SetProjection(m); }
        /// Writes View * Projection; it must match them or culling disagrees with the draw.
        static void SetViewProj(const float m[16]) { game::camera::SetViewProj(m); }

        /// Writes the eye position (same per-frame rewrite as SetView).
        static void SetPosition(const Vec3& position)
        {
            const float p[3] = { position.x, position.y, position.z };
            game::camera::SetPosition(p);
        }

        bool operator==(const Camera& c) const { return m_raw == c.m_raw; }
        bool operator!=(const Camera& c) const { return m_raw != c.m_raw; }

    private:
        void* m_raw = nullptr;
    };
}
