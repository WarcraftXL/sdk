// SPDX-License-Identifier: GPL-3.0-or-later
// The side that publishes example.tally.

#include "Tally.hpp"

using namespace wxl;

namespace
{
    int32_t g_total = 0;

    void __cdecl Add(int32_t amount) { g_total += amount; }
    int32_t __cdecl Total() { return g_total; }

    const TallyApi g_tally = { sizeof(TallyApi), TALLY_API_VERSION, &Add, &Total };
}

bool PublishTally()
{
    return Publish<Tally>(&g_tally);
}
