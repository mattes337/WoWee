# API-01: `CalendarEventCanEdit`

The supplied `Blizzard_Calendar.lua` has seven zero-argument calls to
`CalendarEventCanEdit`. The call at line 1108 chooses the editable event form
or the read-only event view after an event opens. Calls at lines 2988, 2996,
3012, 3486, 3493 and 3533 repeat that choice after event, invite-list, player-
guild, or guild-roster updates. The function is a permission query; it does not
request data or emit an event.

The client already stores the currently open server event in
`CalendarEventDetail`, including `eventId` and `creatorGuid`. Its existing
`CalendarContextEventCanEdit` binding permits editing a selected day event only
when its nonzero creator GUID equals the player's GUID. The new open-event
binding and the context binding now use one pure predicate with the same
owner-only contract. An unopened event, an unknown player, a missing creator,
or a foreign creator returns false.

The open-detail lifecycle is part of that answer. `CalendarCloseEvent` now
clears it. A valid `CalendarOpenEvent` selection clears the previous detail
immediately before requesting the replacement, so the query cannot report a
stale owner while the reply is pending. Invalid selections return before the
clear and preserve the event that is still open.

This does not infer broader guild-event edit rights. Although the stock UI
rechecks the query after guild changes and the open event contains guild flags
and a guild ID, the supplied client state does not establish the target
server's guild-calendar permission rule. Setting the guild-event flag therefore
does not grant a non-creator permission. The server remains authoritative over
any attempted update.

The pure headless regression covers an unknown player, a missing creator, a
foreign player and a matching creator. The packet regression also applies the
predicate to the creator GUID parsed from a real
`SMSG_CALENDAR_SEND_EVENT` fixture. A frozen pre-fix runner
(SHA-256
`716ab029949ec60eb4e07a7b4fde726b4aa459d41789370bcc12693c630f6534`)
failed the real-Lua binding/no-open check only with
`CALENDAR_EDIT_QUERY_MISSING`. Its log is
`D:/wowee-calendar-api-before/stdout.log`, SHA-256
`c8670cee91ee3fdf65dda1445a950115f4424a96067fe2d5807db2425395b44a`.
The same assertion must pass after integration with the binding present and
returning false before an event is opened. No fallback or unconditional
permission was added.

There is no natural public test seam for seeding the private open detail.
Close and valid-open clearing are source-reviewed. Their state transitions
remain runtime-unverified; no test-only setter, runner command, or private-state
hook was added to manufacture that evidence.

## Focused build and test

An isolated worktree at `D:/wowee-calendar-edit` used detached base
`be5b5e28f140b78c1aa5d7a17207fead908d1659` with the exact Calendar source and
test changes committed as
`073afd80c30eb4fc72c931f72a822a4ab60833d2` applied. The existing
`test_calendar_packet` target was built serially with MSBuild `/m:1`; the build
passed. Direct execution passed **859 assertions in 11 test cases**.

The build log is `D:/wowee-calendar-edit-build-retry.log`, SHA-256
`7c11e1553964f60f9c292c228ac84da17a5e109bbf445e9f0c6e8d78556f880c`.
The test log is `D:/wowee-calendar-edit-test-retry.log`, SHA-256
`504a306034a280f21a54bcc3bbd229fa6023c650069147ea5d36e30de6793a03`.

This target compiles `calendar_data.cpp` and the packet regression. It does not
compile `lua_system_api.cpp`, link the full client, or execute the new Lua
binding. Full-client compilation and the post-fix runner check remain pending;
the focused pass is evidence only for the shared permission predicate and its
packet fixture.

## September 9 integration acceptance

The final client and runner build at `415a9daa9ea8831224017f4c02f0fd65501730d1` succeeds. The actual new runner reports that identity and passes the fallback-off `--player` fixture: `CalendarEventCanEdit` exists and returns false before opening; `Calendar_Show` visibly sets CalendarFrame shown and the query remains false; `Calendar_Hide` clears shown state and the query remains false. Invite sort/selection API registrations exist and empty selection is zero. [Exact executable identity and results](runner-animation-calendar-20260909.json). These checks supersede the pending compilation/empty-state binding checks above; populated-event ownership/lifecycle and broader permissions remain unverified.

The final rebuilt runner at `3b516d92b90dc42ab5d50d64b60e24a7e9e4297b` repeats all these checks successfully: [final binary results](runner-animation-calendar-final-20260909.json).
