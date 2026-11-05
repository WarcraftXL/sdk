#!/usr/bin/env luajit
-- SPDX-License-Identifier: GPL-3.0-or-later
-- Holds a C ABI to a checked-in lock file: struct layouts, event rows and #define values of some headers.

local here = (arg and arg[0] or ""):match("^(.*)/[^/]*$") or "."
package.path = here .. "/?.lua;" .. package.path

local probe = require("probe")
local lockfile = require("lockfile")
local rules = require("rules")

-- luajit abi_lock.lua --compiler <clang> --header <file>... --lock <file> --work <dir> (--check | --write | --rebaseline):
-- --check fails on any difference, --write refuses a change without its bump, --rebaseline (an unreleased line) does not.
local function parse_args()
  local opts = { flags = {}, includes = {}, headers = {}, major = {}, minor = {} }
  local lists = { ["--flag"] = "flags", ["--include"] = "includes", ["--header"] = "headers",
                  ["--major"] = "major", ["--minor"] = "minor" }
  local values = { ["--compiler"] = "compiler", ["--events"] = "events", ["--lock"] = "lock", ["--work"] = "work",
                   ["--regenerate"] = "regenerate" }
  local i = 1
  while i <= #arg do
    local a, v = arg[i], arg[i + 1]
    if lists[a] then table.insert(opts[lists[a]], v); i = i + 2
    elseif values[a] then opts[values[a]] = v; i = i + 2
    elseif a == "--check" or a == "--write" or a == "--rebaseline" then opts.mode = a:sub(3); i = i + 1
    else io.stderr:write("abi_lock.lua: unknown argument '", a, "'; see the head of abi_lock.lua\n"); os.exit(2) end
  end
  if not (opts.compiler and opts.lock and opts.work and opts.mode and #opts.headers > 0) then
    io.stderr:write("abi_lock.lua: --compiler, --header, --lock, --work and --check, --write or --rebaseline are required\n")
    os.exit(2)
  end
  return opts
end

local function main()
  local opts = parse_args()
  local current = probe.run(opts)
  local text = lockfile.format(current, opts.regenerate)
  local old_text = lockfile.read(opts.lock)
  if old_text == text then
    if opts.mode == "check" then print("abi_lock.lua: the ABI is the lock's") end
    return 0
  end
  local verdict = rules.judge(old_text and lockfile.parse(old_text), current, opts)
  for _, line in ipairs(verdict.lines) do print(line) end
  if opts.mode == "rebaseline" then
    lockfile.write(opts.lock, text)
    print("abi_lock.lua: rebaselined " .. opts.lock .. " without a version bump")
    return 0
  end
  if opts.mode == "write" then
    if not verdict.bumped then
      print("abi_lock.lua: the lock is left as it was: bump the versions named above first")
      return 1
    end
    lockfile.write(opts.lock, text)
    print("abi_lock.lua: wrote " .. opts.lock)
    return 0
  end
  if verdict.bumped then
    print("abi_lock.lua: the ABI changed with its versions moved: regenerate the lock (" ..
          (opts.regenerate or "--write") .. ") and commit it")
  else
    print("abi_lock.lua: the ABI changed without the version bump it needs")
  end
  return 1
end

os.exit(main())
