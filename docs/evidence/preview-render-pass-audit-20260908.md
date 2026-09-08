# Preview render-pass audit — 2026-09-08

## Scope and result

This read-only audit compares the preview target, pass, framebuffer, graphics
pipeline, dynamic state, and same-frame ImGui sampling path. It establishes no
attachment, resolve, or pipeline/render-pass compatibility defect for DEF004.

`CharacterPreview::createFBO` requests RGBA8, depth, and 4x MSAA. In
`src/rendering/vk_render_target.cpp:81-118`, the pass uses a 4x RGBA8 color
attachment, a 1x RGBA8 resolve attachment, and a 4x D32 depth attachment. The
framebuffer preserves that exact order at line 164. The resolve is stored and
ends in `SHADER_READ_ONLY_OPTIMAL` at line 98; the outgoing dependency at lines
137-145 makes color writes available to fragment-shader reads. Clear-value
counts and indices match the three attachments at lines 296-310.

The character pipelines are created against this target's render-pass handle
and sample count. `src/rendering/character_renderer.cpp:328` applies that
sample count to all five main pipeline variants. No format, sample-count,
subpass, or framebuffer-order mismatch was found. The world pass orders depth
before resolve and ends the resolve in `PRESENT_SRC_KHR`; those differences are
consistent with its own subpass references and framebuffer and do not imply
that the preview pass is incompatible.

The preview's sampled descriptor is registered at
`src/rendering/character_preview.cpp:449` against the resolved single-sample
view. The render graph records the preview pass before the main and overlay
passes (`src/rendering/renderer.cpp:1108`), and ImGui consumes that descriptor
later in the same command buffer at line 1232. The render-pass outgoing
dependency covers that write-to-read transition. No same-frame resource
destruction occurs on this path; explicit character replacement waits for the
device before dropping instance resources, and the enclosing renderer shutdown
wait was established in the earlier ownership audit.

## Dynamic-state finding

`VkRenderTarget::beginPass` sets the dynamic viewport and scissor. The character
pipelines declare viewport, scissor, and depth bias dynamic at
`src/rendering/character_renderer.cpp:334`. Normal material batches set depth
bias before drawing at line 3132. The whole-model fallback previously drew
without setting depth bias itself. Because these pipelines also enable depth
bias, a model with no preceding material batch could reach a draw without
defining the required dynamic value. The fallback now sets neutral depth bias
immediately before its shared indexed/non-indexed draw. The recorded failing
character has ordinary material batches and validation did not report an unset
dynamic-state VUID, so this correction is not established as DEF004's cause.

## Next discriminating pair

If a valid rasterizer-discard run completes while its same-binary nondiscard
control loses the device, the next narrow pair should keep nondiscard,
non-indexed drawing and the same tiny vertex and constant fragment shaders in
both runs. A default-off preview-only flag should select a 1x color/depth target
for one run; the control should retain the current 4x color/resolve/depth target.
This pair preserves the fragment and raster paths while changing the MSAA
attachment, resolve, and pipeline sample state together. A 1x pass and 4x loss
would implicate that combined branch, but would not distinguish resolve from
sample count. It must not be compared causally with rasterizer discard because
Vulkan requires the discard pipeline to omit the fragment stage.

The preview currently requests 4x without clamping it to the device's supported
color/depth sample counts. This is a portability limitation, but the recorded
device successfully created the 4x target and pipelines, so it is not evidence
for the observed failure.
