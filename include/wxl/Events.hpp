// SPDX-License-Identifier: GPL-3.0-or-later
// The bus events: ids from events/Events.def, the args of each, and the tags On<Tag> takes (Overrides.def).

#pragma once

#include "wxl/abi/Common.h"
#include "wxl/events/AssetArgs.hpp"
#include "wxl/events/ExtensionArgs.hpp"
#include "wxl/events/ObjectArgs.hpp"
#include "wxl/events/RenderArgs.hpp"
#include "wxl/events/UnitArgs.hpp"
#include "wxl/events/WorldArgs.hpp"

#include <concepts>
#include <cstddef>
#include <cstdint>

namespace wxl::events
{
    /// The events by table id; Count is one past the highest.
    enum class Event : uint32_t
    {
#define WXL_EVENT(name, id, args) name = id,
#include "wxl/events/Events.def"
#undef WXL_EVENT
        Count = 0
#define WXL_EVENT(name, id, args) + 1
#include "wxl/events/Events.def"
#undef WXL_EVENT
    };

    /// The args struct of an event: `Args<Event::OnUpdate>` is UpdateArgs.
    template <Event E> struct ArgsOf;
#define WXL_EVENT(name, id, args) template <> struct ArgsOf<Event::name> { using type = args; };
#include "wxl/events/Events.def"
#undef WXL_EVENT
    template <Event E> using Args = typename ArgsOf<E>::type;

    /// The event's name, for logs; "?" past Count.
    inline constexpr const char* Name(Event e)
    {
        switch (e)
        {
#define WXL_EVENT(name, id, args) case Event::name: return #name;
#include "wxl/events/Events.def"
#undef WXL_EVENT
        default: return "?";
        }
    }

    namespace detail
    {
        constexpr bool IdsAreDense()
        {
            bool seen[static_cast<uint32_t>(Event::Count)] = {};
#define WXL_EVENT(name, id, args) if ((id) >= static_cast<uint32_t>(Event::Count) || seen[id]) return false; seen[id] = true;
#include "wxl/events/Events.def"
#undef WXL_EVENT
            return true;
        }
        static_assert(IdsAreDense(), "Events.def: ids must be 0..Count-1, each used once");
    }

    /// A handler as the bus calls it: the user pointer and the event's args struct.
    using Handler = void(WXL_CDECL*)(void* user, const void* args);

    /// Where an event that asks keeps its answer: a pointer field of its args that handlers write
    /// through, in priority order, the last word standing; a faulting handler's answer is undone.
    struct AnswerField
    {
        uint32_t offset = 0; ///< of the pointer within the args struct
        uint32_t size   = 0; ///< of what it points at; 0: the event does not ask
    };

    /// The answer field of an event: OnInput's `handled`.
    inline constexpr AnswerField AnswerOf(Event e)
    {
        switch (e)
        {
        case Event::OnInput: return { uint32_t(offsetof(InputArgs, handled)), uint32_t(sizeof(bool)) };
        default:             return {};
        }
    }
}

namespace wxl
{
    /// A map id, as OnWorldEnter and OnWorldLeave give it.
    using MapId = uint32_t;

    using events::DamageKind;
#define WXL_EVENT(name, id, args) using events::args;
#include "wxl/events/Events.def"
#undef WXL_EVENT

    /// The tag of each event, for On<Tag>: `On<Update>` is OnUpdate, with UpdateArgs.
#define WXL_OVERRIDE(Tag, params, args)                                                              \
    struct Tag                                                                                     \
    {                                                                                              \
        static constexpr events::Event kEvent = events::Event::On##Tag;                           \
        static constexpr const char*   kName  = #Tag;                                             \
        using Args                            = events::Args<kEvent>;                              \
    };
#include "wxl/Overrides.def"
#undef WXL_OVERRIDE

    namespace detail
    {
        constexpr uint32_t kOverrideRows = 0
#define WXL_OVERRIDE(Tag, params, args) + 1
#include "wxl/Overrides.def"
#undef WXL_OVERRIDE
            ;
        static_assert(kOverrideRows == static_cast<uint32_t>(events::Event::Count),
                      "Overrides.def: one row per row of events/Events.def");

        /// What an event tag is: a type naming one event and its args.
        template <class T>
        concept EventTag = requires {
            { T::kEvent } -> std::convertible_to<events::Event>;
            typename T::Args;
        };
    }
}
