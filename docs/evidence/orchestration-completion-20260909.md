# September 9 orchestration acceptance

The earlier commits through `496d69bb1` were pushed to `origin/master` before pending work resumed. Subsequent source and evidence commits remain local until final acceptance.

The client and runner linked at `415a9daa9ea8831224017f4c02f0fd65501730d1`. The actual runner reports that revision and passes [stock animation and Calendar empty-state integration](runner-animation-calendar-20260909.json). [Calendar invite projection tests](api-01-calendar-invite-selection-sort.md) pass 885 assertions in 13 cases.

Client run 24 rendered the default production preview, but failed after quit with heap corruption. [Sanitized result](live-login-24-stale-abi-20260909.json) and [mixed-layout evidence](stale-client-abi-20260909.md) preserve the failure. The old client objects and tracking files have been removed for a consistent rebuild; the failed executable and matching symbols remain private.

The first combined input acceptance run exposed byte-based caret movement: insertion of UTF-8 text passed, then Backspace split a multibyte character. All three positives reproduced that defect; the remaining 26 CLI cases passed. The helper correction and final new-binary acceptance are pending. No full input or normal-preview completion is claimed at this checkpoint.

[Single-sample preview comparison](preview-single-sample-20260909.md) and [version generation repair](source-identity-msvc-20260909.md) are separately verified. Host capacity recovered and the existing dedicated DB/auth/world containers are running. World entry, gameplay, multiplayer and the underlying 4x preview fault remain outside the verified scope.
