// SPDX-License-Identifier: GPL-3.0-or-later
// The C++ half of the toolchain layer: the structured-exception guard and small helpers.

#pragma once

#include "wxl/abi/Common.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

// WXL_SEH_TRY(wxl::seh::Catch::Any) { ... } WXL_SEH_EXCEPT { ... WXL_SEH_CODE() ... }: a fault in the body
// lands in the handler (a hand-made fs:[0] frame on clang and GCC; seh-check). No destructor may live in it.
namespace wxl::seh
{
    /// What a guard catches: every exception, or an access violation only (anything else keeps searching).
    enum class Catch : int { Any = 0, AccessViolation = 1 };

    inline bool Accepts(Catch what, DWORD code) noexcept
    {
        return what == Catch::Any || code == EXCEPTION_ACCESS_VIOLATION;
    }

    /// What a guard caught.
    struct Fault
    {
        DWORD     code    = 0;     ///< the exception code: 0xC0000005 an access violation
        uintptr_t address = 0;     ///< where it was raised
        uintptr_t target  = 0;     ///< for an access violation, the address read or written
        bool      write   = false; ///< for an access violation, whether it was a write
    };

    inline void Record(const EXCEPTION_RECORD* record, Fault& fault) noexcept
    {
        fault.code    = record->ExceptionCode;
        fault.address = reinterpret_cast<uintptr_t>(record->ExceptionAddress);
        if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2)
        {
            fault.write  = record->ExceptionInformation[0] == 1;
            fault.target = static_cast<uintptr_t>(record->ExceptionInformation[1]);
        }
    }
}

#if WXL_COMPILER_MSVC

namespace wxl::seh
{
    /// The state a native guard carries from its __try to its filter. Trivially destructible.
    struct Native
    {
        Catch what;
        Fault fault;

        int Filter(EXCEPTION_POINTERS* info) noexcept
        {
            if (!Accepts(what, info->ExceptionRecord->ExceptionCode)) return EXCEPTION_CONTINUE_SEARCH;
            Record(info->ExceptionRecord, fault);
            return EXCEPTION_EXECUTE_HANDLER;
        }
    };
}

#define WXL_SEH_TRY(what) if (::wxl::seh::Native wxl_seh{(what), {}}; true) __try
#define WXL_SEH_EXCEPT __except (wxl_seh.Filter(GetExceptionInformation()))
#define WXL_SEH_CODE() (wxl_seh.fault.code)
#define WXL_SEH_FAULT() (wxl_seh.fault)

#else

/// The dispatcher's entry for a guard frame, a C function so the image's SafeSEH table can name it.
extern "C" inline EXCEPTION_DISPOSITION __cdecl wxl_seh_x86_handle(EXCEPTION_RECORD* record, void* establisher,
                                                                    CONTEXT*, void*);

namespace wxl::seh::x86
{
    /// The head of the thread's handler chain, fs:[0].
    inline void* ChainHead() noexcept
    {
        void* head;
        __asm__ volatile("movl %%fs:0, %0" : "=r"(head));
        return head;
    }

    inline void SetChainHead(void* head) noexcept
    {
        __asm__ volatile("movl %0, %%fs:0" : : "r"(head) : "memory");
    }

    /// The words __builtin_setjmp keeps.
    constexpr int kJumpBufferWords = 5;

    /// One registration on the thread's handler chain: the EXCEPTION_REGISTRATION_RECORD the
    /// dispatcher walks, then ours.
    struct Frame
    {
        Frame*         prev;
        void*          handler;
        Catch          what;
        Fault          fault;
        volatile bool  linked;
        void*          jump[kJumpBufferWords];

        /// Links the frame; the guard's __builtin_setjmp follows at once.
        explicit Frame(Catch c) noexcept
            : prev(static_cast<Frame*>(ChainHead())), handler(reinterpret_cast<void*>(&wxl_seh_x86_handle)),
              what(c), fault(), linked(true)
        {
            SetChainHead(this);
        }
        ~Frame() { Unlink(); }
        Frame(const Frame&)            = delete;
        Frame& operator=(const Frame&) = delete;

        void Unlink() noexcept
        {
            if (!linked) return;
            linked = false;
            SetChainHead(prev);
        }
    };
}

/// The first-pass filter of a guard frame: accepting unwinds the frames below, unlinks ours and jumps
/// to the guard's handler. `used`, so the SafeSEH entry below always has its symbol.
extern "C" inline __attribute__((used)) EXCEPTION_DISPOSITION __cdecl wxl_seh_x86_handle(
    EXCEPTION_RECORD* record, void* establisher, CONTEXT*, void*)
{
    using ::wxl::seh::x86::Frame;
    if (record->ExceptionFlags & (EXCEPTION_UNWINDING | EXCEPTION_EXIT_UNWIND)) return ExceptionContinueSearch;
    Frame* frame = static_cast<Frame*>(establisher);
    if (!::wxl::seh::Accepts(frame->what, record->ExceptionCode)) return ExceptionContinueSearch;
    ::wxl::seh::Record(record, frame->fault);
    RtlUnwind(frame, nullptr, record, nullptr);
    frame->Unlink();
    __builtin_longjmp(frame->jump, 1);
}
__asm__(".safeseh _wxl_seh_x86_handle");

#define WXL_SEH_TRY(what) \
    if (::wxl::seh::x86::Frame wxl_seh_frame(what); __builtin_setjmp(wxl_seh_frame.jump) == 0)
#define WXL_SEH_EXCEPT else
#define WXL_SEH_CODE() (wxl_seh_frame.fault.code)
#define WXL_SEH_FAULT() (wxl_seh_frame.fault)

#endif

namespace wxl
{
    /// A struct that mirrors client memory is pinned to its size at compile time.
    template <class T, std::size_t N>
    inline constexpr bool SizeIs = sizeof(T) == N;

    /// The integer behind an enum class, for flags and client fields.
    template <class E>
    constexpr std::underlying_type_t<E> ToUnderlying(E e) noexcept
    {
        return static_cast<std::underlying_type_t<E>>(e);
    }
}

#define WXL_STATIC_ASSERT_SIZE(type, size) \
    static_assert(::wxl::SizeIs<type, size>, #type " must be " #size " bytes to match the client")
