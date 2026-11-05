-- SPDX-License-Identifier: GPL-3.0-or-later
-- The declarations of structures: field offsets under <Struct>::fields, a layout:: view when typed.

local names = require("gen.names")
local header_files = require("gen.header_files")

local M = {}

-- Atlas field types a layout view can spell, and their C++ types.
local PRIM = { u8 = "uint8_t", i8 = "int8_t", u16 = "uint16_t", i16 = "int16_t", u32 = "uint32_t", i32 = "int32_t",
  u64 = "uint64_t", i64 = "int64_t", f32 = "float", f64 = "double", ptr = "void*", vfptr = "const void*" }

-- The structure's size and every field's offset; a repeated field name takes its offset as a suffix.
local function emit_fields(file, si)
  local s = si.s
  local path = {}
  for i, p in ipairs(si.path) do path[i] = p end
  path[#path + 1] = "fields"
  local scope = header_files.scope_of(file, path)
  scope.items[#scope.items + 1] = { at = -1, order = 0, text = string.format("inline constexpr size_t kSize = 0x%X;", s.size) }
  local seen = { kSize = true }
  for _, f in ipairs(s.fields or {}) do
    local id = names.ident(f.name)
    if seen[id] then id = id .. string.format("_%X", f.off) end
    seen[id] = true
    local note = f.type and (" // " .. f.type) or ""
    scope.items[#scope.items + 1] = { at = f.off, order = 1, text = string.format("inline constexpr size_t %s = 0x%X;%s", id, f.off, note) }
  end
end

-- The members of a layout view: typed, non-overlapping fields, padding between them; nil when no
-- field is typed.
local function layout_members(s)
  local members, at, any = {}, 0, false
  for _, f in ipairs(s.fields or {}) do
    local base, count = (f.type or ""):match("^(.-)%[(%d+)%]$")
    base = base or f.type
    local c = base and PRIM[base]
    if c and f.off >= at and not f.name:find("[%.%[]") and f.size then
      if f.off > at then members[#members + 1] = { text = string.format("uint8_t _pad%X[0x%X];", at, f.off - at) } end
      members[#members + 1] = { text = string.format("%s %s%s;", c, names.ident(f.name), count and ("[" .. count .. "]") or ""), field = f }
      at = f.off + f.size
      any = true
    end
  end
  if not any then return nil end
  if s.size > at then members[#members + 1] = { text = string.format("uint8_t _pad%X[0x%X];", at, s.size - at) } end
  return members
end

-- The layout view as a struct in namespace layout, held to the atlas by static_asserts.
local function emit_layout(file, si)
  local s = si.s
  local members = layout_members(s)
  if not members then return end
  local lname = table.concat(si.path, "_")
  local body = { string.format("struct %s", lname), "{" }
  for _, m in ipairs(members) do body[#body + 1] = "    " .. m.text end
  body[#body + 1] = "};"
  body[#body + 1] = string.format("static_assert(sizeof(%s) == 0x%X);", lname, s.size)
  for _, m in ipairs(members) do
    if m.field then body[#body + 1] = string.format("static_assert(offsetof(%s, %s) == 0x%X);", lname, names.ident(m.field.name), m.field.off) end
  end
  local scope = header_files.scope_of(file, { "layout" })
  scope.items[#scope.items + 1] = { at = -2, order = 0, block = body }
end

-- Adds every structure to its header.
function M.emit(set, structs)
  for _, si in ipairs(structs) do
    local file = header_files.file_of(set, si.file)
    emit_fields(file, si)
    emit_layout(file, si)
  end
end

return M
