# Modern rendering — session plans

One file per phase. **One phase = one commit.** Phases 02–24 are one focused working day
each for one pair of hands (or one agent run). Phase 01 is the exception on purpose: five
techniques that edit the same shaders, UBO block and settings table, done as one pass so
those files are refactored once — two to three days, with a cut order inside the file.

**Shipped through: none.** (Phase files and `tools/reserved_code_check.py` read this line;
bump it in the same commit that ships a phase.)

Context, ranking and rules live in [`../plan-modern-rendering.md`](../plan-modern-rendering.md).
This directory is the execution order. Every phase ends as a production-grade client per
§7.2 there; the phase file's **Verify** list is that section made concrete.

## Order

Sorted by impact ÷ effort with dependencies respected (plan §7.1). Ratio in brackets.

| # | File | Ships | Depends on |
|---|---|---|---|
| 01 | [01-shadows-fog-distance-surfaces.md](01-shadows-fog-distance-surfaces.md) | Shader consolidation (includes, spec constants, one UBO append); F5 capability gating; L1 cascaded shadows (off works again); L2 soft shadows; A1 height fog + aerial perspective; S4 sun shafts; G1 terrain LOD; M3a normal maps and tangents for M2 doodads and terrain; compare v0. **The one multi-day phase** | — |
| 02 | [02-comparison-mode.md](02-comparison-mode.md) | §7.5 `--compare` on the game exe, scene catalogue, report, CI | 01 |
| 03 | [03-depth-prepass-reverse-z.md](03-depth-prepass-reverse-z.md) | F3 pre-pass, reverse-Z, normal target; velocity target reserved | 02 |
| 04 | [04-hdr-pipeline.md](04-hdr-pipeline.md) | F2 HDR scene target, tone map, exposure, Light.dbc recalibration | 03 |
| 05 | [05-bloom-and-lut.md](05-bloom-and-lut.md) | S2 bloom; P5 neutral LUT | 04 |
| 06 | [06-gtao.md](06-gtao.md) | S1 ground-truth ambient occlusion | 03, 04 |
| 07 | [07-texture-cache-and-manifest.md](07-texture-cache-and-manifest.md) | M1 background BC7/ASTC cache, `Data/generated` manifest, `requires` gating, pre-warm CLI | 01 |
| 08 | [08-motion-vectors.md](08-motion-vectors.md) | Per-object and skinned motion vectors; ends FSR2 character ghosting | 03 |
| 09 | [09-taa.md](09-taa.md) | F4 native TAA; P3 dynamic resolution | 08 |
| 10 | [10-sky-probes.md](10-sky-probes.md) | L5 SH ambient + specular probe from the skybox | 04 |
| 11 | [11-clustered-lighting.md](11-clustered-lighting.md) | L4 clustered forward lights from M2 + WMO light data | 04 |
| 12 | [12-pbr.md](12-pbr.md) | L7 GGX shading; M3b class roughness/AO sidecars | 10, 07 |
| 13 | [13-local-and-contact-shadows.md](13-local-and-contact-shadows.md) | L6 local light shadows; L3 contact shadows | 11, 03 |
| 14 | [14-volumetric-fog.md](14-volumetric-fog.md) | A2 froxel fog and god rays | 11, 01 |
| 15 | [15-ssr.md](15-ssr.md) | S3 screen-space reflections; planar water off by default | 03, 10 |
| 16 | [16-fsr31-and-upscaler-interfaces.md](16-fsr31-and-upscaler-interfaces.md) | P1 FSR 3.1 runtime; `Upscaler`/`FrameGen`/`LatencyMode` interfaces | 09 |
| 17 | [17-terrain-tessellation.md](17-terrain-tessellation.md) | G4 tessellation with splat-derived height | 01, 07 |
| 18 | [18-bindless-indirect-m2.md](18-bindless-indirect-m2.md) | G2 bindless materials + indirect draws for M2 (terrain/WMO later) | 01 |
| 19 | [19-vrs.md](19-vrs.md) | G5 variable rate shading | 09 |
| 20 | [20-vendor-nvidia.md](20-vendor-nvidia.md) | Licence review; Streamline DLSS SR/DLAA + Reflex | 16 |
| 21 | [21-vendor-xess-antilag-sgsr.md](21-vendor-xess-antilag-sgsr.md) | XeSS 2.1; `VK_AMD_anti_lag`; SGSR 2 / Arm ASR on Android | 20 |
| 22 | [22-rt-infrastructure-and-shadows.md](22-rt-infrastructure-and-shadows.md) | BLAS/TLAS; R1 ray-traced sun shadows | 01, 08 |
| 23 | [23-rt-ao-and-reflections.md](23-rt-ao-and-reflections.md) | R2 RT AO; R3 RT reflections | 22, 06, 15 |
| 24 | [24-ai-pack.md](24-ai-pack.md) | M2 hash-keyed AI pack reader + once-built pack builder (last of the generated) | 07 |

Not phases: DLSS-G/MFG and DLSS Ray Reconstruction (after 20 and 23 when there is a reason),
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
