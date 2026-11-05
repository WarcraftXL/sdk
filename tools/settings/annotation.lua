-- SPDX-License-Identifier: GPL-3.0-or-later
-- The text of a WXL_SETTINGS / WXL_SETTING annotation: "wxl.setting:range(0, 100), step = 5, ui = slider".

local clang = require("clang")

local M = {}

-- Reads one argument at the head of `rest`: a "string" (positional), name(a, b) or key = value
-- (options). Returns what is left, or nil when the head is none of the three.
local function read_argument(rest, positional, opts)
  local str, after = rest:match('^"([^"]*)"%s*(.*)$')
  if str then
    positional[#positional + 1] = str
    return after
  end
  local name, list, after_call = rest:match("^([%a_][%w_]*)%s*%(([^)]*)%)%s*(.*)$")
  if name then
    local values = {}
    for item in list:gmatch("[^,]+") do values[#values + 1] = tonumber(item:match("^%s*(.-)%s*$")) or item end
    opts[name] = values
    return after_call
  end
  local key, value, after_pair = rest:match("^([%a_][%w_]*)%s*=%s*([^,%s]+)%s*(.*)$")
  if key then
    opts[key] = tonumber(value) or value:match('^"(.*)"$') or value
    return after_pair
  end
  return nil
end

-- Parses an annotation: what ("settings", "setting"), its positional strings, its options, and an
-- error when the arguments cannot be read. Returns nil for an annotation that is not the SDK's.
function M.parse(text)
  local what, args = text:match("^wxl%.(%w+):(.*)$")
  if not what then return nil end
  local positional, opts = {}, {}
  local rest = args
  while true do
    rest = rest:match("^%s*(.-)%s*$")
    if rest == "" then break end
    rest = read_argument(rest, positional, opts)
    if not rest then return what, positional, opts, "cannot read annotation: " .. args end
    if rest:sub(1, 1) == "," then rest = rest:sub(2) elseif rest ~= "" then
      return what, positional, opts, "expected ',' in annotation: " .. args
    end
  end
  return what, positional, opts
end

-- The first SDK annotation of a cursor, parsed; nothing when it has none.
function M.of(cursor)
  for _, text in ipairs(clang.annotations(cursor)) do
    local what, positional, opts, perr = M.parse(text)
    if what then return what, positional, opts, perr end
  end
end

return M
