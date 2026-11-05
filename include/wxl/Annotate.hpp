// SPDX-License-Identifier: GPL-3.0-or-later
// The annotations the settings tool reads; includes nothing, so the tool parses a settings header on any host.

#pragma once

#if defined(__clang__)
#define WXL_ANNOTATE_(what, ...) [[clang::annotate("wxl." what ":" #__VA_ARGS__)]]
#else
#define WXL_ANNOTATE_(what, ...)
#endif

/// A settings struct: WXL_SETTINGS("Section"), the section of the ini.
#define WXL_SETTINGS(...) WXL_ANNOTATE_("settings", __VA_ARGS__)
/// A setting: WXL_SETTING(range(min, max), step = n, ui = slider|checkbox|input), each optional.
#define WXL_SETTING(...) WXL_ANNOTATE_("setting", __VA_ARGS__)
