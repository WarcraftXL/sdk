// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

#include "wxl/game/Binding.hpp"
#include "wxl/offsets/CGWorldFrame.hpp"
#include "wxl/offsets/CSimpleCamera.hpp"
#include "wxl/offsets/CWorldScene.hpp"

/// The world view, projection and camera position. Matrices are row-major float[16], D3D row-vector,
/// left-handed; the returned pointers alias engine globals.
namespace wxl::game::camera
{
    namespace off = wxl::offsets;

    /// The field of view GetFov answers without a camera: 70 degrees.
    constexpr float kDefaultFovRad = 1.2217f;

    inline const float* GetView()       { return reinterpret_cast<const float*>(off::CWorldScene::viewMatrix); }
    inline const float* GetProjection() { return reinterpret_cast<const float*>(off::CWorldScene::projMatrix); }
    /// View * Projection.
    inline const float* GetViewProj()   { return reinterpret_cast<const float*>(off::CWorldScene::s_viewProjMatrix); }

    inline void GetPosition(float out[3])
    {
        const float* p = reinterpret_cast<const float*>(off::CWorldScene::s_camPos);
        out[0] = p[0]; out[1] = p[1]; out[2] = p[2];
    }

    /// The active camera the world render reads its field of view from, or null.
    inline void* GetActiveCamera()
    {
        return Native<off::CGWorldFrame::GetActiveCameraFn>(off::CGWorldFrame::GetActiveCamera)();
    }

    /// Field of view in radians; about 70 degrees when camera is null.
    inline float GetFov(void* camera)
    {
        return camera
            ? At<float>(camera, off::CSimpleCamera::fields::m_fov)
            : kDefaultFovRad;
    }

    // The setters below are overwritten every frame in world; they aim the scene on the glue screens.
    // Projection: index 0 = X scale, 5 = Y scale, 10 = Z scale, 11 = 1, 14 = Z bias.

    inline void SetView(const float m[16])
    { for (int i = 0; i < kMatrixFloats; ++i) reinterpret_cast<float*>(off::CWorldScene::viewMatrix)[i] = m[i]; }

    inline void SetProjection(const float m[16])
    { for (int i = 0; i < kMatrixFloats; ++i) reinterpret_cast<float*>(off::CWorldScene::projMatrix)[i] = m[i]; }

    /// m must equal View * Projection, or culling disagrees with the draw.
    inline void SetViewProj(const float m[16])
    { for (int i = 0; i < kMatrixFloats; ++i) reinterpret_cast<float*>(off::CWorldScene::s_viewProjMatrix)[i] = m[i]; }

    inline void SetPosition(const float pos[3])
    {
        float* p = reinterpret_cast<float*>(off::CWorldScene::s_camPos);
        p[0] = pos[0]; p[1] = pos[1]; p[2] = pos[2];
    }

    /// Builds the world's view and projection once per frame from an eye and a target; detour it to move
    /// the camera in world.
    constexpr uintptr_t kBuildMatricesSeam = off::CWorldScene::Update;

    /// (eye, target), both float[3] in world coordinates.
    using BuildMatricesFn = void(__cdecl*)(const float eye[3], const float target[3]);

    /// kBuildMatricesSeam's name for HookAttachByName.
    inline constexpr char kBuildMatricesPoint[] = "CWorldScene::Update";

    /// Sets the full vertical field of view in radians; null is ignored. In world it holds for this frame only.
    inline void SetFov(void* camera, float fovRad)
    {
        if (camera) At<float>(camera, off::CSimpleCamera::fields::m_fov) = fovRad;
    }

    /// A camera the world renderer accepts; Aim points it at the engine's own method table.
    using Camera = off::layout::CSimpleCamera;

    /// Fills every field of cam; the vectors are unit length, fovRad the full vertical field of view.
    inline void Aim(Camera& cam, const float pos[3], const float forward[3],
                    const float right[3], const float up[3], float fovRad)
    {
        cam.vfptr = reinterpret_cast<const void*>(off::CSimpleCamera::vftable);
        for (int i = 0; i < 3; ++i)
        {
            cam.m_position[i] = pos[i];
            cam.m_facing[i]     = forward[i];
            cam.m_facing[3 + i] = right[i];
            cam.m_facing[6 + i] = up[i];
        }
        cam.m_fov = fovRad;
    }
}
