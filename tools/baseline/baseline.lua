#!/usr/bin/env luajit
-- SPDX-License-Identifier: GPL-3.0-or-later
-- Holds each hand-owned native function to the generated baseline it started from.

local here = (arg and arg[0] or ""):match("^(.*)/[^/]*$") or "."
package.path = here .. "/?.lua;" .. package.path

local xxh64 = require("xxh64")

-- luajit baseline.lua <src dir> [--update] [file...]
-- A native file starts as a copy of a generated baseline (src/gen/) and names it on one line:
--     // baseline: gen/CMapArea_ProcessIffChunks.gen.cpp 0x1234abcd5678ef90
-- The hash is XXH64 of the baseline's bytes with LF line ends (a checkout may write CRLF). When a baseline is
-- generated again and differs, the line no longer matches: carry the change into the native file, then --update.

local M = {}

local function read(path)
  local f = io.open(path, "rb")
  if not f then return nil end
  local text = f:read("*a")
  f:close()
  return text
end

local function write(path, text)
  local f = assert(io.open(path, "wb"))
  f:write(text)
  f:close()
end

--- The C++ sources under a folder, sorted, when the caller (CMake) does not list them (one POSIX `find` call).
local function sources(dir)
  local out = {}
  local pipe = io.popen('find "' .. dir .. '" -type f \\( -name "*.cpp" -o -name "*.hpp" \\) | sort')
  for line in pipe:lines() do out[#out + 1] = line end
  pipe:close()
  return out
end

--- Checks (or with update, rewrites) every baseline line under src; returns status, files checked, messages.
function M.run(src, update, files)
  local status, checked, messages = 0, 0, {}
  for _, path in ipairs(files or sources(src)) do
    local text = read(path)
    local name, hash = text:match("\n// baseline: (%S+) 0x(%x+)\r?\n")
    if not name then name, hash = text:match("^// baseline: (%S+) 0x(%x+)\r?\n") end
    if name then
      local baseline = read(src .. "/" .. name)
      if not baseline then
        messages[#messages + 1] = ("baseline: %s names %s, which is not there"):format(path, name)
        status = 1
      else
        checked = checked + 1
        local now = xxh64.hex((baseline:gsub("\r\n", "\n")))
        if now ~= hash then
          if update then
            local line = "// baseline: " .. name .. " 0x" .. hash
            local at = text:find(line, 1, true)
            write(path, text:sub(1, at - 1) .. "// baseline: " .. name .. " 0x" .. now .. text:sub(at + #line))
            messages[#messages + 1] = ("baseline: %s now names 0x%s"):format(path, now)
          else
            messages[#messages + 1] = ("baseline: %s changed under %s: carry the change over, then --update")
              :format(src .. "/" .. name, path)
            status = 1
          end
        end
      end
    end
  end
  return status, checked, messages
end

if arg and arg[0] and arg[0]:match("baseline%.lua$") and not package.loaded["baseline_test"] then
  local src = arg[1]
  if not src or src:sub(1, 1) == "-" then
    io.stderr:write("usage: luajit baseline.lua <src dir> [--update] [file...]\n")
    os.exit(2)
  end
  local update, files = false, {}
  for i = 2, #arg do
    if arg[i] == "--update" then update = true else files[#files + 1] = arg[i] end
  end
  local status, checked, messages = M.run(src, update, #files > 0 and files or nil)
  for _, m in ipairs(messages) do (status == 0 and io.stdout or io.stderr):write(m, "\n") end
  if status == 0 and not update then print(("baseline: %d native file(s) match their baselines"):format(checked)) end
  os.exit(status)
end

return M
