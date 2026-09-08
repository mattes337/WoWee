# Vulkan validation defects

## DEF-001 Ã‚Â· P0 Ã‚Â· Primary diagnostics inside a secondary-only scene subpass

Status: fixed and verified in the bounded Windows startup smoke. Parent: EVAL-01;
related tasks: QUALITY-04, TEST-10.

The Windows Debug client completed the isolated 120-update startup smoke with
exit code 0 while Vulkan validation reported invalid primary-buffer commands.
The pre-fix log is `build-fork-windows/bin/Debug/logs/fork-smoke120.log`.
The run used `WOWEE_TEST_MAX_UPDATES=120`, `WOWEE_VULKAN_VALIDATION=1`, and
`VK_LAYER_PATH=G:/Dev/VulkanSDK/1.4.357.0/Bin`. No authenticated world/server
scenario was exercised. The exact executable source identity is in that log.

The complete error scan found two unique underlying messages: nine explicit
`vkCmdSetCheckpointNV` errors and nine explicit `vkCmdWriteTimestamp` errors,
followed by the layer duplicate-limit notice for each VUID. These are
`VUID-vkCmdSetCheckpointNV-commandBuffer-recording` and
`VUID-vkCmdWriteTimestamp-commandBuffer-recording`. The commands were recorded
on the primary buffer while a render pass used
`VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS`.

The [Vulkan subpass contents specification](https://docs.vulkan.org/refpages/latest/refpages/source/VkSubpassContents.html)
restricts that primary buffer to executing secondary command buffers until the
subpass advances or the render pass ends. In `Renderer::endFrame`, the
`post-process` GPU mark preceded `vkCmdEndRenderPass`. A post-process pipeline
object can exist while `executePostProcessing` performs no work, leaving the
original secondary-only scene pass open. `VkContext::gpuMark` then emits both
the NVIDIA diagnostic checkpoint and the timestamp into this restricted scope.

The independent fix moves that existing mark immediately after the existing
render-pass end. It retains the checkpoint, timestamp, label, and mark count;
the timing endpoint now also includes final attachment resolves. Inline scene
marks remain within the single-threaded inline branch; shadow/interface marks
already occur outside their passes. No validation configuration or diagnostic
implementation is suppressed or changed.

Regression procedure: rebuild `wowee`, repeat the same isolated startup smoke
with the validation layer enabled, require the 120-update completion and clean
exit, and scan the complete log for Vulkan validation errors. Exit code alone
was insufficient evidence before this fix. This live Vulkan check is the
regression; no source-text assertion substitutes for command-buffer validation.
GPU world rendering, alternative post-processing configurations, and other
platforms remain separate validation obligations.

### Post-fix intermediate run

The rebuilt client completed 120 iterations, dispatched SDL_QUIT and exited 0
with no ERROR/FATAL entries in
`logs/fork-baseline/smoke-validation-fixed/runtime/logs/smoke.log`.
[Sanitized result](evidence/vulkan-diagnostics-intermediate-smoke.json) preserves
binary/log hashes and the emitted source identity (which includes a dirty suffix).
This is clean-runtime evidence, but does **not** yet prove layer activation:
independent wrapper review found `request_validation_layers` silently allows
missing layers, while the prior success gate checked only the requested marker.
The strict gate now requires the post-build enabled marker. Explicit validation
must be required by the client and that stricter smoke must pass before closing
this defect. Wrapper regression tests also cover inherited application override
removal, preventing resource-root escapes and render-skip switches from leaking
into this fresh baseline.

### Strict validation closure

The same rebuilt binary (`c33e4c86f2c6857f026b9f9d1ed321edfd36c62798e82a6885b3c60f394f4f6f`)
passed the [required-layer run](evidence/smoke-required-validation.json): 120
completed iterations, normal SDL_QUIT dispatch, exit 0, the post-instance
`Vulkan validation layers enabled` marker, and no ERROR/FATAL entries. Its
complete log is `logs/fork-baseline/smoke-required-validation/runtime/logs/smoke.log`.
The [missing-layer negative control](evidence/smoke-missing-validation.json)
failed with exit 1 and `requested_layers_not_present`, confirming this binary
and wrapper cannot silently pass without the requested validation layer.
This closes DEF-001 for the tested startup path with the renderer fix
`7ab2f5a7d`; it does not certify world rendering or alternative post-process
settings. Evidence JSON preserves exact source identity and binary/log hashes.

## DEF-002 - P0 - Screenshot reads outside the acquired frame lifetime

Status: source fix, queue regression and positive GPU capture pass; negative
destination and final follow-up build verification pending.
Parent: EVAL-01; related tasks: TEST-04, QUALITY-04.

Source audit found `Renderer::captureScreenshot` immediately copying the image
indexed by `currentImageIndex` through an unrelated immediate submission.
GameScreen PrintScreen and slash-command callers run during UI construction,
while the current frame is still being recorded; Lua callers can also request
between frames, after the previous image was presented. `vkDeviceWaitIdle` does
not submit the current recording or reacquire a presented image. The
[Vulkan presentation contract](https://docs.vulkan.org/refpages/latest/refpages/source/vkQueuePresentKHR.html)
requires reacquisition before using an image released for presentation. This
is a source-confirmed ownership defect; no pre-fix visual comparison is claimed.

Requests now retain their path and Pending status across skipped frames. The
renderer records the copy after its final overlay pass in the acquired frame's
own command buffer, before normal submission/presentation. Only an explicit
screenshot stalls for completion. Staging memory stays alive through that wait;
host-read visibility, allocation invalidation, supported RGBA/BGRA conversion,
map/allocation failures and PNG failure are checked. A scoped staging owner
releases mapped memory and the buffer. `VkContext::endFrame` now reports command
end, submission, or presentation failure so a failed frame cannot produce a
successful capture result. A pending request is cancelled during shutdown.

`captureScreenshot` now returns request acceptance. Chat says Screenshot queued;
only the completed PNG write logs Screenshot saved. The bounded
`WOWEE_TEST_SCREENSHOT_PATH` hook queues a capture at run start and requires
Succeeded at loop completion, otherwise the application exits with failure.
The caller supplies the destination; the hook has no default screenshot path.

The real production queue helper has two Catch2 cases covering accepted/rejected
requests, path preservation, terminal completion, failure and cancellation.
`ctest --test-dir build-headless-ci-final -C Debug -R '^screenshot_request$'
passes. Required live checks: layer-enabled startup with a fresh capture path,
PNG decode/extent and completion assertion, then an unwritable destination that
must fail without claiming saved. This establishes capture mechanics only;
reference-image parity and authenticated world captures remain open.

### Intermediate live capture evidence

The [positive capture result](evidence/smoke-capture-intermediate.json) passed
120-update startup with required Vulkan validation, no ERROR/FATAL entries,
normal quit and exit 0. The PNG is 90,965 bytes, 1280x720 RGBA; root separately
ran Pillow verification and full pixel decode, checked nonblank extrema, and
viewed the login card. The PNG SHA-256 is
`90d5a4335ebb9517c4c4a74c2c7d0fc39f4f745b08c434db367892d518c08202`.
The original run is `logs/fork-baseline/smoke-capture/result.json`; the sanitized
result preserves binary and log hashes and emitted source identity. This
binary includes the capture barrier correction `c21ec96a0` but predates
`b5bdc2cca` caller/output exception handling. No negative-destination result or
post-follow-up executable verification is claimed here.

### TEST-04 acceptance remains open

Queuing a screenshot does not freeze the scene at the API call. The request
stores a destination, not a scene snapshot, and returns immediately. A same-tick
Lua `Screenshot(); frame:Hide()` or move can mutate widget state before the
end-of-frame readback is recorded. The screenshot will capture whichever state
was rendered into that completed frame, not necessarily the call-site state.
Although the renderer waits for its copy and file write before reporting saved,
Completion events acknowledge the finished write, but no synchronous script
wait primitive prevents later same-tick mutations. The startup capture proves acquired
image ownership and successful readback mechanics only. TEST-04 still needs
explicit script completion/wait semantics and a live same-tick hide/move
regression; it must not be checked off from this result.

### Stock screenshot completion events

Stock `WorldFrame.lua` registers `SCREENSHOT_SUCCEEDED` and `SCREENSHOT_FAILED`.
The production request helper now retains terminal outcomes until consumed;
Application drains them after renderer end-of-frame via AddonManager's existing
Lua event boundary. Success is emitted only after successful PNG write; queue
rejection, failed output, and cancellation report failure. Rejection does not
overwrite an earlier pending request. Draining takes a snapshot, so callbacks
that queue another screenshot cannot receive that new completion recursively.
Shutdown closes requests and drains cancellation before UI/renderer teardown;
requests from teardown callbacks are rejected without creating recursive events.
Lua Screenshot still returns zero values. Three helper cases verify request
states, cancellation and consume-once event ordering; real Lua/GPU event delivery
verification remains pending. These asynchronous events do not change the
same-tick mutation gap above.
