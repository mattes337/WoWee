# PORT-08 stationary two-client scenario

Status: **NOT RUN**. Preparation only; no client/server actions, builds or live
assertions were performed for this document. Two dedicated characters and an
authoritative observation hook are prerequisites. This does not close PORT-08.

## Source provenance

Client checkout inspected: `e146401b9e793fb442dc8f9e149c02567a8dabd3`.
Primary server: local AzerothCore checkout
`798d08c58a8e00b7937050963119bb857344b640` at `G:/azerothcore-wotlk`:

- `src/server/game/Handlers/GroupHandler.cpp`, `HandleRaidReadyCheckOpcode`
  and `HandleRaidReadyCheckFinishedOpcode` (approximately lines 702-757).
- `src/server/game/Groups/Group.cpp`, `BroadcastReadyCheck` and
  `OfflineReadyCheck` (approximately lines 1770-1794).

Client production paths inspected: `src/game/social_handler.cpp` group and ready
handlers, `src/game/world_packets_social.cpp` packet builders,
`src/addons/lua_social_api.cpp`, `src/addons/lua_unit_api.cpp`, and
`src/ui/chat/commands/{social,group}_commands.cpp`. Stock extracted
`Interface/FrameXML/{ReadyCheck.lua,ReadyCheck.xml,ChatFrame.lua}` supplies
buttons, slash commands and fade behavior. Record extracted-file hashes in the
actual run manifest; these local data files are not committed fixtures.

## Existing observations and protocol constraints

| Stage | Real incoming evidence | UI/API evidence and limits |
|---|---|---|
| Invite B | `SMSG_GROUP_INVITE` accepted by parser with `canAccept=true` | `PARTY_INVITE_REQUEST(AName)` and B's stock invitation popup |
| Accept invite | `SMSG_GROUP_LIST` parsed on both clients | Roster contains the other GUID, A remains leader; `GROUP_ROSTER_UPDATE` and `PARTY_MEMBERS_CHANGED`; log `Joined group with ...` |
| Start | Server `MSG_RAID_READY_CHECK`, eight-byte initiator GUID | `READY_CHECK` currently carries initiator name; new answers cleared, known unanswered GUIDs query `waiting` |
| Answer yes/no | Server `MSG_RAID_READY_CHECK_CONFIRM`, GUID plus response byte | On leader A, `READY_CHECK_CONFIRM` unit token and numeric 1/0; `GetReadyCheckStatus("party1")` becomes `ready`/`notready` |
| Finish | Server `MSG_RAID_READY_CHECK_FINISHED` | Pending question clears; retained answered GUID still queries `ready`/`notready`; stock finish/fade starts |
| Leave/disband | Parsed group-empty/uninvite/destroyed message | Ready-check state resets; stale GUID status absent |

A request uses an empty outbound `MSG_RAID_READY_CHECK`; an answer uses that
same opcode with one byte (1 or 0). Sending is not server acceptance. The
`You are ready.` / `You are not ready.` and `Ready check initiated.` chat lines
are local send-path messages, so they cannot satisfy response gates. Received
confirmations generate `<name> is Ready.` / `<name> is Not Ready.`; incoming
finish generates `Ready check complete: ...`. `WOWEE_CHAT_LOG=1` plus a unique
`WOWEE_CHAT_LOG_PATH` can preserve these chat lines, but structured state/API
observations are still needed to distinguish correctness from displayed text.

**Server visibility:** AzerothCore broadcasts start to group members, but sends
confirmations only to leader/assistants (`BroadcastReadyCheck`). Ordinary member
B must not be required to receive confirmations or converge its cached response
status. Assert response state on leader A. Dismissing B's popup is local and
must not be treated as a server confirmation.

**Original finish gap (now partially fixed):** AzerothCore emits FINISHED only
after a leader/assistant sends `MSG_RAID_READY_CHECK_FINISHED`. The source audit
initially found no outgoing sender or deadline. A bounded all-confirmations
sender is now implemented; see validation below. There is still no timeout policy. The server's start handler does not arrange an automatic timer or
finish after collecting answers. Waiting longer or seeing both replies cannot
stand in for FINISHED. Timeout, finish and icon-fade acceptance remain blocked
until the production path is exercised live; timeout still needs a policy.
Do not insert packets or call handlers directly to make this scenario green.

## Repeatable execution plan (not executed)

1. Record client commit, executable hashes, server revision/config identity,
   extracted stock UI hashes, fallback setting, per-client config/log roots,
   two character names/GUIDs and process IDs. Use two separate existing accounts,
   same faction and a safe stationary location. Require actual world-entry and
   character identity evidence on both clients before sending any group action.
2. Start observers before invitation. On A, enter `/invite BName` through actual
   chat UI. Wait up to 30 seconds for B's incoming accepted invite and stock
   popup. Click B's stock Accept button. Wait for authoritative rosters on both
   clients, not just the outbound `CMSG_GROUP_ACCEPT` log. Fail on rejection,
   disconnect, unexpected roster/leader or deadline. A new run must have fresh
   evidence; do not reuse an earlier successful roster event.
3. On leader A enter `/readycheck` through chat UI. Wait for incoming start on
   both clients and a new observer epoch. Assert initiator A and leader A's
   `party1` GUID equals B before checking `waiting`. Capture B's actual prompt.
   Click B's stock Yes button (`ConfirmReadyCheck(1)`). Wait for A's incoming
   confirmation of B with response 1, then assert API `ready`, numeric event
   argument 1 and B's correct member icon. Verify B's prompt closes separately.
   A may use the real `/ready` command to send its own response; that command
   does not exercise stock popup dismissal and must be labeled accordingly.
4. After the missing finish path is implemented, require actual FINISHED on
   both clients before marking finish. On A, assert B's answer persists during
   the stock icon's hold/fade. Stock `ReadyCheck_Finish` sets a 10-second hold
   and default 1.5-second fade; use monotonic bounded waits plus observed frame
   state and acknowledged captures, not update counts as presented-frame proof.
   The icon eventually hides; the cached answered status is allowed to remain.
5. Start a fresh check and require a new incoming start. Confirm the previous
   result clears to `waiting`; on B click No (`ConfirmReadyCheck()` with no
   argument). A must receive response 0 and expose `notready`, with numeric
   event argument 0 and the correct not-ready icon. Repeat finish/retention/fade
   gates once the production finish path is available.
6. Start another fresh check and deliberately leave B unanswered. Run this
   timeout phase only after the production timeout policy is specified; use
   its real deadline plus a bounded grace period and require server FINISHED.
   After finish an unanswered GUID queries nil; stock waiting icon becomes AFK
   during its fade. Do not fabricate a response or equate timeout with decline.
7. Leave the group through the real `/leave` command, require parsed roster
   teardown, and assert cached ready-check state resets. Shut down both clients
   normally, retaining transcripts and per-step classifications.

Partial runs may report invite/yes/no phases individually. Missing characters,
observer, finish policy, deadline, capture acknowledgement or incoming packet
are blocked/failed phases, never implied passes. No movement, combat or fake
server is needed for these stationary phases.

## Minimal observer proposal (not implemented)

Add a narrowly opt-in observer that writes JSONL after successful incoming group
and ready-check handlers have updated production state. Each record should have
client/run identity, monotonically increasing receive sequence, monotonic time,
ready-check epoch, incoming opcode, relevant GUID/token, raw response byte,
leader/roster GUIDs and queried pending/status values. Start creates the epoch;
confirmation/finish records belong to that epoch. Restrict recorded fields to
this scenario, with no credentials, arbitrary text or packet payload dumps.

To verify the public contract, pair those records with a read-only Lua event
listener registered on the actual running Lua state before the action. It should
record argument type/value, `UnitGUID("party1")` and
`GetReadyCheckStatus("party1")` at READY_CHECK/CONFIRM/FINISHED, then after fade.
Use the existing public API and actual dispatched events; do not call handlers,
set model fields, synthesize events or replace the stock frames. Record the
matching stock member icon's visible/state/alpha/texture values and acknowledged
screenshots separately. A receive-state observer alone cannot certify delivery
to Lua or visual correctness. Observer I/O failure and absent expected events
must fail the scenario, with finite real-time waits.

## Existing focused coverage

`tests/test_ready_check_state.cpp` and `tests/test_ready_check_member.cpp` are
registered as `ready_check_state` and `ready_check_member` in
`cmake/HeadlessTests.cmake`. They cover per-GUID state retention/reset, response
replacement and roster-token mapping. Earlier passes/provenance are recorded in
`docs/headless-tests.md` and `docs/evidence/port-08-ready-check/README.md`.
No tests were rerun for this preparation, and those pure tests do not prove the
two-client protocol, popup dismissal, event delivery, timeout or icon fade.


## Finish emission design after flow audit

No `FinishReadyCheck` Lua binding or call was found in the current client or
extracted stock Interface tree. `ShowReadyCheck(initiator, timeLeft)` accepts
`timeLeft` but does not use it. The only stock timing values found are the
post-finish 10-second icon hold and 1.5-second fade; neither defines the response
timeout. AzerothCore's eight-byte start packet supplies only the initiator GUID.
A production timeout duration therefore cannot be derived from these sources.
Do not promote the harness's 30-second observation deadline into game policy.

A minimal completion path that is supported by the observed protocol would:

1. On a real incoming start, retain its initiator GUID, create a new epoch and
   snapshot the expected group GUIDs, explicitly including self. Only the client
   whose GUID equals that initiator may own completion. Assistant-initiated
   checks must use this ownership too; another leader must not finish them.
2. Count distinct incoming CONFIRM GUIDs for that epoch and expected roster.
   Both ready and not-ready count as answered. Unknown GUIDs and duplicate
   replies cannot advance completion; local send success cannot count as an
   answer. The initiator must have an actual confirmed answer too; automatic
   self-ready behavior has not been established by the inspected sources.
3. Once every expected GUID is answered, and the initiator is still in the same
   group and authorized as leader/assistant, send the real empty outbound
   `MSG_RAID_READY_CHECK_FINISHED` once. Arm the sent flag only when queuing
   succeeds. Keep state active until the server's incoming FINISHED; do not
   locally synthesize the event, clear answers or trigger a fake fade.
4. Cancel the completion epoch on group teardown, replacement ready-check or
   lost session. Treat roster changes as an explicit invalidation policy rather
   than silently dropping unanswered members to manufacture completion.

This would cover the all-answered yes/no case, but not unanswered timeouts.
Timeout behavior needs additional primary protocol/client evidence or an
explicit documented product policy before implementation. Read-only observers
and synthetic state regressions should also reject duplicate finish sends,
foreign initiators, stale epochs, unmatched GUIDs and failed queues. The all-answered portion is now implemented as described below; timeout remains
open.


## Bounded all-answered implementation and regression

`ReadyCheckCompletion` now snapshots expected GUIDs on the real incoming start
and belongs only to its initiator. The live social handler feeds it only incoming
confirmations. It sends one empty FINISHED packet through the existing socket
when every expected member, including self, has answered and the initiator still
has leader/assistant authority. Unsupported opcode mappings do not send.
Incoming FINISHED remains the only path that finishes visible ready-check state;
answers remain retained for fade. Group teardown, disconnect and a new start
reset completion. Roster membership change or lost authority invalidates it,
without silently shrinking the snapshot or manufacturing a finished event.

Neither AzerothCore's start handler nor the inspected stock Lua automatically
answers for the initiator. The stock initiator hides `ReadyCheckListenerFrame`,
which contains Yes/No, so this bounded path requires the real `/ready` command
from the initiator as well as B's response. Automatic self-answer behavior was
not introduced. This is a remaining UX constraint, not a claimed complete check
flow. The one-byte response still travels to the server and back before counting.

The socket send interface returns void. The helper records the send after its
callback returns and permits another attempt after an exception; it cannot prove
that the socket accepted or delivered the bytes. Actual incoming FINISHED is
required for live success. The wire carries no epoch identifier, so resetting
local counters cannot distinguish a late old wire response from a new one;
never overlap ready checks in the scenario.

MSVC Debug `ready_check_state` passed **48 assertions in 6 cases**, including
partial/duplicate/foreign GUIDs, non-initiators, group/role/reset changes, retained
state, unsupported opcode and throwing-send cases. Its callback receives the
production `network::Packet` serializer and checks WotLK opcode 0x3C6 with an
empty body. An isolated no-send mutation reproducing the former missing sender
failed 2 cases (35/37 assertions passed); restoring the helper passed all 48.
This is a mutation regression, not a claim that a full older client was run.
Local evidence: `build-input-ci-validation/ready-nosend-ctest.txt`,
`ready-finish-restored-build.txt` and `ready-finish-restored-ctest.txt`.
No two-client live scenario, timeout, icon-fade or production full build is
certified by this focused test. No new headless target was added; the existing
ready-check test now also compiles `src/network/packet.cpp`.
