# Modern rendering — session plans

One file per phase. **One phase = one commit.** Phases 02–25 are one focused working day
each for one pair of hands (or one agent run). Phase 01 is the exception on purpose: five
techniques that edit the same shaders, UBO block and settings table, done as one pass so
those files are refactored once — two to three days, with a cut order inside the file.

**Shipped through: phase 01.** (Phase files and `tools/reserved_code_check.py` read this line;
bump it in the same commit that ships a phase.)

Phase 01 shipped its consolidation (steps 1–5), F5 capability gating, A1 height
fog, L1 cascaded shadows with the off switch, L2 Poisson and PCSS filters, G1
terrain LOD, and compare v0 — `tools/capture_scene` had to be written rather
than found, because §7.5 of the plan says it already exists and it did not. Two
things did not land: S4 sun shafts, and M3a normal maps everywhere beyond the
tangent routine, which is extracted and tested in
`include/rendering/tangent_frame.hpp`. Neither has a phase of its own yet; the
normal-map cache belongs beside phase 07's generated-asset manifest and the
shafts beside another screen-space pass. See
[`../evidence/phase-01/README.md`](../evidence/phase-01/README.md).

Context, ranking and rules live in [`../plan-modern-rendering.md`](../plan-modern-rendering.md).
This directory is the execution order. Every phase ends as a production-grade client per
§7.2 there; the phase file's **Verify** list is that section made concrete.

## Order

Sorted by impact ÷ effort with dependencies respected (plan §7.1). Ratio in brackets.

| # | File | Ships | Depends on |
|---|---|---|---|
| 01 | [01-shadows-fog-distance-surfaces.md](01-shadows-fog-distance-surfaces.md) | **Shipped:** shader consolidation (includes, spec constants, one UBO append); F5 capability gating; A1 height fog + aerial perspective; L1 cascaded shadows and the off switch; L2 Poisson and PCSS; G1 terrain LOD with skirts; compare v0 (`tools/capture_scene`, `tools/compare_scenes.py`). **Not shipped:** S4 sun shafts, M3a normal maps beyond the tangent routine — neither is scheduled yet | — |
| 02 | [02-comparison-mode.md](02-comparison-mode.md) | §7.5 `--compare` on the game exe, scene catalogue, report, CI | 01 |
| 03 | [03-depth-prepass-reverse-z.md](03-depth-prepass-reverse-z.md) | F3 pre-pass, reverse-Z, normal target; velocity target reserved | 02 |
| 04 | [04-hdr-pipeline.md](04-hdr-pipeline.md) | F2 HDR scene target, tone map, exposure, Light.dbc recalibration | 03 |
| 05 | [05-bloom-and-lut.md](05-bloom-and-lut.md) | S2 bloom; P5 neutral LUT | 04 |
| 06 | [06-gtao.md](06-gtao.md) | S1 ground-truth ambient occlusion | 03, 04 |
| 07 | [07-texture-cache-and-manifest.md](07-texture-cache-and-manifest.md) | M1 background BC7/ASTC cache, `Data/generated` manifest, `requires` gating, pre-warm CLI; texture-quality mip cap; structured load diagnostics; DDS ingestion | 01 |
| 08 | [08-motion-vectors.md](08-motion-vectors.md) | Per-object and skinned motion vectors; ends FSR2 character ghosting; instance bounds and exact editor picking | 03 |
| 09 | [09-unit-outlines.md](09-unit-outlines.md) | Reaction-coloured target and mouseover outlines from the pre-pass instance-ID target | 08, 03 |
| 10 | [10-taa.md](10-taa.md) | F4 native TAA; P3 dynamic resolution and supersampling | 08 |
| 11 | [11-sky-probes.md](11-sky-probes.md) | L5 SH ambient + specular probe from the skybox | 04 |
| 12 | [12-clustered-lighting.md](12-clustered-lighting.md) | L4 clustered forward lights from M2 + WMO light data | 04 |
| 13 | [13-pbr.md](13-pbr.md) | L7 GGX shading; M3b class roughness/AO sidecars | 11, 07 |
| 14 | [14-local-and-contact-shadows.md](14-local-and-contact-shadows.md) | L6 local light shadows; L3 contact shadows | 12, 03 |
| 15 | [15-volumetric-fog.md](15-volumetric-fog.md) | A2 froxel fog and god rays | 12, 01 |
| 16 | [16-ssr.md](16-ssr.md) | S3 screen-space reflections; planar water off by default | 03, 11 |
| 17 | [17-fsr31-and-upscaler-interfaces.md](17-fsr31-and-upscaler-interfaces.md) | P1 FSR 3.1 runtime; `Upscaler`/`FrameGen`/`LatencyMode` interfaces | 10 |
| 18 | [18-terrain-tessellation.md](18-terrain-tessellation.md) | G4 tessellation with splat-derived height; height-based layer blending | 01, 07 |
| 19 | [19-bindless-indirect-m2.md](19-bindless-indirect-m2.md) | G2 bindless materials + indirect draws for M2 (terrain/WMO later) | 01 |
| 20 | [20-vrs.md](20-vrs.md) | G5 variable rate shading | 10 |
| 21 | [21-vendor-nvidia.md](21-vendor-nvidia.md) | Licence review; Streamline DLSS SR/DLAA + Reflex | 17 |
| 22 | [22-vendor-xess-antilag-sgsr.md](22-vendor-xess-antilag-sgsr.md) | XeSS 2.1; `VK_AMD_anti_lag`; SGSR 2 / Arm ASR on Android | 21 |
| 23 | [23-rt-infrastructure-and-shadows.md](23-rt-infrastructure-and-shadows.md) | BLAS/TLAS; R1 ray-traced sun shadows | 01, 08 |
| 24 | [24-rt-ao-and-reflections.md](24-rt-ao-and-reflections.md) | R2 RT AO; R3 RT reflections | 23, 06, 16 |
| 25 | [25-ai-pack.md](25-ai-pack.md) | M2 hash-keyed AI pack reader + once-built pack builder (last of the generated) | 07 |

Not phases: DLSS-G/MFG and DLSS Ray Reconstruction (after 21 and 24 when there is a reason),
G3 mesh shaders, R4 RT local shadows, S5 cinematic post, A3 physical sky, DLSS 5, FSR 4,
NTC, MetalFX, and every authored asset (M4 HD models, HDR skies, art LUTs). See plan §7.4
"Deferred" and §8.

## Every phase file has the same shape

- **Ships / Not in this session** — the scope and where each cut went.
- **Steps** — in execution order, each naming the files it touches and how it is checked
  before the next step starts.
- **Settings** — every new schema row with its preset column, `enabledWhen`, `unavailable`.
- **Reserved** — code written now for a later phase, with its `RESERVED(phase-NN, id)` marker.
- **Verify** — the §7.2 exit list made concrete: which compare scenes, which tests, which
  numbers to record.
- **Commit** — the single commit's subject and body, in this repository's style (a sentence
  in the imperative, no prefix, body says what and why).

## Conventions that apply to every phase

- Shaders: edit `assets/shaders/*.glsl`, commit the rebuilt `.spv` beside it (the glob in
  `CMakeLists.txt` picks new files up after a re-run of CMake).
- New per-frame UBO fields are **appended** to `GPUPerFrameData` (`include/rendering/vk_frame_data.hpp`)
  and to the matching `PerFrame` block in every shader that declares it.
- New settings are **appended** to the schema table in `src/ui/settings_schema.cpp` and to
  `kGraphicsPresets` in `include/ui/graphics_presets.hpp` as a new trailing column. Enum
  choices are appended, never inserted.
- No new required Vulkan extension; `enable_extension_if_present` only.
- Barriers through `cmdPipelineBarrier2` (`include/rendering/vk_utils.hpp`).
- Off means today's frame: compare-mode "before" must match the previous phase's golden.
- `./test.sh` (unit + lint) green before the commit.
