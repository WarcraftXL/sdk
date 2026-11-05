#!/usr/bin/env luajit
-- SPDX-License-Identifier: GPL-3.0-or-later
-- Writes include/wxl/offsets/** and the hook-point tables from the atlas, under its own names.

local here = (arg and arg[0] or ""):match("^(.*)/[^/]*$") or "."
local root = here .. "/../.."
package.path = here .. "/?.lua;" .. package.path

local fail = require("gen.fail")

-- luajit tools/offsets/gen.lua [--atlas <dir, default ../atlas>] [--core <wxl-core, its HookPoints.def too>] [--check]:
-- --check writes nothing and exits 1 when a file is not what the atlas gives, 2 on a bad input.
local function parse_args()
  local opts = { atlas = root .. "/../atlas" }
  local i = 1
  while i <= #arg do
    local a = arg[i]
    if a == "--atlas" or a == "--core" then opts[a:sub(3)] = arg[i + 1]; i = i + 2
    elseif a == "--check" then opts.check = true; i = i + 1
    else fail(2, "unknown argument '%s'; see the head of tools/offsets/gen.lua", a) end
  end
  return opts
end

-- The atlas model, from the atlas's own loader.
local function load_atlas(dir)
  package.path = dir .. "/lib/?.lua;" .. package.path
  local ok, load = pcall(require, "atlas.load")
  if not ok then fail(2, "no atlas at %s (pass --atlas <checkout>)", dir) end
  local model = load.load(dir .. "/data")
  if #model.problems > 0 then fail(2, "the atlas does not load:\n  %s", table.concat(model.problems, "\n  ")) end
  return model
end

-- A Lua data file of this folder (aliases.lua, hookpoints.lua), run with no globals.
local function read_table(path)
  local chunk, why = loadfile(path)
  if not chunk then fail(2, "%s", why) end
  setfenv(chunk, {})
  return chunk()
end

local opts = parse_args()
local model = load_atlas(opts.atlas)
local aliases = read_table(here .. "/aliases.lua")
local hookpoints = read_table(here .. "/hookpoints.lua")

local entries = require("gen.entries")
local header_files = require("gen.header_files")
local hook_points = require("gen.hook_points")
local update_fields = require("gen.update_fields")
local output = require("gen.output")

-- Every generated file, by path: relative to this repository, or absolute for the core's.
local built = entries.build(model, aliases, fail)
local set = header_files.new()
require("gen.emit_symbols").emit(set, built.entries, entries.functions_by_address(model))
require("gen.emit_structs").emit(set, built.structs)
local out_files = {}
header_files.render_all(set, out_files)
out_files[update_fields.path] = update_fields.render(model)
local sdk_def, core_def = hook_points.render(hookpoints, built.entries, fail)
out_files[hook_points.sdk_path] = sdk_def
if opts.core then out_files[opts.core .. "/" .. hook_points.core_path] = core_def end

local paths, stale = output.plan(root, out_files)
if opts.check then
  if not output.check(root, out_files, paths, stale, opts.core) then os.exit(1) end
  print(string.format("gen.lua: %d generated file(s) are current", #paths))
  return
end
local written = output.write(root, out_files, paths, stale)
print(string.format("gen.lua: %d file(s), %d written, %d removed", #paths, written, #stale))
