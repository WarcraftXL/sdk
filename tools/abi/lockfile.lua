-- SPDX-License-Identifier: GPL-3.0-or-later
-- The lock file: a line per #define, enumerator and event row, a block per struct; sorted, so a diff reads well.

local M = {}

local function sorted_keys(t)
  local keys = {}
  for k in pairs(t) do keys[#keys + 1] = k end
  table.sort(keys)
  return keys
end

function M.format(model, regenerate)
  local out = { "# The C ABI these headers publish; checked by ctest. Do not edit: regenerate with " ..
                (regenerate or "abi_lock.lua --write") .. "." }
  for _, name in ipairs(sorted_keys(model.defines)) do
    out[#out + 1] = (name:find("::", 1, true) and "enum " or "define ") .. name .. " " .. model.defines[name]
  end
  local rows = {}
  for _, r in ipairs(model.events) do rows[#rows + 1] = r end
  table.sort(rows, function(a, b) return a.id < b.id end)
  for _, r in ipairs(rows) do out[#out + 1] = string.format("event %d %s %s", r.id, r.name, r.args) end
  for _, name in ipairs(sorted_keys(model.structs)) do
    local s = model.structs[name]
    out[#out + 1] = string.format("struct %s size=%d align=%d", name, s.size, s.align)
    for _, field in ipairs(s.fields) do out[#out + 1] = "  " .. field end
  end
  return table.concat(out, "\n") .. "\n"
end

-- The model back from a lock's text.
function M.parse(text)
  local model = { defines = {}, events = {}, structs = {} }
  local current
  for line in text:gmatch("([^\n]*)\n") do
    local name, value = line:match("^define (%S+) (.*)$")
    if not name then name, value = line:match("^enum (%S+) (.*)$") end
    local id, ev, args = line:match("^event (%d+) (%S+) (%S+)$")
    local sname, size, align = line:match("^struct (%S+) size=(%d+) align=(%d+)$")
    if name then model.defines[name] = value
    elseif id then model.events[#model.events + 1] = { id = tonumber(id), name = ev, args = args }
    elseif sname then
      current = { size = tonumber(size), align = tonumber(align), fields = {} }
      model.structs[sname] = current
    elseif current and line:sub(1, 2) == "  " then current.fields[#current.fields + 1] = line:sub(3) end
  end
  return model
end

function M.read(path)
  local f = io.open(path, "rb")
  if not f then return nil end
  local text = f:read("*a"):gsub("\r\n", "\n")
  f:close()
  return text
end

function M.write(path, text)
  local f = assert(io.open(path, "wb"))
  f:write(text)
  f:close()
end

return M
