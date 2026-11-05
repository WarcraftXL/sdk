// SPDX-License-Identifier: GPL-3.0-or-later
// The args of the world events: the tick, entering and leaving a map, input, clicks, target, sounds, chat.

#pragma once

#include "wxl/abi/Plugin.h"

#include <cstdint>

namespace wxl::events
{
    /// OnUpdate: the frame tick.
    struct UpdateArgs
    {
        float    dt;     ///< seconds since the previous frame
        uint32_t timeMs; ///< the frame timestamp
    };

    /// OnWorldEnter: the map entered.
    struct WorldEnterArgs { uint32_t mapId; };

    /// OnWorldLeave: the map being left, still loaded.
    struct WorldLeaveArgs { uint32_t mapId; };

    /// OnInput: a window message. Set `*handled` to swallow it; the game then never sees it.
    struct InputArgs { uint32_t message; uintptr_t wparam; uintptr_t lparam; bool* handled; };

    /// OnWorldClick: a mouse click that reached the world (not one a UI handler consumed).
    struct WorldClickArgs
    {
        uint32_t message;
        int      hitType; ///< 2 an M2 or doodad, 3 terrain or WMO
        float    x;
        float    y;
        float    z;
        void*    objLo;   ///< low half of the engine object handle, null for terrain
        void*    objHi;   ///< high half, null for terrain
    };

    /// OnTargetChanged: the applied target is read through wxl::game.
    struct TargetChangedArgs { void* scriptState; };

    /// OnSoundPlay, read-only: the sound id or name is on the script stack.
    struct SoundPlayArgs { void* scriptState; };

    /// OnChatMessage, read-only: the line as the client records it; strings valid during the call.
    struct ChatMessageArgs
    {
        int32_t     type;          ///< the CHAT_MSG_ kind: 1 say, 2 party, 6 yell, 7 whisper, 17 channel...
        const char* text;
        const char* senderName;    ///< may be empty (system messages)
        uint64_t    senderGuid;
        WXL_Handle  sender;        ///< empty when the sender is not resident
        const char* channelName;   ///< empty outside a channel
        uint32_t    zoneChannelId; ///< the zone channel's id, 0 outside one
        uint32_t    lineId;        ///< the client's line number, as the interface's chat events give it
    };
}
