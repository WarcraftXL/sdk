-- SPDX-License-Identifier: GPL-3.0-or-later
-- XXH64 (seed 0) as xxhash.h computes it, on LuaJIT's 64-bit integers: the hash a native file names its baseline by.

local ffi = require("ffi")
local bit = require("bit")

local M = {}

local P1 = 0x9E3779B185EBCA87ULL
local P2 = 0xC2B2AE3D27D4EB4FULL
local P3 = 0x165667B19E3779F9ULL
local P4 = 0x85EBCA77C2B2AE63ULL
local P5 = 0x27D4EB2F165667C5ULL

local function rotl(x, r) return bit.rol(x, r) end

local function round(acc, lane)
  acc = acc + lane * P2
  return rotl(acc, 31) * P1
end

local function merge(acc, value)
  acc = bit.bxor(acc, round(0ULL, value))
  return acc * P1 + P4
end

--- XXH64 of a Lua string, as a uint64_t.
function M.hash(data)
  local n = #data
  local bytes = ffi.cast("const uint8_t*", data)
  local u64 = ffi.cast("const uint64_t*", bytes)
  local u32 = ffi.cast("const uint32_t*", bytes)
  local i, h = 0, nil
  if n >= 32 then
    local v1, v2, v3, v4 = P1 + P2, P2 + 0ULL, 0ULL, 0ULL - P1
    while i + 32 <= n do
      local q = i / 8
      v1 = round(v1, u64[q])
      v2 = round(v2, u64[q + 1])
      v3 = round(v3, u64[q + 2])
      v4 = round(v4, u64[q + 3])
      i = i + 32
    end
    h = rotl(v1, 1) + rotl(v2, 7) + rotl(v3, 12) + rotl(v4, 18)
    h = merge(merge(merge(merge(h, v1), v2), v3), v4)
  else
    h = P5 + 0ULL
  end
  h = h + ffi.cast("uint64_t", n)
  while i + 8 <= n do
    h = bit.bxor(h, round(0ULL, u64[i / 8]))
    h = rotl(h, 27) * P1 + P4
    i = i + 8
  end
  if i + 4 <= n then
    h = bit.bxor(h, ffi.cast("uint64_t", u32[i / 4]) * P1)
    h = rotl(h, 23) * P2 + P3
    i = i + 4
  end
  while i < n do
    h = bit.bxor(h, ffi.cast("uint64_t", bytes[i]) * P5)
    h = rotl(h, 11) * P1
    i = i + 1
  end
  h = bit.bxor(h, bit.rshift(h, 33)) * P2
  h = bit.bxor(h, bit.rshift(h, 29)) * P3
  return bit.bxor(h, bit.rshift(h, 32))
end

--- The hash as the 16 lowercase hex digits a baseline line writes.
function M.hex(data)
  return bit.tohex(M.hash(data), 16)
end

return M
