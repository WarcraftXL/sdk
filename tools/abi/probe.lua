-- SPDX-License-Identifier: GPL-3.0-or-later
-- The ABI as the compiler lays it out: a probe source over the headers, compiled with clang's record dump.

local M = {}

local function slurp(path)
  local f = io.open(path, "rb")
  if not f then return nil end
  local text = f:read("*a")
  f:close()
  return (text:gsub("\r\n", "\n"))
end

local function quote(s)
  if package.config:sub(1, 1) == "\\" then return '"' .. s .. '"' end
  return "'" .. s:gsub("'", "'\\''") .. "'"
end

-- `#define NAME value` lines of a header whose value is a number or a string literal.
local function defines_of(text, out)
  for line in text:gmatch("[^\n]+") do
    local name, value = line:match("^#define%s+([%w_]+)%s+(.-)%s*$")
    local literal = name and (value:match("^%(?%-?%d+[uUlL]*%)?$") or value:match('^"[^"]*"$'))
    if literal then out[name] = (value:gsub("[()]", "")) end
  end
end

-- The enumerators of `enum class Name : type { A = 0, ... }` with explicit values, as Name::A.
local function enums_of(text, out)
  for enum, body in text:gmatch("enum%s+class%s+([%w_]+)%s*:%s*[%w_]+%s*(%b{})") do
    for name, value in body:gmatch("([%w_]+)%s*=%s*(%-?%d+)") do out[enum .. "::" .. name] = value end
  end
end

-- The rows of an event table: WXL_EVENT(name, id, args).
local function events_of(text)
  local rows = {}
  for name, id, args in text:gmatch("WXL_EVENT%((%w+),%s*(%d+),%s*(%w+)%)") do
    rows[#rows + 1] = { id = tonumber(id), name = name, args = args }
  end
  return rows
end

local function probe_source(headers, structs, events)
  local lines = {}
  for _, h in ipairs(headers) do lines[#lines + 1] = string.format('#include "%s"', h) end
  if events then lines[#lines + 1] = '#include "wxl/Events.hpp"' end
  for _, s in ipairs(structs) do lines[#lines + 1] = string.format("static_assert(sizeof(%s) > 0);", s) end
  return table.concat(lines, "\n") .. "\n"
end

-- Each record the dump lays out: its field lines (offset | declaration) and its size line.
local function records_of(dump)
  local records, current = {}, nil
  for line in (dump .. "\n"):gmatch("([^\n]*)\n") do
    local offset, decl = line:match("^%s*([%d:%-]*) | (.*)$")
    if line:find("^%*%*%* Dumping AST Record Layout") then
      current = nil
    elseif decl and not current and offset ~= "" then
      local name = decl:match("^struct (.+)$") or decl:match("^class (.+)$") or decl:match("^union (.+)$")
      current = { fields = {} }
      if name then records[name] = current end
    elseif current and decl then
      local size, align = decl:match("%[sizeof=(%d+), align=(%d+)")
      if size then current.size, current.align = tonumber(size), tonumber(align)
      elseif offset ~= "" then current.fields[#current.fields + 1] = offset .. " | " .. (decl:gsub("^  ", "")) end
    end
  end
  return records
end

-- { defines = {name = value} (enumerators too), events = {rows}, structs = {name = {size, align, fields}} }
function M.run(opts)
  local model = { defines = {}, events = {}, structs = {} }
  local names = {}
  for _, h in ipairs(opts.headers) do
    local text
    for _, dir in ipairs(opts.includes) do text = text or slurp(dir .. "/" .. h) end
    if not text then error("abi_lock.lua: " .. h .. " is not under any --include") end
    defines_of(text, model.defines)
    enums_of(text, model.defines)
    for name in text:gmatch("typedef%s+struct%s+([%w_]+)") do names[#names + 1] = name end
  end
  if opts.events then
    model.events = events_of(slurp(opts.events) or error("abi_lock.lua: no " .. opts.events))
    for _, row in ipairs(model.events) do names[#names + 1] = "wxl::events::" .. row.args end
  end

  local source = opts.work .. "/abi_probe.cpp"
  local f = assert(io.open(source, "wb"))
  f:write(probe_source(opts.headers, names, opts.events))
  f:close()
  local cmd = { quote(opts.compiler) }
  for _, flag in ipairs(opts.flags) do cmd[#cmd + 1] = quote(flag) end
  for _, dir in ipairs(opts.includes) do cmd[#cmd + 1] = quote("-I" .. dir) end
  cmd[#cmd + 1] = "-fsyntax-only -Xclang -fdump-record-layouts " .. quote(source) .. " 2>&1"
  local pipe = assert(io.popen(table.concat(cmd, " ")))
  local dump = pipe:read("*a")
  local ok = pipe:close()
  if not ok then error("abi_lock.lua: the probe does not compile:\n" .. dump) end

  local records = records_of(dump)
  for _, name in ipairs(names) do
    model.structs[name] = records[name] or error("abi_lock.lua: the compiler laid out no " .. name)
  end
  return model
end

return M
