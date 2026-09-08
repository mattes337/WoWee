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
Close and valid-open clearing are source-reviewed, with compilation still
pending after the focused build ended in C1060 resource exhaustion. Their state
transitions remain runtime-unverified; no test-only setter, runner command, or
private-state hook was added to manufacture that evidence.
