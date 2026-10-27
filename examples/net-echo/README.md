# Net echo

A Lua mod talking to a module of the world server through two opcodes of its own, with `wxl.net`,
on the stock client and the recompiled one:

| Number | Name | Way | Body |
|---|---|---|---|
| `0x521` | `CMSG_ECHO_PING` | client to server | `u32 seq`, `u32 sent_ms`, a string |
| `0x522` | `SMSG_ECHO_PONG` | server to client | the same, sent back |

The numbers are declared in `mod.json` (`"net": { "opcodes": [ "0x521-0x522" ] }`), so no other mod
can take them. The server's module sends one `SMSG_ECHO_PONG` with `seq` 0 when the player enters
the world: until it has, `wxl.net.Send` refuses (`net.Heard()` is false), because a server that
does not know `0x521` closes the connection. Then the mod pings every five seconds and logs each
echo with its round trip.

Everything in the body is little-endian, strings end with a zero byte, as the game's own messages.
On the server, the module answers `CMSG_ECHO_PING` by sending its body back under `0x522`, and
sends the hello on login.

Copy the folder into the game's `Mods` folder. Without a server module answering, the mod only
waits: nothing is sent.
