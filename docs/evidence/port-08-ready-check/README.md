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
