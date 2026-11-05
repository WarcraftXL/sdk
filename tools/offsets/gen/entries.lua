-- SPDX-License-Identifier: GPL-3.0-or-later
-- The atlas entries the headers declare, each with its C++ scope and one identifier no other entry takes.

local names = require("gen.names")

local M = {}

-- Every function, global, vtable and site of the model, with the names they are declared under.
local function collect(model, renames)
  local entries = {}
  local function add(kind, e, name, file, extra)
    local path, id = names.place(name)
    if renames[e.at] and kind == "function" and name == e.name then id = renames[e.at] end
    local item = { kind = kind, e = e, name = name, path = path, id = id, file = file }
    for k, v in pairs(extra or {}) do item[k] = v end
    entries[#entries + 1] = item
  end

  for _, f in ipairs(model.functions) do
    add("function", f, f.name, f._file)
    for _, a in ipairs(f.aka or {}) do add("function", f, a, f._file, { aka = true }) end
  end
  for _, g in ipairs(model.globals) do
    add("global", g, g.name, g._file)
    for _, a in ipairs(g.aka or {}) do add("global", g, a, g._file, { aka = true }) end
  end
  for _, v in ipairs(model.vtables) do add("vtable", v, v.name, v._file) end
  -- A site is declared in its function's header, as <owner>_<what>.
  local fn_by_at = M.functions_by_address(model)
  for _, s in ipairs(model.sites) do
    local fn = fn_by_at[s.fn]
    local owner, what = s.name:match("^(.-)@(.+)$")
    local file = fn and fn._file or s._file
    add("site", s, (owner or s.name) .. "_" .. (what or "site"), file)
  end
  return entries
end

-- Overloads and folded names: one identifier each. The first by address keeps the name; the others
-- take their address as a suffix unless aliases.lua names them. A second name of one entry is skipped.
local function resolve_overloads(entries, scopes_used, fail)
  table.sort(entries, function(a, b)
    if a.e.at ~= b.e.at then return (a.e.at or 0) < (b.e.at or 0) end
    return a.name < b.name
  end)
  local taken = {}
  for _, it in ipairs(entries) do
    local key = table.concat(it.path, "::") .. "::" .. it.id
    local other = taken[key]
    if other and other.e == it.e then
      it.skip = true
    elseif other then
      it.id = it.id .. string.format("_%06X", it.e.at % 0x1000000)
      key = table.concat(it.path, "::") .. "::" .. it.id
      if taken[key] then fail(2, "two atlas entries take the name %s", key) end
      taken[key] = it
    else
      taken[key] = it
    end
    if not it.skip then M.register_scope(scopes_used, it.path) end
  end
end

-- Marks a scope path and every scope above it as used.
function M.register_scope(scopes_used, path)
  for i = 1, #path do scopes_used[table.concat(path, "::", 1, i)] = true end
end

-- The first atlas function at each address.
function M.functions_by_address(model)
  local by_at = {}
  for _, f in ipairs(model.functions) do by_at[f.at] = by_at[f.at] or f end
  return by_at
end

-- The entries and structures to declare: { entries = {...}, structs = {...} }. An entry whose name is
-- also a scope gets a "_" suffix, so a name is never both.
function M.build(model, aliases, fail)
  local renames = {}
  for _, a in ipairs(aliases.renames or {}) do renames[a.at] = a.name end

  local scopes_used = {}
  local entries = collect(model, renames)
  resolve_overloads(entries, scopes_used, fail)

  -- A structure is a scope: its field offsets go under <Struct>::fields.
  local structs = {}
  for _, s in ipairs(model.structs) do
    local path, id = names.place(s.name)
    path[#path + 1] = id
    M.register_scope(scopes_used, path)
    structs[#structs + 1] = { s = s, path = path, id = id, file = s._file }
  end

  for _, it in ipairs(entries) do
    if not it.skip and scopes_used[names.qualified(it.path, it.id)] then it.id = it.id .. "_" end
  end
  return { entries = entries, structs = structs }
end

return M
