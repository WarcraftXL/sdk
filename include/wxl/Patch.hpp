// SPDX-License-Identifier: GPL-3.0-or-later
// wxl::Patch: client bytes written through the core, checked first, given back on disable (low_level).

#pragma once

#include "wxl/Core.hpp"
#include "wxl/Result.hpp"

#include <cstdint>
#include <initializer_list>
#include <span>

namespace wxl
{
    /// Writes `bytes` at `address` if the client holds `expected` there (empty: no check). Needs
    /// "permissions": { "low_level": true } in wxl.json. `wxl::Patch("jz to jmp", site, {0x74}, {0xEB});`
    inline Result<void> Patch(const char* name, uintptr_t address, std::span<const uint8_t> expected,
                              std::span<const uint8_t> bytes)
    {
        if (!detail::Has(&WXL_Api::Patch)) return Error::Unsupported;
        if (bytes.empty() || (!expected.empty() && expected.size() != bytes.size())) return Error::InvalidArgument;
        const int status = Api()->Patch(name, address, expected.empty() ? nullptr : expected.data(), bytes.data(),
                                        uint32_t(bytes.size()));
        if (status == WXL_OK) return {};
        return Error(status);
    }

    /// The same with byte lists.
    inline Result<void> Patch(const char* name, uintptr_t address, std::initializer_list<uint8_t> expected,
                              std::initializer_list<uint8_t> bytes)
    {
        return Patch(name, address, std::span<const uint8_t>(expected.begin(), expected.size()),
                     std::span<const uint8_t>(bytes.begin(), bytes.size()));
    }

    /// One value (an immediate, a displacement, a pointer) replaced by another of the same type.
    template <class T>
    Result<void> PatchValue(const char* name, uintptr_t address, const T& expected, const T& value)
    {
        return Patch(name, address, std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&expected), sizeof(T)),
                     std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&value), sizeof(T)));
    }
}
