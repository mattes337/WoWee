# World-entry command buffer overflow and resize recovery

The user world-entry crash at 09:49 on September 9 produced a Windows Debug runtime failure: `Stack around the variable 'validCmds' was corrupted.` The minidump stack identifies `Renderer::renderWorld`, `_RTC_CheckStackVars` and `_RTC_StackFailure`. Local decoded records are `D:/wowee-client-session-20260909/crash-stack.log` and `crash-variable.log`.

`validCmds[6]` was filled with up to seven secondary command buffers: terrain, sky, WMO, selection, characters, M2 and post effects. Commit `553419ed9` sizes it by `NUM_SECONDARIES` (8). Draw order and recording predicates remain unchanged. Independent review verified the maximum append count and neighboring worker/secondary array bounds.

## Additional failure found during validation

The first long test rendered the world without the stack failure, but was manually stopped before its delayed screenshot/trace contract completed. Its expected harness failure is retained under `D:/wowee-private-live-login/world-entry-buffer-fix-01`.

The second run exposed a separate zero-extent resize failure. Vulkan rejected a 0x0 swapchain, depth creation failed, and the caller continued to index an empty framebuffer vector. The matching dump identifies `Renderer::beginFrame` and the vector bounds check, not `validCmds`. Results and decoded stack are retained under `world-entry-buffer-fix-02` in the same private root.

Commit `c3ee1d858` checks the actual Vulkan surface extent before destroying existing swapchain resources, leaves recreation pending while unavailable, and stops the main frame on recreation failure. Commit `83b8e4d55` also stops the loading-screen path on failed recreation, checks overlay bounds before indexing, and preserves the previous MSAA sample state/pending request if both rebuild attempts fail. These guards were independently reviewed.

## Validation boundary

The [world-entry test](world-entry-buffer-fix-20260909.json) passes on `c3ee1d858`, executable SHA-256 `109fa287e315eb41ec4e84f998e1c867536be3a117efdbaa794d9ea587f35c0f`: fresh local account fixture, Enter World at updates 600/602, real parallel terrain/WMO/M2 rendering, screenshot at update 680 and normal exit at update 700. Visual review shows Elwynn Forest, Woweetrial, Deputy Willem and the stock interface. There are no validation errors, and shutdown reports zero live VMA allocations.

The final `83b8e4d55` build includes the additional loading/MSAA caller guards and compiles successfully. Its executable SHA-256 is `b25035fbd6d274854c84dfa43d5aeac29ffd1a37d5af5f48220dac5df01f957c`. Those final caller guards were build/review checked; the world test above is explicitly the preceding executable. Automated minimize/restore and MSAA-setting-change journeys are not claimed. Debug world rendering remains slow, roughly 1–2 FPS in this scene; broad gameplay and performance acceptance remain open.

The [final-build startup smoke](world-guards-startup-20260909.json) also passes on the final executable: 120 updates, screenshot completion, exit code 0 and normal shutdown with no classified errors. This is offline startup evidence, not a second world-entry run.
