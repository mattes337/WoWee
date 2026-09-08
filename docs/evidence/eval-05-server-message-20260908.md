# EVAL-05 WotLK server-message audit

Date: 2026-09-08

Client source baseline: `4a73bc7195e11cae617c26f407ae76ca728146e1`

Server source: pinned local AzerothCore checkout
`G:/azerothcore-wotlk` at `798d08c58a8e00b7937050963119bb857344b640`.
This is a source-contract audit. The separately pinned emulator actually used
by the runtime work reports `a5e0e6b8f2bf+`; it is a different revision and no
packet was sent to it for this audit.

## Contract

- `src/server/game/Server/Protocol/Opcodes.h` assigns
  `SMSG_CHAT_SERVER_MESSAGE` wire opcode `0x291`.
- `src/server/game/World/IWorld.h` defines ServerMessages.dbc ids: shutdown
  time 1, restart time 2, string 3, shutdown cancelled 4, restart cancelled 5.
- `src/server/game/Server/Packets/ChatPackets.cpp` writes the id as `int32`
  followed by the optional null-terminated string.
- `src/server/game/Server/WorldSessionMgr.cpp` supplies that string only when
  the id is at most 3. Cancellation packets therefore contain exactly the
  four-byte id.
- Extracted stock FrameXML routes system text through `CHAT_MSG_SYSTEM` and
  defines `SERVER_MESSAGE_PREFIX` as `[SERVER]`; it registers no event for this
  world-server packet and does not decode the wire body. Its similarly named
  `KNOWLEDGE_BASE_SERVER_MESSAGE` event reads `KBSystem_GetServerNotice()` for
  the support panel and is unrelated.

The mapped wowee handler previously discarded the type and called every packet
an announcement. It also dropped types 4 and 5 because it required a nonempty
string. The shared decoder now preserves the five typed meanings. The legacy
logical `SMSG_SERVER_MESSAGE` registration uses the same decoder, but remains
unmapped in the WotLK profile because it is not a second WotLK wire opcode.
Unknown future ids retain the old generic server-text fallback when they carry
a valid C string. This change preserves the existing UI-error call for timer
types, but does not claim stock-equivalent banner placement, localization, or
full UI warning acceptance; those require a runtime presentation scenario.

## Validation

```text
cmake --build build-input-ci-validation --target test_server_message --config Release -j 2
ctest --test-dir build-input-ci-validation -C Release -R ^server_message$ --output-on-failure
```

The test uses only synthetic packet bodies and makes no server connection or
transmission. An adapter of the old mapped-handler semantics demonstrates that
the same timer inputs were all labelled announcements and both cancellation
inputs were dropped before the change. The regression covers all five
AzerothCore type values, the absent string on cancellation packets, short and
unterminated bodies, absent timer text, and the unknown-id fallback.
