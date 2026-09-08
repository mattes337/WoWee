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
change.

The fixture-local missing-shader run recorded in
`live-login-15-missing-character-fragment-20260908.json` exercised a real
failure after material resources were initialized. With the copied character
fragment shader intentionally absent, each preview retry reported the same
three expected error kinds and the process completed its ordered authentication,
character-list, input-trace, and shutdown lifecycle with Vulkan validation
enabled. Shutdown reported zero live VMA allocations in three allocator blocks;
no other ERROR or FATAL kind occurred. This validates final cleanup for the
shader-load failure and repeated preview retry path on the executable identified
by its SHA-256. It does not cover injected allocator/Vulkan creation failures,
the world-renderer owner branch, or successful preview rendering. The persisted
stdout binds the executable to source revision
`f0411db78b04bddfe0b77a8a26f3251389a2eadf`; the compact evidence also records
the executable and stdout SHA-256 values.
