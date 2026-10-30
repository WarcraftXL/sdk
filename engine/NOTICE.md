# Notice

The headers under `include/wxl/engine/` describe structures of the World of Warcraft 3.3.5a client
(`Wow.exe`, build 12340), a work owned by Blizzard Entertainment. They are generated from the
client fork ([WarcraftXL/client](https://github.com/WarcraftXL/client)), a fork of
[SatyPardus/wowdecomp](https://github.com/SatyPardus/wowdecomp), which is a decompilation of that
client.

Upstream carries no license, and a decompilation cannot be licensed by the party that produced it.
These headers are therefore **not** covered by the SDK's MIT license, and no copyright grant is made
or implied over them: they are published, like the client fork, as a description a mod needs to
work with the client its user already owns. They hold no game code and no game asset: names, types
and offsets.

`include/wxl/engine/support.hpp` is written by hand but follows the fork's templates (`M2Array`,
`TSList`...), so it falls under this notice too. What is ours carries its own SPDX line and is MIT
like the rest of the SDK: `include/wxl/engine/check.hpp`, and the generator, `core/tools/engine/`.
