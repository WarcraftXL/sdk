-- SPDX-License-Identifier: GPL-3.0-or-later
-- The settings tool: reads WXL_SETTINGS structs through libclang, writes <stem>.settings.hpp beside them.

local script_dir = arg[0]:match("^(.*)[/\\]") or "."
package.path = script_dir .. "/?.lua;" .. package.path

local clang = require("clang")

local function fail(message)
  io.stderr:write("settings: " .. message .. "\n")
  os.exit(2)
end

-- luajit tools/settings/settings.lua <header> [--out <file>] [--check] [-q] [-I <dir>]... [--libclang <path>] [...]:
-- exit 0 written or up to date, 1 stale under --check, 2 when the header breaks a rule (file:line and the fix).
local function parse_args()
  local options = { includes = {} }
  local i = 1
  while i <= #arg do
    local a = arg[i]
    if a == "--out" then i = i + 1; options.out = arg[i]
    elseif a == "--check" then options.check = true
    elseif a == "-I" then i = i + 1; options.includes[#options.includes + 1] = arg[i]
    elseif a == "--libclang" then i = i + 1; options.libclang = arg[i]
    elseif a == "--sdk-include" then i = i + 1; options.sdk_include = arg[i]
    elseif a == "--resource-dir" then i = i + 1; options.resource_dir = arg[i]
    elseif a == "-q" then options.quiet = true
    elseif a:sub(1, 1) ~= "-" and not options.header then options.header = a
    else fail("unknown argument " .. a) end
    i = i + 1
  end
  if not options.header then fail("usage: luajit settings.lua <header> [--out <file>] [--check] [-I <dir>]...") end
  return options
end

local function realpath(p)
  local h = io.popen('realpath "' .. p:gsub('"', '\\"') .. '" 2>/dev/null')
  local r = h and h:read("*l")
  if h then h:close() end
  return r or p
end

-- The header parsed as C++20 against the SDK's include folder; fails when it does not compile.
local function parse_header(options, header)
  local sdk_include = realpath(options.sdk_include or (script_dir .. "/../../include"))
  local resource_dir = options.resource_dir or os.getenv("WXL_CLANG_RESOURCE_DIR") or "/usr/lib/llvm-19/lib/clang/19"
  local args = { "-x", "c++", "-std=c++20", "-fsyntax-only", "-I", sdk_include, "-resource-dir", resource_dir, "-Wno-everything" }
  for _, dir in ipairs(options.includes) do args[#args + 1] = "-I"; args[#args + 1] = dir end
  local tu, perr = clang.parse(header, args)
  if not tu then fail(options.header .. " does not compile:\n" .. perr) end
  return tu
end

-- Writes `text` to `out` unless it holds it already (CRLF read as LF); under --check, a stale file
-- exits 1 instead. Returns true when the file was written.
local function write_if_changed(out, text, check)
  local fh = io.open(out, "rb")
  local current = fh and fh:read("*a")
  if fh then fh:close() end
  current = current and current:gsub("\r\n", "\n")
  if current == text then return false end
  if check then
    io.stderr:write("settings: " .. out .. " is stale; build with the tool, or run it, and commit the result\n")
    os.exit(1)
  end
  local wh = assert(io.open(out, "wb"))
  wh:write(text)
  wh:close()
  return true
end

local options = parse_args()
local ok, err = pcall(clang.load, options.libclang)
if not ok then fail(err) end

local header = realpath(options.header)
local structs, errors = require("reader").read(parse_header(options, header), header)
if #errors > 0 then fail(#errors .. " error(s):\n  " .. table.concat(errors, "\n  ")) end

local base = header:match("([^/\\]+)$")
local out = options.out or (header:gsub("%.[^.]+$", "") .. ".settings.hpp")
local text = require("traits").render(structs, base)
if not write_if_changed(out, text, options.check) then os.exit(0) end
if not options.quiet then
  local n = 0
  for _, s in ipairs(structs) do n = n + #s.fields end
  local stem = header:match("([^/\\]+)%.[^.]+$") or header
  print(("settings: %d setting(s) of %s -> %s"):format(n, stem, out))
end
