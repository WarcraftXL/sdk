-- SPDX-License-Identifier: GPL-3.0-or-later
-- Atlas names as C++ scopes and identifiers: "CGUnit_C::GetAura" -> scope { "CGUnit_C" }, "GetAura".

local M = {}

-- Words C++ or <cstdint> reserves, and macros of the Windows headers: an identifier gets a "_" after them.
M.keywords = {}
for w in ([[alignas alignof and asm auto bool break case catch char class const constexpr const_cast
  continue decltype default delete do double dynamic_cast else enum explicit export extern false float for
  friend goto if inline int long mutable namespace new noexcept not nullptr operator or private protected
  public register reinterpret_cast return short signed sizeof static static_assert static_cast struct switch
  template this throw true try typedef typeid typename union unsigned using virtual void volatile while xor
  size small near far min max]]):gmatch("%S+") do M.keywords[w] = true end

-- Operator spellings and the word each one becomes in "operator_<word>".
local OPERATORS = {
  ["*="] = "mul_assign", ["+="] = "add_assign", ["-="] = "sub_assign", ["/="] = "div_assign", ["=="] = "eq",
  ["!="] = "ne", ["[]"] = "index", ["()"] = "call", ["->"] = "arrow", ["<<"] = "shl", [">>"] = "shr",
  ["<="] = "le", [">="] = "ge", ["*"] = "mul", ["+"] = "add", ["-"] = "sub", ["/"] = "div", ["="] = "assign",
  ["<"] = "lt", [">"] = "gt", ["!"] = "not", ["~"] = "compl", ["&"] = "and", ["|"] = "or", ["^"] = "xor",
  [" new"] = "new", [" delete"] = "delete", [" new[]"] = "new_array", [" delete[]"] = "delete_array",
}

-- A name's segments, split on the `::` outside template arguments.
local function segments(name)
  local out, depth, cur, i = {}, 0, "", 1
  while i <= #name do
    local c = name:sub(i, i)
    if c == "<" then depth = depth + 1 elseif c == ">" then depth = depth - 1 end
    if depth == 0 and name:sub(i, i + 1) == "::" then
      out[#out + 1] = cur; cur = ""; i = i + 2
    else
      cur = cur .. c; i = i + 1
    end
  end
  out[#out + 1] = cur
  return out
end

-- "operator+=" -> "operator_add_assign"; an unknown operator spells its characters in hex.
local function operator_ident(op)
  local key = op:gsub("%s+", "")
  if key == "new" or key == "delete" or key == "new[]" or key == "delete[]" then key = " " .. key end
  if OPERATORS[key] then return "operator_" .. OPERATORS[key] end
  local parts = {}
  for c in op:gmatch("%S") do parts[#parts + 1] = string.format("%02X", c:byte()) end
  return "operator_" .. table.concat(parts)
end

-- One segment as a C++ identifier; `scope_last` is the enclosing segment, to spot a constructor.
function M.ident(seg, scope_last)
  local op = seg:match("^operator%s*(.*)$")
  if op and (op:match("^%p") or op:match("^new") or op:match("^delete")) then return operator_ident(op) end
  if seg:sub(1, 1) == "~" then return "dtor" end
  if scope_last and seg == scope_last then return "ctor" end
  local s = seg:gsub("[<>,%s%*&%[%]%(%)%.@:%-]+", "_"):gsub("_+$", ""):gsub("__+", "_")
  if s == "" then s = "_" end
  if s:match("^%d") then s = "_" .. s end
  if M.keywords[s] then s = s .. "_" end
  return s
end

-- The scope path and identifier of an atlas name.
function M.place(name)
  local segs = segments(name)
  local path = {}
  for i = 1, #segs - 1 do path[i] = M.ident(segs[i]) end
  return path, M.ident(segs[#segs], segs[#segs - 1])
end

-- The qualified C++ name of a scope path and identifier: "a::b::id", or "id" at the top.
function M.qualified(path, id)
  return table.concat(path, "::") .. (#path > 0 and "::" or "") .. id
end

return M
