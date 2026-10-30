// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The console variable service: settings a mod gives the client, kept where the game keeps its
// own. A variable a mod registers is one of the client's CVars: the console reads and sets it
// (`/console myMod_scale 2`, `set myMod_scale 2`, `cvarlist`), the game's interface reads and sets
// it with GetCVar, GetCVarBool, GetCVarDefault, GetCVarInfo and SetCVar, and the client saves it
// with its own. The same calls read and set the client's own variables.
//
//   register_cvar  a variable of the calling mod: its default, how it is saved, its help
//   release        the calling mod gives one up: it stays, with its value, until the client ends
//   set_range      the numbers a variable of the calling mod accepts
//   exists         whether a variable is registered
//   get            its value as text; get_number, get_bool read it as a number or a switch
//   get_default    the value it was registered with
//   set            gives it a value; set_number, set_bool write a number or a switch
//   reset          gives it its default back
//   flags          what it is: the WXL_CVAR_* bits below
//
// Names. Any name of 1 to WXL_CVAR_MAX_NAME characters without a space, a quote, a comma, a
// semicolon, a tab or a line break, compared without case, as the client compares them. The
// client's variables and the interface's share the one namespace: start a mod's names with a
// prefix of its own (`myMod_scale`), which also groups them in Config.wtf and `cvarlist`.
//
// Saving. Without a save flag a variable lives for the session. WXL_CVAR_SAVED writes it to
// WTF/Config.wtf, the file of the installation every account shares; WXL_CVAR_ACCOUNT to the
// account's own settings (WTF/Account/<ACCOUNT>/config-cache.wtf), and WXL_CVAR_CHARACTER to the
// character's (WTF/Account/<ACCOUNT>/<realm>/<character>/config-cache.wtf), which the client also
// keeps on the server when the server stores account data. A value equal to the default is not
// written, as the client does for its own. Config.wtf is written when the client saves its
// settings (leaving the world, quitting); an account's and a character's when the player leaves
// the world.
//
// When the saved value arrives. The client reads Config.wtf once, early in its start, before
// any file of the game is opened, and keeps the lines of names nobody has registered: a
// registration at any time later finds its saved value there and keeps it. A registration made
// before (a mod's load, WXL_STAGE_EARLY) waits and is made just before the client reads the
// file, so it gets its saved value too and the console's help for it; in between, get answers
// the default and set answers WXL_E_PENDING. An account's variables are read when the player
// logs into the account and enters the world, a character's when the character enters the world:
// until then they hold their default, and a value set meanwhile is replaced by the saved one.
// The client empties those two kinds when the account logs in and when the character leaves the
// world; the runtime registers the mods' ones again at once, so they never disappear.
//
// Changes. CVarChanged (WorldScript) is raised for every change of a value, whoever made it -- a
// mod, the console, the interface's SetCVar, a reset --, before the client writes it, with the
// name, the value replaced and the new one. It is the one way to follow a variable: none is
// raised for a set that leaves the value as it is, nor for a latched variable's set, which
// waits for the next start.
//
// Ownership. A name is held by the mod that registered it, until the mod releases it, is
// disabled or unloads: then the variable stays with its value and its flags, and a mod may
// register it again. Another mod's registration of a held name, or of the client's own
// variable, is refused (WXL_E_INVALID, with the reason in the log). Every mod and the interface
// may get and set any variable.
//
// Main thread only: a call off it answers WXL_E_INVALID. Every refusal is written to the log
// under the tag `cvar`, with what to do instead.

#ifndef WXL_SERVICE_CVAR_H
#define WXL_SERVICE_CVAR_H

#include "wxl/abi.h"

#define WXL_CVAR_SERVICE         "wxl.cvar"
#define WXL_CVAR_SERVICE_VERSION 1

/// The longest name a variable may have: the console reads names of up to 63 characters.
#define WXL_CVAR_MAX_NAME 63

/// The longest value a saved variable may hold with a name of `name_length` characters: the
/// client writes each one as a line `SET <name> "<value>"` of at most 259 characters.
#define WXL_CVAR_MAX_SAVED_VALUE(name_length) (251u - (name_length))

/// The longest value any variable may hold.
#define WXL_CVAR_MAX_VALUE 2047

// What a variable is, for register_cvar and as flags answers it. The client's own bits behind
// each are named, for reading a variable in a debugger.

/// Written to WTF/Config.wtf, for the whole installation (the client's archive bit, 0x1).
#define WXL_CVAR_SAVED 0x1u
/// Written to the account's settings, read when the account enters the world (category 0x10).
#define WXL_CVAR_ACCOUNT 0x2u
/// Written to the character's settings, read when the character enters the world (category 0x20).
#define WXL_CVAR_CHARACTER 0x4u
/// The console and the interface cannot set it ("is read only", 0x100); its mod can.
#define WXL_CVAR_READ_ONLY 0x8u
/// A set waits for the next start: the value is written to Config.wtf and read at the next
/// start, the variable keeps its current one until then (0x2). Needs WXL_CVAR_SAVED.
#define WXL_CVAR_LATCHED 0x10u
/// The interface's SetCVar changes it only from the game's own secure code, as for a protected
/// action (0x8); the console and mods are not held.
#define WXL_CVAR_PROTECTED 0x20u
/// The interface does not see it: GetCVar answers nil, SetCVar does not find it (0x40); the
/// console and mods do.
#define WXL_CVAR_HIDDEN 0x40u
/// Every bit register_cvar takes.
#define WXL_CVAR_ALL_FLAGS 0x7Fu

// Bits flags answers only.

/// The client's own code registered it.
#define WXL_CVAR_CLIENT 0x100u
/// The interface registered it (its RegisterCVar, or WxlRegisterCVar).
#define WXL_CVAR_INTERFACE 0x200u
/// A mod registered it (held or released).
#define WXL_CVAR_MOD 0x400u
/// A set of it waits for the next start: Config.wtf will have that value.
#define WXL_CVAR_PENDING 0x800u

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wxl_cvar_api
{
    uint32_t struct_size;

    /**
     * @brief Registers a variable for the calling mod.
     * @param name           its name (see the header's "Names")
     * @param default_value  the value it starts with, and gets back at reset
     * @param flags          WXL_CVAR_SAVED, _ACCOUNT, _CHARACTER, _READ_ONLY, _LATCHED,
     *                       _PROTECTED, _HIDDEN; 0 for a variable of the session
     * @param help           a line the console's `help <name>` shows; may be empty
     *
     * The variable keeps the value the client saved for it, if any; a new one starts at the
     * default. Registering a name the calling mod holds again updates its flags and the default
     * reset and get_default use, and keeps its value; the client keeps the first default (the
     * console's cvar_default, the interface's GetCVarDefault, what a save leaves out) and the
     * first help until it restarts. Before the client has read Config.wtf the registration waits
     * and is made then, as described above; it answers WXL_OK, and every call already treats
     * the name as registered. WXL_E_INVALID for a malformed name or value, a bit outside
     * WXL_CVAR_ALL_FLAGS, _ACCOUNT with _CHARACTER, _LATCHED without _SAVED or with _ACCOUNT or
     * _CHARACTER, _READ_ONLY with _ACCOUNT or _CHARACTER, a name another mod or the interface
     * holds, or a variable of the client's own.
     */
    wxl_result(WXL_CALL* register_cvar)(wxl_str name, wxl_str default_value, uint32_t flags, wxl_str help);

    /// Gives up a variable the calling mod holds. It stays, with its value, its flags and its
    /// range, until the client ends; any mod may register it again. WXL_E_NOT_FOUND when the
    /// calling mod does not hold it.
    wxl_result(WXL_CALL* release)(wxl_str name);

    /**
     * @brief The numbers a variable of the calling mod accepts, from min to max included.
     *
     * Any set outside, or of something that is not a number, is refused from everywhere: the
     * console and SetCVar leave the value as it was, set answers WXL_E_INVALID, and a saved
     * value outside is dropped for the default. A value outside at the time of the call goes
     * back to the default. min = max = 0 lifts the range. WXL_E_INVALID for a bound that is no
     * number, min above max, or a default outside; WXL_E_NOT_FOUND when the calling mod does
     * not hold the variable.
     */
    wxl_result(WXL_CALL* set_range)(wxl_str name, double min, double max);

    /// Whether a variable of this name is registered, by the client, the interface or a mod.
    wxl_bool(WXL_CALL* exists)(wxl_str name);

    /// Its value as text, valid until the variable changes: copy it to keep it. WXL_E_NOT_FOUND
    /// for a name nobody registered.
    wxl_result(WXL_CALL* get)(wxl_str name, wxl_str* out);

    /// Its value as a number. WXL_E_INVALID when the text is not one.
    wxl_result(WXL_CALL* get_number)(wxl_str name, double* out);

    /// Its value as a switch, read as the interface's GetCVarBool reads it: true for a text
    /// starting with 1-9, t, T, y, Y, or "on", "enabled"; false otherwise.
    wxl_result(WXL_CALL* get_bool)(wxl_str name, wxl_bool* out);

    /// The value it was registered with, valid until it is registered again.
    wxl_result(WXL_CALL* get_default)(wxl_str name, wxl_str* out);

    /**
     * @brief Gives a variable a value, as the console would, and raises CVarChanged.
     *
     * A latched variable keeps its value until the next start (flags answers WXL_CVAR_PENDING).
     * WXL_E_INVALID for a value with a quote or a line break, one too long (WXL_CVAR_MAX_VALUE,
     * or WXL_CVAR_MAX_SAVED_VALUE for a saved variable), outside its range, or a variable read
     * only to the calling mod (the client's, or another mod's WXL_CVAR_READ_ONLY one);
     * WXL_E_PENDING before the client has read Config.wtf; WXL_E_NOT_FOUND for a name nobody
     * registered.
     */
    wxl_result(WXL_CALL* set)(wxl_str name, wxl_str value);

    /// Sets a number, written as the shortest text that reads back as the same double.
    wxl_result(WXL_CALL* set_number)(wxl_str name, double value);

    /// Sets a switch, written "1" or "0".
    wxl_result(WXL_CALL* set_bool)(wxl_str name, wxl_bool value);

    /// Gives a variable its default back, as set would.
    wxl_result(WXL_CALL* reset)(wxl_str name);

    /// What a variable is: WXL_CVAR_* bits, those register_cvar takes and those only this
    /// answers. WXL_E_NOT_FOUND for a name nobody registered.
    wxl_result(WXL_CALL* flags)(wxl_str name, uint32_t* out);
} wxl_cvar_api;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // WXL_SERVICE_CVAR_H
