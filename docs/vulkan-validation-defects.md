# Vulkan validation defects

## DEF-001 Â· P0 Â· Primary diagnostics inside a secondary-only scene subpass

Status: fix implemented; post-fix GPU validation pending. Parent: EVAL-01;
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
