-- SPDX-License-Identifier: GPL-3.0-or-later
-- The generator's error exit: the message on stderr, then the exit code (1 stale, 2 bad input).

return function(code, fmt, ...)
  io.stderr:write("gen.lua: ", string.format(fmt, ...), "\n")
  os.exit(code)
end
