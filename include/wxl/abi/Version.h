// SPDX-License-Identifier: GPL-3.0-or-later
// The SDK version: a core M.n loads an extension built for M.m when m <= n (minor = additions only).

#ifndef WXL_VERSION_H
#define WXL_VERSION_H

#define WXL_SDK_VERSION_MAJOR 1
#define WXL_SDK_VERSION_MINOR 3

/// Major and minor packed as 0xMMMMmmmm, as WXL_PluginInfo::sdkVersion carries them.
#define WXL_SDK_VERSION_MAKE(major, minor) ((((unsigned)(major)) << 16) | ((unsigned)(minor) & 0xFFFFu))
#define WXL_SDK_VERSION WXL_SDK_VERSION_MAKE(WXL_SDK_VERSION_MAJOR, WXL_SDK_VERSION_MINOR)

#endif // WXL_VERSION_H
