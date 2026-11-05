// SPDX-License-Identifier: GPL-3.0-or-later
// wxl::game::script: the client's Lua stack and script registry, for script functions.

#pragma once

#include <cstddef>

#include "wxl/game/Binding.hpp"
#include "wxl/offsets/CSimpleTexture.hpp"
#include "wxl/offsets/FrameScript_Object.hpp"
#include "wxl/offsets/free/F.hpp"
#include "wxl/offsets/free/FrameScript.hpp"
#include "wxl/offsets/free/O.hpp"
#include "wxl/offsets/free/lua.hpp"

/// Lua stack access for script functions. Indices are one-based; a method call puts its object at 1.
namespace wxl::game::script
{
    namespace off = wxl::offsets;

    /// The object a script method was called on; a script error when it is not of class `typeId`.
    /// FrameScript_GetObjectThis takes the state in ESI and the type id on the stack.
    inline void* ObjectThis(void* state, int typeId)
    {
        void* self;
        const uintptr_t fn = off::FrameScript_GetObjectThis;
        __asm
        {
            push esi
            mov esi, state
            push typeId
            call fn
            add esp, 4
            pop esi
            mov self, eax
        }
        return self;
    }

    /// The script type id at `objectTypeGlobal` (a class's s_objectType), claimed from the shared
    /// counter when unset, as the client does on first use.
    inline int ClassTypeId(uintptr_t objectTypeGlobal)
    {
        if (Read<int>(objectTypeGlobal) == 0)
        {
            const int id = Read<int>(off::FrameScript_Object::s_objectTypes) + 1;
            Write<int>(off::FrameScript_Object::s_objectTypes, id);
            Write<int>(objectTypeGlobal, id);
        }
        return Read<int>(objectTypeGlobal);
    }

    /// A script function, as the method tables and the global registry hold it.
    using Function = int(__cdecl*)(void* state);

    /// Where the client refuses a script callback outside its image ("Invalid function pointer");
    /// a detour skips the original only for its own pointers.
    constexpr uintptr_t kValidateCallbackSeam = off::OsValidateFunctionPointer;
    using ValidateCallbackFn = off::OsValidateFunctionPointerFn;

    /// Loads a .toc and its files: once per script context, after it is built, before any OnLoad.
    constexpr uintptr_t kInterfaceLoadSeam = off::FrameXML_CreateFrames;
    using InterfaceLoadFn = off::FrameXML_CreateFramesFn;

    /// Number of values on the stack: at a call's start, the arguments (with the object of a method call).
    inline int ArgCount(void* state)
    { return Native<off::lua_gettopFn>(off::lua_gettop)(state); }

    /// True for a number or a string convertible to one.
    inline bool IsNumber(void* state, int index)
    { return Native<off::lua_isnumberFn>(off::lua_isnumber)(state, index) != 0; }

    /// True for a string or a number convertible to one.
    inline bool IsString(void* state, int index)
    { return Native<off::lua_isstringFn>(off::lua_isstring)(state, index) != 0; }

    /// The argument as a number, or 0 when not convertible.
    inline double ToNumber(void* state, int index)
    { return Native<off::lua_tonumberFn>(off::lua_tonumber)(state, index); }

    /// The argument as a string, or null; owned by the state and valid only until the call returns.
    inline const char* ToString(void* state, int index)
    { return Native<off::lua_tolstringFn>(off::lua_tolstring)(state, index, nullptr); }

    /// Lua truth: false only for nil, false and a missing argument.
    inline bool ToBoolean(void* state, int index)
    { return Native<off::lua_tobooleanFn>(off::lua_toboolean)(state, index) != 0; }

    /// The pointer a light userdata holds, or null. A frame's table holds its object at key 0.
    inline void* ToUserData(void* state, int index)
    { return Native<off::lua_touserdataFn>(off::lua_touserdata)(state, index); }

    inline void PushNumber(void* state, double value)
    { Native<off::lua_pushnumberFn>(off::lua_pushnumber)(state, value); }

    /// Pushes a C string (lua_pushstring).
    inline void PushString(void* state, const char* value)
    { Native<off::lua_pushstringFn>(off::lua_pushstring)(state, value); }

    /// Pushes a copy of `length` bytes, NUL bytes included.
    inline void PushLString(void* state, const char* bytes, size_t length)
    { Native<off::lua_pushlstringFn>(off::lua_pushlstring)(state, bytes, length); }

    inline void PushBoolean(void* state, bool value)
    { Native<off::lua_pushbooleanFn>(off::lua_pushboolean)(state, value ? 1 : 0); }

    /// Raises a script error (Lua format string). It longjmps: no destructor may be pending in the caller.
    template <class... Args>
    inline void Error(void* state, const char* format, Args... args)
    { Native<off::luaL_errorFn>(off::luaL_error)(state, format, args...); }

    constexpr int kRegistryIndex = -10000; // LUA_REGISTRYINDEX
    constexpr int kGlobalsIndex  = -10002; // LUA_GLOBALSINDEX
    constexpr int kTypeNil       = 0;
    constexpr int kTypeTable     = 5;
    constexpr int kTypeFunction  = 6;

    /// Type tag of a slot; a negative index counts from the top.
    inline int Type(void* state, int index)
    { return Native<off::lua_typeFn>(off::lua_type)(state, index); }

    /// Sets the stack top, dropping or nil-filling; a negative index counts from the top.
    inline void SetTop(void* state, int index)
    { Native<off::lua_settopFn>(off::lua_settop)(state, index); }

    inline void PushNil(void* state)
    { Native<off::lua_pushnilFn>(off::lua_pushnil)(state); }

    /// Pushes a copy of the value at `index` (lua_pushvalue).
    inline void PushValue(void* state, int index)
    { Native<off::lua_pushvalueFn>(off::lua_pushvalue)(state, index); }

    /// Pops the top value into `index`.
    inline void Replace(void* state, int index)
    { Native<off::lua_replaceFn>(off::lua_replace)(state, index); }

    /// Pops a key and pushes t[key], without __index.
    inline void RawGet(void* state, int tableIndex)
    { Native<off::lua_rawgetFn>(off::lua_rawget)(state, tableIndex); }

    /// Pushes t[n], without __index.
    inline void RawGetI(void* state, int tableIndex, int n)
    { Native<off::lua_rawgetiFn>(off::lua_rawgeti)(state, tableIndex, n); }

    /// Pops a value and a key and sets t[key] = value, without __newindex.
    inline void RawSet(void* state, int tableIndex)
    { Native<off::lua_rawsetFn>(off::lua_rawset)(state, tableIndex); }

    /// Pops a key and pushes the next key and value (start with nil); 0 at the end, pushing nothing.
    inline int Next(void* state, int tableIndex)
    { return Native<off::lua_nextFn>(off::lua_next)(state, tableIndex); }

    /// Pushes the texture metatable (__index = the texture methods); false, pushing nothing, before a context built it.
    inline bool PushTextureMetatable(void* state)
    {
        const int ref = Read<int>(off::CSimpleTexture::s_metatable);
        if (ref <= 0) return false;

        const int base = ArgCount(state);
        RawGetI(state, kRegistryIndex, ref);
        if (Type(state, -1) == kTypeTable) return true;

        SetTop(state, base);
        return false;
    }

    /// Registers a global script function. Frame methods go through wxl::game::glue::AddMethods.
    inline void Register(const char* name, Function function)
    { Native<off::FrameScript_RegisterFunctionFn>(off::FrameScript_RegisterFunction)(name, reinterpret_cast<void*>(function)); }

    /// The active Lua state, or null between contexts. A UI reload replaces it.
    inline void* Context()
    { return Native<off::FrameScript_GetContextFn>(off::FrameScript_GetContext)(); }

    /// Runs a chunk on the active context, untainted. Errors go to the client's script error handler.
    inline void Execute(const char* source, const char* chunkName)
    { Native<off::FrameScript_ExecuteFn>(off::FrameScript_Execute)(source, chunkName, nullptr); }

    /// lua_pcall: 0 with `resultCount` results (-1: all), else the error object; `errorHandler` 0 for none.
    inline int PCall(void* state, int argCount, int resultCount, int errorHandler)
    { return Native<off::lua_pcallFn>(off::lua_pcall)(state, argCount, resultCount, errorHandler); }

    /// Pops the top value into a table and returns its key, or -1 for nil.
    inline int Ref(void* state, int tableIndex)
    { return Native<off::luaL_refFn>(off::luaL_ref)(state, tableIndex); }

    inline void Unref(void* state, int tableIndex, int ref)
    { Native<off::luaL_unrefFn>(off::luaL_unref)(state, tableIndex, ref); }

    /// Pushes the registry value `ref` (lua_rawgeti on the registry).
    inline void PushRef(void* state, int ref)
    { RawGetI(state, kRegistryIndex, ref); }

    /// Compiles `format` with its %s replaced by `body`; the registry key of what it returns, or -1
    /// (errors through `status` when not null).
    inline int CompileFunction(const char* chunkName, const char* format, const char* body,
                               void* status)
    {
        return Native<off::FrameScript_CompileFunctionFn>(off::FrameScript_CompileFunction)(
            chunkName, format, body, status);
    }
}
