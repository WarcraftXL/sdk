// SPDX-License-Identifier: GPL-3.0-or-later
// The stock image kit: Wow.exe laid at its base, one of its functions run, stubbed and given back.

#include "StockImage.hpp"

#include "wxl/offsets/free/S.hpp"

#include <cstdio>
#include <cstring>

namespace
{
    using StrLenFn = uint32_t(__stdcall*)(const char*);

    uint32_t __stdcall CountedLength(const char*) { return 1234; }
}

int main(int argc, char** argv)
{
    namespace stock = wxl::stock;
    const char* exe = stock::ExePath(argc, argv);
    if (!exe)
    {
        std::printf("no Wow.exe (argument or WXL_WOW_EXE): skipped\n");
        return stock::kSkipped;
    }
    std::string why;
    const bool mapped = stock::Map(exe, why);
    stock::Check(mapped, ("Wow.exe laid at 0x00400000" + (mapped ? std::string() : ": " + why)).c_str());
    if (!mapped)
        return stock::Result();

    const auto length = reinterpret_cast<StrLenFn>(wxl::offsets::SStrLen);
    stock::Check(length("terrain") == 7, "Wow.exe's own SStrLen runs here");
    stock::Stub(wxl::offsets::SStrLen, reinterpret_cast<const void*>(&CountedLength));
    stock::Check(length("terrain") == 1234, "a stub takes its calls");
    stock::Unstub(wxl::offsets::SStrLen);
    stock::Check(length("height") == 6, "unstubbed, Wow.exe's own again");
    return stock::Result();
}
