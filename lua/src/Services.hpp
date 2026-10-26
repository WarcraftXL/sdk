// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The services in Lua: one module per service table, wxl.storage, wxl.data, wxl.script,
// wxl.hook, wxl.patch, each function marshalled from its row of wxl/services/<service>.def by the
// kinds it takes and answers. Nothing here names a service member: a row added to a table
// reaches Lua without a change below. What takes a callback is a shim (Shims.hpp).

#ifndef WXL_LUA_SERVICES_HPP
#define WXL_LUA_SERVICES_HPP

#include "Lua.hpp"
#include "wxl/abi.h"

namespace wxl::lua
{
    class ScriptMod;

    /**
     * @brief Sets wxl.<service> on the table at the top of the stack, for one mod.
     *
     * Each module holds a function per row a script reaches. A service the runtime does not
     * publish still has its module: every call answers nil, "unsupported". A service that came
     * with a later contract than the "abi" the mod's manifest asks for (WXL_API_SERVICE's
     * `since`) is a module whose every use raises an error naming both and the abi to ask for;
     * a member that came later than its service (WXL_API_SINCE) raises the same error when the
     * mod reaches for it. A raw service the mod's manifest does not declare (in "requires"
     * or "optional") is a module whose every use raises an error saying so.
     */
    void PushServiceModules(lua_State* L, ScriptMod& mod);

    /// Gives back what the state holds until it unloads -- patches, newest first -- and closes
    /// what it still has open. Called before the state closes.
    void CloseServiceResources(lua_State* L);

    /// What a Lua caller is told a result was: "unsupported", "invalid", "not_found", ...
    const char* ReasonOf(wxl_result result);

    /// nil and ReasonOf(result): what a call that failed answers. Returns 2.
    int PushResultFailure(lua_State* L, wxl_result result);

    /// wxl.render's resources, which its shims hand out and read as the generated rows do.
    enum class RenderResource
    {
        image,
        buffer,
        pipeline,
    };

    /// Pushes a wxl.<Image|Buffer|Pipeline> box holding @p value, given back when collected.
    void PushRenderResource(lua_State* L, RenderResource kind, wxl_handle value);

    /// The handle of the box of that kind at @p index; WXL_HANDLE_NONE for anything else, or one
    /// given back already.
    wxl_handle ToRenderResource(lua_State* L, int index, RenderResource kind);
} // namespace wxl::lua

#endif // WXL_LUA_SERVICES_HPP
