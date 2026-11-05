// SPDX-License-Identifier: GPL-3.0-or-later
// Lays Wow.exe at 0x00400000 in this executable's own reserved room (linked low by wxl_add_stock_test).

#include "StockImage.hpp"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace wxl::stock
{
    namespace
    {
        /// Where Wow.exe goes: zeroes of this image's .bss, which the linker places below and past its base.
        uint8_t g_room[0xF00000];
        std::vector<uint8_t> g_file;
        int g_checks = 0;
        int g_failures = 0;

        constexpr uint8_t kJump = 0xE9;
        constexpr size_t kJumpSize = 5;

        uint32_t Read16(size_t at) { return static_cast<uint32_t>(g_file[at] | g_file[at + 1] << 8); }
        uint32_t Read32(size_t at) { return Read16(at) | Read16(at + 2) << 16; }

        bool ReadFile(const char* path, std::string& why)
        {
            FILE* file = std::fopen(path, "rb");
            if (!file)
                return why = std::string(path) + " cannot be read", false;
            uint8_t chunk[65536];
            for (size_t n; (n = std::fread(chunk, 1, sizeof chunk, file)) > 0;)
                g_file.insert(g_file.end(), chunk, chunk + n);
            std::fclose(file);
            return true;
        }

        /// What an import no DLL here provides (the client's own codec DLLs) answers: the test stops.
        void UnboundImport()
        {
            std::printf("FAIL stock code called an import this process could not bind\n");
            std::exit(1);
        }

        /// Each import of Wow.exe resolved in this process, so stock code reaching the system works.
        void BindImports(uintptr_t rva)
        {
            for (auto* entry = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(kBase + rva); entry->Name; ++entry)
            {
                const char* dll = reinterpret_cast<const char*>(kBase + entry->Name);
                HMODULE module = LoadLibraryA(dll);
                auto* names = reinterpret_cast<IMAGE_THUNK_DATA32*>(kBase + (entry->OriginalFirstThunk
                                                                                ? entry->OriginalFirstThunk
                                                                                : entry->FirstThunk));
                auto* slots = reinterpret_cast<IMAGE_THUNK_DATA32*>(kBase + entry->FirstThunk);
                for (; names->u1.AddressOfData; ++names, ++slots)
                {
                    const char* name = IMAGE_SNAP_BY_ORDINAL32(names->u1.Ordinal)
                                           ? reinterpret_cast<const char*>(IMAGE_ORDINAL32(names->u1.Ordinal))
                                           : reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(kBase + names->u1.AddressOfData)->Name;
                    FARPROC proc = module ? GetProcAddress(module, name) : nullptr;
                    const void* to = proc ? reinterpret_cast<const void*>(proc) : reinterpret_cast<const void*>(&UnboundImport);
                    slots->u1.Function = static_cast<DWORD>(reinterpret_cast<uintptr_t>(to));
                }
            }
        }
    }

    const char* ExePath(int argc, char** argv)
    {
        if (argc > 1 && argv[1][0])
            return argv[1];
        const char* env = std::getenv("WXL_WOW_EXE");
        return env && env[0] ? env : nullptr;
    }

    bool Map(const char* path, std::string& why)
    {
        if (!ReadFile(path, why))
            return false;
        if (g_file.size() < 0x400 || Read16(0) != 0x5A4D)
            return why = "not a PE file", false;
        const size_t pe = Read32(0x3C);
        const size_t optional = pe + 24;
        const uint32_t base = Read32(optional + 28);
        const uint32_t size = Read32(optional + 56);
        const uint32_t headers = Read32(optional + 60);
        const uintptr_t room = reinterpret_cast<uintptr_t>(g_room);
        if (base != kBase)
            return why = "its base is not 0x00400000", false;
        if (room > base || room + sizeof g_room < base + size)
            return why = "this test's room does not cover Wow.exe: link it with wxl_add_stock_test", false;
        DWORD was = 0;
        if (!VirtualProtect(reinterpret_cast<void*>(base), size, PAGE_EXECUTE_READWRITE, &was))
            return why = "its room cannot be made executable", false;
        std::memcpy(reinterpret_cast<void*>(base), g_file.data(), headers);
        const size_t table = optional + Read16(pe + 20);
        for (uint32_t i = 0; i < Read16(pe + 6); ++i)
        {
            const size_t s = table + 40 * i;
            const uint32_t rva = Read32(s + 12), rawSize = Read32(s + 16), raw = Read32(s + 20);
            if (raw + rawSize > g_file.size())
                return why = "a section runs past the file", false;
            std::memcpy(reinterpret_cast<void*>(base + rva), g_file.data() + raw, rawSize);
        }
        const uint32_t imports = Read32(optional + 96 + 8);
        if (imports)
            BindImports(imports);
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(base), size);
        return true;
    }

    void Stub(uintptr_t address, const void* to)
    {
        auto* at = reinterpret_cast<uint8_t*>(address);
        const auto rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(to) - (address + kJumpSize));
        at[0] = kJump;
        std::memcpy(at + 1, &rel, sizeof rel);
        FlushInstructionCache(GetCurrentProcess(), at, kJumpSize);
    }

    void Unstub(uintptr_t address)
    {
        const size_t pe = Read32(0x3C);
        const size_t table = pe + 24 + Read16(pe + 20);
        for (uint32_t i = 0; i < Read16(pe + 6); ++i)
        {
            const size_t s = table + 40 * i;
            const uint32_t rva = Read32(s + 12), raw = Read32(s + 20), rawSize = Read32(s + 16);
            if (address >= kBase + rva && address + kJumpSize <= kBase + rva + rawSize)
            {
                std::memcpy(reinterpret_cast<void*>(address), g_file.data() + raw + (address - kBase - rva), kJumpSize);
                FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(address), kJumpSize);
                return;
            }
        }
    }

    void Check(bool ok, const char* what)
    {
        ++g_checks;
        g_failures += ok ? 0 : 1;
        std::printf("%-4s %s\n", ok ? "ok" : "FAIL", what);
    }

    int Result()
    {
        std::printf("%d checks, %d failures\n", g_checks, g_failures);
        return g_failures ? 1 : 0;
    }
}
