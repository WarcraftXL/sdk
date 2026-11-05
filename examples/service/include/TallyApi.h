// SPDX-License-Identifier: GPL-3.0-or-later
// The example.tally service: a running total any extension can add to.

#ifndef TALLY_API_H
#define TALLY_API_H

#include <stdint.h>

#define TALLY_API_NAME    "example.tally"
#define TALLY_API_VERSION 1

typedef struct TallyApi
{
    uint32_t structSize; ///< members are only ever appended: check it before using a newer one
    uint32_t apiVersion;
    void(__cdecl* Add)(int32_t amount);
    int32_t(__cdecl* Total)(void);
} TallyApi;

#endif
