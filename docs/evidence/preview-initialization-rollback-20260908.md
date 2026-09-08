# Preview and character-renderer initialization rollback - 2026-09-08

This note records source-level failure handling. It is not evidence for the
cause or resolution of DEF-004.

`9276b87f4` makes `CharacterPreview::createFBO` all-or-nothing. Failure to
create the dummy shadow image/view, descriptor pool, either mapped per-frame
uniform buffer, either descriptor set, or the ImGui texture registration now
destroys the resources already created and makes preview initialization fail.
A successful VMA allocation with no mapped pointer is also rejected. Cleanup
nulls mapped pointers and descriptor handles so destruction and retry see a
consistent empty state.

`ecc8821d2` covers the next transaction boundary. If the private
`CharacterRenderer` fails to initialize, its destructor runs its existing
worker/device completion and partial-resource cleanup before the completed FBO
is destroyed. The character-creation screen drops that failed preview, allowing
a later screen entry to retry instead of retaining an object with no camera.

`be0537134` checks the material and bone descriptor layouts and pools, both VMA
mapped material rings, the pipeline layout, both shader loads, and all five
required main pipelines. Each failure identifies the resource and Vulkan result
and rolls partial state back through `CharacterRenderer::shutdown`. The world
renderer owner resets a failed renderer and does not attempt shadow-pipeline
initialization through it. Shutdown clears ring mapped pointers even when no
buffer handle was returned.

Review established that the cleanup functions tolerate null and partially
created handles and that `CharacterRenderer` installs its `VkContext` before the
first checked allocation. No allocator or Vulkan creation failure was injected:
these calls currently reach VMA and Vulkan directly, and adding a mockable API
layer was outside this bounded correction. `git diff --check` passed for each
change. Compilation and healthy-path runtime results, if any, must be recorded
with the build or run that actually executes them; this note claims neither.
