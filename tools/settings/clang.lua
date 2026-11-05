-- SPDX-License-Identifier: GPL-3.0-or-later
-- The part of libclang's stable C API (clang-c/Index.h) the settings tool uses, through LuaJIT's FFI.

local ffi = require("ffi")

ffi.cdef [[
typedef void* CXIndex;
typedef struct CXTranslationUnitImpl* CXTranslationUnit;
typedef void* CXFile;
typedef struct { const void* data; unsigned private_flags; } CXString;
typedef struct { int kind; int xdata; const void* data[3]; } CXCursor;
typedef struct { int kind; void* data[2]; } CXType;
typedef struct { const void* ptr_data[2]; unsigned int_data; } CXSourceLocation;
typedef void* CXDiagnostic;
struct CXUnsavedFile;

CXIndex clang_createIndex(int excludeDeclarationsFromPCH, int displayDiagnostics);
int clang_parseTranslationUnit2(CXIndex index, const char* source_filename,
                                const char* const* command_line_args, int num_command_line_args,
                                struct CXUnsavedFile* unsaved_files, unsigned num_unsaved_files, unsigned options,
                                CXTranslationUnit* out_TU);
unsigned clang_getNumDiagnostics(CXTranslationUnit tu);
CXDiagnostic clang_getDiagnostic(CXTranslationUnit tu, unsigned index);
int clang_getDiagnosticSeverity(CXDiagnostic diagnostic);
CXString clang_formatDiagnostic(CXDiagnostic diagnostic, unsigned options);
unsigned clang_defaultDiagnosticDisplayOptions(void);
void clang_disposeDiagnostic(CXDiagnostic diagnostic);

const char* clang_getCString(CXString string);
void clang_disposeString(CXString string);

CXCursor clang_getTranslationUnitCursor(CXTranslationUnit tu);
unsigned clang_visitChildren(CXCursor parent, void* visitor, void* client_data);
int clang_getCursorKind(CXCursor cursor);
CXString clang_getCursorSpelling(CXCursor cursor);
CXCursor clang_getCursorSemanticParent(CXCursor cursor);
CXCursor clang_getCursorDefinition(CXCursor cursor);
unsigned clang_equalCursors(CXCursor a, CXCursor b);
int clang_Cursor_isNull(CXCursor cursor);
CXSourceLocation clang_getCursorLocation(CXCursor cursor);
void clang_getSpellingLocation(CXSourceLocation location, CXFile* file, unsigned* line,
                               unsigned* column, unsigned* offset);
CXString clang_getFileName(CXFile file);
CXString clang_Cursor_getRawCommentText(CXCursor cursor);

CXType clang_getCursorType(CXCursor cursor);
CXType clang_getCanonicalType(CXType type);
CXCursor clang_getTypeDeclaration(CXType type);
]]

local M = {}

-- Cursor kinds (enum CXCursorKind).
M.kind = {
  StructDecl = 2, ClassDecl = 4, FieldDecl = 6, Namespace = 22, TranslationUnit = 350, AnnotateAttr = 406,
}

-- The range of invalid cursor kinds (CXCursor_FirstInvalid .. CXCursor_LastInvalid).
local FIRST_INVALID_KIND, LAST_INVALID_KIND = 70, 73

-- Type kinds (enum CXTypeKind).
M.type = {
  Bool = 3, Char_U = 4, UChar = 5, UShort = 8, UInt = 9, ULong = 10, ULongLong = 11, UInt128 = 12,
  Char_S = 13, SChar = 14, Short = 16, Int = 17, Long = 18, LongLong = 19, Int128 = 20,
  Float = 21, Double = 22, Record = 105,
}

-- The integer type kinds, by signedness.
local T = M.type
M.unsigned = { [T.Char_U] = true, [T.UChar] = true, [T.UShort] = true, [T.UInt] = true, [T.ULong] = true,
  [T.ULongLong] = true, [T.UInt128] = true }
M.signed = { [T.Char_S] = true, [T.SChar] = true, [T.Short] = true, [T.Int] = true, [T.Long] = true,
  [T.LongLong] = true, [T.Int128] = true }

local CHILD_VISIT_CONTINUE = 1        -- CXChildVisit_Continue
local DIAGNOSTIC_ERROR = 3            -- CXDiagnostic_Error; Fatal is 4
local SKIP_FUNCTION_BODIES = 0x200    -- CXTranslationUnit_SkipFunctionBodies: declarations are all it reads

local lib

--- Loads libclang. `path` overrides the default, Debian's clang 19.
function M.load(path)
  path = path or os.getenv("WXL_LIBCLANG") or "/usr/lib/llvm-19/lib/libclang-19.so.1"
  local ok, loaded = pcall(ffi.load, path)
  if not ok then
    error(("cannot load libclang from %s (%s); pass --libclang <path> or set WXL_LIBCLANG"):format(path, loaded), 0)
  end
  lib = loaded
  M.lib = lib
  return M
end

--- A CXString's text; disposes of the CXString.
function M.string(s)
  local p = lib.clang_getCString(s)
  local r = p ~= nil and ffi.string(p) or ""
  lib.clang_disposeString(s)
  return r
end

-- clang_visitChildren passes two CXCursor structs by value, which LuaJIT callbacks cannot take: the visitor
-- is declared as the C ABI places them (System V x86-64: on the stack; Windows x64: by pointer).
local visitor_type, unpack_cursor
if ffi.arch == "x64" and ffi.os ~= "Windows" then
  visitor_type = "int (*)(void*, intptr_t, intptr_t, intptr_t, intptr_t, intptr_t, uint64_t, const void*, const void*, const void*, uint64_t, const void*, const void*, const void*)"
  unpack_cursor = function(_, _, _, _, _, _, kx, d0, d1, d2)
    local c = ffi.new("CXCursor")
    ffi.cast("uint64_t*", c)[0] = kx
    c.data[0], c.data[1], c.data[2] = d0, d1, d2
    return c
  end
elseif ffi.arch == "x64" and ffi.os == "Windows" then
  visitor_type = "int (*)(const CXCursor*, const CXCursor*, void*)"
  unpack_cursor = function(cursor)
    local c = ffi.new("CXCursor")
    ffi.copy(c, cursor, ffi.sizeof("CXCursor"))
    return c
  end
else
  error("the settings tool runs on x86-64 (Linux, macOS or Windows); this is " .. ffi.os .. "/" .. ffi.arch, 0)
end

local collected
local visitor -- kept alive for the process

--- The direct children of a cursor, as a list of cursors.
function M.children(cursor)
  visitor = visitor or ffi.cast(visitor_type, function(...)
    collected[#collected + 1] = unpack_cursor(...)
    return CHILD_VISIT_CONTINUE
  end)
  local outer = collected
  collected = {}
  lib.clang_visitChildren(cursor, visitor, nil)
  local result = collected
  collected = outer
  return result
end

function M.kind_of(c) return lib.clang_getCursorKind(c) end
function M.spelling(c) return M.string(lib.clang_getCursorSpelling(c)) end

--- Where a cursor is: file, line.
function M.location(c)
  local file = ffi.new("CXFile[1]")
  local line = ffi.new("unsigned[1]")
  lib.clang_getSpellingLocation(lib.clang_getCursorLocation(c), file, line, nil, nil)
  if file[0] == nil then return "?", 0 end
  return M.string(lib.clang_getFileName(file[0])), tonumber(line[0])
end

--- The cursor's doc comment, without its comment markers; "" when it has none.
function M.doc(c)
  local raw = M.string(lib.clang_Cursor_getRawCommentText(c))
  if raw == "" then return "" end
  local lines = {}
  for line in (raw .. "\n"):gmatch("(.-)\r?\n") do
    line = line:gsub("^%s*///<?%s?", ""):gsub("^%s*/%*%*<?%s?", ""):gsub("%s*%*/%s*$", ""):gsub("^%s*%*%s?", "")
    lines[#lines + 1] = line
  end
  while lines[1] == "" do table.remove(lines, 1) end
  while lines[#lines] == "" do table.remove(lines) end
  return table.concat(lines, "\n")
end

--- The annotate attributes of a cursor, as their strings.
function M.annotations(c)
  local result = {}
  for _, child in ipairs(M.children(c)) do
    if M.kind_of(child) == M.kind.AnnotateAttr then result[#result + 1] = M.spelling(child) end
  end
  return result
end

--- The fully qualified name of a declaration: "wxl::Unit", "std::basic_string".
function M.qualified_name(c)
  local parts = {}
  while lib.clang_Cursor_isNull(c) == 0 do
    local k = M.kind_of(c)
    if k == M.kind.TranslationUnit or k >= FIRST_INVALID_KIND and k <= LAST_INVALID_KIND then break end
    local name = M.spelling(c)
    if name ~= "" then table.insert(parts, 1, name) end
    c = lib.clang_getCursorSemanticParent(c)
  end
  return table.concat(parts, "::")
end

--- Parses `file` with clang's arguments `args`. Returns the translation unit, or nil and the
--- errors as text.
function M.parse(file, args)
  local index = lib.clang_createIndex(0, 0)
  local argv = ffi.new("const char*[?]", #args, args)
  local tu = ffi.new("CXTranslationUnit[1]")
  local code = lib.clang_parseTranslationUnit2(index, file, argv, #args, nil, 0, SKIP_FUNCTION_BODIES, tu)
  if code ~= 0 then return nil, ("libclang could not parse %s (error %d)"):format(file, code) end
  local errors = {}
  for i = 0, lib.clang_getNumDiagnostics(tu[0]) - 1 do
    local d = lib.clang_getDiagnostic(tu[0], i)
    if lib.clang_getDiagnosticSeverity(d) >= DIAGNOSTIC_ERROR then
      errors[#errors + 1] = M.string(lib.clang_formatDiagnostic(d, lib.clang_defaultDiagnosticDisplayOptions()))
    end
    lib.clang_disposeDiagnostic(d)
  end
  if #errors > 0 then return nil, table.concat(errors, "\n") end
  return { tu = tu[0], index = index, cursor = lib.clang_getTranslationUnitCursor(tu[0]) }
end

return M
