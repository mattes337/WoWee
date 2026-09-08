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

## Current state and dependencies

The client has no selected-invite or sort-criterion state. The open
`CalendarEventDetail` owns a vector of invitees with stable `inviteId` and
`guid` values. Selection must therefore be stored by open `eventId` and
`inviteId`, then resolved back to the current one-based displayed index. A raw
index would select a different person after sorting and could survive an
invite removal incorrectly. Close, valid replacement-open, detail replacement,
and a missing invite must all resolve to no selection.

Sorting cannot be represented only by a criterion flag. The stock UI expects
the subsequent indexed `CalendarEventGetInvite` calls and every indexed
mutation/context action to address the displayed order. Reordering the packet
vector would conflate server order and UI order. A bounded implementation needs
a projection of invite IDs and one shared displayed-index resolver for all
invite APIs.

Name data is available through `GameHandler::lookupName(guid)`. A class lookup
also exists, but the current `CalendarEventGetInvite` binding returns empty
`className` and `classFilename` strings, and the invite packet itself carries
no class. Until the lookup is deliberately wired and its unknown-class behavior
is tested, the active `class` sort criterion has no reliable displayed value to
order. Status is present on each invitee.

## Bounded next implementation

Implement selection and sorting together rather than returning a default
criterion beside a no-op sorter. Keep selection by invite identity, build a
stable displayed-order projection for `name`, `class`, and `status`, and route
every invite-index consumer through it. Tests should cover selection surviving
a reorder, removal resolving to zero, event replacement clearing selection,
same-criterion reverse toggles, new-criterion forward order, stable ties, and
unknown class data. No binding was added in this audit.
