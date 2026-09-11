# Phase 01 — evidence

What was measured, what was not, and why.

Phase file: [`../../modern-rendering/01-shadows-fog-distance-surfaces.md`](../../modern-rendering/01-shadows-fog-distance-surfaces.md).

This file was written in four passes. The first shipped the consolidation and
height fog and cut everything that could only be checked in a picture, because
`tools/capture_scene` — which §7.5 of the plan describes as already existing —
was not in the tree. The second wrote that tool and shipped the shadow and
terrain work behind it, and cut the sun shafts and the normal maps for time.
The third shipped those two and set out to re-run the terrain LOD measurement at
the open-horizon cameras the phase file actually names — and rendered nothing at
all, because the extracted asset tree was gone by the time it tried. The fourth,
this one, got the assets back by not needing them: the client reads the game's
own MPQ archives, so there is nothing to extract, and everything the third pass
owed has been rendered and measured. The parts of the earlier passes that are
still true are kept below rather than rewritten; the ones that said "not
measured, blocked" have been replaced by what was measured.

**Every number in this file was taken on an RTX 2070 SUPER, Vulkan SDK
1.4.357.0, Windows 11, reading `G:\WoW AzerothCore`'s eighteen archives
directly.** The pictures are 1280x720 and the frame times are noted with the
resolution they were taken at.

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

## Shipped in the third pass

The second pass cut S4 and M3a for time. Both are here now, and this section of
the file was rewritten rather than left saying they were not.

| Step | What | Verified by |
|---|---|---|
| 10 | **S4 sun shafts.** `sunshaft_mask.frag` over a half-resolution blit of the finished frame, two thirty-two-tap radial blurs into an `R8` pair, and `sunshaft_composite.frag` added over the frame in the interface pass. Gated on `LensFlare::sunOnScreen()`, which is the flare's own arithmetic answered rather than only used, so the two effects agree about whether the sun is there | the pictures below: +4.54 of 255 over the frame at 07:00, +0.07 with the sun behind the camera, 0.0898 ms on the GPU |
| 12 | **M3a tangent frames.** `vec4 tangent` on the M2 GPU vertex (Lengyel over the model's first UV set, 22 floats a vertex now) and on `pipeline::TerrainVertex` (analytic, from the two world axes the chunk's texture coordinates run along) | `test_tangent_frame`, which now pins the handedness the grid actually has |
| 12 | **M3a `NormalMapCache`.** Generation on `ThreadPool::frameWorkers`, source capped at 512², strength 3 for a doodad and 2 for a tileset, maps under `kMinVariance` dropped rather than bound, the rest written to `Data/generated/<expansion>/normals/<hash>.rgba` and read back next run, the directory held under 2 GB oldest-first | `test_normal_map_cache` over the hash and the box filter; `howtos/generated-normal-maps.md`; and by 1551 files and 198.6 MB of them after one cold Elwynn load, read back on the next |
| 12 | **M3a in the shaders.** `m2.frag` reads the map on the free set-1 binding, 3, through `parallax.glsl`; `terrain.frag` reads four of them on bindings 8–11, blended by the same alphas the albedo is. Both behind `SPEC_NORMAL_MAP_EVERYWHERE`, whose GLSL default is off | `shader_offpath_identity`: both modules unchanged at 806 and 454 instructions |
| 13 | `normalmapscope`, `sunshafts` and `sunshaftstrength` schema rows, preset columns, field bindings, side effects, save and load, and the start-up apply | `settings_persist_check`, `dead_setting_check`, `settings_panel_layout` |

## Shipped in the fourth pass

Nothing new was added. What this pass shipped is the four defects a loaded world
found, and the three things that stood between this machine and a loaded world -
both lists are in "What a loaded world found" below - plus one measurement tool:
`capture_scene --dwell` now reports the GPU's own timestamps per pass, which is
the only way a technique costing a tenth of a millisecond can be measured at a
camera whose frame time moves by more than a millisecond between runs.

## Cut

| Cut | Why |
|---|---|
| **Geomorph** (step 11's last clause) | First in the phase file's own cut order. Skirts alone |
| **Parallax occlusion on the ground** (part of step 12) | Half of the order's last item, taken in preference to the whole of it. The ground has its four normal maps and not the march for relief over them: `parallaxOcclusionMap` reads one sampler by name, and the layer a terrain pixel would march is whichever of four has the highest alpha there, so the cost would have been a per-pixel choice between four inlined copies of a sixty-four-step loop. The bump is the part that shows at standing height; the relief is the part that shows on a wall |

The phase file's cut order is *geomorph → PCSS → sun shafts → terrain normal
maps (keep M2)*. Across the two passes what actually went is geomorph, and then
the march for relief over the terrain's maps rather than the maps themselves —
which is less than the last item rather than more, and is the only place either
pass cut something the order does not name in those words. PCSS shipped out of
order in the second pass because it is a hundred lines inside a shader that was
being written anyway; the shafts and the terrain maps, which that pass cut, are
both here.

---

## The third pass had no pictures, and the fourth did not need an extraction

The third pass could render nothing. `D:\wowee-phase01\Data\extracted` was a
junction into a checkout this worktree does not own, that checkout emptied it,
and `asset_extract` could not rebuild the tree because StormLib was not in this
build's vcpkg install. The client started, loaded FrameXML, drew its minimap
over a flat grey frame and exited cleanly, and the two techniques that pass
added had nothing to act on.

**What was wrong was the premise, not the tree.** This client does not need an
extracted tree: `Application::initialize` calls `pipeline::detectGameInstall`
on `WOW_INSTALL_PATH`, hands the archives it finds to
`AssetManager::setGameArchives`, and `AssetManager::initialize` then reads
everything out of them when there is no `manifest.json` to read instead. That
path is the whole point of the drop-in contract, it has been in the tree the
whole time, and `capture_scene` was already on it, because it drives
`core::Application` rather than a loader of its own.

Three things stood between that and a picture, and all three are fixed here.

- **StormLib was absent, so the archive path could not be compiled.** It is a
  vcpkg port; installed into this build's own `vcpkg_installed`, the configure
  line now says `wowee direct MPQ reads: ENABLED`. It could not be installed
  before that because CMake then refused to configure at all: the
  `asset_extract` block ends in a `FATAL_ERROR` when it cannot find LibTomCrypt
  and LibTomMath as separate libraries, and vcpkg's StormLib bundles them. The
  same file's `wowee_link_stormlib` already says, in a comment, that a bundled
  StormLib needs neither. That `FATAL_ERROR` is a `STATUS` line now, and it is
  the reason the third pass believed the machine could not read its own
  archives.
- **`capture_scene` had no way to name the installation.** It took `-d <data>`
  and nothing else, so the archive path could only be reached by exporting an
  environment variable the tool's own help did not mention. `--install <path>`
  sets `WOW_INSTALL_PATH` the way `-d` sets `WOW_DATA_PATH`.
- **A failure stopped the tool on a modal dialog.** `showStartupError` opens a
  message box unless `WOWEE_NO_ERROR_DIALOG` is set, and the first archive
  failure here sat on one for ten minutes with nobody to click it. The tool sets
  that variable now: a harness is exactly what the flag was written for.

With those three, `Data/` holds no manifest and no extracted tree and the log
reads:

```
[INFO ] Found game installation: G:\WoW AzerothCore (wotlk, locale enUS, 18 archives)
[INFO ] No manifest in D:\wowee-phase01\Data; reading the installation's archives directly
[WARN ] Interface fonts loaded: 5 of 5 from the archives
[WARN ] AddonManager: 23 addon(s) in the installation's archives
[INFO ] Online terrain streaming complete: 49 tiles loaded
[WARN ] SLOW renderWorld breakdown: ... terrain chunks drawn=2711 culled=9828 resident=12539
```

---

## What a loaded world found, and what it cost

Four things were wrong that only a frame could have shown. Every one of them is
fixed in this commit.

**1. The deferred descriptor writes were not deferred far enough, and every
normal map that arrived invalidated command buffers.** This is the one the third
pass named as the risk it was carrying, and it was right to.
`VkContext::deferAfterAllFrameFences` queues a callback to every frame slot and
runs it when the last slot's fence has been waited on - which is correct for
destroying something, because nothing records a destroyed object again, and
wrong for writing a descriptor, because every frame records the same set again.
By the time the counter reached zero the other slot had been re-recorded and
re-submitted with that set bound in it. The validation layer said so on the
first map that landed:

```
[ERROR] Vulkan: vkUpdateDescriptorSets(): pDescriptorWrites[0].dstBinding (3) ...
        is in use by VkCommandBuffer 0x1efdd931510.
        VUID-vkUpdateDescriptorSets-None-03047
```

and then a hundred and fifty more lines as the command buffers that named those
sets went invalid and their `vkCmdBeginRenderPass`, `vkCmdDrawIndexed` and
`vkEndCommandBuffer` were all refused. `VkContext::deferUntilAllFramesIdle` is
the operation that was actually wanted: it waits on the frame timeline's last
value at the top of `beginFrame`, so nothing submitted is still running and this
frame has recorded nothing yet, and then runs the queued writes. After it, a
run that binds 1 280 maps over 12 800 terrain chunks and 3 328 doodad batches
produces **no `vkUpdateDescriptorSets` error at all**.

**2. The capture tool had been putting the player model on the lens.** The line
that aims the shadow cascades - `renderer->getCharacterPosition() = renderPos` -
is also what `Renderer::update` syncs the character instance to, so every shot
this tool has ever taken carried a wall of robe or a forearm across a corner of
the frame, at whatever part of the model the near plane cut. It is in the
pictures the second pass committed. The model is hidden now unless `--char`
asked for one.

**3. Two renders of one camera were not the same frame.** The clouds drift, the
water moves and the foliage sways by the fixed 1/60 the tool hands
`Renderer::update`, so their phase is a function of the frame count - and the
settle loop stopped when the terrain streamer went quiet, which is a different
count every run. A sun-facing camera read **94.5 % of pixels changed and SSIM
0.881 between two renders whose only difference was one setting**, almost all of
it sky that had moved. The shot is taken on a fixed frame now (`kShotFrame`,
900), and the doodads' animation phases - drawn from a `random_device`-seeded
generator so a stand of trees does not sway in unison - are pinned by
`WOWEE_M2_ANIM_SEED`, which the tool sets and nothing else does. The same camera
now reads 26.6 %, and the Goldshire lake camera 0.2 %. The floor is measured per
camera below rather than assumed away.

**4. PCSS was rendering as Poisson.** Moving `shadowfilter` from Poisson to
PCSS changed **0.042 % of pixels at a mean of 0.001 / 255** - this scene's own
noise - and moving `shadowlightsize` from 1.5 yd to 5 yd changed 0.036 %, which
is a setting doing nothing at all. The blocker search ran; what it computed was
`(receiverDepth - blockerDepth) / blockerDepth`, similar triangles for a point
light, in normalized shadow-map depth. A cascade is orthographic and its light
is directional, so that quantity is neither a length nor a proportion of one:
for a cascade a few hundred yards deep it lands around a twentieth, which widens
a two-texel filter to three. `cascadeDepthRange()` reads the cascade's depth
extent out of its own matrix the way `cascadeHalfExtent()` already read its
width, so the depth difference becomes yards, and the penumbra is that
separation times the light's width times a stated exaggeration - the sun's real
half-degree would be a four-centimetre edge under a five-yard eave. Afterwards
`shadowlightsize` 1.5 → 5 yd moves **12.25 % of pixels, SSIM 0.9918**.

And one thing that was wrong and is not this phase's: **the character pipeline
declares `VK_DYNAMIC_STATE_DEPTH_BIAS` and its whole-model fallback path never
called `vkCmdSetDepthBias`**, which is the error the third pass recorded as
pre-existing and left alone. It is one line, and it is set now.

**And one thing that looked like a fifth and is not this phase's**: start-up
raises about a hundred and sixty validation errors on the frame the MSAA rebuild
resets the frame synchronisation. It reads as the cascaded shadow path's doing
until the same command is run twice - with every phase-01 key at its off value
it gives 3 one run and 161 the next. See "Five minutes in it" below, which has
the numbers and the attempted fix that made it worse.

---

## What did not need a frame

| | |
|---|---|
| `shader_offpath_check.py` | 0 shaders moved. `character.frag` 1752, `m2.frag` 806, `terrain.frag` 454, `wmo.frag` 731 instructions - the same counts as before the techniques landed, and the same counts after the PCSS penumbra was rewritten, because that code lives behind `SPEC_SHADOW_FILTER == 2` and the default is 0 |
| `shader_feature_check.py` | 7 constants, 0 disagreements between the GLSL and the C++ |
| `reserved_code_check.py` | 15 markers, 0 for a phase that has already shipped, 0 malformed |
| Static memory | `sizeof(pipeline::TerrainVertex)` 44 → 60 bytes, so the terrain mega vertex buffer goes 66 MB → 90 MB, which the startup log prints. The M2 vertex goes 72 → 88 bytes. Both are the `vec4` tangent, on every vertex whether or not its material has a map, because the buffer is uploaded once and the map arrives later |
| The `vkCreateGraphicsPipelines ... Vertex attribute at location 2/3 not consumed` warnings | Not this work's, and pre-existing: the terrain, WMO and M2 shadow pipelines all describe `shadow.vert`'s bone inputs, and the compiled module drops them |


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
with one setting moved and writes a heat map between them. 1280x720, the shot
taken on frame 900 of each run, every asset read out of the installation's
archives.

| Camera | Position | Target or angles | Map | Time |
|---|---|---|---|---|
| `goldshire-lake` | `-9462,-67,70` | `-9200,-320,50` | Azeroth | 09:00 |
| `goldshire-road` | `-9462,-67,62` | `-9350,-30,55` | Azeroth | 09:00 |
| `stormwind-gate` | `-8950,650,120` | `-8700,650,95` | Azeroth | 09:00 |
| `westfall-sentinel-hill` | `-10640,1030,55` | `-10640,1700,20` | Azeroth | 09:00 |
| `tanaris-dunes` | `-7150,-3780,45` | `-8200,-3900,0` | Kalimdor | 09:00 |
| `duskwood-road` | `-10520,-1170,90` | `-11200,-1170,40` | Azeroth | 05:30 |
| `elwynn-sun-0700` | `-9462,-67,120` | `--angles 9.7,129.5,0` | Azeroth | 07:00 |
| `elwynn-sun-0630` | `-9462,-67,120` | `--angles 5.05,132.4,0` | Azeroth | 06:30 |

### The noise floor, first

A camera is rendered twice with the *same* settings and the two are compared.
Nothing below it means anything; everything below is quoted against it. The
world loader draws a wall-clock-dependent number of frames before the fixed
count starts, so the animated half of a scene is not in quite the same place
twice - and how much that matters is entirely a property of what is in frame.

| Camera | Pixels changed | Mean abs | SSIM | What is moving |
|---|---|---|---|---|
| `goldshire-lake` | **0.214 %** | 0.009 / 255 | 0.999650 | a canopy edge and a strip of sky |
| `tanaris-dunes` | **0.322 %** | 0.098 / 255 | 0.997992 | cloud over bare dunes |
| `stormwind-gate` | **5.166 %** | 0.248 / 255 | 0.996464 | two trees and a lot of cloud |
| `westfall-sentinel-hill` | **5.194 %** | 0.296 / 255 | 0.995342 | autumn foliage across the middle of the frame |
| `elwynn-sun-0700` | **26.600 %** | 1.758 / 255 | 0.969403 | a whole Elwynn canopy, seen from above, in wind |

`control-goldshire-lake.diff.png` and `control-elwynn-sun-0700.diff.png` are the
best and worst of those.

Everything in `img/` was re-rendered for this pass, out of the archives, at
1280x720, with the player model out of frame and the shot on a fixed frame -
the second pass's pictures had none of those and are not kept beside these. The
two `goldshire-lake-terrainlod` pairs are removed rather than re-rendered: the
threshold they were measured against was written for an open horizon, and
Westfall and Tanaris are that test. The other three controls' before/after frames are not
kept - they are two copies of a picture that is already in this directory - and
their numbers are the row above.

### M3a — normal maps on doodads and the ground

`normalmapscope` 0 (buildings and characters) → 1 (everything).

| Camera | Files | Pixels changed | Mean abs | SSIM | Floor |
|---|---|---|---|---|---|
| `goldshire-lake` | `goldshire-lake-normalmaps.*` | 37.902 % | 1.437 / 255 | 0.962427 | 0.214 % |
| `goldshire-road` | `goldshire-road-normalmaps.*` | 59.501 % | 1.890 / 255 | 0.947339 | 0.214 % |
| `stormwind-gate` | `stormwind-gate-normalmaps.*` | 5.579 % | 0.205 / 255 | 0.996196 | 5.166 % |

The two Elwynn cameras are the technique working: trunk bark reads as relief,
the grass and the dirt road under the camera gain a light-and-shade grain that
follows the texture, and the canopy picks up contrast. Nothing is black,
nothing is magenta, and the highlights stay on the side the sun is on - which
is the check worth stating, because the terrain tangent's handedness was flipped
from +1 to a derived sign in the previous commit and a wrong sign there shows as
slopes lit from the wrong vertical direction. The WMO walls in
`stormwind-gate-normalmaps.before.png`, whose frame has always been right, light
the same way in both.

**Stormwind is the honest null.** 5.579 % against a floor of 5.166 % is nothing:
that camera is stone and roof, which is WMO geometry that had its normal maps
before this phase, and the strip of terrain in frame is cobble seen at a glancing
angle. `normalmapscope` moves what it says it moves and no more.

### S4 — sun shafts

`sunshafts` 0 → 1, `sunshaftstrength` at its default 0.5.

| Camera | Pixels changed | Mean abs | SSIM | Signed mean | Fully white pixels | Floor |
|---|---|---|---|---|---|---|
| `elwynn-sun-0700` | 98.342 % | 10.921 / 255 | 0.717708 | **+4.54 / 255** | 0.366 % → 1.706 % | 26.6 % |
| `elwynn-sun-0630` | 76.645 % | 3.413 / 255 | 0.948274 | **+1.60 / 255** | 0.144 % → 0.188 % | — |
| `elwynn-sun-behind` | 32.669 % | 1.919 / 255 | 0.941048 | **+0.07 / 255** | 0.000 % → 0.000 % | — |

The signed mean is the column that matters here, because "pixels changed"
counts the canopy noise as well. The shafts *add*: +4.54 code values over the
whole frame at 07:00, +12.3 in the hundred-pixel square around the sun. With the
sun behind the camera - the same position and time, yaw turned 180 degrees -
the frame moves by **+0.07 of 255 and gains no saturated pixel at all**, which
is the "off-screen sun shows nothing" row answered: `renderMask` returns false
before it blits anything.

**At 07:00 the sun stands in open sky above the canopy and the result is a
halo**; at 06:30 it is behind the ridge and the result is rays - the difference
image fans out from a point on the skyline in distinct streaks broken by the
treeline, which is the picture §S4 promises. It is faint (+1.60 mean) because
the flare's own sun visibility, which gates the mask, is `smoothstep(-0.05,
0.25, sunHeight)` and the sun is five degrees up.

**Not blown out.** At 07:00 the fraction of the frame at 255 in all three
channels goes from 0.366 % to 1.706 %, all of it the disc and the sky touching
it; the disc was already at 255 before the shafts were added. At 06:30 it goes
from 0.144 % to 0.188 %.

**The constants were not changed.** The five numbers `sun_shafts.hpp` warns have
never been seen on screen - the mask threshold 0.72 and its softness 0.25, the
radius 0.85, and the two blur passes' decay and weight - produce, at both times
of day, an effect that is visible, directional, and does not clip the frame. The
header's note that the first person with a frame should expect to move them is
kept, because "defensible at two Elwynn cameras" is not "tuned"; what is removed
is the claim that nobody has looked.

### G1 — terrain LOD

`terrainlod` Off → Balanced, at the two open-horizon cameras the phase file
names and which two passes had owed.

| Camera | Pixels changed | Mean abs | SSIM | Floor | Frame time Off → Balanced |
|---|---|---|---|---|---|
| `westfall-sentinel-hill` | **2.854 %** | 0.176 / 255 | 0.997069 | 5.194 % | 12.885 ms → 12.716 ms |
| `tanaris-dunes` | **4.420 %** | 0.270 / 255 | 0.993315 | 0.322 % | 6.505 ms → 6.422 ms |

Frame times at 1920x1032, `--dwell 30 --setting vsync=0`, means over 2 300 and
4 600 frames.

**Against the phase file's "Balanced differs from Off by < 0.5 % of pixels":**
at Westfall the difference is **smaller than that camera's own frame-to-frame
noise** - 2.854 % against a 5.194 % floor - so it cannot be measured there at
all, let alone exceed a threshold. At Tanaris, where the floor is 0.322 %, the
difference is real and is **4.420 %, not under 0.5 %**. The threshold is not met
and is not restated as met. What it is worth saying beside that is the
magnitude: the mean absolute difference over the whole frame is 0.270 of 255,
one code value in a thousand pixels, and the SSIM is 0.9933. The pixels that
move are the silhouettes of dunes against the sky, which is exactly where a
mesh at a quarter of the vertices moves and where the threshold was aimed.

`westfall-sentinel-hill-terrainlod-wireframe.*` and
`tanaris-dunes-terrainlod-wireframe.*` are the same pair drawn as lines, which
is where the triangles went: 13.119 % and 10.709 % of pixels, SSIM 0.9438 and
0.8946.

**It is faster, by 1.3 % at both cameras.** That is small, and smaller than this
measurement's own run-to-run spread; a pair taken at 1280x720 an hour earlier
read +2.0 % at Westfall and −16 % at Tanaris. The direction is consistent at the
resolution the two were taken back to back at, and the honest summary is that
terrain LOD does not cost anything and does not obviously pay either at cameras
bound by draw calls rather than by terrain vertices.

### L1 and L2 — cascades, the off switch, and the filters

Re-run from the archives to confirm nothing regressed.

| What | Files | Setting | Pixels changed | Mean abs | SSIM |
|---|---|---|---|---|---|
| Cascades | `goldshire-lake-cascades.*` | `shadowcascades` 0 → 2 (1 → 3 maps) | 1.446 % | 0.161 / 255 | 0.996963 |
| Shadows off | `goldshire-lake-shadows-off.*` | `shadows` 1 → 0, at 3 cascades | 83.947 % | 10.391 / 255 | 0.796314 |
| Poisson | `goldshire-road-filter-poisson.*` | `shadowfilter` 0 → 1, at 3 cascades | 1.762 % | 0.087 / 255 | 0.999168 |
| PCSS | `goldshire-road-filter-pcss.*` | `shadowfilter` 1 → 2, at 3 cascades | 0.087 % | 0.003 / 255 | 0.999987 |
| Sun width | `goldshire-road-lightsize.*` | `shadowlightsize` 1.5 → 5 yd, PCSS | 12.254 % | 0.578 / 255 | 0.991791 |

The second pass measured the cascade pair at 35.26 % and the PCSS pair at
14.55 %. Both of those runs had the player's own model parked on the lens and
both compared frames whose clouds and foliage had moved; neither number survives
the two fixes above, and these replace them rather than sitting beside them.

**PCSS at the default sun width is still nearly a no-op, and that is now a
statement about the setting rather than about the code.** Before the penumbra
fix, `shadowlightsize` 1.5 → 5 yd moved 0.036 % of pixels: the widening was
computed in normalized depth and could not reach a texel whatever the setting
said. After it, the same move is 12.254 % - and 1.5 yd itself lands close to the
fixed two-texel radius Poisson uses, which is why filter 1 → 2 is small. A
player who wants visibly soft shadows moves the sun width.



---

## Five minutes in it, with validation on

`capture_scene --dwell 300` draws the settled frame for five minutes and then
takes the shot. `VK_LAYER_PATH` pointed at the installed SDK's `Bin`,
`WOWEE_VULKAN_VALIDATION=1`, `goldshire-lake` at 09:00, every asset out of the
archives, the normal-map cache warm.

| Configuration | Frames | Mean | Worst | Vulkan errors *during the five minutes* |
|---|---|---|---|---|
| Ultra: 4 cascades, PCSS, LOD Near, normal maps everywhere, shafts at full strength, height fog | 917 in 300.09 s | 327.3 ms | 370.3 ms | none |
| The same with **shadows off** | 2690 in 300.11 s | 111.6 ms | 163.3 ms | none |

No device loss in either, and both exited cleanly. The frame times are a third
of a second because the validation layer is checking every one of ten thousand
draws; the numbers worth quoting are in the frame-time section below, taken
without it.

Each run's log carries the same three `[ERROR]` lines the earlier passes
recorded, and they are the whole of what the shadows-off run produces:

- `PaperDollFrame.lua:269` and `PVPBattlegroundFrame.lua:123`, both FrameXML Lua
  and both present on `c00ab904e`;
- one `vkQueueSubmit(): pSubmits[0] performs a layout transition on presentable
  VkImage ... but the image has not been acquired`, emitted **after** the soak
  ends, by `Renderer::captureScreenshot`, which reads back the swapchain image
  that was last presented. Pre-existing, unchanged here, once per screenshot and
  never during rendering. The fix is to re-acquire and re-present around the
  readback, which changes the client's screenshot path and belongs with whoever
  owns it.

**The Ultra run produces a hundred and fifty-odd more, and every one of them is
on one frame during start-up.** They begin on the line after
`Frame synchronisation reset after a rebuild` - the start-up MSAA rebuild, which
recreates the swapchain, every render pass and every pipeline - and stop three
seconds later, before the world has finished streaming; nothing is raised for
the five minutes after. Each is a `VkDescriptorSet ... was destroyed or updated
without UPDATE_AFTER_BIND` reaching a command buffer that had bound it, reported
once per command recorded into that buffer.

**It is not this phase's, and the number is not reproducible.** Chasing it by
configuration looked conclusive and was not:

| Run, validation on, 10 s dwell | `[ERROR]` lines |
|---|---|
| every phase-01 key at its off value | **3**, then **161** on the next run of the same command |
| `shadows=1`, cascade count moved to 1, every other key moved too | **3**, then **157** |
| `shadows=1`, cascade count left at its default of 3 | **157**, then **161** |

The same command twice gives 3 one time and 161 the next, with every phase-01
key at its *off* value, so what decides it is where the start-up rebuild falls
relative to the frames already recorded rather than any setting. The first
sample of each row is what made it look like the cascaded path's doing; the
second is what says it is not.

**An attempted fix is recorded here so the next reader does not repeat it.**
Calling `VkContext::resetFrameSyncState()` after `applyShadowCascadeChange`,
which is what the MSAA rebuild beside it does and which the comment there says
is exactly for command buffers a rebuild left mid-cycle, took a run that had
read 3 to **161**. Reverted.

What is known: it is confined to start-up, it is a hundred and sixty lines on
one frame and none afterwards, five minutes of rendering with validation on
raises nothing beyond the three ordinary lines, and no run in this session lost
a device. It belongs to whoever owns the start-up rebuild order, not to this
phase.




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

### Measured

`fogmodel` 0 (linear) → 1 (height), and the ΔE is CIE76 in L\*a\*b\* over a
thirteen-row band centred on the strongest luminance gradient in the upper half
of the frame, which is the skyline.

| Camera | Pixels changed | Mean abs | SSIM | Horizon ΔE76 mean | median | p95 | Whole-frame ΔE76 mean |
|---|---|---|---|---|---|---|---|
| `duskwood-road`, 05:30 | 67.510 % | 1.904 / 255 | 0.968214 | **1.04** | 0.56 | 4.73 | 1.31 |
| `westfall-sentinel-hill`, 09:00 | 67.823 % | 13.388 / 255 | 0.966890 | **9.24** | 7.64 | 22.79 | 7.24 |

**Duskwood is inside the rule and Westfall is not.** At Duskwood the horizon is
a wooded ridge near the zone's own fog end, the two models agree there by
construction, and the whole visible difference is mist collecting in the valley
below the camera - which is the technique doing what it is for. At Westfall's
Sentinel Hill the horizon is a line of hills a few hundred yards out, well
*short* of `fogEnd`, and that is where an exponential ramp and a linear one
differ most: the exponential model has already put nine ΔE of grey over them
while the linear one has barely started. The picture is not wrong - hills that
far away being hazier is the point - but the phase file's rule says the horizon
lands within ΔE 3 of where the artists put it, and at this camera it does not.

The rule is stated at `fogEnd`; the disagreement is at half of it. Either the
rule wants restating as "at `fogEnd`, and expect the middle distance to be
greyer", or the falloff wants a shoulder that keeps the near half of the ramp
closer to linear. That is a phase-02 decision and is not taken here.

**Not measured:** every Light.dbc zone at 06/12/18. Two zones at two times is
what this pass rendered; a per-zone sweep is a catalogue of cameras that does
not exist yet.


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

## Frame time, load time and memory

`capture_scene --dwell N --setting vsync=0`, the same frame drawn several
thousand times rather than a walk, validation off.

### The preset question the phase file asks

Three configurations at `goldshire-lake`, 1280x720, 30 s each:

| Configuration | Mean | Worst |
|---|---|---|
| Every phase-01 key at its pre-phase value - 1 cascade, PCF, LOD off, no normal maps, no shafts, linear fog | 13.629 ms | 380.7 ms |
| 2 cascades, Poisson, LOD Balanced, normal maps everywhere, shafts, height fog - preset Medium's columns | **13.551 ms** | 23.1 ms |
| 4 cascades, PCSS, LOD Near, all of it - preset Ultra's | 17.421 ms | 64.5 ms |

**The phase file's claim is that G1 pays for L1 and the frame comes out ahead,
and at this camera it now does, by 0.6 %.** The third pass measured the same
comparison as 2.6 % *slower* and recorded the prediction as not met; that
measurement was taken with no world loaded, on a frame that drew nothing, and it
does not survive. The full Ultra shadow set still costs 27.8 %, which is what
four cascades and a blocker search are for.

The worst-frame column is a streaming hitch, not a technique: the run that reads
380 ms is the one whose dwell began while tiles were still finalising.

### Sun shafts, against the phase file's 0.3 ms ceiling

The pass carries a GPU timestamp, so this is read rather than differenced.
`elwynn-sun-0700` at 1920x1032, 30 s:

| | |
|---|---|
| `gpu sun-shafts` | **0.0898 ms** over 1748 frames |
| the same mark at `westfall-sentinel-hill` | 0.128 ms over 2360 frames |
| `gpu post-process`, for scale | 5.88 ms |

**Well inside 0.3 ms.** Differencing whole-frame means would not have answered
this: the two runs read 13.32 ms and 17.17 ms, a difference forty times the
thing being measured, because the sun-facing camera's frame time depends on how
much canopy the streamer had finished. The mark is the measurement.

There is no mark inside the scene pass: it records into secondary command
buffers, and `vkCmdWriteTimestamp` cannot be called on a primary inside a render
pass begun with `VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS`. So terrain LOD
and the normal maps are measured by whole-frame means below, with the spread
that implies.

### The normal-map cache: cold, warm, and off

`goldshire-lake`, 1280x720, 20 s dwell each, run back to back. "Load to shot" is
from `Loading online world terrain` to `the shot is frame 900`.

| | Maps | Load to shot | Dwell mean |
|---|---|---|---|
| `normalmapscope=0` - no maps at all | — | 25.6 s | 12.904 ms |
| **Cold**: the sidecar directory deleted first | 1278 derived, 2 read back | 29.0 s | 14.451 ms |
| **Warm**: the same run again | 1280 read back, 0 derived | 28.2 s | 13.267 ms |

- **Cold load is +13.2 % over no maps at all**, inside the phase file's "within
  15 %". The generation is off-thread and the first frames of a newly seen model
  are flat and then bump, which is what the setting's tooltip says.
- **Warm load is +10.0 %, not the "within 0 %" the phase file asks for.** Reading
  1 280 sidecars back off disk, decoding them and uploading 202 MB of texture is
  not free even when nothing is derived. The number the phase file wanted is the
  one where the maps are already resident, and nothing here is.
- Steady-state frame time with the maps costs **+2.8 %** warm (13.267 against
  12.904), and a separate 40 s pair at the same camera read 13.498 against
  13.744, **+1.8 %**. Both are inside the run-to-run spread and neither is
  distinguishable from zero.

The cache on disk after a cold Elwynn load: **1551 files, 198.6 MB**, under the
2 GB bound by a wide margin. The cache reports its own GPU side every 256
uploads: **1280 maps bound, 202 MB**.

### Video memory

`nvidia-smi` cannot report per-process GPU memory under WDDM - it answers
`[N/A]` - so this is the whole device's `memory.used`, sampled before the
process starts and at its peak during a 25 s dwell at `goldshire-lake`:

| | Before | Peak | Delta |
|---|---|---|---|
| `normalmapscope=0` | 1499 MiB | 3016 MiB | 1517 MiB |
| `normalmapscope=1` | 1408 MiB | 3404 MiB | **1996 MiB** |

**+479 MiB, or +31.6 %**, against the phase file's "+40 %" ceiling. That is a
whole-device figure with whatever else the desktop was doing in it, so treat it
as approximate and read it beside the cache's own 202 MB and the static vertex
growth: `pipeline::TerrainVertex` 44 → 60 bytes takes the terrain mega buffer
from 66 MB to 90 MB, and the M2 vertex goes 72 → 88 bytes.

### Terrain LOD

1920x1032, 30 s each, taken back to back:

| Camera | LOD Off | LOD Balanced |
|---|---|---|
| `westfall-sentinel-hill` | 12.885 ms | **12.716 ms** (−1.3 %) |
| `tanaris-dunes` | 6.505 ms | **6.422 ms** (−1.3 %) |

A pair taken at 1280x720 an hour earlier read +2.0 % and −16.2 %, which is the
spread this measurement has when the two halves are not run back to back. The
defensible statement is that terrain LOD is free at these cameras and does not
obviously pay: both are bound by the number of chunks recorded rather than by
the vertices in them, and a reduced level draws the same number of chunks with
fewer indices in each.


---

## What was checked, and how

| | |
|---|---|
| `ctest -C Release -j 8` | **197 of 207 pass.** The ten failures are the ten this machine already had: five "Not Run" targets that need `vulkan.h` on a test that does not link it (`shared_rules`, `spline_body`, `m2_structs`, `indoor_shadows`, `catalog_subprocess`), `unicorn_stub_compiles`, `open_formats`, `open_format_emitter`, `cli_paths`, and `sweep_guard`. Nothing new. `shader_offpath_identity`, `terrain_lod`, `tangent_frame`, `height_fog`, `normal_map_cache` and `settings_panel_layout` all pass |
| `shader_offpath_check.py` | 0 shaders moved, after the PCSS penumbra was rewritten as well as before it |
| `shader_feature_check.py` | 7 constants, 0 disagreements between the GLSL and the C++ halves |
| `reserved_code_check.py` | 15 markers, 0 for a phase that has already shipped, 0 malformed |
| `sweep_guard` | **14 over the pinned ceiling, against the nine the third pass recorded, and none of the five extra is a regression.** The four that count something read exactly what they read before: `dead_symbol_check` 51 against a ceiling of 2, `duplicate_block_check` 4, `handler_twin_check` 2, `posix_only_check` 1 - still `local_time.hpp` flagging its own `#else` branch. The other five are guards that cannot see their subject, because this checkout deliberately has no `Data/interface`: it reads the game's archives instead of an extracted tree, which is the point of this pass. Each of those five is now counted twice - once for "could not read its own count" and once for "reports no population" - where before it was counted once, and that doubling is the whole of the difference. Restoring an extracted `Data/interface` would put it back at nine |
| `nvidia-smi` | Answers `[N/A]` for per-process GPU memory under WDDM, so the video-memory row is a whole-device delta. Said here rather than left for a reader to wonder about a suspiciously round number |
