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

The follow-up per-character failure gate suppresses automatic reinitialization
after `CharacterPreview::initialize` fails for the currently selected nonzero
character GUID. Selecting a different character permits one attempt; clicking
the selected row again, refreshing the list, resetting the screen, or replacing
the asset manager explicitly permits another. Healthy preview reuse and model
load failures are outside this gate.

The paired runs in `live-login-16-17-preview-retry-gate-20260908.json` execute
that contract on one immutable client binary. Run 16 produced one occurrence of
each expected shader-initialization error across 1,800 updates. Run 17 used one
explicit click on the selected Woweetrial row at update 900 and produced exactly
two of each. Both completed the ordered authentication, character-list, input,
and shutdown lifecycle without starting world entry or reporting another error
kind, and both ended with zero live VMA allocations in three blocks. This proves
the selected-GUID suppression and explicit same-row retry paths for this shader
failure. It does not exercise another GUID, either Refresh path, allocator or
Vulkan creation failures, world-renderer ownership, or successful preview
rendering. The binary's source marker ends in `-dirty`, so its SHA-256 is the
immutable identity for these runs.
