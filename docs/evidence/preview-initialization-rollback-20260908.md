# Preview initialization rollback

Commits `9276b87f4` and `ecc8821d2` repair two source-confirmed failure paths.

Previously, `createFBO()` returned no status. Failures after creating the render
target could leave its validity check true, allowing initialization to continue
with missing UBOs or descriptor sets. It now returns success only after the
entire resource sequence completes, rejects a null mapped UBO pointer, and
destroys partial resources on failure. Cleanup clears mapped pointers and
descriptor handles.

If the subsequent character-renderer initialization failed, the complete FBO
remained owned by an incomplete preview. Retrying replaced the render target,
whose destructor deliberately does not release Vulkan objects. The failure
branch now resets the character renderer and explicitly destroys the FBO.
Character creation also releases a failed preview so a later entry can retry.

The renderer destructor performs its existing worker/device wait before the
FBO is destroyed. Earlier FBO construction failures occur before any preview
composite submission; the preceding image transitions and clear use existing
synchronous submission helpers.

Validation at implementation time is source and cleanup-order review plus
`git diff --check`. Allocation failure has not been injected into the real
Vulkan/VMA workflow; no synthetic predicate test is presented as ownership
coverage. These repairs do not establish the cause of DEF-004.
