# Modern Rendering Techniques — Assessment and Plan

**Status:** Assessment only. Nothing here is implemented. Read-only survey of the renderer as of 2026-09-10.
**Scope:** Which modern real-time rendering techniques fit this client, what each costs, and how each
would be added without breaking the hardware the client runs on today.

Every claim about the current code cites the file it was read from. Cite these rather than
re-deriving them.

---

## 1. Ground rules

Three constraints shape every entry below.

1. **The old hardware keeps working.** Nothing here may raise the floor. Every technique is
   detected at runtime, is off when its feature is missing, and when off leaves the frame
   bit-identical to today. See §3 for what "the floor" actually is.
2. **Every technique is one setting.** A setting is a row in `kSettingsSchema`
   (`include/ui/settings_schema.hpp`), with `enabledWhen` for dependencies on other settings and
   `unavailable` for hardware the client detected cannot do it. The panel greys it and explains;
   the write path refuses it. That mechanism exists and is the only one to use. No environment
   variables, no compile-time gates for player-facing features.
3. **Asset packs are orthogonal to code, and generated ones are made locally.** Upscaled
   textures, PBR sidecars and higher-poly models live beside the extracted data — hand-made
   packs under `Data/override/` (`src/pipeline/asset_manager.cpp:63`,
   `docs/asset-pipeline-gui.md`), tool-generated ones under `Data/generated/<expansion>/`
   with a manifest (§4.11). A pack never requires a code feature to be on, and a code
   feature never requires a pack: an upscaled diffuse looks better with Blinn-Phong, and
   clustered lighting works on stock 256×256 BLPs. Where a technique *benefits* from pack
   data (a real normal map rather than the Sobel-derived one) it reads the sidecar if present
   and falls back to today's path if not. **Deterministic generated assets are never downloaded** — the player's own game version is
   the source and the repository's tool the generator. **AI-generated assets are made once**,
   by the project, over all supported versions at the same time, and matched to the player's
   files by content hash (§4.11).

Techniques are ranked by (visible payoff for a 2004–2010 art style ÷ implementation and
compatibility risk). A WoW scene is low-poly, hand-painted, vertex-lit and fogged; techniques
that flatter that (shadows, ambient occlusion, lighting reach, atmosphere) rank above techniques
built for photoreal assets (full PBR, virtual geometry).

---

## 2. Where the renderer stands today

Measured by reading, not assumed.

### Frame

| Thing | State | Where |
|---|---|---|
| Scene colour | 8-bit `B8G8R8A8_UNORM`, LDR; the "tone map" is a shoulder on 8-bit values | `vk_context.cpp:1190`, `assets/shaders/postprocess.frag.glsl` |
| HDR intermediates | Only inside the FSR2/FSR3 path (`R16G16B16A16_SFLOAT`) | `post_process_pipeline.cpp:895-900` |
| Depth | `D32_SFLOAT`, conventional Z, no pre-pass | `vk_context.cpp:1563` |
| Motion vectors + camera jitter | **Exist**, for FSR2 (`fsr2_motion.comp.glsl`, `Camera` sub-pixel jitter) | `docs/architecture.md` Camera |
| Anti-aliasing | MSAA 1/2/4/8x, FXAA, FSR 1, FSR 2.2 (own compute + AMD SDK), FSR 3 frame gen | `post_process_pipeline.hpp` |
| Render graph | Exists, auto-inserts barriers, used for 5 pre-passes (shadow, reflection, cull …) | `render_graph.hpp`, `renderer.cpp:3948` |
| Sync | Timeline semaphores; sync2 through a lowering wrapper `cmdPipelineBarrier2` | `docs/plan-modernization.md` §Phase 8, `vk_utils.hpp:151` |
| Render passes | Classic `VkRenderPass`/`VkFramebuffer` (12 / 29); no dynamic rendering | `docs/plan-modernization.md` Gap 3 |
| Descriptors | Per-material sets; no descriptor indexing, no BDA, no indirect-count | same |
| Shader variants | **Zero** `VkSpecializationInfo` uses; all toggles are uniform `int` branches | grep of `src/rendering` |
| Vulkan floor | Instance asks 1.2, accepts 1.1; every extension optional (`enable_extension_if_present`) | `vk_context.cpp:435,567,718-751` |

### Lighting and shadows

| Thing | State | Where |
|---|---|---|
| Sun shadow | One orthographic map, 512–4096², 3×3 PCF, 40–500 yd half-extent, no cascades | `renderer.hpp:284-315`, `terrain.frag.glsl:50-61` |
| Shadows off | **Cannot be turned off** — loses the device; held on in code | `renderer.hpp:327-330`, `graphics_presets.hpp` note |
| Shading model | Forward Blinn-Phong, `pow(NdotH, 32) * specularIntensity` | `m2.frag.glsl:219`, `wmo.frag.glsl:320`, `character.frag.glsl:333` |
| Local lights | 64 point lights in the per-frame UBO, looped per fragment in every shader | `vk_frame_data.hpp:12,32-34`, `m2.frag.glsl:71-86` |
| Ambient | One colour from Light.dbc bands; WMO interiors use MOHD ambient | `lighting_manager.hpp`, `wmo_loader.hpp:192` |
| Fog | Linear start/end, one colour, sky-blend knob | `terrain.frag.glsl:178`, `settings_panel.hpp` fog rows |
| Normal maps / POM | Runtime Sobel from the diffuse (`normal_map.hpp`), POM 16/32/64 steps, WMO + characters only | `normal_map.hpp`, `pom_quality.hpp` |
| Water | Gerstner, GGX, planar reflection (re-renders the scene), half-res refraction copy, foam, SSS | `water_renderer.hpp:65`, `water.frag.glsl:99-115` |
| Sky | M2 skybox authoritative; procedural stars/clouds/celestials/lens flare as layers | `docs/SKY_SYSTEM.md` |
| AO, bloom, SSR, volumetrics, DoF, motion blur | None (grep finds only grass "bloom" colour and a glow-card heuristic) | — |

### Geometry and culling

| Thing | State | Where |
|---|---|---|
| Terrain | Full 145-vertex chunk mesh at every distance, **no LOD**, 4-layer splat, one descriptor set per chunk | `terrain_mesh.hpp:138`, `plan-modernization.md` Phase 8 |
| M2 | Skin LOD 0–3 by distance (40/80/150 yd), instanced by (model, LOD) | `m2_renderer_render.cpp:1376-1382` |
| WMO | Portal culling, no LOD | `docs/architecture.md` |
| GPU culling | Compute frustum + HiZ cull for M2, **result read back to CPU**, no indirect draw | `plan-grass.md` §1 "Important delta", `hiz_system.hpp` |
| GPU-driven | Grass is the first compute-compacted indirect draw | `plan-grass.md` |

### Assets

| Thing | State | Where |
|---|---|---|
| Texture formats | BLP DXT1/3/5 → BC1/2/3 passed through; RGBA8 otherwise; mips by `vkCmdBlitImage` | `vk_texture.cpp:278-280,436` |
| Override textures | PNG sidecar beside the `.blp`, decoded to **uncompressed RGBA8**, ≤8192² | `asset_manager.cpp:390-441` |
| Override packs | `Data/override/` merged from ordered packs by the asset GUI | `docs/asset-pipeline-gui.md` |
| Mobile | No BC → decode to RGBA8; ASTC/ETC2 not produced | `asset_manager.cpp:326`, `vk_context.cpp:610-615` |

Two things stand out. The renderer already owns the two hardest prerequisites of modern
temporal techniques — per-pixel motion vectors and a jittered camera — because FSR2 needed them.
And the settings schema already has the exact gating semantics (`enabledWhen`, `unavailable`)
that a capability-tiered feature set needs. Most of what follows is plumbing between things
that exist.

---

## 3. The hardware floor, honestly

The brief says "keep compatibility for models from 2010 on". Vulkan decides what that means:

| Vendor | Oldest with a Vulkan driver | Year | What it offers |
|---|---|---|---|
| NVIDIA | Kepler (GTX 600/700) | 2012 | Vulkan 1.2, tessellation, multi-draw indirect, descriptor indexing, BDA. No fp16, no VRS, no mesh, no RT |
| NVIDIA | Fermi (GTX 400/500) | 2010 | **No Vulkan driver.** Cannot run this client today |
| AMD | GCN 1 (HD 7000) | 2012 | Vulkan 1.2 (Windows, driver frozen), 1.3 via RADV on Linux; same feature class as Kepler |
| AMD | TeraScale (HD 5000/6000) | 2009–11 | No Vulkan |
| Intel | Skylake | 2015 | Vulkan 1.3 |
| Apple | MoltenVK on any Metal GPU | — | Vulkan 1.2/1.3 subset; no mesh shaders, no ray query, no VRS |
| Android | arm64, API 33 | — | Vulkan 1.1–1.3; ASTC/ETC2 only |

So the client's real floor is **2012-class Vulkan 1.1/1.2 hardware**, and the promise this
plan can keep is: *whatever runs the client today keeps running it, at today's quality*. Nothing
older is reachable, and nothing here changes that.

Define three capability tiers, detected once in `VkContext` and exposed as one enum:

| Tier | Requirement | Expected hardware | Unlocks |
|---|---|---|---|
| **T0 Baseline** | Vulkan 1.1, no optional features | Everything that runs today | All screen-space and CPU-side techniques; every fallback path |
| **T1 Core-1.2** | `descriptorIndexing`, `bufferDeviceAddress`, `drawIndirectCount`, `multiDrawIndirect`, tessellation | Kepler/GCN1 and up, MoltenVK, most Android | Bindless materials, GPU-driven draws, terrain tessellation |
| **T2 Modern** | Any of `VK_KHR_fragment_shading_rate`, `VK_EXT_mesh_shader`, `VK_KHR_ray_query`, `shaderFloat16` | Turing/RDNA2 (2018–2020) and up; **not** MoltenVK | VRS, mesh-shader geometry, ray-traced shadows/AO/reflections |
| **V Vendor** | A vendor runtime found on disk at start-up (Streamline/NGX, XeSS, FSR 3.1 runtime) plus that vendor's hardware | Per-vendor, see §4.9 | DLSS, XeSS, DLSS-G, Reflex/Anti-Lag; always with a T0 fallback |

Each optional feature is its own flag, not just the tier — a T2 card without `ray_query` still
gets VRS. The tier is only a summary for the presets.

A setting whose feature is missing gets its `unavailable` string filled at start-up
("This GPU has no ray query support") and the schema does the rest.

---

## 4. Techniques

Each entry: what it is, why it pays here, tier, fallback, and a plan brief enough to start from.
"Touches" lists where the change lands. Effort is in sessions of the size the other plans use.

### 4.1 Foundation — the plumbing later entries stand on

Not a phase of their own: each lands in the phase of the first technique that needs it
(§7.1). F1 and F5 are in phase 01; F3 is phase 03, F2 phase 04; F4 is phases 08–09.

#### F1. Shader variants via specialization constants
**What.** Replace uniform `int enableX` branches with `layout(constant_id = N) const bool`, and
build the pipeline permutations a material actually needs.
**Why.** Every technique below adds a toggle. Today each toggle would be another runtime `if`
in a fragment shader that already has a dozen, and the fallback path pays for the branch.
Specialization constants make "off" literally today's shader.
**Tier.** T0 (Vulkan 1.0 feature).
**Plan.** (1) A `ShaderFeatures` bitset and a pipeline cache keyed by (shader, features).
(2) Convert existing `enableNormalMap`, `enableParallax`, `alphaTest`, `unlit` first — they are
the precedent. (3) Persist the extra permutations in the existing pipeline cache file.
**Touches.** `vk_pipeline.hpp/.cpp`, every `*.frag.glsl`, the four material renderers.
**Effort.** 1–2 sessions. **Risk.** Low; mechanical.

#### F2. HDR scene target + real tone mapping + exposure
**What.** Render the scene into `B10G11R11_UFLOAT` (T0) or `R16G16B16A16_SFLOAT`, tone map
(AgX or ACES fitted) in the post pass, with a slow auto-exposure from a log-luminance
histogram. Keep the swapchain 8-bit UNORM; add scRGB/HDR10 output later (§4.7 P4).
**Why.** Bloom, volumetric light, sun glare and physically scaled lights are all impossible on
an 8-bit target; the current shoulder curve on LDR is a no-op for most pixels. This is the single
change most other entries depend on.
**Tier.** T0. Both formats are mandatory colour-attachment formats in Vulkan 1.0.
**Fallback.** Setting `hdr_pipeline` off → today's UNORM target and today's post shader.
**Plan.** (1) Off-screen scene target already exists for FSR/FXAA — make it the default path,
format chosen by the setting. (2) Tone map + exposure in `postprocess.frag`. (3) Convert
`lightColor`/`ambientColor` to linear and scale sun/local lights so today's look is reproduced
at exposure 1.0 — verify with the screenshot harness. (4) `Brightness` becomes exposure bias.
**Touches.** `post_process_pipeline.cpp`, `renderer.cpp` scene pass, `lighting_manager.cpp`.
**Effort.** 2 sessions. **Risk.** Medium — every colour constant in the shaders was tuned for
gamma space; expect a calibration pass.

#### F3. Depth pre-pass + reverse-Z
**What.** Draw opaque terrain/WMO/M2 depth-only first, then colour with `EQUAL` depth test.
Flip to reverse-Z (`1 - z`, `GREATER`) with a `D32_SFLOAT` depth that already exists.
**Why.** Halves fragment cost on overdraw-heavy WoW cities; gives SSAO/SSR/contact shadows
(§4.3) a complete depth buffer before the colour pass; reverse-Z removes the z-fighting on
2400-yard view distance that a 2004 engine at 1000 yards never had.
**Tier.** T0.
**Fallback.** Pre-pass is a setting; reverse-Z is unconditional once shipped (it is not a
visual choice and has no hardware dependency).
**Plan.** (1) Reverse-Z: projection helper in `camera.cpp`, compare ops, clear values, HiZ
build direction, shadow-bias sign, FSR2 depth convention flag. (2) Pre-pass reuses the shadow
pass's depth-only pipelines (they already exist per renderer). (3) Alpha-tested batches stay in
the colour pass.
**Touches.** `camera.cpp`, `renderer.cpp`, `hiz_system.cpp`, `post_process_pipeline.cpp`
(FSR2 depth flag), all pipeline depth states.
**Effort.** 1–2 sessions. **Risk.** Medium for reverse-Z (touches everything that reads depth).

#### F4. Native TAA
**What.** Temporal anti-aliasing at native resolution using the existing FSR2 jitter and motion
vectors: history buffer, reprojection, neighbourhood clamp, optional sharpen (RCAS exists).
**Why.** The client has MSAA, FXAA and FSR2-upscaling but no native-res temporal AA — the
cheapest good AA for foliage and alpha-tested geometry, and the reconstruction step every
screen-space effect below (AO, SSR, volumetrics) needs to hide its noise.
**Tier.** T0.
**Fallback.** Off → MSAA/FXAA path untouched. Mutually exclusive with FSR2 via `enabledWhen`.
**Plan.** (1) Factor jitter + motion-vector generation out of the FSR2 state so it can run with
FSR2 off. (2) **Per-object motion vectors**: today's `fsr2_motion.comp.glsl` reprojects depth
with the previous camera only, so anything that moves or animates has zero velocity and
ghosts. Add previous model matrix per instance and previous bone palette per character, write
an `RG16F` velocity attachment from every vertex shader, and keep the depth-reprojection path
for terrain/WMO (static). This also fixes the existing FSR2 ghosting on characters and feeds
every vendor upscaler in §4.9. (3) One compute pass; can literally start from
`fsr2_accumulate.comp.glsl` at scale 1.0. (4) Add to the anti-aliasing enum **at the end**
(schema order is persisted).
**Touches.** `post_process_pipeline.cpp`, `camera.cpp`, `settings_schema.cpp` AA row.
**Effort.** 1–2 sessions. **Risk.** Low; the hard parts already exist.

#### F5. Capability tiers + schema wiring
**What.** The detection described in §3, a `RenderCaps` struct on `VkContext`, and start-up
code that fills `SettingDesc::unavailable` for every technique whose flag is missing.
**Plan.** (1) Query `VkPhysicalDeviceFeatures2` chain for the flags in §3. (2) A table
`{settingKey, requiredCapBit, reasonText}`. (3) Presets gain new columns **appended** per the
rule in `graphics_presets.hpp`; a preset that names a T2 technique on a T0 card applies
everything else and greys that row.
**Effort.** 1 session. **Risk.** Low.

### 4.2 Shadows and lighting

#### L1. Cascaded shadow maps
**What.** 3–4 cascades along the view frustum (e.g. 0–40, 40–120, 120–350, 350–view distance)
into one 2D-array depth image, with stable texel snapping (which `renderer.cpp:3564` already
does for the single map) and a cascade-blend band.
**Why.** One 4096² ortho map over 300–500 yards gives ~0.2 yd/texel at the player's feet. WoW
characters are 2 yards tall. Cascades give ~0.02 yd/texel near and keep far shadows. This is the
single largest visible improvement available and it is entirely T0.
**Tier.** T0.
**Fallback.** `shadow_cascades = 1` is today's map. Keep the per-frame double buffering.
**Plan.** (1) Depth image becomes `VK_IMAGE_VIEW_TYPE_2D_ARRAY` with N layers; one render pass
per cascade (or one pass with multiview later). (2) `lightSpaceMatrix` → `lightSpaceMatrix[4]`
+ split distances in the per-frame UBO (std140: append, do not reorder). (3) A shared
`shadow_common.glsl` include with cascade select + PCF, replacing the four identical
`sampleShadowPCF` copies. (4) Fix the device-lost-on-disable bug on the way — the disabled path
is a cascade count of 0 that binds the white fallback that `shadow_params.hpp` already
describes.
**Touches.** `renderer.cpp` shadow pass, `vk_frame_data.hpp`, `shadow_params.hpp`, all five
lit shaders.
**Effort.** 2–3 sessions. **Risk.** Medium (four renderers draw into the map).

#### L2. Soft shadows: PCSS or moment-based
**What.** Percentage-closer soft shadows (blocker search + variable PCF) on the near cascades;
or, cheaper and better on old hardware, a 5×5 Poisson/rotated PCF.
**Why.** Hard 3×3 PCF on cascade 0 looks like a 2004 stencil shadow. Penumbra sells the sun.
**Tier.** T0. PCSS at high step counts is a quality dropdown (`shadow_filter`: PCF3, PCF
Poisson, PCSS).
**Plan.** One function in `shadow_common.glsl`, selected by specialization constant (F1).
**Effort.** 1 session. **Risk.** Low.

#### L3. Screen-space contact shadows
**What.** Short depth-buffer ray march toward the sun (8–16 steps) to darken the contact zone
that shadow-map bias always loses.
**Why.** Feet on ground, props on tables — where a 2004 art style shows the seam most.
**Tier.** T0. Needs F3 depth pre-pass to be cheap.
**Plan.** One full-screen compute pass writing an R8 mask; lit shaders multiply it into the
sun term. Denoise through TAA (F4).
**Effort.** 1 session. **Risk.** Low.

#### L4. Clustered forward lighting
**What.** Replace the 64-light per-fragment UBO loop with a view-space cluster grid (16×9×24),
light list in an SSBO built by compute, each fragment walks only its cluster. Lights come
from M2 light blocks and WMO `MOLT` chunks (both parsed already or trivially) instead of a
heuristic budget.
**Why.** Orgrimmar at night has hundreds of braziers and lamps; 64 is a per-frame cull budget
and the per-fragment loop costs the same whether a light is near or not. Clustered lighting
gives every torch a real light at constant cost and is the prerequisite for local-light shadows
(L6) and volumetrics (A2).
**Tier.** T0 — SSBOs and compute are Vulkan 1.0. T1 makes the cull cheaper (indirect).
**Fallback.** `lighting_mode = legacy` keeps the UBO array; both paths behind F1 constant.
**Plan.** (1) Compute pass: cluster AABBs (once per resize), light assignment per frame.
(2) `clusterLights.glsl` include with the same `localLightContribution` signature as today.
(3) Raise `MAX_LOCAL_LIGHTS` to 1024 in the SSBO path only. (4) Emission from
`m2_glow_card.hpp` heuristics becomes a light source too.
**Touches.** new `cluster_lighting.cpp`, `lighting_manager.cpp`, `m2_loader.cpp` (light
block), `wmo_loader.cpp` (MOLT), all lit shaders.
**Effort.** 3 sessions. **Risk.** Medium.

#### L5. Image-based ambient: sky SH + specular probes
**What.** Each frame (or on time-of-day change) render the skybox + sun into a 32² cubemap,
project to 2nd-order spherical harmonics for diffuse ambient, and prefilter 5 mips for
specular. Interiors use per-WMO-group probes baked at load from the MOHD ambient and the
group's own lit geometry.
**Why.** Today ambient is one flat colour; upward-facing surfaces at dusk are the same as
downward. Directional ambient is what makes low-poly models read as volumes. Cheap on any
hardware because the probe is tiny.
**Tier.** T0.
**Fallback.** Off → `ambientColor` as today.
**Plan.** (1) A `SkyProbe` pass in the render graph after the sky is parameterised.
(2) SH9 in the per-frame UBO (append 7 vec4s). (3) Lit shaders: `ambient = SH(n)`; specular
`textureLod(probe, R, rough * 4)` with the Blinn exponent mapped to roughness.
**Effort.** 2 sessions. **Risk.** Low–medium (calibration against Light.dbc looks).

#### L6. Local light shadows
**What.** Shadow-casting for the N nearest/brightest point lights via a cube-map atlas (6 faces
each) or dual-paraboloid, updated round-robin.
**Why.** A brazier that lights the wall but not the shadow of the orc beside it looks wrong once
L4 has made every brazier a light.
**Tier.** T0 for the atlas; T1 (multiview) makes cube rendering one pass.
**Plan.** Budget setting `local_shadow_count` 0/2/4/8; atlas 2048²; reuse the depth-only
pipelines from F3; only static WMO/M2 doodads cast into these (characters are the CSM's job).
**Effort.** 2–3 sessions. **Risk.** Medium (cost scales with WMO complexity).

#### L7. Physically-based shading (opt-in, pack-aware)
**What.** GGX/Schlick/Smith replacing Blinn-Phong, with roughness/metalness from an `_orm`
sidecar when a pack provides one and from the existing per-material heuristics otherwise
(cloth rough, metal armour glossy — the `wmo_material_class.hpp` and `m2_model_classifier.cpp`
classifiers already carry that intent).
**Why.** Water already uses GGX (`water.frag.glsl:99`); the rest of the world uses a 1990s
exponent. PBR with a flat-roughness fallback still looks better under L5 probes because
energy is conserved. But it is a *look* change and hand-painted specular-in-diffuse art can
fight it, so it stays opt-in.
**Tier.** T0.
**Plan.** (1) `brdf.glsl` include used by water and everything else. (2) Sidecar loading in
`asset_manager.cpp` (`_n.png`, `_orm.png` beside the `.blp`; KTX2 later — see M1).
(3) Default roughness per material class; user setting `shading_model: legacy|pbr`.
**Effort.** 2 sessions. **Risk.** Medium (art calibration; pair with F2).

### 4.3 Screen-space effects

All of these need F2 (HDR), F3 (depth pre-pass), and F4 (TAA to hide their noise). All T0.

#### S1. GTAO / SSAO
**What.** Ground-truth ambient occlusion (horizon-based, 2–4 slices, half-res, temporally
accumulated) multiplied into the ambient term; optional bent normals feeding L5.
**Why.** The highest payoff-per-effort of any screen-space effect on low-poly, flat-ambient
art. Corners of Ironforge, under-eaves of Goldshire, folds of a cape.
**Plan.** One compute pass after the pre-pass, one bilateral upsample. XeGTAO reference
implementation is MIT and Vulkan-portable.
**Effort.** 1–2 sessions.

#### S2. Physically-based bloom
**What.** Progressive downsample/upsample (Call of Duty 2014 style) on the HDR target, no
threshold, low intensity.
**Why.** Sun glare, lava, spell effects, Blizzard's own "full-screen glow" the 2008 client had
and this one does not.
**Plan.** 6-mip chain in compute, blended in tone map. Trivial once F2 exists.
**Effort.** 1 session.

#### S3. Screen-space reflections
**What.** Hi-Z ray march (HiZ pyramid already exists, `hiz_system.hpp`) for glossy surfaces,
falling back to the L5 probe where the ray leaves the screen.
**Why.** Primarily to **replace the planar water reflection**, which re-renders the whole scene
from a mirrored camera (`water_renderer.hpp:115-129`) — the single most expensive optional pass
in the client. SSR also gives wet stone, polished floors and ice something to reflect.
**Plan.** (1) SSR pass writes a reflection colour + confidence; (2) water samples it instead of
the planar texture when `water_reflection = ssr`; (3) keep planar as the choice for people who
want off-screen reflections. Later: Hi-Z from the current frame after F3, not the previous.
**Effort.** 2 sessions. **Risk.** Medium (water is 2181 lines of tuned code).

#### S4. Volumetric light shafts (screen-space)
**What.** Radial blur from the sun's screen position masked by depth (the cheap 2008
technique), gated to when the sun is on screen. Distinct from true volumetrics (A2).
**Why.** Costs almost nothing, looks like WoW's own sun rays in Elwynn, works on T0.
**Effort.** Half a session.

#### S5. Depth of field, motion blur, film grain, chromatic aberration
**What.** Standard post effects. Motion vectors exist (F4), so per-object motion blur is real,
not camera-only.
**Why.** Low priority for a game view, useful for the character screen, cinematics, and
screenshots. Each is a bool in the schema. Listed for completeness; do after everything above.
**Effort.** 1 session for the set.

### 4.4 Atmosphere

#### A1. Height fog + aerial perspective
**What.** Replace linear distance fog with exponential height fog plus a simple aerial
perspective (in-scatter toward the sun colour, out-scatter by distance) whose parameters are
*derived from* Light.dbc's fog colour/start/end so every zone keeps its authored look.
**Why.** Linear fog is the most dated thing on screen at 2400 yards. Height fog puts mist in
Duskwood valleys and leaves peaks clear. Keeps the skybox authoritative (`docs/SKY_SYSTEM.md`
warns against replacing it, and this does not).
**Tier.** T0. **Fallback.** `fog_model = linear`.
**Plan.** One `fog.glsl` include; `fogParams` gains height and density in appended UBO slots.
**Effort.** 1 session. **Risk.** Low.

#### A2. Froxel volumetric fog and god rays
**What.** A 160×90×64 view-frustum-aligned volume: compute pass injects sun (shadowed by CSM)
and clustered lights (L4), ray-march integrates, scene applies by depth lookup. Temporal
reprojection across frames.
**Why.** True light shafts through Stranglethorn canopies and torch glow in Blackrock. The
technique of the last decade that suits this art best after CSM and AO.
**Tier.** T0 at low resolution; T2 `shaderFloat16` halves its cost. Needs L1 and L4.
**Plan.** Three compute passes; the CSM sample function from L1; quality setting sets froxel
depth count.
**Effort.** 3 sessions. **Risk.** Medium.

#### A3. Physically-based sky *for lighting only*
**What.** A Hillaire-style atmosphere LUT evaluated for the sun direction, used to drive L5's
probe and A1's scatter colours — **not** drawn, because the M2 skybox stays authoritative.
**Why.** Gives sunrise/sunset ambient a real spectrum without touching what the player sees in
the sky. Optional; L5 from the skybox alone gets most of the way.
**Effort.** 1–2 sessions. **Priority.** Low.

### 4.5 Geometry and GPU-driven rendering

#### G1. Terrain LOD with geomorphing
**What.** Per-chunk index buffers at 4 levels (145 → 81 → 25 → 9 vertices per chunk) chosen
by distance, with vertex morphing so transitions do not pop, and skirt strips on chunk edges.
**Why.** Every chunk currently draws at full density to 2400 yards. This is pure cost with no
visible payoff beyond ~200 yards, and it is the biggest T0 performance win available. Faster
frames are what fund the techniques above on old hardware.
**Tier.** T0. **Fallback.** `terrain_lod = off`.
**Plan.** (1) Index buffers generated once in `terrain_mesh.cpp`. (2) LOD select in
`terrain_renderer.cpp` where distance culling already runs. (3) Morph factor as a vertex
attribute or computed from `viewPos` in `terrain.vert.glsl`.
**Effort.** 2 sessions. **Risk.** Low–medium (cracks).

#### G2. Bindless materials + multi-draw indirect
**What.** All terrain layer textures and M2/WMO material textures in one descriptor array
(`descriptorIndexing`, `nonuniformEXT`), per-draw material index in an SSBO, and one
`vkCmdDrawIndexedIndirectCount` per renderer with the GPU cull (which exists) writing the draw
list instead of a CPU-readback mask.
**Why.** `plan-modernization.md` Phase 8 deferred descriptor indexing until "a feature that
needs it" — this is that feature. It removes the per-chunk terrain bind, the CPU readback
stall in M2 culling (`m2_renderer_render.cpp:877-885`), and is the prerequisite for G3.
**Tier.** T1. **Fallback.** Today's bind-per-draw path, unchanged.
**Plan.** (1) Texture registry handing out array indices; (2) grass's compaction shader as the
template (`plan-grass.md`); (3) M2 first (cull already on GPU), then terrain, then WMO.
**Effort.** 4 sessions across the three renderers. **Risk.** Medium–high (touches every
material binding site).

#### G3. Mesh shaders for terrain and WMO
**What.** Meshlet-based drawing with per-meshlet frustum/cone/HiZ culling in the task stage.
**Why.** Only worth it after G2; the gain over indirect draws is meshlet-level culling of
large WMOs (Undercity, Karazhan). Real but niche.
**Tier.** T2 (`VK_EXT_mesh_shader`), never MoltenVK. **Fallback.** G2 path.
**Effort.** 3 sessions. **Priority.** Low; do last of the geometry items.

#### G4. Terrain tessellation + displacement
**What.** Hardware tessellation on near chunks with displacement from a height sidecar (pack
data) or from the splat alpha as a cheap approximation.
**Why.** Cobblestone and rock in Stormwind at the player's feet. Pairs with POM which already
exists — tessellation where close, POM further, flat beyond.
**Tier.** T1 (tessellation feature; on all T1 hardware, MoltenVK included).
**Plan.** Tess control/eval shaders for terrain only; `terrain_tessellation` quality enum.
**Effort.** 2 sessions. **Risk.** Medium (cracks with G1; do G1 first).

#### G5. Variable rate shading
**What.** `VK_KHR_fragment_shading_rate` with a per-frame rate image: 1× at edges (Sobel of
the previous frame's luminance) and 2×2 in flat areas; 2×2 everywhere in fog.
**Why.** Free 20–40 % fragment cost at 4K on T2 cards, invisible with TAA on. Two hundred lines.
**Tier.** T2. **Fallback.** Off.
**Effort.** 1 session. **Risk.** Low.

### 4.6 Ray tracing (T2 only, long horizon)

Requires `VK_KHR_acceleration_structure` + `VK_KHR_ray_query` (ray query in the fragment or
compute stage; no RT pipelines, which keeps the shader model simple). BLAS per M2 model and WMO
group at load; skinned characters refit per frame; TLAS rebuilt per frame from the same instance
list the culling uses. Never available under MoltenVK.

| Item | What | Replaces | Effort |
|---|---|---|---|
| **R1. RT sun shadows** | 1 ray per pixel toward the sun, denoised through TAA; hybrid — CSM beyond 100 yd | L1+L2 at Ultra | 3 sessions after BLAS infra |
| **R2. RT ambient occlusion** | 1–2 short rays per pixel | S1 | 1 session on R1's infra |
| **R3. RT reflections** | Rays for glossy surfaces and water, probe fallback | S3 | 2 sessions |
| **R4. RT local light shadows** | Ray per light per pixel within cluster | L6 | 1 session |

The BLAS infrastructure (2 sessions) is the real cost; each effect after it is small. Justify it
only after L1–L6 and S1–S3 exist, because those are the fallbacks R1–R4 must degrade to and
they cover 100 % of the hardware.

### 4.7 Presentation

| Item | What | Tier | Effort |
|---|---|---|---|
| **P1. FSR 3.1 upscaler** | Update the AMD path to the 3.1 API (better ghosting than 2.2); own fallback stays | T0 | 1 session |
| **P2. Vendor upscalers** | DLSS and XeSS as *runtime-loaded* optional libraries, same jitter/motion inputs FSR2 uses. Detailed in §4.9 | vendor | see §4.9 |
| **P3. Dynamic resolution** | Scale the FSR2/TAA internal resolution against a frame-time target | T0 | 1 session |
| **P4. HDR display output** | `VK_COLOR_SPACE_HDR10_ST2084_EXT` / scRGB swapchain when the monitor offers it; tone map to PQ | T0 (needs F2) | 1 session |
| **P5. Colour grading LUT** | 32³ LUT applied in tone map; per-zone LUTs as a pack item | T0 | Half a session |

### 4.8 Asset packs — orthogonal, optional, offline

These change files in `Data/override/`, never code paths. The client gains the *ability* to
read richer files; whether it does depends only on the pack being installed.

#### M1. Compressed override textures (KTX2 / BC7 / BC5 / ASTC)
**What.** Read `.ktx2` sidecars (Basis Universal or raw BC7/BC5/ASTC with mips) before `.png`.
Extend `tools/blp_convert` into a pack compiler: BLP or PNG in → KTX2 with mips out, BC7 for
colour, BC5 for normals, ASTC 6×6 for the Android build.
**Why.** Today a PNG override is uploaded as **uncompressed RGBA8** (`asset_manager.cpp:431`).
A 2048² upscale is 16 MB + mips, ×4 versus BC7; an HD pack of a few thousand textures does not
fit in 4–8 GB of VRAM without this. This is the prerequisite for every other pack item.
**Tier.** T0 (BC on desktop, ASTC/ETC2 on mobile — both already probed in `vk_context.cpp:610`).
**Effort.** 2 sessions (loader + tool).

#### M2. AI-upscaled diffuse packs
**What.** Offline Real-ESRGAN (or similar) 2×–4× of every BLP, with the alpha channel
upscaled separately and re-thresholded for alpha-tested textures, packed by M1. A curated
allow-list rather than everything: terrain tilesets, WMO walls, character skins; skip UI,
particles, and anything with baked text.
**Why.** The most visible "modern" change for the least engine risk. Packs already exist in the
community for the original client (the CharSections auto-detect in `docs/status.md` mentions
"HD-textured clients"); this only needs the format.
**Engine work.** None beyond M1. Pipeline work: a script in `tools/` and a pack manifest.

#### M3. PBR sidecars (normal, roughness/AO/metal)
**What.** Offline-generated `_n.ktx2` and `_orm.ktx2` per diffuse, from a height-estimation
model or from the same Sobel/blur the client runs at load (`normal_map.hpp`) but at pack
resolution. Read by L7 when present; the runtime Sobel path remains the fallback.
**Why.** Moves per-load CPU work offline, and lets an artist hand-fix the ones that matter.
**Engine work.** The sidecar lookup in L7. Pack work: script.

#### M4. Higher-poly model overrides
**What.** `.m2` + `.skin` files in the override directory shadow the archive's, which
`resolveFile` already honours (`asset_manager.cpp:177-183`). Community HD character and
creature models for 3.3.5a exist and use the same format. Optionally a subdivision + normal
bake pass in the pack tool for doodads.
**Engine work.** None for drop-in models. A check that the override's `.skin` LOD set matches
the LOD selector's expectations (`availableLODs` mask handles missing levels already).

#### M5. HD skybox and cloud packs
**What.** Higher-resolution skybox M2 textures via M2; optional equirect HDR sky per Light.dbc
band for L5's probe (a pack-supplied probe beats a rendered one). Keep the M2 skybox drawn.
**Engine work.** L5 reads a probe texture if the pack ships one.

### 4.9 Vendor-dependent mechanisms

State of the vendor SDKs as of September 2026, checked against the vendors' own pages (sources
at the end of this section). Everything here is a **fourth tier, "V"**: available only on one
vendor's hardware, through one vendor's binary, and only if that binary is present at runtime.
The client already has the exact shape this needs — the AMD FSR3 runtime is loaded from a path
(`WOWEE_FFX_SDK_RUNTIME_LIB`, `docs/AMD_FSR2_INTEGRATION.md` "Path A"), falls back cleanly when
absent ("Path C"), and reports which backend is active in the settings panel. Every item below
follows that pattern: **no vendor binary in the repository, runtime-loaded, off when missing,
`unavailable` string says why.**

The inputs every one of these wants are the same four the renderer already produces for FSR2:
jittered colour, depth, motion vectors, and the jitter offset + camera matrices. That is why
this section is cheap: the integration is a backend switch on the existing post-process path,
not a renderer change. Ray Reconstruction and DLSS 5 are the exceptions and are called out.

#### NVIDIA

| Mechanism | What | Vulkan | Hardware | Verdict |
|---|---|---|---|---|
| **DLSS Super Resolution / DLAA** (DLSS 4.5, transformer model) | ML upscaler; DLAA is the same at native res, i.e. a better F4 | Yes, via Streamline or NGX directly (`nvpro-samples/vk_streamline`) | RTX 20+ | **Do.** Best upscaler available on NVIDIA; drop-in backend beside FSR2 in `post_process_pipeline.cpp`. Adds a `DLSS` choice to the FSR-quality enum (appended) |
| **DLSS Frame Generation / Multi Frame Gen** (2×–6×, "Dynamic MFG" in 4.5) | Interpolated frames | Yes (Streamline DLSS-G supports Vulkan; VSync-with-FG is D3D12-only) | RTX 40+ (MFG: RTX 50) | **Do**, after SR. The FSR3 frame-gen plumbing already exists; DLSS-G takes the same inputs plus a HUD-less colour, which the ImGui/FrameXML pass order already gives |
| **Reflex** (`VK_NV_low_latency2`) | CPU-side frame pacing to cut input latency; required by DLSS-G to keep latency sane | Yes (core extension, Streamline SDK 2.14+) | RTX 20+; open `low_latency_layer` reimplements it for AMD/Intel on Linux | **Do**, small: two calls around input sampling in `application.cpp`. Pairs with `VK_AMD_anti_lag` below in one `LatencyMode` abstraction |
| **DLSS Ray Reconstruction** | ML denoiser for ray-traced signals, replaces hand denoisers | Yes | RTX 20+ | Only after R1–R3 exist; it wants normals, albedo, roughness and specular hit-distance buffers the forward renderer does not write today. Plan it with the RT phase, not before |
| **DLSS 5 "Neural Rendering"** | A neural post-process that re-lights and re-materials the finished frame toward photorealism from colour + motion vectors, with per-object intensity/tone/mask controls for the developer. Launched 2026-09-03 with NBA 2K27 | Not stated by NVIDIA yet | RTX 50 only (not 5060/5060 Ti); heavy — reports of ~90→40 fps on a 5080 before MFG | **Track, do not plan.** Three reasons: (1) it changes the art — a hand-painted 2004 look pushed toward "photoreal materials" is the opposite of what the packs in §4.8 try to preserve, and the per-object mask controls exist precisely because studios need to fence it off; (2) no public integration path or Vulkan statement yet; (3) one GPU generation. Revisit when an SDK with Vulkan support exists; if ever adopted it is an opt-in "neural look" toggle, off in every preset, with the mask driven by the classifiers in `m2_model_classifier.cpp` so characters and UI are excluded |
| **Neural Texture Compression** (RTX NTC, cooperative vectors) | All PBR channels of a material in one neural bundle, decoded in-shader; 80 %+ VRAM cut claimed | Yes — `VK_NV_cooperative_vector`, and cooperative vectors are in Vulkan 1.3 cross-vendor now | Tensor-core GPUs for the fast path; a slow DP4a fallback exists | **Interesting for §4.8 M1–M3**, exactly where an HD pack's VRAM cost lives. But: the pack compiler would emit a second format, and BC7/ASTC already solves the problem on every GPU. Prototype after M1 ships and only if a 4× pack still does not fit 8 GB. Transcode-to-BC on load is the sane middle: NTC on disk, BC in VRAM, no shader change |
| **RTX Mega Geometry** (`VK_NV_cluster_acceleration_structure`) | Cluster-BLAS builds for animated/tessellated geometry | Yes | RTX 20+ (driver 572+) | Only meaningful with R1–R4 and G4 tessellation. Skinned characters refit per frame is the case it solves. Long horizon |
| **Smooth Motion** (driver frame gen) | Driver-level interpolation, no integration | Yes (added to the NVIDIA app requirements) | RTX 50 | Nothing to implement. Keep frame pacing stable and present from one queue so driver FG does not stutter |

#### AMD

| Mechanism | What | Vulkan | Hardware | Verdict |
|---|---|---|---|---|
| **FSR 3.1.5 upscaler + frame gen** | The last non-ML FSR; cross-vendor | Yes | RX 500+ and any vendor | **Do (P1).** Replace the in-tree 2.2 path with the 3.1 API via the runtime-loaded Path A; it is the fallback every other vendor's absence lands on |
| **FSR 4.1 upscaler / FSR 4 frame gen** (FSR SDK 2.3, June 2026) | ML upscaler and FG | **No — DirectX 12 only.** Vulkan is explicitly unsupported for the Redstone set | RDNA 3/4 (FG: RDNA 4 only) | **Blocked** by API. Nothing to do until AMD ships a Vulkan backend; the settings row can exist greyed with "FSR 4 has no Vulkan support" so players are told rather than left guessing. The driver-level FSR 4 override of FSR 3.1 games on Windows may still apply to a 3.1 integration — that is AMD's side, not ours |
| **FSR Ray Regeneration / Radiance Caching** (Redstone) | ML denoiser; neural GI cache (0.9 preview) | No | RDNA 4 | Same as above; would pair with R1–R3 and DLSS-RR if a Vulkan path appears |
| **Anti-Lag 2** (`VK_AMD_anti_lag`, Vulkan 1.3.291) | Same idea as Reflex | Yes | RDNA+ | **Do**, with Reflex, as one abstraction |
| **AFMF 2.1** (driver frame gen) | Driver-level interpolation | Yes (DX11/12, Vulkan, OpenGL) | RX 6000+ | Nothing to implement |

#### Intel

| Mechanism | What | Vulkan | Hardware | Verdict |
|---|---|---|---|---|
| **XeSS 2.1 Super Resolution** | ML upscaler, XMX fast path on Arc, DP4a fallback on any SM 6.4-class GPU | Yes (DX11/DX12/Vulkan; 2.1 improved the Vulkan path) | Arc; DP4a on GTX 10+/RX 5000+ | **Do.** Third backend beside FSR and DLSS; the only ML upscaler that runs on non-Intel cards, which makes it the ML choice for Pascal/RDNA1–2 owners |
| **XeSS Frame Gen + XeLL** | FG and low latency, opened to NVIDIA/AMD GPUs in 2.1 | Not confirmed for Vulkan — verify in the 2.1 SDK before planning | Arc; others with FG on | Add if the Vulkan backend covers it; otherwise greyed row |

#### Apple, Qualcomm, Arm

| Mechanism | What | Reachable from this client | Verdict |
|---|---|---|---|
| **MetalFX** upscaling + frame interpolation + denoising (Metal 4) | Apple's ML upscaler | **No.** The client draws through MoltenVK; MetalFX is a Metal API taking Metal textures. Reaching it means a native Metal presentation path beside the Vulkan one | Not worth a second backend for one effect. FSR 3.1 and F4 TAA cover macOS. Revisit only if a Metal path is built for another reason |
| **Snapdragon Game Super Resolution 2** | Shader-only spatial/temporal upscaler, open source, tuned for Adreno | Yes, plain GLSL | **Do for Android**: it is what FSR 1 is on desktop, at mobile cost. Two shaders |
| **Arm Accuracy Super Resolution** | Open-source FSR2 derivative tuned for Mali | Yes | Same as SGSR — pick one per SoC by runtime vendor check; both are cross-vendor shaders, so "vendor-dependent" only in tuning |

#### Integration shape

1. **Streamline as the one integration point** for NVIDIA + Intel (it hosts DLSS, DLSS-G,
   Reflex, XeSS plugins; FSR 3 too, though the in-tree AMD path already exists). Streamline's
   framework is source-available; **the DLSS-G plugin and the NGX runtime are prebuilt DLLs
   with their own redistribution terms, separate from the Streamline source licence.** So:
   the client dlopen()s `sl.interposer` from a path the player supplies or the NVIDIA app
   installs, exactly like the FSR3 runtime; nothing NVIDIA-owned is committed. A licence
   read of the NGX/DLSS terms is the first task of the session, before code.
2. **One `Upscaler` interface** in `post_process_pipeline.cpp` with backends
   `Internal FSR2 / AMD FSR 3.1 / DLSS / XeSS / SGSR-ASR`, all fed the same jittered colour,
   depth, motion-vector and camera inputs. The enum in the schema is appended, never reordered.
3. **One `FrameGen` interface**: `AMD FSR3 / DLSS-G / XeSS-FG`, plus "driver" meaning do
   nothing and let Smooth Motion/AFMF run.
4. **One `LatencyMode`** wrapping `VK_NV_low_latency2` and `VK_AMD_anti_lag`; the open
   `low_latency_layer` makes both work on Linux for the other vendors without code.
5. Settings panel shows the active backend name and version, as it already does for FSR2.

**Effort.** Streamline + DLSS-SR + Reflex: 2 sessions. DLSS-G: 1 session on top. XeSS: 1
session. SGSR/ASR: 1 session. Licence review: half a session, first.

Sources for this section: [NVIDIA GTC 2026 DLSS 5 announcement](https://www.nvidia.com/en-us/geforce/news/death-stranding-2-crimson-desert-dlss-4-multi-frame-gen/),
[Club386 DLSS 5 release](https://www.club386.com/nvidia-dlss-5-release-date/),
[Corsair DLSS 5 guide](https://www.corsair.com/us/en/explorer/gamer/gaming-pcs/nvidia-dlss-5-everything-you-need-to-know/),
[NVIDIA DLSS 4.5 SDK](https://developer.nvidia.com/blog/nvidia-dlss-4-5-delivers-super-resolution-upgrades-and-new-dynamic-multi-frame-generation),
[Streamline programming guide, DLSS-G](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS_G.md),
[vk_streamline sample](https://github.com/nvpro-samples/vk_streamline),
[Streamline 2.14.1 / DLSS 310.9.1 notes](https://en.gamegpu.com/news/igry/nvidia-obnovila-streamline-sdk-i-biblioteki-dlss-do-versii-310-9-1),
[RTX NTC SDK](https://github.com/NVIDIA-RTX/RTXNTC),
[RTX Mega Geometry](https://github.com/NVIDIA-RTX/rtxmg),
[Smooth Motion Vulkan](https://www.tomshardware.com/pc-components/gpus/nvidia-adds-vulkan-compatibility-to-smooth-motion-frame-generation-complete-with-a-boost-for-emulators),
[AMD FSR SDK 2.3](https://gpuopen.com/amd-fsr-sdk/),
[AMD FSR Redstone for developers](https://gpuopen.com/learn/amd-fsr-redstone-developers-neural-rendering/),
[VK_AMD_anti_lag](https://www.tomshardware.com/pc-components/gpus/amd-anti-lag-steps-out-of-its-comfort-zone),
[VK_NV_low_latency2](https://registry.khronos.org/vulkan/specs/latest/man/html/VK_NV_low_latency2.html),
[low_latency_layer](https://github.com/Korthos-Software/low_latency_layer),
[Intel XeSS SDK 2.1.0](https://github.com/intel/xess/releases/tag/v2.1.0),
[XeSS 2.1 cross-vendor FG](https://www.tomshardware.com/pc-components/gpus/xess-sdk-2-1-release-opens-up-intels-framegen-tech-to-compatible-amd-and-nvidia-gpus-xe-low-latency-also-goes-cross-platform-if-framegen-is-enabled),
[MetalFX frame interpolation](https://hothardware.com/news/apple-unveils-metalfx-frame-interpolation-ai-game-rendering-bandwagon),
[Arm ASR vs GSR vs MetalFX](https://www.dtgre.com/2025/03/arm-asr-vs-qualcomm-gsr-vs-apple-metalfx-guide.html).

### 4.10 What each technique needs — assets, automation, code, extra data

The question this table answers: *can this technique be built end-to-end by code and scripts,
with no artist and no hand-tuned per-asset data?* Those are the first pass. Everything that
needs a human per asset, an external licence decision, or a vendor binary comes after.

Column meanings:

- **Assets** — `none`: stock files untouched. `optional pack`: better with pack data, full
  fallback without. `required`: cannot work without new asset data.
- **Automation** — how the asset side gets made. `n/a` when there is none. `script`: a
  deterministic tool run. `AI`: an offline model run (upscaler, height/normal estimator),
  still unattended. `manual`: an artist per asset. `curate`: automatic bulk with a human
  allow-list pass afterwards, which the first pass can ship without.
- **Code** — `shader`, `renderer`, `loader`, `tool`, `plumbing` (pipeline/sync/settings).
- **Extra data** — anything the technique needs that does not exist yet and where it comes
  from. Verified against the code: motion vectors today are **camera-only** depth reprojection
  (`fsr2_motion.comp.glsl:24-40`) — nothing moving or skinned has a velocity; tangent frames
  are generated at load for **WMO and characters only** (`character_renderer.cpp:1834`,
  `wmo.vert.glsl:32`), not for M2 doodads or terrain; WMO lights are parsed
  (`wmo_loader.hpp:212`), M2 light blocks are not.
- **First pass** — ✓ when Assets ∈ {none, optional pack with script/AI automation}, Code only,
  no licence or vendor gate, no manual step.

| # | Technique | Assets | Automation | Code | Extra data it needs, and source | First pass |
|---|---|---|---|---|---|---|
| F1 | Spec constants | none | n/a | plumbing, shader | — | ✓ |
| F2 | HDR + tone map | none | n/a | renderer, shader | Linear-light re-scaling of Light.dbc colours: computed at load, no asset edit. Calibration by golden compare (script) | ✓ |
| F3 | Pre-pass + reverse-Z | none | n/a | renderer, plumbing | — | ✓ |
| F4 | TAA | none | n/a | renderer, shader | **Per-object motion vectors**: previous model matrix per instance and previous bone palette per character, written by every vertex shader into an `RG16F` velocity target. Code only; without it moving things ghost | ✓ |
| F5 | Caps + schema | none | n/a | plumbing | — | ✓ |
| L1 | CSM | none | n/a | renderer, shader | — | ✓ |
| L2 | Soft shadows | none | n/a | shader | — | ✓ |
| L3 | Contact shadows | none | n/a | shader | Depth from F3 | ✓ |
| L4 | Clustered lights | none | script (none needed; data is in the files) | loader, renderer, shader | Light sources: WMO `MOLT` (parsed), **M2 light block** (add to `m2_loader.cpp`), glow-card heuristic (exists) | ✓ |
| L5 | Sky probes | none (optional pack for M5 HDR sky) | n/a | renderer, shader | — | ✓ |
| L6 | Local shadows | none | n/a | renderer | Light list from L4 | ✓ |
| L7 | PBR shading | optional pack (M3) | AI/script for the pack; heuristics without | shader, loader | **Tangent frames for M2 doodads and terrain** (generate at load, Lengyel, as characters do); roughness from material class tables | ✓ (heuristic mode) |
| S1 | GTAO | none | n/a | shader | Depth + normals: reconstruct normals from depth, or write a normal target in F3's pre-pass (better; code) | ✓ |
| S2 | Bloom | none | n/a | shader | HDR from F2 | ✓ |
| S3 | SSR | none | n/a | shader, renderer | Depth, normals, roughness (from L7 heuristics or constant) | ✓ |
| S4 | Light shafts | none | n/a | shader | Sun screen position (exists in lens flare) | ✓ |
| S5 | DoF / motion blur / grain | none | n/a | shader | Velocity from F4 | ✓ |
| A1 | Height fog | none | n/a | shader | Parameters derived from Light.dbc at load | ✓ |
| A2 | Volumetric fog | none | n/a | renderer, shader | CSM, cluster list | ✓ |
| A3 | Physical sky for lighting | none | n/a | renderer | — | ✓ (low value) |
| G1 | Terrain LOD | none | script (index sets built at runtime) | loader, renderer, shader | — | ✓ |
| G2 | Bindless indirect | none | n/a | plumbing, renderer | — | ✓ |
| G3 | Mesh shaders | none | script (meshlet build at load or in pack tool) | renderer, shader | Meshlets: generated, never authored | ✓ (T2 only) |
| G4 | Tessellation | optional pack (height sidecar) | AI (height estimation) / script (from splat alpha) | shader | Height maps; fallback from existing Sobel height in `normal_map.hpp` alpha | ✓ (fallback mode) |
| G5 | VRS | none | n/a | renderer, shader | Previous-frame luminance | ✓ (T2 only) |
| R1–R4 | Ray tracing | none | n/a | renderer, shader | BLAS/TLAS from existing geometry; skinned refit needs bone-transformed vertices (compute) | ✓ (T2 only) |
| P1 | FSR 3.1 | none | n/a | plumbing | Vendor runtime, but AMD's is redistributable and already wired | ✓ |
| P3 | Dynamic resolution | none | n/a | plumbing | — | ✓ |
| P4 | HDR output | none | n/a | plumbing, shader | — | ✓ |
| P5 | Colour LUT | optional pack (per-zone LUTs) | manual for art LUTs; neutral default is code | shader | — | ✓ (neutral LUT) |
| M1 | KTX2/BC7 override loader + pack compiler | **required** for M2–M5 to be usable, none for the loader itself | script (batch convert) | loader, tool | — | ✓ |
| M2 | AI-upscaled diffuse packs | required (that is the pack) | **AI, once**: Real-ESRGAN-class model on the project's build machine over the hash-union of all four extractions; alpha upscaled separately and re-thresholded; path-pattern allow-list; `curate` improves it later | tool, loader (hash-keyed pack reader) | Nothing engine-side beyond M1 | ✗ (phase 24: once-built on a build machine, last of the generated) |
| M3 | PBR sidecars | required (the pack) | **script** locally for Sobel normals and class-based roughness/AO (first pass); **AI, once** for estimated normal/height (DeepBump/Marigold-class), shipped in the same hash-keyed pack as M2 | tool, loader (sidecar lookup in L7) | — | ✓ (script mode) |
| M4 | Higher-poly models | required | **manual** (community HD packs or modelling). Automated subdivision produces blobs on hand-modelled low-poly | none (override dir already resolves `.m2`/`.skin`) | — | ✗ |
| M5 | HD skybox / cloud textures | optional pack | AI, once (part of the M2 pack) for textures; manual for HDR equirect | none beyond M2 | — | ✓ (upscale part) |
| V | DLSS / XeSS / Reflex / Anti-Lag | none | n/a | plumbing | Vendor binary at runtime; **licence review is a human step** | ✗ (second pass) |
| V | DLSS 5 | none | n/a | — | RTX 50, no Vulkan path, art-direction decision | ✗ |
| V | SGSR 2 / Arm ASR | none | n/a | shader | — | ✓ |
| V | NTC | required (re-encoded packs) | script | tool, loader, shader | Cooperative vectors; transcode-to-BC middle path avoids the shader part | ✗ (only if BC7 packs overflow) |

**Two code-only prerequisites the table exposes** that were folded into other entries before:

- **Per-object motion vectors** (row F4). Every temporal technique — TAA, DLSS, XeSS, FSR2/3,
  GTAO denoise, SSR denoise, volumetric reprojection, motion blur — is wrong on anything that
  moves until this exists. Today FSR2 ghosts characters for this reason. It is one instance
  field (previous model matrix), one extra bone palette per character, one output attachment,
  and a velocity-aware `fsr2_motion` replacement. The velocity target is reserved in phase 03's
  pre-pass and consumed by phase 08.
- **Tangent generation for M2 doodads and terrain** (row L7). Characters and WMOs already
  compute tangents at load; extend the same function to the other two vertex formats. Needed
  by normal maps from packs (M3), by PBR, and by tessellation displacement.

**Excluded from the first pass, and why:** M4 (needs an artist), the vendor runtimes (need a
licence read and a binary the repo cannot carry), DLSS 5 (art direction, hardware, API), NTC
(no problem to solve until M2 packs overflow VRAM). Everything else is code plus unattended
scripts, and that is the whole of phases 01–23 (§7.4). The AI pack (phase 24) is the one
generated item that waits, and authored assets are after it.

### 4.11 Where generated assets are made: deterministic locally, AI once

Two classes of generated asset, and they are treated differently because their cost is
different by three orders of magnitude.

| Class | Cost per WotLK extraction | Made | Distributed |
|---|---|---|---|
| **Deterministic** — BC/ASTC encodes, Sobel normals, class-based roughness, splat-derived height, sky probes, LOD tables | Milliseconds to a second per asset; the whole extraction in the background over a few sessions of play | On the player's machine, by the client itself on loading screens and idle frames; `tools/generate_assets` pre-warms the same cache for those who want it done at once | Never. Nothing to download |
| **AI** — upscaled diffuse, estimated normal/height, upscaled skybox | Tens of GPU-hours per model version, tens of GB output | **Once**, by the project, on a build machine, over the union of all supported expansions | As opt-in packs, outside the repository, keyed by source content hash |

#### Deterministic: always local

**Rule.** No deterministic generated asset is ever downloaded. The player picks the game
version; the tool follows. Two reasons:

1. **Version freedom.** The client supports Vanilla 1.12, TBC 2.4.3, WotLK 3.3.5a and Turtle
   1.18 through per-expansion data roots (`Data/expansions/<id>`, manifest field `expansion`,
   `asset_manifest.hpp:59`). Local generation is right for whichever one is installed.
2. **It is cheap enough that shipping it would be the expensive option.** A BC7 encode of an
   extraction is an hour once; a download of the same is gigabytes per player.

#### AI: once, then shared

**Rule.** AI outputs are produced once per (model, model version, parameters) and never
regenerated on players' machines. Re-running a 4× upscale of forty thousand textures on every
client is days of GPU time per player and a different result per GPU driver; the same work on
one build machine is done once and identical for everyone.

How "once" also covers every supported version:

- **Key by source content hash, not by expansion or path.** Most textures are byte-identical
  across 1.12, 2.4.3 and 3.3.5 — the same `foo.blp` has the same bytes. The pack builder
  walks the union of all four extractions, deduplicates by hash, and upscales each distinct
  source once. A pack entry says *"for a source whose bytes hash to H, here is the
  derivative"*. The client matches on H against its own extraction, so a Vanilla player uses
  every entry whose source they have and none they do not, and a Turtle texture that differs
  from its WotLK namesake simply gets no entry until the pack is rebuilt with Turtle's file
  in the union. No per-expansion packs; one pack, four versions.
- **Ship transcodable, encode locally.** The pack carries KTX2 in Basis Universal UASTC;
  the client's deterministic tool (or the loader on first touch) transcodes to BC7 on desktop
  and ASTC on Android. One artefact serves every platform; the platform-specific encode is
  the cheap local step it always was.
- **Pack version is deliberate.** A pack bumps only when the model or its parameters change
  and the result is judged worth another build. Model drift is not a reason to regenerate.
  The manifest records model name, weights hash and parameters so a player can see what made
  their textures.

**Where the pack lives and the licence stance.** An AI-upscaled Blizzard texture is a
derivative of a Blizzard texture. The README's "contains no Blizzard assets" is a statement
about this repository, so the pack is built and hosted **outside it** — a release asset on a
separate channel, installed through the existing pack mechanism in the asset pipeline GUI
(`docs/asset-pipeline-gui.md`: activate pack → rebuild override), the same way third-party HD
packs are installed today and curated by `asset_pack_curate.py`. Whether to host such a pack
at all, and where, is a project decision to make with the licence in front of it; this
document only makes sure the client and the tooling do not depend on the answer. **The
build pipeline, the manifest format and the hash matching are in the repository. The
outputs are not.** A player who would rather not download anything can run the same build
script locally with `--ai`; it is the same code and the same manifest, just slow.

#### What runs where

| Kind | Made | Persisted | Examples |
|---|---|---|---|
| **Load-time, cheap** (< ~1 ms per asset) | In the client, at load, every time | No — memory only | Tangent frames, terrain LOD index sets, runtime Sobel normal map (`normal_map.hpp`), mip chains via blit, meshlets for small models |
| **Loading-screen generator** (milliseconds to a second per asset) | In the client, on loading screens and idle frames, by a background worker; first touch of an asset may take the slow path once | `Data/generated/<expansion>/…` with the manifest | BC7/BC5/ASTC encodes of decoded and generated textures, class-based roughness/AO, splat-derived terrain height, sky probe cache. `tools/generate_assets` is the same code as a pre-warm CLI, never a requirement |
| **AI pack** | Project build machine, once per pack version; `--ai` locally for those who insist | Installed pack under `Data/override/` (hand-made) or `Data/packs/<name>/` (hash-keyed AI) | Upscaled diffuse, estimated height/normal, upscaled skybox |

The load-time kind needs no manifest and no setting. The other two write into one manifest,
and **the manifest is what enables the setting**.

#### The generated-asset manifest

`Data/generated/<expansion>/generated.json`, written by the tool, read by `AssetManager`
at start-up beside `manifest.json`. Installed AI packs carry their own `pack.json` keyed by
source hash; the tool merges the two views into what the client reads.

```json
{
  "schema": 1,
  "expansion": "wotlk",
  "source_manifest_hash": "…",
  "generators": {
    "textures.bc7":  { "version": 3, "params": { "quality": "high" }, "files": 41213, "complete": true },
    "textures.pbr":  { "version": 1, "params": { "normal": "sobel", "roughness": "class" }, "files": 41213, "complete": false, "done": 30011 }
  },
  "packs": {
    "hd-diffuse-x2": { "version": 2, "model": "realesrgan-x2-v0.3", "weights_hash": "…", "matched": 12890, "unmatched": 311 }
  },
  "entries": { "world/…/foo.blp": { "src_hash": "…", "bc7": "…ktx2", "n": "…", "orm": "…", "pack": "hd-diffuse-x2" } }
}
```

What the client does with it:

- Each pack-dependent setting names the generator or pack it needs (a `requires` string on
  the schema row, next to `enabledWhen`/`unavailable`). At start-up, `unavailable` is filled
  from the manifest: absent → *"Not generated. Run `tools/generate_assets --pbr` or use the
  asset pipeline GUI"*; `complete: false` → *"Generation 73 % done; partial results are
  used"*; AI pack missing → *"Requires the HD diffuse pack"*; `unmatched` → tooltip lists how
  many of the player's textures the pack has no entry for.
- `source_manifest_hash` mismatch means the player re-extracted or switched patch level:
  deterministic output is marked stale and greyed with that reason; AI pack entries are
  unaffected because they match on content hash, not on the extraction. Nothing is deleted;
  the tool's next run diffs `src_hash` per entry and regenerates only what changed.
- Per-file lookup goes through the same `resolveFile` chain that already checks
  `Data/override/` first (`asset_manager.cpp:177-183`): **override → AI pack (by hash) →
  generated → extracted → archive.** A hand-made pack still beats everything, so M4-style
  community packs and generated data coexist.

The manifest replaces "the existence of the file enables the setting" because a 40 000-file
directory scan at every start is slow on old disks, and a directory cannot say *why* it is
incomplete, *which* tool made it, or *which* pack version it came from.

#### The tool

One entry point, `tools/generate_assets` (Python, beside `asset_pack_curate.py`; the BC
encoder, the Basis transcoder and any model runner are subprocesses it drives), with the asset
pipeline GUI gaining one page that calls it and shows progress. The same script with
`--build-pack` is what the project's build machine runs to produce an AI pack.

Requirements that come straight from "must work with every supported game version":

1. **Enumerate through the manifest, never the file system.** `manifest.json` lists every
   extracted file with its WoW path; the tool walks that, so it sees exactly what the client
   sees, for whichever expansion root it is pointed at (`--expansion vanilla|tbc|wotlk|turtle`,
   default: the one the client's config selects). `--build-pack` takes several roots and
   unions them by hash.
2. **Read formats through the same code the client reads them with.** BLP1 (Vanilla) and
   BLP2 (`blp_loader.cpp:136-140`), M2 `MD20` at the versions the four expansions use, WMO
   v17, ADT — the tool must not carry a second parser that drifts. Either the loaders are
   built as a small shared library with a C API the script calls (preferred: one parser), or
   the tool is a C++ target linking `src/pipeline/` (the `tools/asset_extract` precedent).
3. **Deterministic and incremental.** Output = f(source bytes, generator version, params).
   `src_hash` per entry; re-run skips what is current. Interrupting is safe: `complete:
   false`, resume picks up. AI packs are additionally **pinned**: the model weights hash is in
   the manifest and a pack is never rebuilt implicitly.
4. **Classify by path and by content, not by expansion.** The upscale allow-list ("terrain,
   WMO, character skins; not UI, particles, text") is a rule over WoW paths and texture
   properties, so it holds in 1.12 and 3.3.5 alike. Custom-zone textures outside the stock
   tree (`custom_zones/textures/`, already probed by `tryLoadPngOverride`) follow the same
   rules and, having no shared hash, are the one case where `--ai` locally is the only route.
5. **Output formats are per-platform** for the deterministic tool (`--target desktop|android`,
   default host): BC7/BC5 KTX2 on desktop, ASTC 6×6 on Android. AI packs are platform-neutral
   UASTC and transcoded locally. The client's existing `blockCompressionSupported()` check
   decides which it reads.
6. **A CI matrix that runs the deterministic tool headless on a 200-file sample of each of
   the four extractions** and checks the manifest, the file count, and a golden hash per
   generator version. The pack builder is tested on the same samples for hash-union
   correctness (a texture present in three extractions appears once).
7. **Budget printed before starting**: file count, estimated minutes, and disk. For `--ai`
   locally the estimate is GPU-hours, so the player sees why the pack exists.

#### First-release scope

Phases 1 and 2 ship the deterministic generators — Sobel normal/height and tangents at
load (phase 01), then the BC7/ASTC cache (phase 07), `textures.pbr` class roughness/AO
(phase 12), `terrain.height`
from splat alpha (phase 17) and `sky.probe` (phase 10) on loading screens — plus the manifest and the
settings wiring. The hash-keyed pack reader and the `--build-pack` builder are phase 24, the
last of the generated-asset work; whether a pack is published is the licence decision above.
The client behaves identically either way, differing only in which `unavailable` string a
setting shows.

---

## 5. Compatibility matrix

| Technique | T0 | T1 | T2 | MoltenVK | Android | Fallback when off |
|---|---|---|---|---|---|---|
| F1 spec constants | ✓ | ✓ | ✓ | ✓ | ✓ | n/a (pure plumbing) |
| F2 HDR + tone map | ✓ | ✓ | ✓ | ✓ | ✓ | UNORM target, today's post shader |
| F3 pre-pass / reverse-Z | ✓ | ✓ | ✓ | ✓ | ✓ | no pre-pass |
| F4 TAA | ✓ | ✓ | ✓ | ✓ | ✓ | MSAA/FXAA |
| L1 CSM | ✓ | ✓ | ✓ | ✓ | ✓ (2 cascades) | 1 cascade = today |
| L2 soft shadows | ✓ | ✓ | ✓ | ✓ | ✓ | 3×3 PCF |
| L3 contact shadows | ✓ | ✓ | ✓ | ✓ | costly | off |
| L4 clustered lights | ✓ | ✓ | ✓ | ✓ | ✓ | 64-light UBO |
| L5 sky probes | ✓ | ✓ | ✓ | ✓ | ✓ | flat ambient |
| L6 local shadows | ✓ | ✓ | ✓ | ✓ | off | none |
| L7 PBR | ✓ | ✓ | ✓ | ✓ | ✓ | Blinn-Phong |
| S1–S4 screen-space | ✓ | ✓ | ✓ | ✓ | low-res | off |
| A1 height fog | ✓ | ✓ | ✓ | ✓ | ✓ | linear fog |
| A2 volumetric fog | ✓ (low) | ✓ | ✓ (fp16) | ✓ | off | A1 |
| G1 terrain LOD | ✓ | ✓ | ✓ | ✓ | ✓ | full mesh |
| G2 bindless indirect | — | ✓ | ✓ | ✓ | most | bind-per-draw |
| G3 mesh shaders | — | — | ✓ | ✗ | ✗ | G2 |
| G4 tessellation | — | ✓ | ✓ | ✓ | some | POM only |
| G5 VRS | — | — | ✓ | ✗ | some | off |
| R1–R4 ray tracing | — | — | ✓ (RT) | ✗ | ✗ | L1/S1/S3/L6 |
| V: DLSS SR / DLAA / FG / Reflex | — | — | NVIDIA RTX 20+ (FG 40+, MFG 50) | ✗ | ✗ | FSR 3.1 / TAA |
| V: XeSS 2.1 SR | — | Arc; DP4a on GTX 10+/RX 5000+ | ✓ | ✗ | ✗ | FSR 3.1 |
| V: FSR 4 / Redstone | — | — | ✗ (no Vulkan backend) | ✗ | ✗ | FSR 3.1 |
| V: Anti-Lag 2 / Reflex latency | — | vendor ext or open layer | ✓ | ✗ | ✗ | none |
| V: DLSS 5 neural rendering | — | — | RTX 50 only, no Vulkan statement | ✗ | ✗ | not planned |
| V: SGSR 2 / Arm ASR | ✓ (shader-only) | ✓ | ✓ | ✓ | ✓ | FSR 1 |
| M1–M5 packs | ✓ | ✓ | ✓ | ✓ | ✓ (ASTC) | stock assets |

---

## 6. Orthogonality rules (binding)

1. **One schema row per technique**, key named after the technique, default off unless the
   entry above says otherwise. Presets may turn it on by an **appended** column.
2. **Hardware gating through `unavailable`**, filled at start-up from `RenderCaps`. Never a
   silent downgrade: a setting that cannot be honoured is greyed with the reason.
3. **Off means today's code.** Enforced by F1: the off-variant of each shader must be
   byte-identical SPIR-V to the pre-feature shader (add a test that compiles both and diffs).
4. **UBO growth is append-only** (`GPUPerFrameData` is std140-mirrored in seventeen shaders).
5. **Packs never gate code and code never requires packs.** A sidecar lookup returns "absent"
   and the runtime path runs. A pack installed on a T0 machine is fully used by T0 shaders.
5a. **Deterministic generated assets are made on the player's machine; AI assets are made
   once and matched by content hash.** The generated manifest (§4.11) is what turns a
   pack-dependent setting on; absence, partial completion, staleness, tool version and
   missing pack each produce their own `unavailable` text. The tool enumerates through
   `manifest.json`, parses with the client's own loaders, and runs in CI against all four
   supported expansions. An AI pack is never rebuilt implicitly.
6. **No new required extension, ever.** `enable_extension_if_present` is the only spelling.
7. **Screenshot goldens per technique**, on and off, through the existing capture harness
   (`tools/capture_scene`), so the "off is identical" promise is measured, not asserted.

---

## 7. Phased plan

### 7.1 How the order was chosen

Every technique scored on two axes: **impact** — how much of the screen changes, for how many
players, on the hardware tiers it reaches (T0 counts triple: it is every player) — and
**effort** in sessions, including the prerequisites it drags in. Sorted by impact ÷ effort,
then grouped into phases along dependency lines. The prerequisite plumbing rides in the phase
of the first technique that needs it, never earlier and never as a phase of its own. The
"Phase" column is the coarse theme number; the session-level numbering (01–28) is in
`modern-rendering/README.md`.

| # | Technique | Impact (1–5) | Effort (sessions, incl. prereqs) | Ratio | Hard dependency | Phase |
|---|---|---|---|---|---|---|
| S4 | Light shafts (screen-space) | 3 | 0.5 | 6.0 | sun screen pos (exists) | 1 |
| A1 | Height fog + aerial perspective | 4 | 1 | 4.0 | — | 1 |
| L2 | Soft shadows (PCSS / Poisson) | 3 | 1 | 3.0 | L1 | 1 |
| P1 | FSR 3.1 runtime | 3 | 1 | 3.0 | — | 3 |
| S2 | Bloom | 4 | 1 | 4.0 | F2 | 2 |
| S1 | GTAO | 5 | 1.5 | 3.3 | F3 | 2 |
| L1 | Cascaded shadow maps (+ fixes shadow-off device loss) | 5 | 2.5 | 2.0 | — | 1 |
| G1 | Terrain LOD + geomorph | 4 (perf on T0) | 2 | 2.0 | — | 1 |
| L3 | Contact shadows | 2 | 1 | 2.0 | F3 | 3 |
| L5 | Sky SH + specular probes | 4 | 2 | 2.0 | — | 4 |
| G5 | VRS | 2 (T2 only) | 1 | 2.0 | T2 | 6 |
| F3 | Depth pre-pass + reverse-Z (+ normal target) | 3 | 1.5 | 2.0 | — | 2 |
| F2 | HDR target + tone map + exposure | 3 (+ enabler) | 2 | 1.5 | — | 2 |
| L7 | PBR shading (heuristic mode) | 3 | 2 | 1.5 | F2, tangents (M3a) | 4 |
| S3 | SSR (retires planar water re-render) | 3 (+ perf) | 2 | 1.5 | F3, HiZ | 5 |
| F4 | Native TAA incl. per-object motion vectors | 4 | 3 | 1.3 | F3 | 3 |
| L4 | Clustered lighting (+ M2 light block parse) | 4 | 3 | 1.3 | F2 | 4 |
| A2 | Froxel volumetric fog | 4 | 3 | 1.3 | L1, L4 | 5 |
| V | DLSS / XeSS / Reflex / Anti-Lag | 4 | 3 (+ licence) | 1.3 | F4's inputs | 7 |
| L6 | Local light shadows | 3 | 2.5 | 1.2 | L4 | 4 |
| G4 | Terrain tessellation | 2 | 2 | 1.0 | G1, M3b height | 5 |
| M3a | Load-time normal/height + tangents for M2 doodads and terrain (extends `normal_map.hpp`, cached to disk in the background) | 3 | 1 | 3.0 | — | 1 |
| M1 | Background BC7/ASTC cache of decoded and generated textures, with the `Data/generated` manifest | 2 (VRAM, load time) | 2 | 1.0 | — | 2 |
| M3b | Class-based roughness/AO sidecars, splat-derived height — loading-screen generators | 2 | 1 | 2.0 | M1 | 2 |
| M2 | AI upscale pack: once-built, hash-keyed reader + builder | 5 | 4 (+ GPU-days once, + licence decision) | 1.2 but externally gated | M1 | 9 |
| M4/M5 | Authored assets: HD models, HDR sky equirect, art LUTs | 3 | manual | — | — | after 9 |
| G2 | Bindless + indirect | 2 (perf) | 4 | 0.5 | T1 | 6 |
| G3 | Mesh shaders | 1 | 3 | 0.3 | G2, T2 | 6 |
| R1–R4 | Ray tracing set | 3 (T2 RT only) | 10+ | 0.3 | T2 RT | 8 |
| P3 | Dynamic resolution | 2 | 1 | 2.0 | F4 or FSR | 3 |
| P4 | HDR display output | 2 | 1 | 2.0 | F2 | 2 |
| P5 | Neutral colour LUT | 1 | 0.5 | 2.0 | F2 | 2 |
| S5 | Motion blur / DoF / grain | 1 | 1 | 1.0 | F4 | 3 |
| A3 | Physical sky for lighting | 1 | 1.5 | 0.7 | L5 | deferred |

F1 (specialization constants) and F5 (capability detection + schema gating) have no impact of
their own and are the toggle mechanism every entry relies on. They are the first steps of
phase 01.

### 7.2 What "a phase ships" means

Every phase ends as a client a player can run every day. Not "feature-complete for the phase"
— **production grade**, which here means all of:

1. `./test.sh` green (unit + lint), validation layers clean on the three desktop platforms
   and Android, no new required extension (`enable_extension_if_present` only).
2. Every new setting has its schema row, tooltip, `enabledWhen`/`unavailable`/`requires`,
   a preset column appended, and the FrameXML options panel shows it (the schema does this;
   verify it).
3. The off-path of every new toggle is byte-identical SPIR-V to the previous release's shader
   (the F1 diff test) and pixel-identical on the goldens.
4. Comparison-mode output (§7.5) for each technique on every scene tagged with it: before,
   after, diff, numbers, and the phase's report page under `docs/evidence/phase-N/`.
5. Frame time on the T0 reference machine within 5 % of the previous release with every new
   toggle off, and a recorded number with them on.
6. `docs/status.md`, `CHANGELOG.md` and a howto for anything the player has to do (run a
   tool, install a pack) updated. `docs/plan-modern-rendering.md` §7.4 progress row filled.
7. No reserved-code marker (§7.3) older than two phases left in the tree.

If a technique in a phase cannot meet these by the end of the phase, it moves to the next
phase; the phase still ships without it. A phase is a release, not a promise.

### 7.3 Preparation work and reserved code

A phase may build plumbing beyond what its own techniques need, when doing it now is cheaper
than doing it twice and it does not change the frame with everything off. Examples: phase 01's
per-frame UBO gets the appended slots for cascade matrices *and* the SH9 coefficients that
phase 10 fills; phase 03's pre-pass writes the normal target GTAO needs *and* the velocity
target phase 08 fills; phase 07's texture cache stores a `pack` field per entry that only
the phase 24 AI-pack reader fills. Phase numbers in markers are the session numbers from
`modern-rendering/README.md`.

Every such piece of code is marked where it lives:

```cpp
// RESERVED(phase-10, L5-sky-probes): SH9 slots appended so the UBO layout does not change
// again when probes land. Zero-filled until then; shaders ignore them.
glm::vec4 skySH[7];
```

Rules for the marker:

- Format is `RESERVED(phase-N, <technique-id>): <why now>` on the line above the code, in
  C++, GLSL, CMake and Python alike.
- `tools/reserved_code_check.py` (new, small, joins the existing `tools/*_check.py` family)
  lists every marker with its phase and fails CI when a marker names a phase at or below the
  "Shipped through" line in `modern-rendering/README.md` — the phase that consumes the code
  removes the marker in the same commit.
- Reserved code is never a player-visible setting. A schema row exists only when the
  technique behind it works. Reserved shader inputs are bound to zero-filled buffers or the
  existing white/flat fallbacks (`shadow_params.hpp` pattern), never left unbound.
- Reserved code is tested the same as live code: the off-path identity test (§7.2 item 3)
  covers it, because it must not change the frame.
- Two-phase limit: a marker older than two shipped phases is either consumed or deleted.
  Speculation that has waited that long was wrong about being needed.

### 7.4 The phases

**Executed as 24 single-commit phases in
[`modern-rendering/README.md`](modern-rendering/README.md).** Each file there is the
authoritative scope for its session: what ships, what was cut and where it went, the steps
in order, every settings row, every `RESERVED` marker, the verify list, and the commit
message. This section keeps only the shape.

Generated assets are not one thing. What the client can make for itself at load or on a
loading screen — normals, tangents, roughness classes, a compressed-texture cache — comes
early (01, 07, 12, 17) because it is cheap and makes every later technique better. What has
to be made once on a build machine (AI upscales) is the last phase (28). What has to be made
by a person (HD models, HDR skies, art LUTs) is not a phase; the client already reads it.
Phase 01 is deliberately one multi-day phase: its five techniques edit the same shaders,
UBO block and settings table, and refactoring those once beats doing it five times.

| Phases | Theme | What the player has at the end |
|---|---|---|
| 01 | Shadows, fog, distance, surfaces | Cascaded and soft shadows (and a working off switch), height fog and sun shafts, terrain LOD, normal maps on everything — all T0, no download; the lit shaders consolidated once |
| 02 | Instrument | `--compare` on the executable, 25 verified scenes, evidence pages |
| 03–06 | Light and depth | Reverse-Z pre-pass, HDR and tone mapping, bloom, GTAO |
| 07 | Cache | Background BC7/ASTC texture cache with the generated-asset manifest and `requires` gating |
| 08–09 | Temporal | Real motion vectors (FSR2 stops ghosting), native TAA, dynamic resolution |
| 10–15 | Lights, materials, atmosphere | Sky probes, clustered lights from the game's own light data, GGX with class roughness, local and contact shadows, volumetric fog, SSR replacing planar water |
| 16–17 | Upscaler interfaces, tessellation | FSR 3.1 behind one `Upscaler` interface; tessellated near terrain |
| 18–19 | GPU-driven | Bindless indirect M2, VRS |
| 20–21 | Vendor | Licence review; DLSS + Reflex; XeSS, Anti-Lag, mobile upscalers — runtime-loaded, never committed |
| 22–23 | Ray tracing | BLAS/TLAS, RT shadows, RT AO and reflections on RT hardware only |
| 24 | AI pack | Hash-keyed once-built texture pack reader and builder |

Twenty-four commits. The client is a different game after 01, on every GPU it runs on today.

**Deferred** (no phase): DLSS-G/MFG and Ray Reconstruction (after 20/23 with a licence
outcome), G3 mesh shaders, R4 RT local shadows, S5 cinematic post, A3 physical sky, DLSS 5
neural rendering (art direction, RTX 50 only, no Vulkan path yet), FSR 4/Redstone (no
Vulkan backend), NTC (only if BC7 packs overflow VRAM), MetalFX (needs a Metal path),
authored assets. Each has a greyed schema row saying why, or no row at all where there is
nothing to grey.

### 7.5 Comparison mode — before and after, from one flag

Every phase has to prove its impact with pictures, not adjectives, and every off-path has to
prove it changed nothing. One mechanism does both: start the client with a compare flag, get
a before image, an after image, a difference image and the numbers.

#### What exists

`tools/capture_scene` (`howtos/capture-scene-screenshots.md`) already renders a scene
headless from a map, a camera, a time of day, weather, and an optional character with
equipment, and writes a PNG plus an entity manifest. It has per-system switches
(`--no-shadows`, `--no-water`, …) but no way to set a *renderer setting*, no second render,
and no diff. It runs on Lavapipe in Docker, which is T0 only — fine for CI, useless for
anything T1/T2/vendor.

#### What is built (phase 02; phase 01 ships a v0 of it because its own exit needs one)

**One flag on the game executable**, so the comparison runs on the player's real GPU with the
player's real settings as the baseline:

```bash
wowee --compare L1-csm --scene goldshire-inn-morning --out compare/phase1
wowee --compare L1-csm,L2-soft,A1-height-fog --scene all --out compare/phase1
wowee --compare S1-gtao --scene ironforge-forge --value 2 --frames 48 --out compare/s1
```

- `--compare <ids>`: technique ids from §4 (`L1-csm`, `S1-gtao`, …), which are also the
  schema keys. `all-phase-N` expands to the phase's list from a table in the binary.
- `--scene <name|all|all-for-technique>`: from `tools/compare_scenes.json` (below). Default
  is every scene tagged with the technique.
- `--value`: the on-value; default is the Ultra preset column for that key.
- `--frames N`: render N frames before capturing, default 1 for static techniques and 32 for
  anything temporal (TAA, GTAO, SSR, volumetrics, upscalers), so the history has converged.
  With `--sequence` it writes every frame instead, for ghosting and stability checks.
- No window, no UI, no network: the same in-process path `capture_scene` uses, moved into a
  small `SceneSetup` library both executables link, so the two never drift.
- `capture_scene` gains the same `--compare/--scene` flags for the Docker/CI path.

**Per technique per scene it writes:**

| File | What |
|---|---|
| `<scene>.<tech>.before.png` | Technique off, every other setting at the baseline |
| `<scene>.<tech>.after.png` | Technique on at `--value` |
| `<scene>.<tech>.diff.png` | Per-pixel difference as a heat map, with the percentage of pixels above threshold in the corner |
| `<scene>.<tech>.side.png` | Before and after side by side with a labelled wipe at the middle — the picture for changelogs and `docs/status.md` |
| `<scene>.<tech>.json` | SSIM and PSNR before/after, GPU frame time before/after (median of the last 16 frames), the full settings dump for both runs, the capability tier the run had, the git hash, the generated-asset manifest state (§4.11) |
| `<scene>.<tech>.sequence/` | Only with `--sequence`: numbered frames, plus an MP4 when libav is present (the loading-screen player already links it) |

**Two assertions the mode makes, with exit codes CI reads:**

1. `before.png` must match the previous release's golden for that scene within tolerance —
   this is §6 rule 3 and §7.2 item 3 measured rather than asserted. A technique whose off
   state moved a pixel fails here.
2. `after.png` must differ from `before.png` by more than a floor — a technique that changed
   nothing is either broken or pointed at the wrong scene.

`tools/compare_report.py` folds a directory of these into one HTML page per phase: a grid of
side-by-sides with the numbers under each, sorted by SSIM delta. That page *is* the phase's
impact evidence, and its numbers replace the guessed impact column in §7.1 once a phase has
run.

#### The scene catalogue

`tools/compare_scenes.json`, one entry per scene: name, map, camera, target, time of day,
weather, optional character block (reusing `capture_scene`'s fields), the expansions it exists
in, and the technique ids it demonstrates. Coordinates are server coordinates as
`capture_scene` takes them. Only the Stormwind gate camera (`-9462,-67,57`) is confirmed from
the existing howto; **every other position below is from memory of the world and is to be
verified with `pywowlib/tools/terrain_height.py` / `wmo_height.py` before it is committed** —
that check is part of the session that writes the file.

Chosen so that each technique has at least one scene that exists in **all four supported
expansions** (Vanilla-era Azeroth and Kalimdor), plus TBC/WotLK scenes where those show
something the old world cannot.

| Scene | Where, and why it shows the change | Time / weather | Techniques |
|---|---|---|---|
| `stormwind-gate` | The confirmed anchor. Long stone walls, statues, a road to the horizon | 09:00 clear; 17:30 | L1, L2, G1, F2, M2/M3 (stone close-up variant), S3 (moat) |
| `goldshire-inn-morning` | Lion's Pride Inn, human character in plate standing by the door, trees behind. Feet-on-ground contact, near/far shadow split, cloth vs metal | 08:00 low sun | **L1, L2, L3, L7, F4** (character runs across, `--sequence`), M2/M3 |
| `goldshire-inn-interior-night` | Inside the inn, fireplace and candles, character by the hearth | 22:00 | **L6, L4, S1, A2** (smoky room), F2/S2 (fire bloom) |
| `elwynn-road-sunrise` | Forest road between Goldshire and Stormwind, camera facing the sun through the canopy | 06:30 | **S4, A2, A1**, L5 (sky ambient on the road) |
| `northshire-abbey` | Abbey courtyard, deep alcoves, stone and wood | 12:00 | **S1**, L7, M3 |
| `westfall-sentinel-hill` | Sentinel Hill looking west over the plains to the sea. Kilometres of open terrain, windmills, the far coast | 15:00 | **G1** (with `--wireframe` overlay for the LOD rings), A1, L5, G5 |
| `duskwood-road` | Darkshire road into the valley, lamps along it | 05:30 mist; 21:00 rain | **A1** (height fog in the valley), L4/L6 (lamps), A2, weather |
| `lakeshire-lake` | Redridge, Lakeshire docks looking across Lake Everstill to the mountains, a rowboat and pier in frame | 16:00 | **S3** (replaces planar), S2, F2, L5 (sky reflection) |
| `crossroads-plains` | The Barrens, Crossroads looking south-east, kodos on the road, Thunder Bluff mesa on the horizon | 13:00 heat | **G1**, A1 (aerial perspective over distance), G5 |
| `orgrimmar-valley-of-strength-night` | Valley of Strength, every brazier and torch lit, Grommash Hold behind | 22:30 | **L4, L6, F2, S2**, A2 (smoke), S1 (canyon walls) |
| `orgrimmar-drag` | The Drag's narrow canyon, midday, overhangs and rope bridges | 12:00 | **S1**, L1 (deep cascade split), L7 |
| `thunder-bluff-dawn` | Foot of the mesa lift looking up; then the top looking out over Mulgore | 06:00 | **A1, L5** (dawn ambient on cliffs), G1, A2 |
| `ironforge-great-forge` | The Great Forge, lava trench, anvils, dwarves | any (interior) | **F2/S2** (lava), **S1**, L4, L7 (metal), L6 |
| `kharanos-snow` | Kharanos in falling snow, character in fur, footprints | 10:00 snow 0.8 | Weather, L7 (snow specular), S1, G4 (snow displacement), M2 |
| `searing-gorge-night` | Cauldron rim at night, magma below, Blackrock Mountain behind | 23:00 | **F2, S2** (magma bloom), L4 (magma as light), A2 |
| `stranglethorn-canopy` | Grom'gol road under the jungle canopy toward Booty Bay, sun low behind the leaves | 07:00 | **A2, S4** (god rays), F4 (foliage stability, `--sequence`), L1 (leaf shadows) |
| `booty-bay-harbour` | Booty Bay from the dock, the whole town over the water | 17:00 | **S3**, L4/L6 (lanterns at 20:00 variant), F2 |
| `deadmines-foundry` | Deadmines, Foundry hall with the fires and the water channel (map 36) | interior | **S1, L4, L6, S3** (wet floor), dungeon perf |
| `ashenvale-astranaar` | Astranaar bridge and lanterns under the purple canopy | 19:00 | L4/L6, A2, S4, L5 |
| `tanaris-dunes` | Gadgetzan out onto the dunes, heat haze, a caravan | 14:00 | **G1, G4** (dune tessellation), A1, L5 |
| `character-portrait` | The character-preview framing (`character_preview.cpp`): one character, each race, plate and cloth | studio | **L7, L2, M2/M3**, DLAA/DLSS at 4K, RT (R1/R2) |
| `stormwind-harbour` *(WotLK only)* | Harbour from the Cathedral steps, ships, open water to the horizon | 18:00 | **S3**, F2, G1 |
| `zangarmarsh-glow` *(TBC+)* | Zangarmarsh mushrooms and spore lights at night | 23:00 | **L4** (hundreds of small emitters), S2, A2 |
| `grizzly-hills-vista` *(WotLK)* | Grizzly Hills ridge over the lake and pines in haze | 16:00 | **A1, A2**, G1, S3 |
| `howling-fjord-cliffs` *(WotLK)* | Valgarde from the water looking up the fjord | 08:00 | S3, L1 (cliff cascades), A1 |

Every scene is also captured with **all techniques off** once per phase as the regression
golden, and the three from `docs/perf_baseline.md` (Stormwind, Barrens, a dungeon) double as
the frame-time scenes for §7.2 item 5.

Temporal techniques (F4, S1, S3, A2, upscalers, RT denoising) use `--sequence` on
`goldshire-inn-morning` (running character) and `stranglethorn-canopy` (wind in foliage):
a still hides ghosting, and ghosting is the failure those techniques have.

#### Plan

1. Extract `capture_scene`'s scene construction into `src/tools/scene_setup.{hpp,cpp}`
   (or the existing `include/tools/` if that is what it is for); both executables link it.
2. `--compare` in `main.cpp`: parse, build the scene, run before/after with the settings
   API (`SettingsPanel::setSettingValue` by key, which exists), capture through the same
   readback the screenshot key uses, write the files, exit with the assertion code.
3. `tools/compare_scenes.json` + the coordinate verification pass.
4. `tools/compare_report.py`; a CI job running the Lavapipe subset for every PR that touches
   `src/rendering/` or `assets/shaders/`.
5. Each phase adds its technique ids and any new scene the phase needs, and lands its report
   page under `docs/evidence/phase-N/` (the directory already exists for this kind of thing).

**Effort.** 1 session (phase 02), then a fraction of a session per phase. **Risk.** Low; it is
`capture_scene` with a second render and a subtraction.

---

## 8. Explicitly not recommended

| Proposal | Why not |
|---|---|
| Deferred / G-buffer rendering | WoW's material zoo is blend-mode heavy (M2 has 7 blend modes, `m2_blend_mode.hpp`); a G-buffer serves the opaque 60 % and forces a second forward path for the rest. Clustered forward (L4) gives deferred's light count without splitting the renderer. |
| Virtual geometry (Nanite-style) | Assets are 200–5000 triangles. There is nothing to virtualise. |
| Virtual texturing | Terrain is 4 layers of tiling 256² textures; the bindless array (G2) holds the whole set. Only revisit if M2 packs push past VRAM even with BC7. |
| Replacing the skybox with a procedural sky | `docs/SKY_SYSTEM.md` documents why: the M2 skybox is authoritative and zone-authored. A3 drives lighting only. |
| Global illumination (DDGI / SSGI / voxel) | Real GI on hand-painted art with baked shadow in the diffuse double-lights everything. L5 probes plus S1 AO is the honest version. Reconsider only if M3 packs strip baked lighting from the diffuse, which is a content project. |
| Compute-based software rasteriser | Same reason as virtual geometry. |
| Requiring Vulkan 1.3 | MoltenVK reports 1.2, the Kepler/GCN1 floor is 1.2, and the sync2 wrapper already gives the useful part of 1.3 without it. |
| Committing any vendor binary (NGX, DLSS-G, XeSS DLLs) to the repository | Redistribution terms differ from the Streamline source licence; runtime-load from a player-supplied path, as the FSR3 runtime already does. |
| DLSS 5 as a default or preset value | Re-lights hand-painted art toward photorealism; RTX 50 only; no Vulkan statement. An opt-in look, never a quality tier. |
| Building a native Metal path to reach MetalFX | One effect does not justify a second presentation backend; FSR 3.1 and TAA serve macOS. |
