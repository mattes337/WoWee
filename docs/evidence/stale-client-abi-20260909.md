# Stale client `GameHandler` ABI evidence — 2026-09-09

## Runtime failures

Live-login run 24 used client SHA-256
`e8aa2c809339e406d8401b23457ba9936d592e4a03e9f0e782057561404073dd`.
The default single-sample production character preview rendered and its capture
succeeded. On quit, after SDL shutdown had begun, the process exited with
`3221226356` (`0xC0000374`, heap corruption); shutdown and trace completion were
not recorded. Its artifacts are under
`D:/wowee-private-live-login/live-login-24`.

Debugger run 25 stopped earlier with `0xC0000005` in
`GameHandler::handlePacket` during a map/list lookup. This is an access violation,
not a captured heap diagnostic, and does not by itself locate the original
memory corruption.

The preceding client, SHA-256
`11c57ac6a829970fdcab3bc949b554e9ed74f4d0347788ffa6e57a5d1e5da616`,
completed the matched production single-sample run 23 cleanly.

## Proven mixed ABI build

Commit `b1883bc67` added `CalendarInviteViewState` as a value member at the end of
`GameHandler` in `include/game/game_handler.hpp`. This increases the concrete
class size. `src/core/application.cpp` allocates the class with
`std::make_unique<game::GameHandler>`, so that translation unit compiles the
allocation size into `application.obj`.

The client objects used for run 24 have incompatible build times:

- `application.obj`: 2026-09-08 19:47:28 UTC, before the member existed.
- `game_handler_packets.obj`: 2026-09-09 07:06:11 UTC.
- `game_handler.obj`: 2026-09-09 07:06:12 UTC.
- `game_handler.hpp`: 2026-09-09 07:04:18 UTC.

The current client `Cl.items.tlog` maps `application.cpp` to the old object, but
the current `CL.read.1.tlog` has no dependency section for `application.cpp` and
therefore no surviving `application.cpp -> game_handler.hpp` read edge. MSBuild
reused an allocator compiled for the old layout while linking constructors and
packet handling compiled for the new layout.

The independently rebuilt `framexml_run` does not have this mismatch:
`framexml_run.obj`, its `application.obj`, and its `game_handler.obj` were all
built on 2026-09-09 around 07:09 UTC. Its current compiler read tlog contains
both `application.cpp` and `framexml_run.cpp` dependency sections for
`game_handler.hpp`.

## Interpretation and recovery

The mixed class-layout compilation is proven by the source change, object
timestamps, allocator site, and missing compiler-read edge. An old-size
allocation used by a new-layout constructor is expected to write beyond its
allocation and can explain both observed failures. That causal link remains an
inference until a clean-layout client rebuild passes the same runtime case; run
25 did not capture the originating heap write.

No additional Calendar source fix is justified by this evidence. Once the input
matrix releases the binary, discard all 428 client objects and the client
compiler/linker tlogs, rebuild the client from a single consistent source state,
and repeat the production single-sample fixture. After compiler tracking records
have been lost or truncated, ABI-changing header updates require a clean rebuild
rather than another incremental link.
