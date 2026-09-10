# Phase 03 — Depth pre-pass, reverse-Z, normal target

**One session, one commit.** Depends on: 02. Player sees: no z-fighting on far cliffs at
2400 yards; a cheaper frame in cities. Under the hood: the depth, normal and (reserved)
velocity buffers every screen-space technique from here on reads.

## Ships

- **Reverse-Z** unconditionally: `D32_SFLOAT` already; projection flips, compare ops
  `GREATER`, clear 0.0, HiZ build takes max, shadow bias sign, FSR2 depth-inverted flag.
- **F3** depth pre-pass for opaque terrain/WMO/M2/characters (alpha-tested batches stay in
  the colour pass) writing depth and a `R16G16_SNORM` octahedral **normal** target; colour
  pass with `EQUAL`.
- **Velocity target** `R16G16_SFLOAT`, written by the pre-pass from camera reprojection
  (exactly what `fsr2_motion.comp.glsl` computes today) — reserved for phase 08.

## Not in this session

- Per-object motion vectors → 08. GTAO → 06. Contact shadows → 13.

## Steps

1. Reverse-Z: `camera.cpp` projection (`glm::perspectiveRH_ZO` with near/far swapped or the
   explicit matrix), every `VkPipelineDepthStencilStateCreateInfo` (grep `VK_COMPARE_OP_LESS`),
   clear values (grep `depthStencil = {1.0f`), `hiz_build.comp.glsl` min→max and
   `m2_cull_hiz.comp.glsl`'s test, shadow pass (keep it conventional — it is its own
   projection), `post_process_pipeline.cpp` FSR2 `depthInverted = true`, water refraction
   depth reads, the sky's `gl_Position.z = w` trick becomes `0`. Compare mode on all scenes:
   pixel-identical except far z-fight pixels.
2. Pre-pass pipelines: the four renderers' shadow pipelines are already depth-only with the
   same vertex inputs (`shadow.vert.glsl`, `character_shadow.vert.glsl`); derive
   `prepass.vert/frag` from them with the normal output. Alpha-tested materials
   (`alphaTest`, `colorKeyBlack` in `M2Material`) are excluded by the same classification
   the shadow pass uses.
3. Render graph (`render_graph.hpp`): new node `prepass` before `main`; outputs `depth`,
   `normals`, `velocity`; `main` declares `depth` as input, its pipelines switch to
   `EQUAL` + depth write off for the opaque set.
4. HiZ: build from the pre-pass depth, this frame, so occlusion culling in
   `m2_renderer_render.cpp:706-886` uses current depth rather than last frame's.
5. Settings: `depthprepass` (below). With it off the graph node is disabled and `main`
   writes depth as today; normals/velocity are then produced by a cheap reconstruct pass
   so consumers (10, 12, 13) always have them.

## Settings

| key | kind | default | L / M / H / U | note |
|---|---|---|---|---|
| `depthprepass` | Bool | 1 | 0/1/1/1 | Off = today's single pass; Low preset off because on 2012 GPUs the extra vertex work can cost more than it saves |

## Reserved

```cpp
// RESERVED(phase-08, F4-motion-vectors): the velocity attachment holds camera-only
// reprojection; per-instance previous transforms arrive in phase 08.
// RESERVED(phase-06, S1-gtao): the normal target has no reader yet.
```

## Verify

- Compare mode `all` with `depthprepass` off: bit-identical to the phase-02 golden except
  pixels that z-fought (count them; expect < 0.05 %). With it on: identical to off within
  1 LSB (the `EQUAL` test can differ on edges).
- Frame time Stormwind (heaviest overdraw) on T0 and T2: record on/off; the setting's
  default per preset follows the measurement.
- Validation clean; the sync2 wrapper handles the new depth read-after-write.

## Commit

```
Draw depth first, and count it backwards

A depth pre-pass with a normal target, then colour with an EQUAL test,
halves the fragment work in cities and gives the screen-space passes
to come a depth and normal buffer to read. Depth is reverse-Z now,
which ends the z-fighting a 2400-yard view distance brought. A
velocity target rides along, camera-only until phase 08 fills it.
```
