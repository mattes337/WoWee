# Phase 01 — evidence

What was measured, what was not, and why.

Phase file: [`../../modern-rendering/01-shadows-fog-distance-surfaces.md`](../../modern-rendering/01-shadows-fog-distance-surfaces.md).

---

## The blocker, first, because it decided the scope

**`tools/capture_scene` does not exist in this repository, and neither does
`howtos/`.**

`docs/plan-modern-rendering.md` §7.5 opens with "`tools/capture_scene`
(`howtos/capture-scene-screenshots.md`) already renders a scene headless from a
map, a camera, a time of day, weather, and an optional character with
equipment, and writes a PNG plus an entity manifest." That is the premise the
phase's entire Verify list rests on, and it is not true of this tree:

```
$ ls tools/ | grep capture      # nothing
$ ls howtos/                    # no such directory
$ grep -rl capture_scene --include='*.txt' --include='*.md' .
docs/modern-rendering/01-shadows-fog-distance-surfaces.md
docs/modern-rendering/02-comparison-mode.md
docs/plan-modern-rendering.md
```

The only screenshot machinery that exists is `Renderer::captureScreenshot`
(`src/rendering/renderer.cpp`), which writes the current swapchain to a
PNG — no scene setup, no camera placement, no time of day, no headless entry
point, no way to run without logging into a server.

So there was no way to render *any* before/after picture in this session, and
therefore no way to check "off is pixel-identical", no compare v0, no
per-technique side-by-sides, no fog ΔE calibration against a rendered horizon,
no frame time, no load time and no VRAM figure. Building the harness is a phase
of its own; it is phase 02's subject, and phase 02 is written as if the v0 in
phase 01 already existed.

**What was done instead.** The techniques whose correctness is only visible in
a picture were cut rather than shipped unseen (see *Cut*, below), and the
"off is today's frame" promise — which is the load-bearing one, because
everything after this phase leans on it — was measured a different way, at the
SPIR-V level, where it can be measured without a GPU. See *Off-path identity*.

**For phase 02.** Correct §7.5 before building on it, and treat "the harness
exists" as the first thing to check rather than the first thing to assume.

---

## Shipped

| Step | What | Verified by |
|---|---|---|
| 1 | F1 specialization constants: `ShaderFeatures`, `ShaderSpecialization`, `PipelineBuilder::setSpecialization`, the four lit renderers building their variant | builds; off-path identity |
| 1 | `tools/shader_feature_check.py` — the GLSL and C++ halves of the feature set agree about every id, type and default | `shader_features_agree` test; canaried before it was pinned |
| 2 | F5 `RenderCaps` on `VkContext`, tier + per-feature flags, `applyRenderCapsToSchema` filling `SettingDesc::unavailable`, `tools/reserved_code_check.py` | `reserved_code` test; `sweep_guard` |
| 3 | Shader includes: `shader_features.glsl`, `shadow_common.glsl`, `fog.glsl`, `parallax.glsl`, `lit_common.glsl`; the four lit shaders drop their copies | off-path identity, exactly |
| 4 | One UBO append: `cascadeMatrix[4]`, `shadowSplits`, `shadowMeta`, `fogHeight`, `fogSunColor`, `skySH[7]` | off-path identity |
| 5 | `shader_offpath_identity` — the test itself | it caught a real regression; see below |
| 9 | A1 exponential height fog with sun in-scatter, calibrated against Light.dbc's own fog end | `test_height_fog` — 11 cases, 110 assertions |
| 13 | `fogmodel` and `fogaerial` schema rows, a `fogModel` preset column, all seven places | `settings_persist_check`, `dead_setting_check`, `persisted_but_unread_check` |

## Cut

The phase file's cut order is *geomorph → PCSS → sun shafts → terrain normal
maps*. This session cut further than that, and the reason for every one of them
is the same: **there is no way in this tree to look at the result.**

| Cut | Where it went | Why |
|---|---|---|
| G1 terrain LOD (step 11), geomorph included | 01b | Skirts need 32 more vertices per chunk and a change to a 145-vertex layout that grass sampling and the hole mask both assume. Cracks between LOD rings are the failure mode, and a crack is a pinhole of void that only a picture shows |
| M3a normal maps and tangents (step 12) | 01b | Tangent handedness is the failure mode, and a mirrored tangent frame looks like slightly wrong lighting, not like an error |
| S4 sun shafts (step 10) | 01b | A screen-space effect with no screen to check it on |
| L1 cascades, L2 filters (steps 6, 7) | 01b | The largest and the one with a device loss in its history. The UBO slots and the `SPEC_SHADOW_CASCADES` / `SPEC_SHADOW_FILTER` constants are in place and marked `RESERVED(phase-01b, L1-csm)`, so the block does not move again |
| The `shadows` off switch | 01b | `setShadowsEnabled` still holds shadows on. The phase asks to "reproduce the old device loss first to confirm the cause, then confirm the fix on the T0 machine" — neither half is possible here, and re-enabling a control documented to end the session within a second, untested, is worse than leaving it off |
| Compare v0 (step 8) | 02 | `tools/capture_scene` does not exist |

Consolidation steps 1–5 were never at risk; they are what the rest of the plan
stands on and they are complete.

---

## Off-path identity

`tools/shader_offpath_check.py`, run by ctest as `shader_offpath_identity` and
by `sweep_guard` with a ceiling of zero.

The plan's §6.3 asks for "byte-identical SPIR-V to the pre-feature shader".
Byte-identical is not achievable once a module declares a specialization
constant: the constant is still a runtime value inside the module, so the
branch is there in the binary and the driver folds it at pipeline creation.
Comparing the bytes would compare the wrong thing.

What is compared instead is the variant a caller with no `VkSpecializationInfo`
actually gets:

```
reference = glslc -O tests/shaders_prephase/<name>          | spirv-opt -O
current   = glslc -O -I assets/shaders assets/shaders/<name> | spirv-opt \
              --freeze-spec-const --fold-spec-const-op-composite -O
```

then the multiset of function-body instructions with result ids erased —
insensitive to id renumbering and basic-block ordering, which `spirv-opt` does
routinely, and sensitive to an instruction added, removed or given a different
literal.

Result on this tree:

```
ok   character.frag.glsl (1752 instructions)
ok   m2.frag.glsl (806 instructions)
ok   terrain.frag.glsl (454 instructions)
ok   wmo.frag.glsl (731 instructions)
0 shader(s) whose off-path moved
```

That covers, and proves identical: four functions extracted into three
includes; six members appended to the `PerFrame` block; three boolean
specialization constants gating the existing `enableNormalMap`, `enablePOM` and
shadow branches; and the whole height-fog path behind `SPEC_FOG_MODEL`.

It is a real check, not a formality — it failed the moment the height fog went
in, because `--freeze-spec-const` alone leaves an `OpSpecConstantOp IEqual`
behind that `-O` will not fold, and the dead branch stayed in the module. That
is exactly the class of "the off path quietly costs something now" the check
exists for.

What it does not cover: decorations, the entry-point interface, and debug
names, all of which live outside the function bodies. A uniform block growing a
member no branch reads is not a difference, because std140 offsets of the
members already there do not move — which is the append-only rule.

---

## The one thing that was checked on a real GPU

There is no scene harness, but the client itself runs, and running it is what
proves the specialization path builds pipelines a driver accepts. Built here,
launched with `WOWEE_LOG_LEVEL=INFO` and left at the login screen for fifty
seconds on an NVIDIA RTX 2070 SUPER (driver 591.86, Vulkan 1.4.325):

```
[INFO ] Render capability tier: T2 Modern (descriptorIndexing yes,
        bufferDeviceAddress yes, drawIndirectCount yes, multiDrawIndirect yes,
        tessellation yes, fragmentShadingRate yes, meshShader yes,
        rayQuery yes, shaderFloat16 yes, maxImage2D 32768,
        maxImageArrayLayers 2048)
[INFO ] Shader variant: bits 0x7, cascades 1, shadow filter 0, fog model 1
[INFO ] Lit pipelines rebuilt for the new shader variant
...
[INFO ] GPU, last frame: 1.54435ms across 2 passes
```

Zero `[ERROR]` lines in 396, and no device loss. That is:

- `RenderCaps` filled from a real device, every flag and both limits;
- the saved `fogmodel=1` reaching `Renderer::setFogModel` through the seven
  places a setting has to pass through;
- the deferred rebuild running between frames and the four lit renderers
  building their pipelines with `SPEC_FOG_MODEL = 1` specialized in — which the
  driver accepted, since the client kept drawing for the next forty seconds.

What it is **not**: a picture of anything. The login screen draws no terrain,
no buildings and no doodads, so the fog was specialized into pipelines that
nothing then asked to shade a world with. The frame numbers above are the
interface, not a scene, and are not comparable to anything.

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

The two numerical traps in the closed form are covered: a ray level with what
it is looking at divides by zero (the limit is taken below a threshold, and the
test checks both sides of that threshold agree), and a camera far below the
base height exponentiates to infinity (clamped, and the test stands the camera
2000 yd under).

**Not measured:** the ΔE against a rendered horizon, on every Light.dbc zone at
06/12/18. That needs a renderer and a screenshot. The calibration above is the
model-level statement of the same rule; the picture-level one is owed.

---

## Numbers

| Verify item | Result |
|---|---|
| `shader_offpath_identity` | **pass** — 0 of 4 shaders moved |
| `reserved_code` | **pass** — 22 markers, 0 stale, 0 malformed |
| `shader_features_agree` | **pass** — 6 constants, 0 disagreements |
| ctest | 194 of 204 pass. 10 fail, every one of them for a reason that predates this branch — see *Pre-existing failures* |
| `test_height_fog` | **pass** — 110 assertions in 11 cases |
| `sweep_guard` | fails, with **exactly the nine** sweeps that fail at `HEAD` on this machine, and no others. Run twice, side by side: once from a `git archive` of `HEAD`, once from this branch. The `HEAD` copy has no extracted `Data/interface`, so it *skipped* three sweeps this branch runs — which makes the comparison conservative rather than flattering: the branch runs more checks and still lands on the same nine |
| Compare v0, all scenes | **not measured** — no `tools/capture_scene` |
| Per-technique before/after | **not measured** — same |
| Fog ΔE per zone at 06/12/18 | **not measured** — same |
| Shadows off, 5 min in Stormwind, T0 | **not measured** — the switch was not shipped |
| Frame time, three `perf_baseline.md` scenes, T0, Medium | **not measured** — no headless harness and no T0 machine here. Also: `docs/perf_baseline.md` has never been filled in. All three scenarios read `_pending_`, and Tracy is not vendored, so the "must be lower than pre-phase" comparison has no pre-phase number on either side of it |
| Load time, Stormwind, cold and warm normal-map cache | **not applicable** — the normal-map cache was cut |
| VRAM before/after | **not measured** |

No number in this file is estimated. Where it says "not measured" nothing was
measured.

### Cameras, for whoever builds the harness

The phase asks for the Stormwind gate camera plus three or four picked in
Goldshire, Elwynn and Westfall, recorded here. None of them could be verified
against terrain, so they are written down as *unverified* rather than as scene
data: run them through `pywowlib/tools/terrain_height.py` before they go into
`tools/compare_scenes.json`.

| Name | Map | Camera | Target | Confirmed? |
|---|---|---|---|---|
| `stormwind-gate` | Azeroth (0) | `-9462,-67,57` | `-9462,-67,50` | Yes — the one camera the plan calls confirmed |
| the rest | — | — | — | No — not written down here rather than written down wrong |

Guessing four more coordinates from memory of the world and recording them as
though they had been checked is exactly the failure mode §7.5 warns about when
it says every position but the Stormwind gate "is to be verified with
`pywowlib/tools/terrain_height.py` / `wmo_height.py` before it is committed".
Nothing here could run that check, so nothing here claims to have.

---

## The committed `.spv`

`assets/shaders/*.spv` are **tracked** — seventy-three of them — even though
`.gitignore` lists `*.spv` with only the footprint pair excepted. `.gitignore`
does not untrack a file that is already tracked, and both `CMakeLists.txt` and
`docs/plan-grass.md` §1 say why they are: they are the fallback when `glslc` is
absent, and they are what the embedded-shader table is generated from.

This commit carries four of them — `terrain.frag`, `wmo.frag`, `m2.frag` and
`character.frag` — because those four sources changed. The other fifty-four
were rebuilt only because the Vulkan SDK on this machine (1.4.357.0) is not the
one that produced the committed bytes: the disassembly is instruction-for-
instruction identical and only the ids and the generator word differ. Those
were restored to `HEAD` rather than committed, because a fifty-four-file binary
diff that changes nothing is noise in the one place a reviewer cannot read the
diff.

A build with `glslc` recompiles all of them anyway. A build without it now gets
four shaders that match their sources and sixty-nine that already did.

---

## Pre-existing failures

These fail on this branch and are not caused by it. Each was read and
attributed; none is a regression.

| Test | Why |
|---|---|
| `shared_rules`, `spline_body`, `m2_structs`, `indoor_shadows`, `catalog_subprocess` | Do not build under MSVC. Four cannot find `vulkan/vulkan.h` — those test targets are not given `Vulkan_INCLUDE_DIR` — and `catalog_subprocess` calls `popen`, which Windows does not have. All four include sites predate this branch |
| `unicorn_stub_compiles`, `open_formats`, `open_format_emitter`, `cli_paths` | Windows/toolchain, unrelated to rendering |
| `sweep_guard` | Fails at `HEAD` too, with the same nine: `posix_only_check` (1, ceiling 0), `dead_symbol_check` (51, ceiling 2), `duplicate_block_check` (4, ceiling 0), and `framexml_unreachable_verbs`, `startup_latch_check` and `cvar_default_agreement` reporting no count on a Windows checkout. The four sweeps this phase adds all report clean |

### Three sweeps that were blind on Windows, and are not any more

Not scope, but they were in the way of the statement above, and each is the
exact failure mode `sweep_guard` exists to catch — a check that cannot run
reads exactly like a clean tree:

- `chat_line_twice_check.py` and `framexml_contract_check.py` read C++ sources
  with `path.read_text()` and no encoding. One file in `src/game` is UTF-8, and
  Windows' default is cp1252, so both raised on it and reported nothing. They
  read as UTF-8 now.
- `framexml_frame_emitted_check.py` looked for the emitter at `build/bin/
  framexml_emit`. A multi-config generator — which is what a Windows build uses
  — puts it at `build/bin/Release/framexml_emit.exe`, so the check reported
  that it could not run on every Windows run since it was written. It now looks
  under the config directories and for the `.exe`. Newly able to run, it reports
  0 of 2369 named frames missing from the emitter.

The MSVC build also needs `-DWOWEE_WARNINGS_AS_ERRORS=OFF`: `/W4 /WX` turns
`C4458` (a parameter hiding a member, in `include/game/entity.hpp`) into an
error in a dozen translation units, none of them touched here. That switch is
documented in `CMakeLists.txt` for exactly this — "a bisect or a new-compiler
build".
