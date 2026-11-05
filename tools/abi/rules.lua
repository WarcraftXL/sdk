-- SPDX-License-Identifier: GPL-3.0-or-later
-- What a change of the ABI needs: an addition a minor (or major) version, a break a major one, a service
-- table WXL_<Name>Api its own WXL_<NAME>_API_VERSION.

local M = {}

local function moved(old, new, macros)
  for _, m in ipairs(macros) do
    if old.defines[m] ~= new.defines[m] then return true end
  end
  return false
end

-- True when `old` is `new` cut short: members appended at the end.
local function prefix(old, new)
  if #old > #new then return false end
  for i = 1, #old do if old[i] ~= new[i] then return false end end
  return true
end

-- WXL_GraphicsDeviceApi -> WXL_GRAPHICS_DEVICE_API_VERSION.
local function table_version(name)
  local stem = name:match("^WXL_(%w+)Api$")
  if not stem then return nil end
  return "WXL_" .. stem:gsub("(%l)(%u)", "%1_%2"):upper() .. "_API_VERSION"
end

local function is_version(name, opts)
  for _, list in ipairs({ opts.major, opts.minor }) do
    for _, m in ipairs(list) do if m == name then return true end end
  end
  return name:match("_API_VERSION$") ~= nil
end

local function diff(old, new, opts, report)
  for name, value in pairs(new.defines) do
    local was = old.defines[name]
    if not is_version(name, opts) then
      if was == nil then report("added", "define " .. name)
      elseif was ~= value then report("break", "define " .. name .. ": " .. was .. " -> " .. value) end
    end
  end
  for name in pairs(old.defines) do
    if new.defines[name] == nil and not is_version(name, opts) then report("break", "define " .. name .. " removed") end
  end
  local rows = {}
  for _, r in ipairs(new.events) do rows[r.id] = r end
  for _, r in ipairs(old.events) do
    local now = rows[r.id]
    if not now then report("break", "event " .. r.id .. " " .. r.name .. " removed")
    elseif now.name ~= r.name or now.args ~= r.args then report("break", "event " .. r.id .. " is now " .. now.name .. " " .. now.args) end
    rows[r.id] = nil
  end
  for id, r in pairs(rows) do report("added", "event " .. id .. " " .. r.name) end
  for name, s in pairs(new.structs) do
    local was = old.structs[name]
    if not was then report("added", "struct " .. name, name)
    elseif was.size ~= s.size or table.concat(was.fields, "\n") ~= table.concat(s.fields, "\n") then
      if prefix(was.fields, s.fields) and was.align == s.align then report("added", "struct " .. name .. ": members appended", name)
      else report("break", "struct " .. name .. ": layout changed", name) end
    end
  end
  for name in pairs(old.structs) do
    if not new.structs[name] then report("break", "struct " .. name .. " removed", name) end
  end
end

-- { lines = {...}, bumped = bool }: what changed, and whether the versions it needs moved.
function M.judge(old, new, opts)
  local verdict = { lines = {}, bumped = true }
  if not old then
    verdict.lines[1] = "abi_lock.lua: no lock yet"
    return verdict
  end
  local added, broken, tables = false, false, {}
  diff(old, new, opts, function(kind, what, struct)
    verdict.lines[#verdict.lines + 1] = (kind == "break" and "  break: " or "  added: ") .. what
    if kind == "break" then broken = true else added = true end
    local version = struct and table_version(struct)
    if version and (new.defines[version] or old.defines[version]) then tables[version] = true end
  end)
  local function need(ok, what)
    if not ok then
      verdict.bumped = false
      verdict.lines[#verdict.lines + 1] = "  needs: " .. what
    end
  end
  if broken and #opts.major > 0 then need(moved(old, new, opts.major), "a new " .. table.concat(opts.major, " or ")) end
  if added and not broken and #opts.minor + #opts.major > 0 then
    local any = {}
    for _, m in ipairs(opts.minor) do any[#any + 1] = m end
    for _, m in ipairs(opts.major) do any[#any + 1] = m end
    need(moved(old, new, any), "a new " .. table.concat(any, " or "))
  end
  for version in pairs(tables) do need(old.defines[version] ~= new.defines[version], "a new " .. version) end
  return verdict
end

return M
