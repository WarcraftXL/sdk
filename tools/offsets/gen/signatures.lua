-- SPDX-License-Identifier: GPL-3.0-or-later
-- Atlas signatures as C++ function pointer types, spelled with <cstdint> alone.

local names = require("gen.names")

local M = {}

-- The types a signature may name as they are; anything else is a pointer to void or not spelled.
local SCALAR = {
  ["void"] = true, ["bool"] = true, ["char"] = true, ["float"] = true, ["double"] = true, ["int"] = true,
  ["unsigned"] = true, ["unsigned int"] = true, ["short"] = true, ["long"] = true, ["unsigned char"] = true,
  ["unsigned short"] = true, ["unsigned long"] = true, ["size_t"] = true, ["uintptr_t"] = true,
  ["int8_t"] = true, ["int16_t"] = true, ["int32_t"] = true, ["int64_t"] = true, ["uint8_t"] = true,
  ["uint16_t"] = true, ["uint32_t"] = true, ["uint64_t"] = true, ["long long"] = true,
  ["unsigned long long"] = true, ["signed char"] = true, ["intptr_t"] = true, ["ptrdiff_t"] = true,
}

-- A parameter or return type as C++ can spell it with <cstdint> alone, or nil.
local function ctype(t)
  t = t:gsub("^%s+", ""):gsub("%s+$", ""):gsub("%s+", " ")
  if t == "..." then return "..." end
  local base, ptr = t:match("^(.-)%s*([%*&]+)$")
  if base then
    local const = base:match("^const ") and "const " or ""
    local core = base:gsub("^const ", "")
    local stars = ptr:gsub("&", "*")
    if SCALAR[core] then return const .. core .. stars end
    return const .. "void" .. stars
  end
  local core = t:gsub("^const ", "")
  if SCALAR[core] then return core end
  -- A function pointer typedef of the client's (LuaCFunction, TexFillFn): a pointer.
  if core:match("Fn$") or core:match("Function$") or core:match("Proc$") or core:match("Callback$") then return "void*" end
  return nil
end

-- A parameter declaration's type and name; the name is nil when the declaration has none.
local function split_param(decl)
  if decl == "..." then return "...", nil end
  local t, n = decl:match("^(.-[%w_%*&>%s])%s*([%a_][%w_]*)$")
  if not t or t:match("^%s*$") or t:match("^%s*const%s*$") or SCALAR[decl] then return decl, nil end
  if n and SCALAR[n] or n == "const" then return decl, nil end
  return t, n
end

-- One parameter as written in the pointer type: "int count", "void*", "...".
local function param_text(c, n)
  if c == "..." then return c end
  if not n then return c end
  return c .. " " .. (names.keywords[n] and (n .. "_") or n)
end

-- The function pointer type of an atlas function, or nil and why not. A thiscall is spelled as a
-- fastcall with an unused EDX parameter after `this`, so a hook can take its arguments.
function M.fn_type(f)
  if not f.conv then return nil end
  if f.conv == "regparm" then return nil, "regparm (" .. table.concat(f.regs or {}, ", ") .. ")" end
  local ret = ctype(f.ret or "void")
  if not ret then return nil, "returns " .. f.ret .. " by value" end
  local list = {}
  for i, decl in ipairs(f.params or {}) do
    local t, n = split_param(decl)
    local c = ctype(t)
    if not c then return nil, "takes " .. t .. " by value" end
    list[#list + 1] = param_text(c, n)
    if i == 1 and f.conv == "thiscall" then list[#list + 1] = "void* edx" end
  end
  local conv = f.conv
  if conv == "thiscall" then conv = "fastcall" end
  return string.format("%s(__%s*)(%s)", ret, conv, table.concat(list, ", "))
end

return M
