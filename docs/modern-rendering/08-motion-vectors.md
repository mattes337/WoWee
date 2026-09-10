# Phase 08 — Per-object and skinned motion vectors

**One session, one commit.** Depends on: 03. Player sees: FSR2 stops smearing running
characters and swaying trees. Every temporal technique after this (TAA, GTAO history, SSR,
volumetrics, DLSS, XeSS) inherits correct motion.

## Ships

- Previous-frame model matrix per M2 instance and per WMO/character draw; previous bone
  palette per skinned instance; every vertex shader outputs `prevClip` and the pre-pass
  writes `velocity = (clip.xy/w − prevClip.xy/w) * 0.5` minus the jitter delta.
- `fsr2_motion.comp.glsl` retired: FSR2 (own and AMD SDK) and the AMD FSR3 runtime take the
  velocity target directly. Terrain and static WMO keep the depth-reprojection formula in
  the vertex shader (same result, no extra state).
- Foliage wind (`m2.vert.glsl:104-126`) and player brush displacement are evaluated for
  both frames' times so grass and trees have velocity too.

## Steps

1. Instance data: `M2 instance SSBO` (`m2_renderer_instance.cpp`) gains `mat4 prevModel`;
   copy current→prev at the end of each update. WMO push constants (`WMOPushConstants`)
   gain `prevModel` (WMO transports move). Characters: the bone palette SSBO doubles
   (ping-pong by frame index).
2. Vertex shaders: `m2.vert`, `wmo.vert`, `character.vert`, `terrain.vert`, `grass.vert`,
   `water.vert`: compute `prevClip` with the previous view-projection from `PerFrame`
   (append `mat4 prevViewProjection`, unjittered, and `vec2 jitterDelta`).
3. Pre-pass fragment writes velocity; for alpha-tested and blended batches that skip the
   pre-pass, the colour pass writes velocity through a second attachment (they matter:
   leaves).
4. `post_process_pipeline.cpp`: FSR2 dispatch takes the velocity image; delete the motion
   compute pass and its resources.
5. Debug view `--compare velocity` renders the velocity target as colour (compare mode
   gains a `--debugview` flag).

## Settings

None. This is correctness for a technique already shipped (FSR2); no toggle.

## Reserved

Consumes `RESERVED(phase-08, …)` from 07. None new.

## Verify

- `--sequence` on `goldshire-inn-morning` (running character) and `stranglethorn-canopy`
  (wind) with FSR2 on: ghosting gone — SSIM against a native-resolution render ≥ 0.95 per
  frame where phase 07 scored ~0.85 on the character.
- Debug velocity view: static scene under a still camera is black; the character's
  silhouette carries velocity; foliage does under wind.
- Frame time: +0.05 ms (one extra attachment); recorded.

## Commit

```
Give moving things motion vectors

Every instance keeps its previous transform and skinned models their
previous bones, so the velocity target carries real motion instead of
camera reprojection alone. FSR2's motion pass is gone; it reads the
target. Running characters and windblown trees stop smearing, and the
temporal passes to come start from correct motion.
```
