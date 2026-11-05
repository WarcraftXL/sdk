-- SPDX-License-Identifier: GPL-3.0-or-later
-- The WXL_SETTINGS structs of a parsed header, each field checked against the rules of a setting.

local clang = require("clang")
local annotation = require("annotation")

local K, T = clang.kind, clang.type

local M = {}

-- The SettingInfo kinds, and the options WXL_SETTING takes.
local BOOL, INTEGER, NUMBER, TEXT = "WXL_SETTING_KIND_BOOL", "WXL_SETTING_KIND_INTEGER", "WXL_SETTING_KIND_NUMBER",
  "WXL_SETTING_KIND_TEXT"
local UIS = { slider = true, checkbox = true, input = true }
local OPTIONS = { range = true, step = true, ui = true }

-- The setting kind of a field's type, or nil for a type that is no setting.
local function kind_of_type(t)
  local lib = clang.lib
  local canon = lib.clang_getCanonicalType(t)
  local k = canon.kind
  if k == T.Bool then return BOOL end
  if clang.signed[k] or clang.unsigned[k] then return INTEGER end
  if k == T.Float or k == T.Double then return NUMBER end
  if k == T.Record and clang.qualified_name(lib.clang_getTypeDeclaration(canon)):match("basic_string$") then
    return TEXT
  end
  return nil
end

-- Every struct or class at namespace scope, in source order.
local function walk(cursor, found)
  for _, c in ipairs(clang.children(cursor)) do
    local k = clang.kind_of(c)
    if k == K.Namespace then walk(c, found)
    elseif k == K.StructDecl or k == K.ClassDecl then found[#found + 1] = c end
  end
end

-- Applies range, step and ui to a field; `err` reports a bad option.
local function apply_options(field, opts, label, err)
  local numeric = field.kind == INTEGER or field.kind == NUMBER
  if opts.range ~= nil then
    local r = opts.range
    if type(r) ~= "table" or #r ~= 2 or type(r[1]) ~= "number" or type(r[2]) ~= "number" or r[2] < r[1] then
      err(label .. ": range takes two numbers, the lowest then the highest: range(0, 100)")
    elseif not numeric then
      err(label .. ": only a number has a range")
    else
      field.min, field.max = r[1], r[2]
    end
  end
  if opts.step ~= nil then
    if type(opts.step) ~= "number" or opts.step < 0 then err(label .. ": step is a number: step = 5")
    else field.step = opts.step end
  end
  if opts.ui ~= nil then
    if not UIS[opts.ui] then err(label .. ": ui is slider, checkbox or input")
    else field.ui = opts.ui end
  end
  for key in pairs(opts) do
    if not OPTIONS[key] then
      err(("%s: WXL_SETTING does not know '%s'; it takes range(min, max), step = n and ui = slider|checkbox|input"):format(label, key))
    end
  end
  -- Without ui: a checkbox for a bool, a slider for a number with a range, an input otherwise.
  if field.ui == "" then
    field.ui = field.kind == BOOL and "checkbox" or ((numeric and field.max >= field.min) and "slider" or "input")
  end
end

-- One field of a settings struct, or nil after reporting why it is no setting. `keys` holds the
-- names seen so far in the struct.
local function read_field(f, owner, keys, err_at)
  local name = clang.spelling(f)
  local label = owner .. "::" .. name
  local what, _, opts, perr = annotation.of(f)
  if perr then err_at(f, perr) return nil end
  if what ~= "setting" then
    err_at(f, label .. " has no WXL_SETTING(): annotate it, or keep it out of the settings struct")
    return nil
  end
  local kind = kind_of_type(clang.lib.clang_getCursorType(f))
  if not kind then
    err_at(f, label .. ": a setting is a bool, an integer, a float, a double or a std::string")
    return nil
  end
  local field = { name = name, kind = kind, doc = clang.doc(f), min = 0, max = -1, step = 0, ui = "" }
  if field.doc == "" then err_at(f, label .. " needs a /// comment: it is the ini's comment and the panel's help") end
  if keys[name] then err_at(f, label .. ": a key is unique in its extension") end
  keys[name] = true
  apply_options(field, opts, label, function(msg) err_at(f, msg) end)
  return field
end

-- A settings struct: { cpp = qualified name, section, fields }.
local function read_struct(c, positional, aerr, err_at)
  local s = { cpp = clang.qualified_name(c), section = positional[1], fields = {} }
  if aerr then err_at(c, aerr) end
  if not s.section or not s.section:match("^[%a_][%w_]*$") then
    err_at(c, s.cpp .. ": WXL_SETTINGS takes its ini section's name, a C identifier: WXL_SETTINGS(\"Damage\")")
  end
  local keys = {}
  for _, f in ipairs(clang.children(c)) do
    if clang.kind_of(f) == K.FieldDecl then
      local field = read_field(f, s.cpp, keys, err_at)
      if field then s.fields[#s.fields + 1] = field end
    end
  end
  if #s.fields == 0 then err_at(c, s.cpp .. " has no setting") end
  return s
end

-- True for a struct's definition in the header itself (not a declaration, not an included file).
local function defined_here(c, header)
  local lib = clang.lib
  local def = lib.clang_getCursorDefinition(c)
  return clang.location(c) == header and lib.clang_Cursor_isNull(def) == 0 and lib.clang_equalCursors(def, c) ~= 0
end

-- The settings structs of a translation unit, and the errors as "file:line: message" lines.
function M.read(tu, header)
  local errors, structs = {}, {}
  local function err_at(c, msg)
    local file, line = clang.location(c)
    errors[#errors + 1] = ("%s:%d: %s"):format(file or header, line or 0, msg)
  end
  local found = {}
  walk(tu.cursor, found)
  for _, c in ipairs(found) do
    local what, positional, _, aerr = annotation.of(c)
    if what == "settings" and defined_here(c, header) then
      structs[#structs + 1] = read_struct(c, positional, aerr, err_at)
    end
  end
  if #structs == 0 and #errors == 0 then errors[1] = header .. ": no WXL_SETTINGS struct" end
  return structs, errors
end

return M
