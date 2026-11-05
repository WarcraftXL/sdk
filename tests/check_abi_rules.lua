-- SPDX-License-Identifier: GPL-3.0-or-later
-- check_abi_rules.lua <sdk dir>: the ABI lock's rules on made-up layouts (what needs which version bump).

package.path = arg[1] .. "/tools/abi/?.lua;" .. package.path
local rules = require("rules")

local failures = 0
local function check(cond, what)
  if not cond then failures = failures + 1; print("FAIL " .. what) end
end

local function model(over)
  local m = {
    defines = { WXL_SDK_VERSION_MAJOR = "1", WXL_SDK_VERSION_MINOR = "3", WXL_OK = "0", WXL_THINGS_API_VERSION = "1" },
    events = { { id = 0, name = "OnA", args = "AArgs" } },
    structs = { WXL_ThingsApi = { size = 8, align = 4, fields = { "0 | uint32_t structSize", "4 | uint32_t apiVersion" } } },
  }
  over(m)
  return m
end

local opts = { major = { "WXL_SDK_VERSION_MAJOR" }, minor = { "WXL_SDK_VERSION_MINOR" } }
local base = model(function() end)

local function append(m)
  m.structs.WXL_ThingsApi = { size = 12, align = 4, fields = { "0 | uint32_t structSize", "4 | uint32_t apiVersion", "8 | int (*)(void) Count" } }
end

check(rules.judge(base, model(append), opts).bumped == false, "an appended member without bumps passes")
check(rules.judge(base, model(function(m) append(m); m.defines.WXL_SDK_VERSION_MINOR = "4" end), opts).bumped == false,
      "an appended table member passes without its table version")
check(rules.judge(base, model(function(m) append(m); m.defines.WXL_SDK_VERSION_MINOR = "4"; m.defines.WXL_THINGS_API_VERSION = "2" end), opts).bumped,
      "an appended table member with both bumps is refused")
check(rules.judge(base, model(function(m) m.events[2] = { id = 1, name = "OnB", args = "BArgs" }; m.defines.WXL_SDK_VERSION_MINOR = "4" end), opts).bumped,
      "a new event row with a minor bump is refused")
check(rules.judge(base, model(function(m) m.defines.WXL_OK = "1"; m.defines.WXL_SDK_VERSION_MINOR = "4" end), opts).bumped == false,
      "a changed status code passes with a minor bump")
check(rules.judge(base, model(function(m) m.events[1].args = "OtherArgs"; m.defines.WXL_SDK_VERSION_MAJOR = "2" end), opts).bumped,
      "a changed event row with a major bump is refused")
check(rules.judge(base, model(function(m) m.structs.WXL_ThingsApi.fields[2] = "4 | uint16_t apiVersion"; m.defines.WXL_SDK_VERSION_MINOR = "4"; m.defines.WXL_THINGS_API_VERSION = "2" end), opts).bumped == false,
      "a changed member passes with a minor bump")

print(failures .. " failure(s)")
os.exit(failures == 0 and 0 or 1)
