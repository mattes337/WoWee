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

Machine: RTX 2070 SUPER, Vulkan SDK 1.4.357.0, Windows 11, 1920x1032. Every
picture named here is in `docs/evidence/phase-01/img/` as `.before.png`,
`.after.png` and `.diff.png`, and the numbers are what
`tools/compare_scenes.py` printed.

**The third pass — S4 and M3a — has no pictures, and the reason is not a
judgement.** `Data/extracted`, which is a junction into a checkout this worktree
does not own, was emptied while that pass was building. Every ADT, M2, WMO and
BLP the capture tool reads is gone from the machine; the client starts, loads
FrameXML and draws its interface over an empty grey frame, and
`asset_extract` cannot rebuild the tree here because StormLib is not in this
vcpkg install and the target is therefore not built at all. The rows below say
**not measured, blocked** where that is what happened, and say what was
measured instead. See `docs/evidence/phase-01/README.md`.

| Item | Result |
|---|---|
| Identity — `shader_offpath_identity` | **pass.** 0 shaders moved; character 1752, m2 806, terrain 454, wmo 731 instructions, the same counts as before the techniques landed — including after M3a put a normal-map path into `m2.frag` and `terrain.frag`, which is what `SPEC_NORMAL_MAP_EVERYWHERE` exists for. It caught two instructions there: `vec2 finalUV = TexCoord` above the branch let the optimizer fold two later loads of `TexCoord` into that one |
| Identity — a rendered frame at every new key's off value | **pass for the second pass's keys, not measured for the third's.** The same camera rendered from `c00ab904e` in a second worktree was **bit-identical** to this branch at defaults (numpy max abs difference 0 over RGB). `normalmapscope` and `sunshafts` did not exist then and no frame could be rendered after they did; what stands in for it is the SPIR-V identity above, which is the stronger half of the same promise and the half a picture cannot make precise |
| L1 `stormwind-gate` / `goldshire-inn-morning` / `orgrimmar-drag` / `stranglethorn-canopy` | **partial.** Two Elwynn cameras, not four across three continents: `goldshire-lake` at 09:00, `shadowcascades` 1→3, 35.26 % of pixels changed, SSIM 0.947440. The other three cameras were not rendered |
| L2 filter 0→1→2 | **pass.** `goldshire-road` 09:00: PCF→Poisson 4.18 % of pixels, SSIM 0.996499; Poisson→PCSS 14.55 %, SSIM 0.977045 |
| A1 fog cameras | **not measured.** `test_height_fog` pins the calibration arithmetic; no zone was rendered at 06/12/18 |
| S4 `elwynn-road-sunrise` | **not measured, blocked.** S4 ships. The pair could not be rendered: no world would load. What *was* measured is that a 45-second soak with validation on, `sunshafts=1` and `sunshaftstrength=1`, draws 36 263 frames without a device loss and without a validation message that does not also appear with the same keys off |
| G1 `westfall-sentinel-hill` / `tanaris-dunes`, + wireframe, Balanced within 0.5 % of Off | **fail on the threshold at the one camera that could be rendered, and still not tested at the two it names.** `goldshire-lake`, Off→Balanced: 28.55 % of pixels changed by more than one code value, against a stated ceiling of 0.5 %. The mean absolute difference over the frame is 1.277 / 255 and SSIM is 0.972705; the camera is under a forest canopy with 8x MSAA, where a mesh change moves almost every ground pixel by a code value or two. The third pass set out to re-run this at Sentinel Hill and the Tanaris dunes, where the threshold was written for an open horizon, and could not: the asset tree was gone. The threshold is still not met and is still not restated as met. Wireframe pair rendered (`terrainlod` Off→Far, 33.08 % of pixels, SSIM 0.961014) |
| M3a `northshire-abbey` / `stormwind-gate` ground / `kharanos-snow` | **not measured, blocked**, for the same reason. M3a ships: the tangent attribute on both vertex formats, the cache, the two shader paths and the `normalmapscope` row. What is measured is that `m2.frag` and `terrain.frag` are unchanged with the scope constant at its default, and that `test_tangent_frame` pins both frames |
| Fog calibration, every zone at 06/12/18 | **not measured** |
| Shadows off: 5 min, no device loss, validation clean | see the row below; measured with `capture_scene --dwell 300` rather than by walking Stormwind |
| Frame time, preset Medium, lower than pre-phase | **fail as stated, and the measurement says why.** See below |
| Load time, VRAM | **not measured, blocked.** Both are about the normal-map cache, which ships and which nothing in this session could make generate a single map: they are derived from textures, and there are no textures. The one number that can be stated is static: `sizeof(pipeline::TerrainVertex)` goes from 44 to 60 bytes, so the terrain mega vertex buffer goes from 66 MB to 90 MB, and the M2 vertex from 72 to 88. `NormalMapCache` logs its own map count and megabytes every 256 uploads, so the figure is there to be read the first time a zone loads |
| Unit tests: splits, snapping, LOD watertightness, tangents | **pass.** `test_terrain_lod` (watertightness, the skirt ring, one-level-apart), `test_tangent_frame` — which now pins the handedness the terrain grid actually has rather than the +1 it was assumed to have — `test_height_fog`, and `test_normal_map_cache` over the hash a generated map is filed under and the box filter that puts an oversized source under the cap |
| Lint: reserved-code check | **pass.** `reserved_code_check.py`: 15 markers, 0 for a shipped phase, 0 malformed. `shader_feature_check.py`: 7 constants, 0 disagreements |
| `./test.sh` / `ctest` green | **pass, against the ten failures this machine already had.** 197 of 207; the ten are five "Not Run" targets that need `vulkan.h`, `unicorn_stub_compiles`, `open_formats`, `open_format_emitter`, `cli_paths` and `sweep_guard`, and `sweep_guard` is over its ceiling on the same nine sweeps with the same numbers as before |

**On the frame time.** Measured with `capture_scene --dwell 60 --setting
vsync=0` on `goldshire-lake`, which draws the same frame several thousand times
so the number is not a walk:

| Configuration | Mean | Worst |
|---|---|---|
| 1 cascade, PCF, LOD off (what the client did before) | 12.2865 ms | 17.4591 ms |
| 2 cascades, Poisson, LOD off | 12.2805 ms | 19.5521 ms |
| 2 cascades, Poisson, LOD Balanced (preset Medium) | 12.6062 ms | 17.2644 ms |
| 4 cascades, PCSS, LOD Near (preset Ultra's shadow half) | 13.0819 ms | 25.9129 ms |

The phase's claim is that G1 pays for L1 and the frame comes out ahead. At this
camera it does not: preset Medium is 12.61 ms against 12.29 ms, 2.6 % *slower*,
and the whole shadow half of preset Ultra costs 6.5 %. Neither is much, and the
reason both are small is the same reason G1 does not pay for anything here: the
frame is bound by recording ten thousand terrain chunks and seventy thousand M2
instances, and terrain LOD does not reduce that - it draws the same number of
chunks with fewer indices in each. A camera bound by the shadow pass or by
terrain vertex work would say something different. This one says the techniques
are close to free and close to worthless, and that what this scene wants is
fewer draw calls. That is worth knowing and it is not what the phase file
predicted; the prediction is not restated here as met.

The third pass added two more things to this table and could put neither in it.
S4 has a stated ceiling of 0.3 ms at 1080p and M3a a stated bound of +40 % video
memory, and both are measurements of a loaded world; the pass that wrote them had
none. Neither number is guessed at here.

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
