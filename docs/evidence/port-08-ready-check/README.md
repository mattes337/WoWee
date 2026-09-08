# PORT-08 ready-check state evidence

The donor selection is bounded to its per-GUID query precedence and response
lifetime: recorded answer, otherwise waiting during an active check, otherwise
nil. The previous WoWee Lua binding always returned nil and its FINISHED handler
deleted the results. The implementation now keeps server answers independently
of the locally dismissed question, retains them through FINISHED, and clears
them on the next start or group departure. Repeated confirmations replace an
answer and no longer inflate the summary counts.

`provenance.json` identifies the donor commit, complete source-file hashes, local
modification status, and exact source excerpts. These excerpts are snapshots of
the inspected working tree, including the locally modified logic-thread source;
the sibling donor was never modified. Its workspace declares MIT in Cargo.toml;
no separate LICENSE/NOTICE was found. The C++ implementation adapts the behavior
to the existing WoWee SocialHandler and token resolver.

`tests/test_ready_check_state.cpp` covers no-check/invalid GUID, waiting, accept,
decline, unanswered finish, answer retention, next-check clearing, repeated and
changed confirmations, and group-departure clearing. This checks the production
state type used by the packet handlers and queried by the Lua binding. It does
not execute the full packet dispatcher, Lua runtime, or rendered interface.

Reproduce the focused check after configuring the headless CMake build:

```powershell
cmake --build build-headless-20260908 --config Debug --target test_ready_check_state
ctest --test-dir build-headless-20260908 -C Debug -R ready_check_state --output-on-failure
```

PORT-08 remains open until two real clients exercise accept, decline, timeout,
and finish and demonstrate the member icons persisting through the fade and
clearing on the next check. No live server or rendered acceptance is claimed.

## Roster/event follow-up

The next bounded change resolves the actual initiating GUID against the group
roster before trying nearby entities or the name cache. An out-of-range raid
assistant previously inherited the leader's name. Confirmations now carry
`player`, `partyN`, or `raidN` instead of an unresolvable hexadecimal GUID string.
The indices match the existing Lua unit resolver, including excluding self from
party numbering. This is an independent adaptation of WoWee's roster contract,
not copied donor code. Truncated start packets no longer erase the current check.

`test_ready_check_member` covers assistant-versus-leader identity, party indices,
self identity, raid indices, Vanilla/WotLK raid flags, absent self roster entry,
and unknown/zero GUIDs. Run it with the same build/CTest commands above, replacing
`ready_check_state` with `ready_check_member`.

Executed September 8, 2026 with MSVC Debug: state tests passed 27 assertions in
3 cases; member tests passed 14 assertions in 2 cases. The matching two CTest
registrations also passed. Full application and live validation are separate
from these headless results.

The raw response byte follows the captured donor mapping: only `1` is ready;
other recorded values are not ready. A focused assertion for byte `2` prevents
accidentally treating every nonzero response as acceptance.

The local extracted PartyMemberFrame, PlayerFrame, and RaidFrame refresh queries
on READY_CHECK_CONFIRM without using its member argument, so the original GUID
argument alone was not proof that those particular stock icons failed. Rendered
and two-client acceptance remains outstanding after this event correction too.
