// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The engine headers on the architecture this is built for: they compile (each struct's
// static_asserts hold), every type's layout hash is XXH64 of its canonical text, and the table a
// runtime answers wxl.engine from (layouts.def) agrees with the headers.

#include <cstdio>
#include <cstring>

#define XXH_INLINE_ALL
#include "xxhash.h"

#include "wxl/engine/engine.hpp"

namespace
{
    int g_failures = 0, g_checks = 0;

    void Check(bool ok, const char* what, const char* detail = "")
    {
        ++g_checks;
        if (!ok) ++g_failures;
        if (!ok) std::printf("FAIL %s %s\n", what, detail);
    }

#if WXL_ENGINE_X86
    constexpr const char* kArch = "x86";
#define WXL_ENGINE_PICK(x86, x64) x86
#else
    constexpr const char* kArch = "x64";
#define WXL_ENGINE_PICK(x86, x64) x64
#endif
}

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("engine headers on %s\n", kArch);

    int types = 0;
#define WXL_ENGINE_TYPE(Name, fork, size_x86, hash_x86, size_x64, hash_x64)                                  \
    ++types;                                                                                                 \
    Check(sizeof(::wxl::engine::Name) == WXL_ENGINE_PICK(size_x86, size_x64), #Name ": size in layouts.def"); \
    Check(::wxl::engine::Name::kLayoutHash == WXL_ENGINE_PICK(hash_x86, hash_x64), #Name ": hash in layouts.def"); \
    Check(std::strcmp(::wxl::engine::Name::kName, #fork) == 0, #Name ": kName is the fork's spelling");
#define WXL_ENGINE_FIELD(Name, member, offset_x86, offset_x64) \
    Check(offsetof(::wxl::engine::Name, member) == WXL_ENGINE_PICK(offset_x86, offset_x64), #Name "::" #member);
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"
#endif
#include "wxl/engine/layouts.def"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
#undef WXL_ENGINE_FIELD
#undef WXL_ENGINE_TYPE

    int hashed = 0;
#define WXL_ENGINE_CANON(Name, arch, text)                                                                  \
    if (std::strcmp(#arch, kArch) == 0)                                                                     \
    {                                                                                                        \
        ++hashed;                                                                                            \
        const char* canon = text;                                                                           \
        char        detail[96];                                                                             \
        const auto  hash = static_cast<unsigned long long>(XXH64(canon, std::strlen(canon), 0));             \
        std::snprintf(detail, sizeof detail, "XXH64 %016llX, header %016llX", hash,                          \
                      static_cast<unsigned long long>(::wxl::engine::Name::kLayoutHash));                   \
        Check(hash == ::wxl::engine::Name::kLayoutHash, #Name ": the layout hash is XXH64 of its text", detail); \
    }
#include "wxl/engine/canon.def"
#undef WXL_ENGINE_CANON

    Check(types > 0 && hashed == types, "every type has its canonical text");

    // A parsed model array, as each client holds it.
    {
        wxl::engine::M2Array<uint32_t> array{};
        uint32_t                       values[3] = {7, 8, 9};
        array.m_count                            = 3;
        array.Set(values);
        Check(array.Data() == values && array[2] == 9, "M2Array::Set and Data agree on this client's form");
    }

    std::printf("%d type(s), %d check(s), %d failure(s)\n", types, g_checks, g_failures);
    return g_failures ? 1 : 0;
}
