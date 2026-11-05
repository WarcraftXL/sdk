-- SPDX-License-Identifier: GPL-3.0-or-later
-- The declarations of functions, globals, vtables and sites: one constexpr address each.

local names = require("gen.names")
local signatures = require("gen.signatures")
local header_files = require("gen.header_files")

local hex = header_files.hex

local M = {}

-- A function's address, and its <Name>Fn pointer type when the signature can be spelled.
local function emit_function(lines, it)
  local f = it.e
  local t, why = signatures.fn_type(f)
  local line = string.format("inline constexpr uintptr_t %s = %s;", it.id, hex(f.at))
  if it.aka then line = line .. " // folded with " .. f.name
  elseif why then line = line .. " // " .. why end
  lines[#lines + 1] = { at = f.at, order = 1, text = line }
  if t then lines[#lines + 1] = { at = f.at, order = 2, text = string.format("using %sFn = %s;", it.id, t) } end
end

local function emit_global(lines, it)
  local size = it.e.size and string.format(" // 0x%X bytes", it.e.size) or ""
  lines[#lines + 1] = { at = it.e.at, order = 1,
    text = string.format("inline constexpr uintptr_t %s = %s;%s", it.id, hex(it.e.at), size) }
end

-- A vtable's address, and its slot indices in a <Name>_slot namespace beside it.
local function emit_vtable(file, lines, it)
  lines[#lines + 1] = { at = it.e.at, order = 1, text = string.format("inline constexpr uintptr_t %s = %s;", it.id, hex(it.e.at)) }
  local slots = {}
  for _, s in ipairs(it.e.slots) do slots[#slots + 1] = s end
  table.sort(slots, function(a, b) return a[1] < b[1] end)
  local slot_path = {}
  for i, p in ipairs(it.path) do slot_path[i] = p end
  slot_path[#slot_path + 1] = it.id .. "_slot"
  local slot_scope = header_files.scope_of(file, slot_path)
  for _, s in ipairs(slots) do
    slot_scope.items[#slot_scope.items + 1] = { at = s[1], order = 1,
      text = string.format("inline constexpr unsigned %s = %d;", names.ident(s[2]), s[1]) }
  end
end

-- A site: an address inside a function, with the function and the bytes expected there.
local function emit_site(lines, it, fn_by_at)
  local owner = fn_by_at[it.e.fn] and fn_by_at[it.e.fn].name or hex(it.e.fn)
  lines[#lines + 1] = { at = it.e.at, order = 1, text = string.format("inline constexpr uintptr_t %s = %s; // in %s: %s",
    it.id, hex(it.e.at), owner, it.e.bytes) }
end

-- Adds every entry not skipped to its header's namespace.
function M.emit(set, entries, fn_by_at)
  for _, it in ipairs(entries) do
    if not it.skip then
      local file = header_files.file_of(set, it.file)
      local lines = header_files.scope_of(file, it.path).items
      if it.kind == "function" then emit_function(lines, it)
      elseif it.kind == "global" then emit_global(lines, it)
      elseif it.kind == "vtable" then emit_vtable(file, lines, it)
      elseif it.kind == "site" then emit_site(lines, it, fn_by_at) end
    end
  end
end

return M
