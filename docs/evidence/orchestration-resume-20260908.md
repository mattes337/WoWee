# Historical orchestration recovery checkpoint — 2026-09-08

Superseded by the September 9 recovery and completion work. Commits through `496d69bb1` were pushed to `origin/master` before pending work resumed. [Preview recovery](preview-single-sample-20260909.md) records restored capacity and the successful real character preview. The instructions below preserve the earlier checkpoint, not current blockers.

The root orchestrator is Astra; the three worker roles use GPT-5.6 Sol. Continue the existing roadmap task after host recovery. No changes have been pushed.

## Immediate blocker

[Host counter and build evidence](host-memory-exhaustion-20260908.json) records about 28.5 GiB of nonpaged kernel pool, paging failures even in a single-worker build, and all three dedicated emulator containers exited. The responsible kernel component is unknown. The user was asked to save other work and restart Windows; no restart, driver reset or unrelated application termination was performed. Suspend compilation and GPU runs until resources recover. Do not waive failed builds as source validation.

## Preserved work and storage

- Shared checkout: `G:/WoW Projects/wowee`; preserve the original untracked build/tool/data files and dirty sibling `wow-client`.
- Shared build path `G:/WoW Projects/wowee/build-fork-windows` is now a junction to `D:/wowee-shared-build-20260908`. Every copied build file was hash-verified before switching the path. A redundant G: backup remains; only its disposable `.obj`/`.ilk` files were removed. Evidence, binaries and debug symbols were preserved.
- Use `D:/wowee-root-temp` for TEMP/TMP and one compiler worker for the next retry. Root alone owns shared builds, runtime DLL changes and client launches. No build or DLL mutation while any client/runner owns those binaries.
- Last passing client SHA-256: `014c5317e5a261d4c8efe0ab0a6a2a6294270199784004e57ce7e6678bd6d817`. It contains the valid discard pipeline, neutral fallback depth bias and Calendar query. It does not contain the subsequent 1x diagnostic.
- Last verified runner SHA-256: `716ab029949ec60eb4e07a7b4fde726b4aa459d41789370bcc12693c630f6534`. Later runner builds failed before acceptance; verify the actual file before using it.
- Source revision labels are under audit: the newer client reports an older embedded revision. Use the recorded executable hashes for the paired experiments; do not infer binary source from current HEAD.

## Next validation work

1. Verify recovered host memory and dedicated Docker container health. Project `wowee-eval-20260908-af5200`, ignored compose path `logs/fork-baseline/emulator/wowee-eval-20260908-af5200/compose.yml`. Start existing DB/auth/world containers, preserving volumes and existing character Woweetrial. Do not recreate or alter the original database volume. Keep `secrets.json`, `.env` and private input traces out of outputs and commits.
2. Build the shared runner from stable source, including text/key dispatch and Windows wide-argument conversion (`a5bf19221`), Calendar (`073afd80c`) and all later shared corrections. Preserve failed logs under `logs/fork-baseline/build-runner-text-key-calendar*.log`. Record exact binary hash and observed source label; resolve the stale-label issue before declaring a new certified baseline.
3. Execute the combined stock menu/EditBox scenario (`ef6f98e18`) three times in fresh D: fixtures plus its disabled-handler negative. Validate actual hits, menu text/draw order, UTF-8 input/backspaces and three text-change events. This scenario is currently unexecuted. Contract tests passed 181 assertions in eight cases; the separate native argv probe passed, which does not substitute for runner acceptance.
4. Re-run the successful stock animation OnLoad/generic/smoothing assertions and Calendar function/no-open query, then natural stock Calendar show/hide. The pre-fix Calendar failure is preserved at `D:/wowee-calendar-api-before`. Focused Calendar tests pass 859 assertions in 11 cases; populated-event close/open transitions remain runtime-unverified.
5. Build the reviewed 1x preview diagnostic now in `6a41c2740`; its driver requires the exact activation marker. Use new private fixtures under `D:/wowee-private-live-login`, the same procedural vertex and constant fragment fixtures as runs 19/20, nonindexed draw, and no rasterizer discard. Run21 at 1x, then a same-binary default-4x control if appropriate. The 1x source is unbuilt/unexecuted; 23 driver checks passed separately.
6. Refresh the capability ledger using `docs/evidence/headless-configured-tests-20260908.json` only after source and tools stabilize, then run `--check`. A previous check became stale during concurrent runner edits; no current freshness pass is claimed.

## Preserved experiment boundaries

[Runs 19/20](live-login-19-20-rasterizer-discard-pair-20260908.json): valid discard passed 1800 updates/validation/shutdown; nondiscard on the same executable lost the device at frame 62. Discard legally omits the fragment stage, so this only narrows rasterization-or-later work. Run18 is preserved as an invalid pipeline diagnostic. No normal preview, world-entry, gameplay or multiplayer certification is claimed.

The invite selection/sort draft is isolated at `D:/wowee-calendar-edit` and is not integration-ready or compiled. Earlier draft patch: `D:/wowee-invite-view-reviewed.patch`, SHA-256 `b1b0fa85fada2c11561b0478abd69746f70c0c8364c862b698bce8f9e837ddcb`. Required corrections include GameHandler-owned state, localized class names distinct from tokens, safe duplicate identities and consistent displayed-index resolution. Reinspect the worktree before using any later partial edits; never splice the earlier draft as validated code.

Keep documentation-only and source commits scoped with `git commit --only <exact paths>` after staging new files. A shared-index race placed the intended 1x source in the host-evidence commit; do not revert it merely because the commit title omits that change.
