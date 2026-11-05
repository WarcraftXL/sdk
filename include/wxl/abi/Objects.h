// SPDX-License-Identifier: GPL-3.0-or-later
// The wxl.objects service: the client's resident objects as generational handles (C++: wxl/objects/).

#ifndef WXL_OBJECTS_API_H
#define WXL_OBJECTS_API_H

#include <stdint.h>

#include "wxl/abi/Plugin.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WXL_OBJECTS_API_NAME    "wxl.objects"
#define WXL_OBJECTS_API_VERSION 1

/// Main thread only. A handle answers WXL_E_EXPIRED for good once its object leaves; the same GUID
/// coming back gets a new handle.
typedef struct WXL_ObjectsApi
{
    uint32_t structSize;
    uint32_t apiVersion;

    /// The handle of the resident object with that GUID.
    int(__cdecl* FromGuid)(uint64_t guid, WXL_Handle* out);

    /// The handle of a client object pointer (a CGObject_C).
    int(__cdecl* FromPointer)(void* object, WXL_Handle* out);

    /// The client object behind a handle if its type meets `typeMask` (0xFFFFFFFF: any); valid until
    /// control returns to the client.
    int(__cdecl* Resolve)(WXL_Handle handle, uint32_t typeMask, void** out);

    int(__cdecl* Guid)(WXL_Handle handle, uint64_t* out);
} WXL_ObjectsApi;

#ifdef __cplusplus
}
#endif

#endif
