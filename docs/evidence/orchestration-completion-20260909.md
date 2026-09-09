# September 9 orchestration acceptance

Existing commits through `496d69bb1` were pushed to `origin/master` before pending implementation resumed. Final source and evidence are committed after acceptance. The validated executable source boundary is `3b516d92b90dc42ab5d50d64b60e24a7e9e4297b`; subsequent changes are evidence/roadmap updates.

## Build and runtime results

- [Both MSVC Debug targets build](build-final-20260909.json). The client was rebuilt after discarding all 428 stale client objects and 15 tracking files; `application.cpp` now tracks the changed GameHandler header. Both executables report the correct source identity.
- [Default production preview run 26 passes](live-login-26-default-preview-20260909.json): local authentication, realm and character list, real character/backdrop, screenshot after 600 updates, 1,800 updates and normal shutdown with no validation errors. No preview isolation, replacement shaders or rasterizer-discard flags were used.
- [Input acceptance passes](test-05-text-key-validation-20260909.md): three fresh stock menu/EditBox journeys; exact UTF-8 insertion/backspaces and three change events; one strict disabled-handler expected failure; all 27 CLI matrix cases. Focused text-markup tests pass 136 assertions in 15 cases.
- [Final runner animation/Calendar checks pass](runner-animation-calendar-final-20260909.json): stock timer OnLoad/completion/smoothing, unchanged generic alpha, new Calendar registrations, no-open edit/selection state and natural Calendar show/hide. Four malformed CLI cases also reject before setup.
- [Calendar projection tests](api-01-calendar-invite-selection-sort.md) pass 885 assertions in 13 cases. The implementation owns selection/sort state in GameHandler and resolves indexed invite operations consistently.
- Scanner tests (7), inventory tests (6), matrix-driver tests (7), login-driver tests (23), and source-identity generator tests (1) pass. The capability ledger was regenerated and its freshness check passes. The saved 29-test CTest manifest remains configuration evidence; no full headless-suite rerun is implied.

## Preserved failures and limits

[Runs 21–23](preview-single-sample-20260909.md) establish the single-sample preview mitigation; the underlying 4x MSAA fault remains unresolved. [Run 24 and stale ABI analysis](stale-client-abi-20260909.md) preserve the failed incremental client and clean-build recovery. The first input acceptance retains the UTF-8 Backspace failure that drove the source correction.

Host capacity recovered, and only the existing dedicated DB/auth/world containers were restarted; the importer stayed stopped and volumes were preserved. An interactive client session is prepared separately at `D:/wowee-client-session-20260909` using the saved local test account. No test input trace or automatic quit is needed for that session.

World entry, gameplay, multiplayer, populated-event Calendar reconciliation, cross-GPU behavior and a clean-machine distribution remain unverified. The roadmap marks bounded completed tasks while keeping their broader parent gates open.
