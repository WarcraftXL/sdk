#!/usr/bin/env luajit
-- SPDX-License-Identifier: GPL-3.0-or-later
-- Tests of the baseline check and its XXH64: a match, a CRLF checkout, a change, an update, a missing baseline.

local here = (arg and arg[0] or ""):match("^(.*)/[^/]*$") or "."
package.path = here .. "/?.lua;" .. package.path
package.loaded["baseline_test"] = true

local xxh64 = require("xxh64")
local baseline = require("baseline")

local checks, failures = 0, 0
local function check(ok, what)
  checks = checks + 1
  if not ok then failures = failures + 1 end
  print((ok and "ok   " or "FAIL ") .. what)
end

local function put(path, text)
  local f = assert(io.open(path, "wb"))
  f:write(text)
  f:close()
end

check(xxh64.hex("") == "ef46db3751d8e999", "XXH64 of nothing")
check(xxh64.hex("abc") == "44bc2cf5ad770999", "XXH64 of a short input")
check(xxh64.hex(string.rep("a", 100)) == "375041e8b1decfb3", "XXH64 past 32 bytes")

local dir = os.tmpname()
os.remove(dir)
assert(os.execute('mkdir -p "' .. dir .. '/gen" "' .. dir .. '/native"'))
put(dir .. "/gen/A.gen.cpp", "int a;\n")
put(dir .. "/native/A.cpp", "// SPDX\n\n// baseline: gen/A.gen.cpp 0x0000000000000000\nint a;\n")
check(baseline.run(dir) == 1, "a wrong hash fails")
check(baseline.run(dir, true) == 0 and baseline.run(dir) == 0, "--update writes the hash; then it passes")
put(dir .. "/gen/A.gen.cpp", "int a;\r\n")
check(baseline.run(dir) == 0, "a CRLF checkout of the same baseline passes")
put(dir .. "/gen/A.gen.cpp", "int a, b;\n")
check(baseline.run(dir) == 1, "a changed baseline fails")
os.remove(dir .. "/gen/A.gen.cpp")
check(baseline.run(dir) == 1, "a missing baseline fails")
os.execute('rm -r "' .. dir .. '"')

print(("%d checks, %d failures"):format(checks, failures))
os.exit(failures == 0 and 0 or 1)
