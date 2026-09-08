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

Status: fixed and verified for bounded startup capture ownership/completion;
same-tick script mutation/wait contract remains open.
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
verification is recorded below. These asynchronous events do not change the
same-tick mutation gap above.

### Final paired capture and completion-event verification

The rebuilt executable SHA-256
`a319fbc1bd3c2be83338e74e87b56858a023bf8801709e4294c387e8232ab7f7`
passed the [positive event capture](evidence/smoke-capture-events.json): normal
120-update quit, exit 0, exactly one SCREENSHOT_SUCCEEDED and no failure event
or ERROR/FATAL entry. Pillow verified and fully decoded the PNG as 1280x720
RGBA with nonblank RGB extrema; the sanitized result preserves the pixel, log
and binary hashes. The [blocked-destination control](evidence/smoke-capture-unwritable.json)
used an existing regular file as the requested parent directory. It exited 1
with exactly one SCREENSHOT_FAILED, no success event or saved marker, and no
Vulkan validation errors. Both runs enabled the required validation layer and
used the same binary, including completion events and output failure handling.
Original logs remain under `logs/fork-baseline/` in those named directories.

The latest queue helper also passes standalone GCC AddressSanitizer and
UndefinedBehaviorSanitizer in minimal Ubuntu 24.04 with
`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`: 31 assertions in three cases,
matching Windows. These results close DEF-002's acquired-image lifetime and
reported readback/write outcome defect. They do not close TEST-04's separate
same-tick hide/move and deterministic script-wait requirements or certify
reference visual parity.

### Scheduled test capture

`WOWEE_TEST_SCREENSHOT_AFTER_UPDATES` optionally delays the test screenshot
request until that many application update/render iterations have completed.
It requires `WOWEE_TEST_SCREENSHOT_PATH`, accepts digits for 0..1000000, defaults
to zero, and must be less than an enabled bounded-stop count. The request is
queued exactly once before the normal SDL event poll. Skipped update iterations
do not advance the schedule, while a queued renderer request survives skipped
image frames. The existing run-end check still requires actual PNG success.
This enables a later character-list capture without claiming a server-state or
presented-frame condition wait. Three focused helper cases cover default/later
scheduling, one-shot behavior, skipped iterations and invalid/unreachable bounds.

## DEF-004 - P0 - Device loss when opening the character-creation preview

Status: observed, unresolved; no source cause or fix verified. Parent: EVAL-01;
related tasks: QUALITY-04, TEST-04, EVAL character creation/world entry.

The [normal and GPU-assisted run record](evidence/emulator-readiness-20260908.md)
contains the controlled setup and outcomes. Both runs used executable SHA-256
`4b4c8c78559e3aab51015652a9db2015de704cf48cc5f60c00a4d1e9dd355005`
and the same new test account/database. Authentication, world authentication
and an empty character list succeeded. In normal-mode `live-login-05`, clicking
New Hero at (780,425) at completed update 300 loaded the HumanMale character,
composite skin and racial backdrop. Frame 306 then failed vkQueueSubmit with
VK_ERROR_DEVICE_LOST (-4); the driver reported an invalid write at address zero.
Later command-buffer reset validation messages followed the loss and are not
established as its initiating cause. No name submission, creation success,
completed trace or scheduled update-900 capture occurred. Read-only verification
of the new database found zero characters.

`live-login-06` repeated the same input with explicit GPU-assisted validation.
It exited 3 before normal shutdown, reporting GPU-AV internal disablement and
Failed to wait for fence. This is a failed diagnostic run, not useful evidence
of a specific shader instruction, descriptor fault or bounds violation. The
new database again contained zero characters. It did not exhaust its timeout.

Original results, stdout, client logs and character-count.log are retained at
`logs/fork-baseline/emulator/wowee-eval-20260908-af5200/live-login-05/` and
`live-login-06/`. Client log hashes are respectively
`8b9a49a7dc4232c659c57a41226f78141fc2918c5c2ff43d478f987c2cef711e` and
`6fa4db4e95597dda3e099a5b1d7d06b308cb199805d7222e8b3aff823cd94ac3`.

Independent source audit found matching preview descriptor layouts, bounded
material ring offsets, matching 4x color/depth pipeline samples and 1x resolve,
correct framebuffer attachment order and an explicit color-write to sampling
dependency. These checks narrow inspection but do not prove the path correct
or identify the device-loss cause. The shader loops are finite: the preview
key-color search has at most 80 neighboring texel fetches, POM at most 64 steps,
local lighting at most 64 entries, and shadow filtering nine taps. Material
ring exhaustion skips draws rather than wrapping over earlier allocations.
No unbounded loop or demonstrated in-flight ring overwrite was found.

Default-off no-backdrop and no-model-draw switches are diagnostic isolation
controls only. No outcome from a later isolation replay is asserted here, and
no control is a fix or replacement for normal-mode acceptance. Creation,
world entry and normal preview rendering remain blocked pending a reproducible
root cause and a validated correction.

## DEF-005 - P0 - Uploaded buffers lack copy-to-consumer memory dependencies

Status: source-confirmed gap fixed; live synchronization regression pending.
Parent: QUALITY-04; related investigation: DEF-004, causality unproven.

`uploadBuffer` and `uploadIntoBuffer` recorded vkCmdCopyBuffer without a
subsequent buffer memory barrier. Default asynchronous upload batches submit on
the graphics queue, but submission order alone does not provide memory
visibility for later vertex/index/shader access. Texture layout barriers cover
the named images, not these buffers. The conditional begin-frame memory barrier
only targets fragment-shader reads, so it does not cover vertex/index input.
This differs from the [Vulkan buffer-upload synchronization example](https://docs.vulkan.org/guide/latest/synchronization_examples.html#upload-data-from-the-cpu-to-a-vertex-buffer),
which provides a copy-to-consumer dependency when no semaphore intervenes.

Both helpers now record a per-buffer barrier immediately after the copy:
TRANSFER / TRANSFER_WRITE to ALL_COMMANDS / MEMORY_READ | MEMORY_WRITE, covering
only the copied destination range with ignored queue-family indices. The shared
cmdPipelineBarrier2 compatibility path handles core/KHR/legacy recording as it
does for existing barriers. No global validation behavior changes. The broad
consumer scope accommodates generic buffer usage while restricting the memory
range to this allocation.

This addresses default same-queue visibility; an opt-in independent transfer
queue still requires cross-queue semaphore ordering and is not repaired by this
barrier alone. The change is not yet established as the cause or solution of
DEF-004's character-preview device loss. Root's synchronization-validation
before/after replay is the required runtime regression. No source-text test is
used as a substitute for GPU synchronization validation.

### Device-loss recovery diagnostics

Normal preview failures also produced secondary pending-command-buffer reset
validation errors: endFrame latched device loss, then resetFrameSyncState
continued rebuilding fences and resetting command buffers despite its failed
idle wait. The bounded recovery correction returns immediately on known loss
and refuses synchronization reset after any unsuccessful idle wait. Healthy
non-device-loss submission recovery retains its existing reset path. Resources
remain owned for shutdown, and beginFrame already refuses work on a lost device.
This preserves the initiating fault and removes an invalid recovery attempt;
it is not a preview rendering fix. A subsequent fault replay is needed to
verify the secondary reset diagnostics disappear while the original error
remains reported.
