#!/usr/bin/env luajit
-- SPDX-License-Identifier: GPL-3.0-or-later
-- Holds every address in include/wxl/offsets/** to the atlas verifier, run on Wow.exe from disk.

local here = (arg and arg[0] or ""):match("^(.*)/[^/]*$") or "."
local root = here .. "/../.."

-- The atlas sections whose entries a header declares by address.
local KINDS = { "functions", "globals", "vtables", "sites" }

-- Exit status when no Wow.exe is at hand: ctest's skip code.
local EXIT_SKIPPED = 77

local function fail(why)
  io.stderr:write("check_verified.lua: ", why, "\n")
  os.exit(2)
end

-- luajit tools/offsets/check_verified.lua [--atlas <dir>] [--binary <Wow.exe, default $WXL_WOW_EXE>]:
-- exit 0 when every address is verified, 1 when one is not, 77 without a Wow.exe.
local function parse_args()
  local opts = { atlas = root .. "/../atlas", binary = os.getenv("WXL_WOW_EXE") }
  local i = 1
  while i <= #arg do
    local a = arg[i]
    if a == "--atlas" or a == "--binary" then opts[a:sub(3)] = arg[i + 1]; i = i + 2
    else fail("unknown argument '" .. a .. "'") end
  end
  return opts
end

-- The atlas verifier's run over every entry a header can declare.
local function run_verifier(opts)
  package.path = opts.atlas .. "/lib/?.lua;" .. package.path
  local load = require("atlas.load")
  local pe = require("atlas.pe")
  local x86 = require("atlas.x86")
  local verify = require("atlas.verify")

  local model = load.load(opts.atlas .. "/data")
  local image = assert(pe.open(opts.binary))
  local ok, why = image:verify_stock()
  if not ok then fail(why) end
  local objdump = x86.find_objdump()
  if not objdump then fail("no llvm-objdump on PATH") end

  local v = verify.new(model, image, x86.decoder(image, objdump), { only = { functions = true, globals = true,
    vtables = true, sites = true } })
  v:run()
  return model, v.failures
end

-- The atlas name of every known address, and the verifier's failure for each failed one.
local function index_entries(model, failures)
  local failed = {}
  for _, f in ipairs(failures) do
    for _, kind in ipairs(KINDS) do
      for _, e in ipairs(model[kind]) do
        if e._where == f.where then failed[e.at] = f.where end
      end
    end
  end
  local known = {}
  for _, kind in ipairs(KINDS) do
    for _, e in ipairs(model[kind]) do known[e.at] = e.name end
  end
  return known, failed
end

-- The generated headers, relative to root, in byte order.
local function headers()
  local p = io.popen("cd '" .. root .. "' && find include/wxl/offsets -name '*.hpp' | LC_ALL=C sort")
  local files = {}
  for line in p:lines() do files[#files + 1] = line end
  p:close()
  return files
end

-- Prints every uintptr_t address of a header that is no atlas entry or fails the verifier.
-- Returns how many addresses it read and how many it printed.
local function check_header(path, known, failed)
  local checked, bad = 0, 0
  local f = assert(io.open(root .. "/" .. path, "rb"))
  local n = 0
  for line in f:lines() do
    n = n + 1
    local hex = line:match("uintptr_t [%w_]+ = (0x%x+);")
    if hex then
      local at = tonumber(hex)
      checked = checked + 1
      if not known[at] then
        bad = bad + 1
        print(string.format("%s:%d: 0x%08X is no entry of the atlas", path, n, at))
      elseif failed[at] then
        bad = bad + 1
        print(string.format("%s:%d: 0x%08X (%s) fails the atlas verifier", path, n, at, failed[at]))
      end
    end
  end
  f:close()
  return checked, bad
end

local opts = parse_args()
if not opts.binary or opts.binary == "" or not io.open(opts.binary, "rb") then
  print("check_verified.lua: no Wow.exe (set WXL_WOW_EXE or pass --binary); skipped")
  os.exit(EXIT_SKIPPED)
end

local known, failed = index_entries(run_verifier(opts))
local files = headers()
local checked, bad = 0, 0
for _, path in ipairs(files) do
  local c, b = check_header(path, known, failed)
  checked, bad = checked + c, bad + b
end
print(string.format("check_verified.lua: %d address(es) in %d header(s), %d not verified", checked, #files, bad))
os.exit(bad > 0 and 1 or 0)
