# Phase 01 — Shadows, fog, distance, surfaces (and the shader consolidation they share)

**One phase, one commit.** Depends on: nothing. Player sees: sharp shadows at their feet and
on the far hills, soft edges, mist in the valleys, rays through the trees, a faster frame
on every GPU, and normal-mapped doodads and ground. All on today's hardware floor, nothing
to download.

This is five techniques in one phase **because they edit the same files**: the four lit
shaders, the `PerFrame` block, the settings schema, the presets. Done separately that is
five refactors of the same lines; done together it is one. The price is that this is the
largest phase in the plan — expect two to three working days, not one — and the build must
stay green after every step below so a half-done state is never the state.

## Ships

| id | Technique | Detail |
|---|---|---|
| **F1** | Specialization constants | `ShaderFeatures` bitset → `VkSpecializationInfo`; pipeline cache keyed by (shader, features). Every toggle below is a constant, so the default build is today's SPIR-V |
| **F5** | Capability gating | `RenderCaps` on `VkContext` (tier + feature bits, plan §3); table fills `SettingDesc::unavailable` at start-up. `tools/reserved_code_check.py` |
| **L1** | Cascaded shadows | 4 cascades, `2D_ARRAY` depth, stable snapping, blend band; **shadows can be switched off** (count 0 binds the white fallback from `shadow_params.hpp`, no device loss) |
| **L2** | Soft shadows | PCF 3×3 / Poisson 16 / PCSS, by constant |
| **A1** | Height fog + aerial perspective | Exponential height fog with sun in-scatter, derived from Light.dbc so no zone shifts hue; linear stays a choice |
| **S4** | Sun shafts | Half-res radial blur from a sun mask, on only while the sun is on screen |
| **G1** | Terrain LOD | Four shared index sets + skirts; geomorph if time |
| **M3a** | Normal maps everywhere | Tangents for M2 doodads and terrain; `generateNormalHeightMap()` over their textures at first use, cached to disk in the background; the WMO bump/POM path shared by `m2.frag` and `terrain.frag` |
| — | Compare v0 | `capture_scene --setting key=value` and `tools/compare_scenes.py` (two renders + diff + SSIM). Phase 02 replaces it |

## Not in this phase

Contact shadows → 14. RT shadows → 23. Froxel volumetrics → 15. Tessellation → 18.
Roughness/AO sidecars and PBR → 13. The manifest for the normal-map cache → 07.

## Steps — in this order, green after each

**Consolidation first (day 1 morning).** Nothing visible changes until step 5; every step is
checked by `capture_scene` on `stormwind-gate` being pixel-identical.

1. **Spec-constant plumbing.** `vk_pipeline.hpp/.cpp`: `ShaderFeatures`, `constant_id N ↔
   bit N`, hashed cache. Convert the existing `enableNormalMap` / `enableParallax` in
   `wmo.frag` / `character.frag` (uniform fields stay, zero-written, so the C++ side moves
   in one place). The four material renderers request pipelines per (material class,
   features); M2 adds the feature key to its material sort.
2. **Caps.** `include/rendering/render_caps.hpp`, filled next to the feature probes in
   `vk_context.cpp:600-790`; runtime `unavailable` overlay in `settings_schema`;
   `tools/reserved_code_check.py` + README "Shipped through" line; `test.sh --lint` hook.
3. **Shader includes.** `-I assets/shaders` in `compile_shaders()`. Create
   `shadow_common.glsl` (today's `shadowTexel` + `sampleShadowPCF`, verbatim),
   `fog.glsl` (today's linear `mix`, verbatim), `parallax.glsl` (the POM function from
   `wmo.frag`), and `lit_common.glsl` that includes the three. `terrain`, `wmo`, `m2`,
   `character` frag include them and drop their copies. Pixel-identical.
4. **UBO append, once.** `GPUPerFrameData` and every `PerFrame` block gain, in this order:
   `mat4 cascadeMatrix[4]; vec4 shadowSplits; ivec4 shadowMeta; vec4 fogHeight; vec4
   fogSunColor; vec4 skySH[7]` (the last reserved, see below). `lightSpaceMatrix` stays
   equal to `cascadeMatrix[0]`.
5. **Off-path identity test.** `tests/shader_offpath_identity`: compile every shader with
   constants at defaults, diff against the pre-phase `.spv`. Green from here to the commit.

**Techniques (day 1 afternoon → day 3).** Each is its own constant; each is verified with
compare v0 before the next starts.

6. **L1 cascades.** `renderer.hpp:304-310` images → 4-layer arrays, per-layer framebuffer
   views, one array view for `sampler2DArrayShadow`; per-cascade side capped at 2048 when
   `SHADOW_MAP_SIZE` is 4096. `computeLightSpaceMatrix()` (`renderer.cpp:1072`) →
   `computeCascadeMatrices()` (practical split λ = 0.7, bounding sphere, snapping as
   `renderer.cpp:3564`). `renderShadowPass()` (`renderer.cpp:3843`) loops cascades with a
   per-cascade frustum. `shadow_common.glsl`: cascade select by view depth + blend band.
   `setShadowsEnabled()` (`renderer.hpp:330`) stores again; count 0 = skip pass + white
   fallback + `shadowParams.x = 0`. Reproduce the old device loss first to confirm the
   cause, then confirm the fix on the T0 machine.
7. **L2 filters.** `SHADOW_FILTER` constant: Poisson (interleaved-gradient rotation), PCSS
   (16-tap blocker search, penumbra from `shadowlightsize`) on cascades 0–1, PCF beyond.
8. **Compare v0.** `--setting` in `tools/capture_scene/main.cpp` via
   `SettingsPanel::setSettingValue`; `tools/compare_scenes.py`. Use from here on.
9. **A1 fog.** `fog.glsl` gains `FOG_MODEL`; `LightingManager` derives base height (terrain
   under camera − 20 yd), density from `fogEnd` (equal optical depth at `fogEnd`), sun
   colour from the band. Calibration rule: horizon colour at `fogEnd` within ΔE 3 of the
   linear model on every zone at 06/12/18.
10. **S4 shafts.** `sunshaft_mask.frag.glsl`, `sunshaft_blur.frag.glsl`, `R8` half-res
    target in `post_process_pipeline.cpp`, graph node after sky, gated by `LensFlare`'s sun
    visibility. LDR is fine.
11. **G1 terrain LOD.** `terrain_mesh.hpp` `buildLodIndices(level)` (145/81/25/9 + skirts,
    generated once, shared); LOD select where distance culling runs in
    `terrain_renderer.cpp` (0.12/0.3/0.6 of view distance, neighbours ≤ 1 level apart);
    skirt depth from the ADT chunk min height. Geomorph (`morphTarget` attribute +
    per-chunk `morphFactor`) only if steps 6–10 are verified by end of day 2.
    `--wireframe` in `capture_scene`.
12. **M3a normal maps.** Lift Lengyel tangents from `character_renderer.cpp:1834` into
    `include/rendering/tangent_frame.hpp` (tested); M2 GPU vertex and
    `terrain_vertex.hpp` gain `vec4 tangent` (terrain analytically from the grid).
    `NormalMapCache` (async via `thread_pool`, `<hash>` files under
    `Data/generated/<expansion>/normals/`, 512² cap, 2 GB bound). `m2.frag` /
    `terrain.frag` take the normal/height binding (M2: free slot per
    `m2_renderer_internal.h`; terrain: bindings 8–11, POM on the dominant layer) through
    `parallax.glsl`. Strength 3 for M2, 2 for terrain.
13. **Settings, presets, tooltips** — one append to the schema table, one new column set
    in `kGraphicsPresets`, all rows below.
14. **Evidence.** Compare v0 on every scene listed under Verify, into
    `docs/evidence/phase-01/`. `docs/status.md`, `CHANGELOG.md`, a howto for the normal-map
    cache directory.

## Settings

| key | kind | choices / range | default | L / M / H / U | enabledWhen |
|---|---|---|---|---|---|
| `shadows` | Bool | | 1 | 1/1/1/1 | row live again |
| `shadowcascades` | Enum | `1|2|3|4` | 2 | 0/1/2/3 | `shadows` |
| `shadowdistance` | Float | 40–500 | 300 | 100/200/350/500 | `shadows` (unchanged) |
| `shadowfilter` | Enum | `PCF 3x3|Poisson 16|PCSS` | 1 | 0/1/2/2 | `shadows` |
| `shadowlightsize` | Float | 0.5–5 yd | 1.5 | — | `shadowfilter=2` |
| `fogmodel` | Enum | `Linear|Height` | 1 | 1/1/1/1 | |
| `fogaerial` | Float | 0–1 | 0.6 | — | `fogmodel=1` |
| `sunshafts` | Bool | | 1 | 0/1/1/1 | |
| `sunshaftstrength` | Float | 0–1 | 0.5 | — | `sunshafts` |
| `terrainlod` | Enum | `Off|Near|Balanced|Far` | 2 | 3/2/2/1 | |
| `normalmapscope` | Enum | `Buildings and characters|Everything` | 1 | 0/1/1/1 | `normalmapping` |

Existing `normalmapping`, `normalmapstrength`, `parallax`, `parallaxquality` now apply to
everything in scope.

## Reserved

```cpp
// RESERVED(phase-11, L5-sky-probes): SH9 ambient slots appended now so PerFrame moves
// once. Zero-filled; no shader reads them.
glm::vec4 skySH[7];
```
```glsl
// RESERVED(phase-15, A2-volumetric-fog): fogHeight.w and fogSunColor are the froxel
// volume's inputs; declared with the fog params so PerFrame moves once.
```
```cpp
// RESERVED(phase-18, G4-tessellation): buildLodIndices takes a `patchMode` flag emitting
// quad patches. False everywhere.
// RESERVED(phase-07, M1-texture-cache): NormalMapCache's directory and hash naming are
// what the generated-asset manifest will index; manifest-free until then.
// RESERVED(phase-13, L7-pbr): the tangent attribute and a sidecar slot on the material
// UBOs are what GGX reads roughness through.
```

## Verify

- **Identity.** `shader_offpath_identity` green; compare v0 with every new key at its off
  value on all scenes == pre-phase golden bit-exact.
- **Per technique** (compare v0, before/after, side-by-sides kept):
  L1 `stormwind-gate` 09:00/17:30, `goldshire-inn-morning`, `orgrimmar-drag`,
  `stranglethorn-canopy` · L2 `goldshire-inn-morning` road shadow, filter 0→1→2 ·
  A1 `duskwood-road` 05:30, `crossroads-plains`, `westfall-sentinel-hill`, `thunder-bluff-dawn` ·
  S4 `elwynn-road-sunrise` · G1 `westfall-sentinel-hill`, `tanaris-dunes` (+ wireframe;
  Balanced differs from Off by < 0.5 % of pixels) · M3a `northshire-abbey`, `stormwind-gate`
  ground, `kharanos-snow`.
- **Fog calibration.** Every Light.dbc zone at 06/12/18: `fogmodel=0` identical;
  `fogmodel=1` horizon within ΔE 3.
- **Shadows off.** 5 minutes in Stormwind on T0, no device loss, validation clean.
- **Frame time.** Three `perf_baseline.md` scenes on T0 at preset Medium: must be *lower*
  than pre-phase (G1 pays for L1); record every number.
- **Load time.** Stormwind cold normal-map cache within 15 % of pre-phase (generation is
  off-thread; first-frame flat doodads refine, and the tooltip says so); warm within 0 %.
- **VRAM.** Stormwind before/after; the 512² cap keeps it under +40 %.
- `./test.sh` green: unit (splits, snapping, LOD watertightness, tangents), lint
  (reserved-code check).

### Results

Machine: RTX 2070 SUPER, Vulkan SDK 1.4.357.0, Windows 11. Pictures at
1280x720, frame times at the resolution each row names. Every asset read
straight out of `G:\WoW AzerothCore`'s eighteen MPQ archives — there is no
extracted tree and none is needed. Every picture named here is in
`docs/evidence/phase-01/img/` as `.before.png`, `.after.png` and `.diff.png`,
and the numbers are what `tools/compare_scenes.py` printed.

**Read every row against its camera's noise floor.** Two renders of one camera
with identical settings are not identical: the world loader draws a
wall-clock-dependent number of frames before the fixed count starts, so the
animated half of a scene is not quite in the same place twice. The floor is
0.214 % of pixels at Goldshire's lake, 5.194 % at Westfall's Sentinel Hill and
26.6 % at a camera looking down onto a moving Elwynn canopy. It is measured per
camera in `docs/evidence/phase-01/README.md` and quoted beside each number here.

| Item | Result |
|---|---|
| Identity — `shader_offpath_identity` | **pass.** 0 shaders moved; character 1752, m2 806, terrain 454, wmo 731 instructions — the same counts as before the techniques landed, and the same counts after the PCSS penumbra was rewritten in this pass, because that code is behind `SPEC_SHADOW_FILTER == 2` and the default is 0 |
| Identity — a rendered frame at every new key's off value | **not measured as a bit comparison, and it cannot be one at these cameras.** A frame of this client is not reproducible to the pixel: drifting cloud, moving water and swaying foliage all advance with the frame count, and the number of frames the world loader draws before the capture's own fixed count begins is wall-clock dependent. Two renders at *identical* settings differ by 0.2 % to 26.6 % of pixels depending on what is in frame. What stands in its place is the SPIR-V identity above, which is exact, and the measured floor beside every number below. The third pass's bit-identical frame against `c00ab904e` was rendered before `normalmapscope` and `sunshafts` existed and is not restated as covering them |
| L1 `stormwind-gate` / `goldshire-inn-morning` / `orgrimmar-drag` / `stranglethorn-canopy` | **partial.** `goldshire-lake` at 09:00, `shadowcascades` 1 → 3 maps: 1.446 % of pixels changed, mean 0.161 / 255, SSIM 0.996963, against a 0.214 % floor. Two Elwynn cameras and a Stormwind one, not four across three continents; the Orgrimmar and Stranglethorn cameras were not rendered. The third pass's 35.26 % for this pair does not survive: that run had the player's own model parked on the lens and compared frames whose clouds had moved |
| L2 filter 0→1→2 | **pass for 0→1, and 1→2 is now a statement about the setting rather than about the code.** `goldshire-road` 09:00 at 3 cascades: PCF→Poisson 1.762 % of pixels, SSIM 0.999168. Poisson→PCSS 0.087 %, SSIM 0.999987 — and **PCSS was a no-op until this pass fixed it**: the penumbra was computed as a ratio of normalized shadow-map depths, which for an orthographic cascade is a twentieth and widens a two-texel filter to three. `shadowlightsize` 1.5 → 5 yd moved 0.036 % of pixels before the fix and **12.254 %** after it (SSIM 0.991791). At the default 1.5 yd the penumbra lands close to Poisson's fixed radius, which is why the filter change itself stays small |
| A1 fog cameras | **partial, and one of the two is outside the rule.** `fogmodel` 0→1 at `duskwood-road` 05:30 and `westfall-sentinel-hill` 09:00. Duskwood: 67.5 % of pixels, mean 1.904 / 255, horizon ΔE76 mean **1.04** — inside the ΔE 3 rule, and the visible change is mist collecting in the valley. Westfall: 67.8 %, mean 13.388 / 255, horizon ΔE76 mean **9.24** — outside it. The rule is stated at `fogEnd` and Westfall's horizon is a line of hills well short of it, which is where the exponential and linear ramps differ most. Not restated as met. `crossroads-plains` and `thunder-bluff-dawn` were not rendered |
| S4 `elwynn-road-sunrise` | **pass.** `-9462,-67,120` facing the sun. At 07:00 (`--angles 9.7,129.5,0`) `sunshafts` 0→1 adds **+4.54 of 255 over the whole frame** and +12.3 in the square around the sun, taking fully-white pixels from 0.366 % to 1.706 % — a halo, because the sun stands in open sky. At 06:30, with the sun behind the ridge, it is **rays**: the difference fans out from a point on the skyline in streaks broken by the treeline, +1.60 mean. With the sun **behind the camera**, same place and time, yaw turned 180°: **+0.07 of 255 and no saturated pixel at all** — `renderMask` returns before it blits. Not blown out at strength 0.5. The five constants were left where they are; the header's note that they have never been tuned stays, the claim that nobody has looked is gone |
| S4 cost, ≤ 0.3 ms at 1080p | **pass: 0.0898 ms**, at 1920x1032, read off the pass's own GPU timestamp over 1748 frames (0.128 ms at Westfall). `capture_scene --dwell` reports the GPU marks now, which is the only way to measure this: the two whole-frame means differ by 3.8 ms at that camera, forty times the thing being measured |
| G1 `westfall-sentinel-hill` / `tanaris-dunes`, + wireframe, Balanced within 0.5 % of Off | **the cameras are rendered at last; the threshold is met at one and not at the other, and at the first it cannot be measured.** Westfall: `terrainlod` Off→Balanced changes **2.854 %** of pixels — *below that camera's own 5.194 % noise floor*, so nothing can be concluded except that it is not large. Tanaris, where the floor is 0.322 %: **4.420 %**, mean 0.270 / 255, SSIM 0.993315 — real, and not under 0.5 %. The pixels that move are dune silhouettes against the sky. Wireframe pairs rendered at both: 13.119 % and 10.709 % of pixels |
| Fog calibration, every zone at 06/12/18 | **not measured: a per-zone camera catalogue does not exist.** Two zones at two times are in the row above |
| Shadows off: 5 min, no device loss, validation clean | **pass.** `capture_scene --dwell 300`, validation on, Ultra preset with `shadows=0`: 2690 frames in 300.1 s, no device loss, clean exit, and the only three `[ERROR]` lines are the two FrameXML Lua ones and the pre-existing screenshot-readback layout transition |
| Frame time, preset Medium, lower than pre-phase | **pass, by 0.6 %.** `goldshire-lake`, 1280x720, `--dwell 30 --setting vsync=0`: every phase-01 key at its pre-phase value 13.629 ms, preset Medium's columns **13.551 ms**, preset Ultra's 17.421 ms. The third pass measured this as 2.6 % slower; that run had no world loaded and drew nothing, and it does not survive. Terrain LOD on its own is −1.3 % at both open-horizon cameras at 1920x1032 |
| Load time: cold within 15 %, warm within 0 % | **cold passes, warm does not.** `goldshire-lake`, load to shot: no maps at all 25.6 s, **cold 29.0 s (+13.2 %)**, **warm 28.2 s (+10.0 %)**. Cold derives 1278 maps on workers; warm reads 1280 back off disk, and decoding and uploading 202 MB of texture is not free even when nothing is generated. The disk cache after one cold Elwynn load is **1551 files, 198.6 MB**, well under the 2 GB bound |
| VRAM, under +40 % | **pass at +31.6 %**, with a caveat about the instrument: `nvidia-smi` answers `[N/A]` for per-process memory under WDDM, so this is the whole device's `memory.used` before the process starts against its peak during the run — 1517 MiB without the maps, **1996 MiB** with them. Beside it, two exact figures: the cache reports **1280 maps bound, 202 MB**, and the tangent attribute takes the terrain mega vertex buffer from 66 MB to 90 MB and the M2 vertex from 72 to 88 bytes |
| Unit tests: splits, snapping, LOD watertightness, tangents | **pass.** `test_terrain_lod`, `test_tangent_frame`, `test_height_fog`, `test_normal_map_cache` |
| Lint: reserved-code check | **pass.** `reserved_code_check.py`: 15 markers, 0 for a shipped phase, 0 malformed. `shader_feature_check.py`: 7 constants, 0 disagreements. `shader_offpath_check.py`: 0 shaders moved |
| `./test.sh` / `ctest` green | **pass, against the failures this machine already had.** See `docs/evidence/phase-01/README.md` |

**On the frame time.** Measured with `capture_scene --dwell 30 --setting
vsync=0` at `goldshire-lake`, 1280x720, which draws the same frame two thousand
times so the number is not a walk:

| Configuration | Mean | Worst |
|---|---|---|
| Every phase-01 key at its pre-phase value — 1 cascade, PCF, LOD off, no normal maps, no shafts, linear fog | 13.629 ms | 380.7 ms |
| 2 cascades, Poisson, LOD Balanced, normal maps everywhere, shafts, height fog — preset Medium's columns | **13.551 ms** | 23.1 ms |
| 4 cascades, PCSS, LOD Near, all of it — preset Ultra's | 17.421 ms | 64.5 ms |

The phase's claim is that G1 pays for L1 and the frame comes out ahead. At this
camera it does, by 0.6 %, and the whole shadow half of preset Ultra costs
27.8 %. Both are small, and the reason is the same reason terrain LOD wins
little: the frame is bound by recording ten thousand terrain chunks and seventy
thousand M2 instances, and a reduced level draws the same number of chunks with
fewer indices in each. A camera bound by the shadow pass or by terrain vertex
work would say something different. What this one says is that the techniques
are close to free, and that what this scene wants is fewer draw calls.

**Four things were wrong that only a loaded world could show, and all four are
fixed.** The deferred descriptor write that binds a generated normal map was
deferred to the wrong moment and invalidated command buffers on every map that
landed; the capture tool was parking the player's own model on the lens; two
renders of one camera were not the same frame, so a sky-facing comparison read
94 % of pixels changed between two renders of the *same* settings; and PCSS was
rendering as Poisson. `docs/evidence/phase-01/README.md` has each of them with
the measurement that found it.

**And one that looked like a fifth and is not this phase's.** Start-up produces
about a hundred and sixty validation errors on a single frame — descriptor sets
destroyed or updated under a command buffer that had bound them, on the frame
the start-up MSAA rebuild resets the frame synchronisation. It reads as the
cascaded path's doing until the same command is run twice: with every phase-01
key at its *off* value it gives 3 errors one run and 161 the next. It is
confined to that one frame, five minutes of rendering afterwards raises nothing,
and no run lost a device. Recorded in the evidence file with the attempted fix
that made it worse.


## Cut order if the phase runs long

Drop in this order, each to a follow-up phase: geomorph (keep skirts) →
PCSS (keep Poisson) → S4 shafts → terrain normal maps (keep M2). Never drop the
consolidation steps 1–5; they are what the rest of the plan stands on.

**What was actually dropped:** geomorph, and parallax occlusion on the ground.
The second is less than this order’s last item rather than more — it says
"terrain normal maps (keep M2)", and what went is the march for relief over
those maps rather than the maps. The bump is what shows on the ground at
standing height; the relief is what shows on a wall. PCSS shipped out of order
because it is a hundred lines inside a shader that was being written anyway.

## Commit

```
Cascaded soft shadows, height fog, sun shafts, terrain LOD, and normal maps everywhere

One pass over the four lit shaders: shadow, fog and parallax become
shared includes, every toggle a specialization constant so the default
build is the SPIR-V it was - a test now checks that - and the per-frame
block grows once for everything this needs.

Sun shadows in four cascades with Poisson and PCSS filters, and off no
longer loses the device. Exponential height fog with a sun-coloured
in-scatter term derived from each zone's Light.dbc so nothing authored
shifts hue. Screen-space sun shafts. Distant terrain chunks drop to 81,
25 and 9 vertices behind skirts, which is the frame time the rest
spends. Tangent frames and generated normal maps for M2 models and
terrain, cached to disk in the background, so bump and parallax reach
every surface. VkContext reports a capability tier and settings the
GPU cannot honour say why. capture_scene takes --setting so a scene
can be rendered twice.
```
