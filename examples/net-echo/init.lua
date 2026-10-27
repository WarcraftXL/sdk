-- net-echo: two opcodes of the mod's own, a ping it sends and an echo the server sends back.
--
--   CMSG_ECHO_PING 0x521  u32 seq, u32 sent_ms, string text     the client's ping
--   SMSG_ECHO_PONG 0x522  u32 seq, u32 sent_ms, string text     the server's echo of it
--
-- A server module that knows these numbers sends one SMSG_ECHO_PONG with seq 0 when the player
-- enters the world: its hello. Until it has, wxl.net refuses to send (a server that does not know
-- 0x521 would close the connection), so this mod waits for it, then pings every five seconds and
-- logs each round trip.

local net = wxl.net
local CMSG_ECHO_PING, SMSG_ECHO_PONG = 0x521, 0x522

assert(net.Claim(CMSG_ECHO_PING))
assert(net.Claim(SMSG_ECHO_PONG))

local function now() return math.floor(os.clock() * 1000) end

local seq, waited = 0, 0

net.SetHandler(SMSG_ECHO_PONG, function(r)
    local n, sent, text = r:U32(), r:U32(), r:String(255)
    if not r:Ok() then return end -- shorter than an echo: the reader says so, the log too
    if n == 0 then
        print("the server knows the echo: pings start")
    else
        print(string.format("echo %d: %q in %d ms", n, text, now() - sent))
    end
end)

Observe("WorldScript", "OnUpdate", function(delta)
    waited = waited + delta
    if waited < 5 or not net.Heard() then return end
    waited = 0
    seq = seq + 1
    local ok, why = net.Send(CMSG_ECHO_PING, net.Writer():U32(seq):U32(now()):String("ping " .. seq))
    if not ok then print("ping " .. seq .. " not sent: " .. tostring(why)) end
end)
