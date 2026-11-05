// SPDX-License-Identifier: GPL-3.0-or-later
// The stock Wow.exe laid at its own base inside a test process, to run its functions beside generated ones.

#pragma once

#include <cstdint>
#include <string>

namespace wxl::stock
{
    /// Wow.exe's preferred base: every address of the SDK's offsets is valid once Map() succeeded.
    inline constexpr uintptr_t kBase = 0x00400000;

    /// The Wow.exe a test runs against: its first argument, else $WXL_WOW_EXE, else null (skip the test).
    const char* ExePath(int argc, char** argv);

    /// Copies Wow.exe's headers and sections to their addresses (nothing of it runs) and points its import
    /// table at this process's own DLLs. False, with the reason, when the room or the file does not fit.
    bool Map(const char* path, std::string& why);

    /// Sends every call of the function at `address` to `to` (a jump written over its first bytes).
    void Stub(uintptr_t address, const void* to);

    /// Puts the function at `address` back as Wow.exe has it.
    void Unstub(uintptr_t address);

    /// Counts checks and failures for a test's report; Result() is the process exit code.
    void Check(bool ok, const char* what);
    int Result();

    /// The exit code that tells ctest a test was skipped (no Wow.exe).
    inline constexpr int kSkipped = 77;
}
