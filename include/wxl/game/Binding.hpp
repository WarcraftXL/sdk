// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>

/// Typed access to client functions, globals, fields and vtables by address.
namespace wxl::game
{
    /// Floats in a 4x4 matrix, row-major as the client stores it.
    inline constexpr int kMatrixFloats = 16;

    /// A client address as a function pointer: Native<Fn>(addr)(args...).
    template <class Fn>
    inline Fn Native(uintptr_t address) { return reinterpret_cast<Fn>(address); }

    template <class T>
    inline T Read(uintptr_t address) { return *reinterpret_cast<const T*>(address); }

    /// Plain store; the page must already be writable.
    template <class T>
    inline void Write(uintptr_t address, const T& value) { *reinterpret_cast<T*>(address) = value; }

    /// The field of type T at base + offset.
    template <class T>
    inline T& At(void* base, size_t offset)
    { return *reinterpret_cast<T*>(static_cast<uint8_t*>(base) + offset); }

    template <class T>
    inline T& At(uintptr_t base, size_t offset) { return *reinterpret_cast<T*>(base + offset); }

    template <class T>
    inline const T& At(const void* base, size_t offset)
    { return *reinterpret_cast<const T*>(static_cast<const uint8_t*>(base) + offset); }

    /// Slot @p slot of object's vtable, or nullptr when object is null.
    template <class Fn>
    inline Fn Virtual(const void* object, size_t slot)
    {
        if (!object) return nullptr;
        return reinterpret_cast<Fn>((*static_cast<void* const* const*>(object))[slot]);
    }
}
