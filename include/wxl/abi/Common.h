// SPDX-License-Identifier: GPL-3.0-or-later
// The toolchain layer in plain C: one spelling for what MSVC, clang and GCC each write differently.

#ifndef WXL_COMMON_H
#define WXL_COMMON_H

// --- Target: the 32-bit Windows client ----------------------------------------------------------
#if !defined(_WIN32) || defined(_WIN64) || defined(__x86_64__)
#error "WarcraftXL targets the 32-bit Windows client: build with an x86 Windows toolchain (-A Win32)"
#endif
#define WXL_PLATFORM_WINDOWS 1
#define WXL_ARCH_X86         1

// --- Compiler: exactly one WXL_COMPILER_* is 1; clang-cl is clang with WXL_MSVC_FRONTEND ---------
#if defined(__clang__)
#define WXL_COMPILER_CLANG 1
#define WXL_COMPILER_GCC   0
#define WXL_COMPILER_MSVC  0
#elif defined(__GNUC__)
#define WXL_COMPILER_CLANG 0
#define WXL_COMPILER_GCC   1
#define WXL_COMPILER_MSVC  0
#elif defined(_MSC_VER)
#define WXL_COMPILER_CLANG 0
#define WXL_COMPILER_GCC   0
#define WXL_COMPILER_MSVC  1
#else
#error "unsupported compiler: MSVC, clang or GCC"
#endif

#if defined(_MSC_VER)
#define WXL_MSVC_FRONTEND 1
#else
#define WXL_MSVC_FRONTEND 0
#endif

#if defined(__MINGW32__) || defined(__MINGW64__)
#define WXL_MINGW 1
#else
#define WXL_MINGW 0
#endif

#if WXL_COMPILER_MSVC
#define WXL_COMPILER_NAME "msvc"
#elif WXL_COMPILER_CLANG && WXL_MSVC_FRONTEND
#define WXL_COMPILER_NAME "clang-cl"
#elif WXL_COMPILER_CLANG
#define WXL_COMPILER_NAME "clang"
#else
#define WXL_COMPILER_NAME "gcc"
#endif

// --- Preprocessor helpers -----------------------------------------------------------------------
#define WXL_STRINGIFY_(x) #x
#define WXL_STRINGIFY(x)  WXL_STRINGIFY_(x)
#define WXL_CONCAT_(a, b) a##b
#define WXL_CONCAT(a, b)  WXL_CONCAT_(a, b)

#if WXL_MSVC_FRONTEND
#define WXL_PRAGMA(x) __pragma(x)
#else
#define WXL_PRAGMA(x) _Pragma(#x)
#endif

// --- Linkage ------------------------------------------------------------------------------------
#define WXL_EXPORT __declspec(dllexport)
#define WXL_IMPORT __declspec(dllimport)

#if defined(__cplusplus)
#define WXL_EXTERN_C       extern "C"
#define WXL_EXTERN_C_BEGIN extern "C" {
#define WXL_EXTERN_C_END   }
#else
#define WXL_EXTERN_C
#define WXL_EXTERN_C_BEGIN
#define WXL_EXTERN_C_END
#endif

// --- Calling conventions: part of every x86 signature ------------------------------------------
#define WXL_CDECL    __cdecl
#define WXL_STDCALL  __stdcall
#define WXL_THISCALL __thiscall
#define WXL_FASTCALL __fastcall

// --- Function attributes ------------------------------------------------------------------------
#if WXL_MSVC_FRONTEND
#define WXL_FORCEINLINE __forceinline
#define WXL_NOINLINE    __declspec(noinline)
#define WXL_NAKED       __declspec(naked)
#define WXL_NORETURN    __declspec(noreturn)
#define WXL_RESTRICT    __restrict
#else
#define WXL_FORCEINLINE inline __attribute__((always_inline))
#define WXL_NOINLINE    __attribute__((noinline))
#define WXL_NAKED       __attribute__((naked))
#define WXL_NORETURN    __attribute__((noreturn))
#define WXL_RESTRICT    __restrict__
#endif

// --- Hints to the optimiser ---------------------------------------------------------------------
#if WXL_COMPILER_MSVC
#define WXL_LIKELY(x)       (x)
#define WXL_UNLIKELY(x)     (x)
#define WXL_ASSUME(x)       __assume(x)
#define WXL_UNREACHABLE()   __assume(0)
#else
#define WXL_LIKELY(x)       __builtin_expect(!!(x), 1)
#define WXL_UNLIKELY(x)     __builtin_expect(!!(x), 0)
#define WXL_ASSUME(x)       do { if (!(x)) __builtin_unreachable(); } while (0)
#define WXL_UNREACHABLE()   __builtin_unreachable()
#endif

// --- Intrinsics: the return address and its stack slot (GCC needs WXL_KEEP_FRAME_POINTER) --------
#if WXL_MSVC_FRONTEND
#include <intrin.h>
#pragma intrinsic(_ReturnAddress)
#pragma intrinsic(_AddressOfReturnAddress)
#define WXL_RETURN_ADDRESS()            _ReturnAddress()
#define WXL_ADDRESS_OF_RETURN_ADDRESS() _AddressOfReturnAddress()
#define WXL_DEBUGBREAK()                __debugbreak()
#elif WXL_COMPILER_CLANG
#include <intrin.h>
#define WXL_RETURN_ADDRESS()            __builtin_return_address(0)
#define WXL_ADDRESS_OF_RETURN_ADDRESS() _AddressOfReturnAddress()
#define WXL_DEBUGBREAK()                __builtin_debugtrap()
#else
#include <intrin.h>
#define WXL_RETURN_ADDRESS()            __builtin_return_address(0)
#define WXL_ADDRESS_OF_RETURN_ADDRESS() ((void*)((char*)__builtin_frame_address(0) + sizeof(void*)))
#define WXL_DEBUGBREAK()                __asm__ __volatile__("int3")
#endif

// --- Code generation: functions compiled as written; a frame pointer kept (clang: per file) -------
#if WXL_COMPILER_MSVC
#define WXL_OPTIMIZE_OFF       __pragma(optimize("", off))
#define WXL_OPTIMIZE_ON        __pragma(optimize("", on))
#define WXL_KEEP_FRAME_POINTER
#elif WXL_COMPILER_CLANG
#define WXL_OPTIMIZE_OFF       _Pragma("clang optimize off")
#define WXL_OPTIMIZE_ON        _Pragma("clang optimize on")
#define WXL_KEEP_FRAME_POINTER
#else
#define WXL_OPTIMIZE_OFF       _Pragma("GCC push_options") _Pragma("GCC optimize(\"O0\")")
#define WXL_OPTIMIZE_ON        _Pragma("GCC pop_options")
#define WXL_KEEP_FRAME_POINTER __attribute__((optimize("no-omit-frame-pointer")))
#endif

// --- Layout: WXL_PACK_PUSH(1) ... WXL_PACK_POP around a struct that mirrors client memory --------
#define WXL_PACK_PUSH(n) WXL_PRAGMA(pack(push, n))
#define WXL_PACK_POP     WXL_PRAGMA(pack(pop))

#if defined(__cplusplus)
#define WXL_ALIGNAS(n)     alignas(n)
#define WXL_THREAD_LOCAL   thread_local
#elif WXL_MSVC_FRONTEND
#define WXL_ALIGNAS(n)     __declspec(align(n))
#define WXL_THREAD_LOCAL   __declspec(thread)
#else
#define WXL_ALIGNAS(n)     __attribute__((aligned(n)))
#define WXL_THREAD_LOCAL   __thread
#endif

// --- Diagnostics: the signature as a string; warnings silenced per compiler around a header -------
#if WXL_MSVC_FRONTEND
#define WXL_FUNCTION_SIGNATURE __FUNCSIG__
#else
#define WXL_FUNCTION_SIGNATURE __PRETTY_FUNCTION__
#endif

#if WXL_COMPILER_MSVC
#define WXL_WARNINGS_PUSH             __pragma(warning(push))
#define WXL_WARNINGS_POP              __pragma(warning(pop))
#define WXL_WARNING_DISABLE_MSVC(n)   __pragma(warning(disable : n))
#define WXL_WARNING_DISABLE_CLANG(w)
#define WXL_WARNING_DISABLE_GCC(w)
#elif WXL_COMPILER_CLANG
#define WXL_WARNINGS_PUSH             _Pragma("clang diagnostic push")
#define WXL_WARNINGS_POP              _Pragma("clang diagnostic pop")
#define WXL_WARNING_DISABLE_MSVC(n)
#define WXL_WARNING_DISABLE_CLANG(w)  WXL_PRAGMA(clang diagnostic ignored w)
#define WXL_WARNING_DISABLE_GCC(w)
#else
#define WXL_WARNINGS_PUSH             _Pragma("GCC diagnostic push")
#define WXL_WARNINGS_POP              _Pragma("GCC diagnostic pop")
#define WXL_WARNING_DISABLE_MSVC(n)
#define WXL_WARNING_DISABLE_CLANG(w)
#define WXL_WARNING_DISABLE_GCC(w)    WXL_PRAGMA(GCC diagnostic ignored w)
#endif

// --- COM identities: give both WXL_UUID_ATTR on the type and WXL_UUID_DECL after it --------------
#if WXL_MSVC_FRONTEND
#define WXL_UUID_ATTR(str) __declspec(uuid(str))
#define WXL_UUID_DECL(type, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8)
#elif defined(__CRT_UUID_DECL)
#define WXL_UUID_ATTR(str)
#define WXL_UUID_DECL(type, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
    __CRT_UUID_DECL(type, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8)
#else
#define WXL_UUID_ATTR(str)
#define WXL_UUID_DECL(type, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8)
#endif

#endif // WXL_COMMON_H
