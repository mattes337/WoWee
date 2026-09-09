# Single-sample character preview recovery, 2026-09-09

The recovered Windows host built client SHA-256 `11c57ac6a829970fdcab3bc949b554e9ed74f4d0347788ffa6e57a5d1e5da616`. Its embedded source label was stale; see the separate source-identity MSBuild fix. Do not identify this binary by that label.

[Sanitized results](preview-single-sample-20260909.json) preserve three fresh fixtures, each requesting 1,800 updates with Vulkan core validation:

- Run 21: single-sample target, procedural nonindexed triangle and constant fragment shader. Passed authentication, realm/character list, screenshot and normal shutdown. The screenshot visibly contains the magenta triangle.
- Run 22: same executable, shaders and nonindexed mode; only the single-sample switch was removed, restoring the previous 4x default. Device lost at frame 54; no screenshot or normal shutdown.
- Run 23: single-sample target with production shaders, indexed character geometry and backdrop. Passed authentication, realm/character list, capture and normal shutdown without validation errors. Visual inspection confirms Woweetrial and the rendered character/backdrop.

This local comparison implicates the combined multisample target/depth/resolve and pipeline sample state. It does not establish a specific Vulkan violation or driver cause. Production previews now use one sample as a stability mitigation. The historical single-sample diagnostic switch remains accepted and marked for reproducible older commands, but is redundant with the new default.

Host nonpaged pool stayed about 0.73–0.77 GiB after these runs, including the failing control, with roughly 11 GiB available RAM. The dedicated existing DB/auth/world containers were restarted without the importer or volume changes. This supersedes the earlier host-capacity blocker; its original cause remains unknown.

No world entry, gameplay, multiplayer, or cross-GPU certification is claimed. The underlying 4x path remains unresolved. Final default-mode build and acceptance are recorded in the orchestration completion evidence.
