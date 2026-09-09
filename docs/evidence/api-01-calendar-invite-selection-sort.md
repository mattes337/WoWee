# API-01: Calendar invite selection and sorting

## Active stock consumers

The supplied `Blizzard_Calendar.lua` uses
`CalendarEventGetSelectedInvite()` at lines 3313 and 4064 to restore the
selected-row highlight whenever the read-only or editable invite list is
rebuilt. Row clicks call `CalendarEventSelectInvite(button.inviteIndex)` at
lines 3429 and 4197.

Sort-button refresh reads `CalendarEventGetInviteSortCriterion()` as
`criterion, reverse` at line 2915. A click calls
`CalendarEventSortInvites(self.criterion,
self.criterion == CalendarEventGetInviteSortCriterion())` at line 2939: a new
criterion is forward, while clicking the active criterion requests reverse.
`Blizzard_CalendarTemplates.xml` assigns the active buttons the exact strings
`name`, `class`, and `status`. Its possible `party` assignment is commented
out and does not establish an active contract.

## Implemented state and index contract

Commit `b1883bc6720bfd23db51f7d80ac1894a13312107` adds the four bindings and
keeps their state in `GameHandler`. The source boundary is exactly:

- `CMakeLists.txt`
- `include/game/calendar_invite_view.hpp`
- `include/game/game_handler.hpp`
- `src/addons/lua_system_api.cpp`
- `src/game/calendar_invite_view.cpp`
- `src/game/game_handler.cpp`
- `src/game/game_handler_packets.cpp`
- `tests/CMakeLists.txt`
- `tests/test_calendar_packet.cpp`

The server packet vector remains authoritative and in server order. A separate
projection stores source positions and is rebuilt for the active `name`,
`class`, or `status` criterion. `CalendarEventGetInvite`,
`CalendarEventSetStatus`, `CalendarEventSetModerator`, and
`CalendarEventClearModerator` all resolve their one-based displayed index
through the same projection. Equal displayed values retain server order;
unknown names and classes sort last in either direction.

Selection is retained as `(inviteId, guid)` only when that pair identifies one
row. Duplicate invite IDs therefore do not alias reads or mutations, and an
exact duplicate identity resolves to no selection rather than choosing an
arbitrary row. A removed selected row also resolves to zero. The state resets
on a new connection, disconnect, valid event-open request, close, and every
accepted `SMSG_CALENDAR_SEND_EVENT` replacement, including a replacement for
the same event id.

`CalendarEventGetInvite` now returns the class display name from the shared
`kLuaClasses` table and the distinct uppercase filename token through
`luaPushClassToken`. An unknown class returns nil for both instead of a truthy
empty string that callers could use as an invalid table key.

## Compile correction

The first integrated full-client compile found that the new sort binding called
a nonexistent `getEngine` helper. Commit
`415a9daa9ea8831224017f4c02f0fd65501730d1` replaces it with this file's
existing registry lookup for `wowee_lua_engine`, including the required stack
pop. The client linked successfully after that correction. This compile failure
is not hidden by the focused target below, which does not compile
`lua_system_api.cpp`.

## Focused build and test

The reviewed source was kept in `D:/wowee-calendar-invite-final` at commit
`7b0529fa8`, whose Calendar implementation is the source integrated by
`b1883bc67`. A fresh isolated build at
`D:/wowee-calendar-invite-build-full2` used Visual Studio 2022/MSVC 19.44,
the retained vcpkg dependency prefix, and one MSBuild worker. The exact target
command was:

```powershell
cmake --build D:/wowee-calendar-invite-build-full2 --config Release `
  --target test_calendar_packet -- /m:1
```

The build passed. Direct execution of
`D:/wowee-calendar-invite-build-full2/bin/Release/test_calendar_packet.exe`
passed **885 assertions in 13 test cases**. Its SHA-256 is
`a377aacc0a01b2843fa36789f3617d6298e05d41cf9bde844a805b749f5ac0dc`.
The two invite-view cases cover selection through forward/reverse sorting,
selected-row removal, event-id replacement, stable ties, status/class
projections, unknown class placement, unsupported criteria, displayed-source
resolution, and ambiguous duplicate identities.

## Current limits

The focused executable proves the projection and packet-domain behavior. It
does not compile or execute the Lua bindings, manufacture `GameHandler`
lifecycle transitions, or exercise the supplied Calendar UI. The successful
full-client link covers compilation of those bindings but not runtime behavior.
Natural stock FrameXML selection/sort interaction, populated-event close/open
transitions, server-driven detail reconciliation, and two-client mutation
effects remain unverified. Broader Calendar permissions, invitation responses,
signup, locking, copying, guild/arena variants, and removal are still owned by
`BOTH-04`; this bounded implementation does not close Calendar completeness.
