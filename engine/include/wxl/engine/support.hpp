// Engine structures of World of Warcraft 3.3.5a (12340): what the generated headers rest on. See
// sdk/engine/NOTICE.md.
//
// Written by hand: the namespace and architecture switches, and the fork's small templates (model
// arrays and tracks, Storm's lists and arrays), whose layout does not depend on their argument.
// The generated headers hold every instantiation they embed to the fork's size and offsets.

#pragma once

#include <cstddef>
#include <cstdint>

#if defined(_M_IX86) || defined(__i386__)
#define WXL_ENGINE_X86 1
#elif defined(_M_X64) || defined(__x86_64__)
#define WXL_ENGINE_X86 0
#else
#error "wxl/engine describes the 3.3.5a client on x86 (Wow.exe 12340) and x64 (the recompiled client) only"
#endif

// offsetof on a struct with a base is conditionally supported; MSVC and clang-cl both answer it.
#if defined(__clang__)
#define WXL_ENGINE_BEGIN                                                                           \
    _Pragma("clang diagnostic push") _Pragma("clang diagnostic ignored \"-Winvalid-offsetof\"")    \
    namespace wxl::engine {
#define WXL_ENGINE_END                                                                             \
    }                                                                                              \
    _Pragma("clang diagnostic pop")
#else
#define WXL_ENGINE_BEGIN namespace wxl::engine {
#define WXL_ENGINE_END   }
#endif

WXL_ENGINE_BEGIN

struct fixed16;

/**
 * @brief An array of a model file: a count, and where its elements are.
 *
 * In the file, `m_offset` counts bytes from the start of the file. Once the client has parsed the
 * model it no longer does, and the two clients differ:
 *   - the stock client (x86) holds the elements' address in it;
 *   - the recompiled client (x64) holds their distance from the array itself, which a 32-bit field
 *     can carry where a 64-bit address could not.
 * Data(), operator[] and Set() read and write it as the client the mod runs on does.
 */
template <class T>
struct M2Array
{
    uint32_t m_count;
    uint32_t m_offset;

    T* Data() const
    {
#if WXL_ENGINE_X86
        return reinterpret_cast<T*>(static_cast<uintptr_t>(m_offset));
#else
        return reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(this) + static_cast<intptr_t>(static_cast<int32_t>(m_offset)));
#endif
    }

    template <class U = T>
    U& operator[](uint32_t index) const
    {
        return Data()[index];
    }

    /// Points the array at @p data, which the client then reads as its elements.
    void Set(const T* data)
    {
#if WXL_ENGINE_X86
        m_offset = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(data));
#else
        m_offset = data ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(data) - reinterpret_cast<uintptr_t>(this)) : 0;
#endif
    }
};

/// The part every animated value of a model shares: its interpolation, its global loop, and the
/// times of its keys per sequence.
struct M2TrackBase
{
    uint16_t                    m_trackType;
    uint16_t                    m_loopIndex;
    M2Array<M2Array<uint32_t> > m_sequenceTimes;
};

/// An animated value: its keys per sequence.
template <class T>
struct M2Track : M2TrackBase
{
    M2Array<M2Array<T> > m_sequenceKeys;
};

/// A particle's track: one key list for the whole life of the particle.
template <class T>
struct M2PartTrack
{
    M2Array<fixed16> m_times;
    M2Array<T>       m_values;
};

/// A key of a spline track: its value and the two tangents.
template <class T>
struct M2SplineKey
{
    T m_value;
    T m_inTan;
    T m_outTan;
};

/// Storm's intrusive link: the previous link, and the next object (or a tagged end).
template <class T>
struct TSLink
{
    TSLink<T>* m_prevLink;
    T*         m_next;
};

/// Storm's intrusive list: where the link sits in an object, and the list's end link.
template <class T>
struct TSList
{
    ptrdiff_t m_linkoffset;
    TSLink<T> m_terminator;
};

/// A list whose link sits at a fixed place in its objects.
template <class T, int Offset>
struct TSExplicitList : TSList<T>
{
};

/// An object of an intrusive list whose link comes first.
template <class T>
struct TSLinkedNode
{
    TSLink<T> m_link;
};

/// Storm's array: its capacity, its count and its elements.
template <class T>
struct TSBaseArray
{
    uint32_t m_alloc;
    uint32_t m_count;
    T*       m_data;
};

template <class T>
struct TSFixedArray : TSBaseArray<T>
{
};

/// An array that grows by `m_chunk` elements.
template <class T>
struct TSGrowableArray : TSFixedArray<T>
{
    uint32_t m_chunk;
};

WXL_ENGINE_END
