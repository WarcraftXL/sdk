-- SPDX-License-Identifier: GPL-3.0-or-later
-- The generated files against the disk: checked (--check) or written, stale headers removed.

local M = {}

-- The header folder every generated offsets header lives in, relative to the repository.
local HEADER_DIR = "include/wxl/offsets"

local function read(path)
  local f = io.open(path, "rb")
  local text = f and f:read("*a")
  if f then f:close() end
  return text
end

-- A file's text with CRLF endings read as LF: the working tree holds CRLF, the generator LF.
local function normalize(t) return t and (t:gsub("\r\n", "\n")) end

-- The headers on disk under HEADER_DIR, relative to root.
local function existing_headers(root)
  local p = io.popen("cd '" .. root .. "' && find " .. HEADER_DIR .. " -name '*.hpp' 2>/dev/null")
  local list = {}
  for line in p:lines() do list[#list + 1] = line end
  p:close()
  return list
end

-- The generated paths, sorted, and the headers on disk the generator no longer gives.
function M.plan(root, out_files)
  local paths = {}
  for path in pairs(out_files) do paths[#paths + 1] = path end
  table.sort(paths)
  local stale = {}
  for _, path in ipairs(existing_headers(root)) do
    if not out_files[path] then stale[#stale + 1] = path end
  end
  return paths, stale
end

-- An output path on disk: absolute as given, or relative to root.
function M.absolute(root, path) return path:sub(1, 1) == "/" and path or (root .. "/" .. path) end

-- Reports up to ten files that differ or are stale; true when every file is current.
function M.check(root, out_files, paths, stale, core_given)
  local bad = 0
  local function report(path, why)
    bad = bad + 1
    if bad <= 10 then io.stderr:write("gen.lua: ", path, " ", why, "\n") end
  end
  for _, path in ipairs(paths) do
    if normalize(read(M.absolute(root, path))) ~= out_files[path] then report(path, "is not what the atlas gives") end
  end
  for _, path in ipairs(stale) do report(path, "is not generated from the atlas") end
  if bad == 0 then return true end
  io.stderr:write(string.format("gen.lua: %d file(s) differ: run `luajit tools/offsets/gen.lua%s` and commit them\n", bad,
    core_given and " --core <core>" or ""))
  return false
end

-- Removes the stale headers and writes, with CRLF endings, each file whose text changed.
-- Returns how many files were written.
function M.write(root, out_files, paths, stale)
  for _, path in ipairs(stale) do os.remove(M.absolute(root, path)) end
  local written = 0
  for _, path in ipairs(paths) do
    local full = M.absolute(root, path)
    os.execute("mkdir -p '" .. full:match("^(.*)/[^/]+$") .. "'")
    if normalize(read(full)) ~= out_files[path] then
      local o = assert(io.open(full, "wb"))
      o:write((out_files[path]:gsub("\n", "\r\n")))
      o:close()
      written = written + 1
    end
  end
  return written
end

return M
