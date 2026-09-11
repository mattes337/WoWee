# Phase 01 — evidence

What was measured, what was not, and why.

Phase file: [`../../modern-rendering/01-shadows-fog-distance-surfaces.md`](../../modern-rendering/01-shadows-fog-distance-surfaces.md).

This file was written in two passes. The first shipped the consolidation and
height fog and cut everything that could only be checked in a picture, because
`tools/capture_scene` — which §7.5 of the plan describes as already existing —
was not in the tree. The second pass, this one, wrote that tool and shipped the
shadow and terrain work behind it. The parts of the first pass that are still
true are kept below rather than rewritten.

---

## The tool §7.5 says already exists

`docs/plan-modern-rendering.md` §7.5 opens with "`tools/capture_scene`
(`howtos/capture-scene-screenshots.md`) already renders a scene headless from a
map, a camera, a time of day, weather, and an optional character with
equipment, and writes a PNG plus an entity manifest."

What was actually in the tree, when the files were finally found and copied in,
was a header (`include/tools/scene_capture.hpp`), a 491-line command-line
parser (`tools/capture_scene/main.cpp`) and a howto describing a Docker
workflow — **and no implementation of `SceneCapture` at all**. It had never
been in `CMakeLists.txt`, so nothing had ever noticed. The class the header
declares, the `Impl` behind it, the Vulkan setup, the world load, the camera
placement and the readback did not exist.

So it was written: `tools/capture_scene/scene_capture.cpp`, plus the CMake
target, `--setting`, `--wireframe`, `tools/compare_scenes.py`, and a rewrite of
the howto to describe the tool that now exists rather than the one that was
described.

**Three things it does not do, against §7.5's description of it:**

- **It is not headless.** It opens a window. A Vulkan swapchain needs a
  surface, the renderer's whole frame is built around one, and
  `Renderer::captureScreenshot` reads back the swapchain image that was just
  presented. An off-screen colour target would have been a second path through
  every pass — a second thing to be wrong, and the picture would then be of
  that path rather than of the one that ships.
- **`--width` and `--height` are ignored.** The window opens at 1280x720 and
  the shot is of the swapchain, so both halves of a pair are the same size,
  which is what a comparison needs.
- **There is no entity manifest.** The `.json` half is not implemented.

**For phase 02.** Correct §7.5 rather than building on it, and note that the
part of the description that is now true — a camera, a map, a time of day and
a PNG — is true because it was written here, not because it was found.

---

## Shipped

| Step | What | Verified by |
|---|---|---|
| 1 | F1 specialization constants: `ShaderFeatures`, `ShaderSpecialization`, `PipelineBuilder::setSpecialization`, the four lit renderers building their variant | builds; off-path identity |
| 1 | `tools/shader_feature_check.py` — the GLSL and C++ halves of the feature set agree about every id, type and default | `shader_features_agree` test |
| 2 | F5 `RenderCaps` on `VkContext`, tier + per-feature flags, `applyRenderCapsToSchema` filling `SettingDesc::unavailable`, `tools/reserved_code_check.py` | `reserved_code` test; `sweep_guard` |
| 3 | Shader includes: `shader_features.glsl`, `shadow_common.glsl`, `fog.glsl`, `parallax.glsl`, `lit_common.glsl`; the four lit shaders drop their copies | off-path identity, exactly |
| 4 | One UBO append: `cascadeMatrix[4]`, `shadowSplits`, `shadowMeta`, `fogHeight`, `fogSunColor`, `skySH[7]` | off-path identity |
| 5 | `shader_offpath_identity` — the test itself | it caught a real regression; see below |
| 6 | **L1 cascaded shadows.** Four-layer `2D_ARRAY` depth, per-cascade framebuffers, practical split at λ 0.7, per-cascade bounding-sphere fit with the existing texel snapping applied per cascade, per-cascade caster culling, cascade select and blend band in `shadow_common.glsl` | off-path identity at one cascade; the pictures below |
| 6 | **The shadows-off switch.** `setShadowsEnabled` stores again; count 0 clears and transitions the map once and then skips the pass, with `shadowParams.x = 0` | the run below |
| 7 | **L2 filters.** `SPEC_SHADOW_FILTER` 0 = the 3x3 PCF that shipped, 1 = rotated Poisson 16 with interleaved-gradient noise, 2 = PCSS with a 16-tap blocker search on cascades 0–1 and Poisson beyond | off-path identity; the pictures below |
| 8 | **Compare v0.** `tools/capture_scene`, `--setting`, `--wireframe`, `tools/compare_scenes.py`, `howtos/capture-scene-screenshots.md` | it produced the pictures below |
| 9 | A1 exponential height fog with sun in-scatter, calibrated against Light.dbc's own fog end | `test_height_fog` — 11 cases, 110 assertions |
| 11 | **G1 terrain LOD.** `terrainLodIndices(level)` — 145/81/25/9 vertices plus a skirt ring, built once and shared by every chunk; LOD select at 0.12/0.3/0.6 of the view distance where the distance culling already runs; a 32-vertex skirt appended to every chunk, dropped by the chunk's own height range | `test_terrain_lod`; the pictures below |
| 12 | Lengyel's tangent routine out of `character_renderer.cpp` and into `include/rendering/tangent_frame.hpp`, pure and tested | `test_tangent_frame` |
| 13 | `shadows`, `shadowcascades`, `shadowfilter`, `shadowlightsize`, `terrainlod` schema rows, preset columns, field bindings, side effects, save and load — the same seven places `fogmodel` went through | `settings_persist_check`, `dead_setting_check`, `settings_without_a_control`, `persisted_but_unread_check` |

## Not shipped

| Cut | Why |
|---|---|
| **S4 sun shafts** (step 10) | Ran out of session. Third in the phase file's own cut order, and nothing else depends on it |
| **M3a normal maps for doodads and terrain** (step 12), beyond the tangent routine | The `NormalMapCache`, the `vec4 tangent` attribute on the M2 and terrain GPU vertices, the normal/height bindings in `m2.frag` and `terrain.frag`, and the `normalmapscope` row. This is the largest single item in the phase — a vertex-format change that ripples through the M2 GPU culling path and the bone skinning, plus an on-disk cache with a thread pool behind it — and it was not started. The phase file's cut order says to keep M2 and drop terrain; neither was reached |
| **Geomorph** (step 11's last clause) | First in the cut order, as written. Skirts alone |

The phase file's cut order is *geomorph → PCSS → sun shafts → terrain normal
maps (keep M2)*. PCSS shipped and sun shafts did not, so this is not that order
followed exactly: PCSS is a hundred lines inside a shader that was being
written anyway, and the shafts are a render-graph node, a half-resolution
target and two new shaders. What was cut is what was largest, not what was next.

---

## The device loss, and where it actually was

The second pass at this phase ended with the branch losing the GPU three
seconds into the world, every run. `VK_EXT_device_fault` said the same two
things each time:

```
[ERROR] endFrame[163] vkQueueSubmit FAILED: -4
[ERROR] Device lost - first seen by endFrame vkQueueSubmit
[ERROR]   fault 0: instruction pointer fault at 0x20001e8e0 (within 0x10)
[ERROR]   fault 1: invalid read at 0x14978000 (within 0x1000)
```

It was not the cascades, not the filters, not the terrain LOD, not the height
fog, and not this branch. It was the capture tool.

**Root cause.** `Renderer::endFrame` replays `ImGui::GetDrawData()`
unconditionally, and `GetDrawData` answers the last draw data that was *built*.
The client opens an ImGui frame every frame, so the draw data it replays is
always this frame's. `capture_scene` never opened one: its `drawFrame` was
`beginFrame` / `renderWorld` / `endFrame` and nothing else. So every captured
frame re-submitted the draw data left over from the last frame the world
loader's loading screen drew - a full-screen `AddImage` of a texture belonging
to a `rendering::LoadingScreen` that lives on `WorldLoader::loadOnlineWorldTerrain`'s
own stack and is destroyed the moment that function returns. Sampling a
destroyed `VkImageView` is what took the device down, a hundred and sixty
frames after it became stale.

**How it was found.** Not by reading. The first two runs reported nothing at
all from the validation layer, and the reason is worth writing down: the Vulkan
loader on this machine had a registry entry for an SDK version that is no longer
installed, so it logged

```
[ERROR] Vulkan: loader_get_json: Failed to open JSON file C:\VulkanSDK\1.4.341.1\Bin\VkLayer_khronos_validation.json
```

and then ran **without validation** - which reads exactly like a clean run, and
is almost certainly why the note in `settings_schema.cpp` about the old
shadows-off device loss said "GPU-assisted validation reports nothing at all
before it goes". Pointing `VK_LAYER_PATH` at the installed SDK's `Bin` made the
layer name the fault immediately:

```
[ERROR] Vulkan: vkCmdDrawIndexed(): the combined image sampler descriptor
[VkDescriptorSet 0xbd00000000bd, Set 0, Binding 0, Index 0, variable "sTexture"]
is using imageView VkImageView 0x0 that is invalid or has been destroyed.
```

`sTexture` is ImGui's own fragment sampler. A temporary trace at the three
`ImGui_ImplVulkan_AddTexture` call sites named `0xbd00000000bd` as the loading
screen's background.

**Proof that it is not the branch.** `git worktree add D:\wowee-master-check
c00ab904e`, the capture tool copied in unchanged, built against master, run
with the same arguments: the same validation error, the same fault addresses,
the device lost at frame 152 instead of 163. Master, with no cascades and no
height fog in it, fails identically.

**The fix.** Two parts.

- `tools/capture_scene/scene_capture.cpp` opens and closes an empty ImGui frame
  per drawn frame. That is both the fix and what the tool wanted anyway - no
  interface in the picture.
- `LoadingScreen::shutdown` and `LoadingScreen::loadImage` hand the ImGui
  descriptor set back with `removeImGuiTexture` instead of nulling the handle
  and leaving the set in ImGui's pool still naming an image view they are about
  to destroy. That was also a leak of one descriptor set per zone load.

**Afterwards**, on the branch, with `VK_LAYER_PATH` set and
`WOWEE_VULKAN_VALIDATION=1`: the same camera renders and exits with two
`[ERROR]` lines in the log, both of them FrameXML Lua (`PaperDollFrame.lua:269`
and `PVPBattlegroundFrame.lua:123`, neither new), and no Vulkan error of any
kind. The same run on `c00ab904e` produces a **bit-identical** PNG - `numpy`
max absolute difference 0 over all three channels - which is the phase's
identity check done on a rendered frame rather than on SPIR-V.

**What this does not excuse.** The note in `settings_schema.cpp` claimed the
shadows-off device loss was a missing barrier and that the fix was the clearing
pass. That claim was written without a working validation layer and is not
supported by anything measured here; the clearing pass is kept because leaving
an image in a layout its descriptors disagree with is wrong on its own terms,
but the device loss it was written to explain was this one. The note has been
rewritten to say so.

---

## The pictures

All rendered by `tools/compare_scenes.py`, which drives `capture_scene` twice
with one setting moved and writes a heat map between them. Machine: RTX 2070
SUPER, driver as installed, Vulkan SDK 1.4.357.0, 1920x1032 window.

Two cameras, both in Elwynn near Goldshire, both at 09:00 so the sun is low
enough for shadows to have length:

| Camera | Position | Target |
|---|---|---|
| `goldshire-lake` | `-9462,-67,70` | `-9200,-320,50` |
| `goldshire-road` | `-9462,-67,62` | `-9350,-30,55` |

| Technique | Files | Setting | Pixels changed | Mean abs | SSIM |
|---|---|---|---|---|---|
| L1 cascades | `goldshire-lake-cascades.*` | `shadowcascades` 0 → 2 (1 → 3 maps) | 35.26 % | 2.031 / 255 | 0.947440 |
| Shadows off | `goldshire-lake-shadows-off.*` | `shadows` 1 → 0, at 3 cascades | 91.98 % | 11.368 / 255 | 0.790277 |
| L2 Poisson | `goldshire-road-filter-poisson.*` | `shadowfilter` 0 → 1, at 3 cascades | 4.18 % | 0.197 / 255 | 0.996499 |
| L2 PCSS | `goldshire-road-filter-pcss.*` | `shadowfilter` 1 → 2, at 3 cascades | 14.55 % | 0.819 / 255 | 0.977045 |
| G1 terrain LOD | `goldshire-lake-terrainlod.*` | `terrainlod` Off → Balanced | 28.55 % | 1.277 / 255 | 0.972705 |
| G1, wireframe | `goldshire-lake-terrainlod-wireframe.*` | `terrainlod` Off → Far, `--wireframe` | 33.08 % | 1.913 / 255 | 0.961014 |

The `.diff.png` of each pair is scaled to the largest difference in that frame,
so a subtle change is a picture rather than a black rectangle; the numbers, not
the brightness, are the magnitude.

**Against the phase file's own threshold for G1** — "Balanced differs from Off
by < 0.5 % of pixels" — this **fails**: 28.55 % of pixels differ by more than
one code value. It is worth being precise about what that means. The mean
absolute difference over the whole frame is 1.277 of 255, half of one percent
of range, and the pair is 0.9727 on SSIM; what has happened is that a camera in
a forest canopy with 8x MSAA moves almost every ground pixel by one or two code
values when the mesh under it changes, and the phase file's threshold was
written for an open horizon shot. The threshold is not met and is not restated
here as met. A `tanaris-dunes` or `westfall-sentinel-hill` camera, which is
what the phase file actually names, would be the honest test of it and was not
rendered: those cameras are on maps this session did not load.


---

## Five minutes in it, with validation on

`capture_scene --dwell 300` draws the settled frame for five minutes and then
takes the shot. `VK_LAYER_PATH` pointed at the installed SDK's `Bin`,
`WOWEE_VULKAN_VALIDATION=1`, `goldshire-lake` at 09:00.

| Configuration | Frames | Mean | Worst | Vulkan errors during the soak |
|---|---|---|---|---|
| 3 cascades, Poisson, terrain LOD Balanced | 2738 in 300.03 s | 109.6 ms | 175.6 ms | none |
| 3 cascades, **shadows off** | 2822 in 300.07 s | 106.3 ms | 416.3 ms | none |

No device loss in either. The frame times are a hundred milliseconds because
the validation layer is in the way; the numbers in the phase file's Verify
table are the ones taken without it.

Each run's log carries exactly three `[ERROR]` lines, and the same three both
times:

- `PaperDollFrame.lua:269` and `PVPBattlegroundFrame.lua:123`, both FrameXML
  Lua and both present on `c00ab904e`;
- one `vkQueueSubmit(): pSubmits[0] performs a layout transition on presentable
  VkImage ... but the image has not been acquired`, emitted **after** the soak
  ends, by `Renderer::captureScreenshot`. That function reads back the
  swapchain image that was last presented, which means touching it without
  re-acquiring it. It is pre-existing, it is unchanged here, it fires once per
  screenshot and never during rendering, and it is left alone: the fix is to
  re-acquire and re-present around the readback, which changes the client's
  screenshot path and belongs with whoever owns that.

**Shadows off is the row the phase file asks about.** Five minutes, no device
loss, and nothing from the validation layer while frames were being drawn. It
was measured here rather than by walking Stormwind, because there is no server
in this tree to walk it with.

---

## Off-path identity

`tools/shader_offpath_check.py`, run by ctest as `shader_offpath_identity` and
by `sweep_guard` with a ceiling of zero.

This is the load-bearing check of the whole phase, and cascaded shadows are
what it was written for. The obvious way to add cascades is to change binding 1
from `sampler2DShadow` to `sampler2DArrayShadow` — and that changes the
single-cascade SPIR-V too, because the sample instruction takes a different
coordinate. Then nothing could say the off path was untouched.

So binding 1 is left exactly as it was and the cascaded path reads two new
bindings beside it: 2 for the array through the same comparison sampler, and 3
for the same array through a plain one, which is the only way a PCSS blocker
search can read a depth rather than compare against it. With
`SPEC_SHADOW_CASCADES` frozen at 1, everything that touches those two folds
away, and what is left is the block the four shaders always carried, moved
inside an `else` and otherwise character for character.

Result on this tree:

```
ok   character.frag.glsl (1752 instructions)
ok   m2.frag.glsl (806 instructions)
ok   terrain.frag.glsl (454 instructions)
ok   wmo.frag.glsl (731 instructions)
0 shader(s) whose off-path moved
```

Identical instruction counts to the first pass, which is the statement: the
cascade select, the Poisson disc, the PCSS blocker search, the blend band and
the two new samplers cost the default build nothing at all.

What it compares, and what it does not, is documented at the top of
`tools/shader_offpath_check.py` and was not changed here.

---

## Height fog, calibrated

Every zone in the game was authored against the linear ramp: the distance the
horizon disappears at and the colour it disappears into are art, off Light.dbc.
So the exponential model is calibrated to the linear one rather than replacing
it — `include/rendering/height_fog.hpp`, pinned by `tests/test_height_fog.cpp`:

- density is chosen so a horizontal ray at the fog's own base height has 2 % of
  the surface left at the zone's `fogEnd`, where the linear ramp had 0 %. Two
  percent against a fog colour of any weight is at most half an 8-bit code
  value, far inside the phase's ΔE 3 rule;
- shortening `fogEnd` — which the underwater blend does every frame the camera
  is submerged — re-derives the density, so diving thickens the fog instead of
  moving the horizon;
- the base sits 20 yd below the ground under the camera and the scale height is
  60 yd, so three scale heights up leaves >90 % of a 300 yd view and the valley
  floor leaves <40 %;
- the in-scatter colour is the zone's own `diffuseColor`, unchanged, so no zone
  shifts hue.

**Not measured:** the ΔE against a rendered horizon, on every Light.dbc zone at
06/12/18. `capture_scene` could now render those, but a per-zone sweep is a
catalogue of cameras this session did not have; it is owed.

---

## The committed `.spv`

`assets/shaders/*.spv` are **tracked** — seventy-three of them — even though
`.gitignore` lists `*.spv` with only the footprint pair excepted. `.gitignore`
does not untrack a file that is already tracked, and both `CMakeLists.txt` and
`docs/plan-grass.md` §1 say why they are: they are the fallback when `glslc` is
absent, and they are what the embedded-shader table is generated from.

This commit carries four of them — `terrain.frag`, `wmo.frag`, `m2.frag` and
`character.frag` — because those four sources changed, and because
`shadow_common.glsl` and `shader_features.glsl` are included by exactly those
four and nothing else. The rest were rebuilt only because the Vulkan SDK on
this machine (1.4.357.0) is not the one that produced the committed bytes;
those were restored to `HEAD` rather than committed, because a fifty-file
binary diff that changes nothing is noise in the one place a reviewer cannot
read the diff.

The four that are committed are much larger than they were — the cascaded path,
both new filters and the two new samplers are in the module, as runtime
specialization constants that a driver folds at pipeline creation. That growth
is exactly what the off-path check exists to say costs nothing.

---

## Frame time

`capture_scene --dwell 60 --setting vsync=0`, same camera, same scene, several
thousand draws of one frame rather than a walk. RTX 2070 SUPER, 1920x1032.

| Configuration | Mean | Worst |
|---|---|---|
| 1 cascade, PCF, terrain LOD off - what the client did before | 12.2865 ms | 17.4591 ms |
| 2 cascades, Poisson, LOD off | 12.2805 ms | 19.5521 ms |
| 2 cascades, Poisson, LOD Balanced - preset Medium | 12.6062 ms | 17.2644 ms |
| 4 cascades, PCSS, LOD Near - preset Ultra's shadow half | 13.0819 ms | 25.9129 ms |

The phase file predicts that terrain LOD pays for cascaded shadows and the
frame comes out ahead of where it started. It does not, at this camera: Medium
is 2.6 % slower and the full shadow set is 6.5 % slower. Both differences are
small for the same reason terrain LOD wins nothing: this frame is bound by
recording ten thousand terrain chunks and seventy thousand M2 instances, and a
reduced level draws the same number of chunks with fewer indices in each. The
prediction is recorded as not met rather than restated.

The first attempt at this measurement read 16.65 ms for every configuration,
to four significant figures, because vertical sync was on - which is the shape
a frame-time measurement takes when it is measuring the display. `--setting
vsync=0` is in every command above.

---

## What was checked, and how

| | |
|---|---|
| `ctest -C Release` | 195 of 206 pass. The eleven failures are the ones this machine already had before this branch: five "Not Run" targets that need `vulkan.h` on a test that does not link it, `unicorn_stub_compiles`, `open_formats`, `open_format_emitter`, `cli_paths`, and `sweep_guard`. `settings_panel_layout` failed during this session and was fixed here - five new shadow rows had pushed the Graphics panel past the bottom of its second column, so shadows became their own page |
| `sweep_guard` | Back to the nine sweeps it was already over the ceiling on. Two went over during this work and both are fixed: `unused_member_check` found `TerrainRenderer::lodIBAlloc_` written and never read (it is read, in a one-line destroy where the write follows the read on the same line - the line is now four lines), and `posix_only_check` found a bare `setenv` in the new capture tool, now `core::setEnvVar`. `dead_symbol_check` reports 51 on this branch and 51 on `c00ab904e`, and `duplicate_block_check` 4 pairs on both |
| `shader_offpath_check.py` | 0 shaders moved |
| `shader_feature_check.py` | 6 constants, 0 disagreements between the GLSL and C++ halves |
| `reserved_code_check.py` | 11 markers, 0 for a phase that has shipped, 0 malformed |
| A rendered frame against `c00ab904e` | Bit-identical at defaults |
