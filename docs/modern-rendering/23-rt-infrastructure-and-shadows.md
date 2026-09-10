# Phase 23 — Ray-tracing infrastructure and ray-traced sun shadows

**One session, one commit.** Depends on: 01 (fallback), 08 (motion for denoise).
Player sees, on RT hardware: pixel-exact sun shadows from every leaf and railing within
100 yards, cascades beyond. Everyone else: the phase-22 frame.

## Ships

- BLAS per M2 model / WMO group built at load (`VK_KHR_acceleration_structure`), skinned
  characters refit per frame from a compute skinning pass writing world-space vertices,
  TLAS rebuilt per frame from the visible instance list the cull already produces.
- **R1** — `rt_shadow.comp.glsl` with `VK_KHR_ray_query`: one ray per pixel toward the
  sun (cone-jittered by `shadowlightsize`), half-res, temporal accumulation on the velocity
  target, bilateral upsample; blended with cascade 2+ beyond the RT range in
  `shadow_common.glsl` (a third `SHADOW_FILTER` value).
- Alpha-tested geometry through any-hit alpha lookup (foliage matters here).

## Steps

1. Enable both extensions + `bufferDeviceAddress` if present; `RenderCaps.rayQuery`.
2. `AccelerationStructures` in `rendering/rt/`: BLAS build on the transfer queue at model
   load, compaction; TLAS per frame on the graphics queue before the shadow node.
3. Skinned refit: `skin_to_world.comp.glsl` over the character SSBOs; BLAS update mode.
4. Shadow ray pass + denoise; `shadow_common.glsl` variant.
5. Settings; `unavailable` on everything without `rayQuery` (all MoltenVK, all T0/T1).

## Settings

| key | kind | choices | default | L / M / H / U | requires |
|---|---|---|---|---|---|
| `rtshadows` | Enum | `Off|On` | 0 | 0/0/0/1 | cap `rayQuery`; `enabledWhen=shadows` |
| `rtrange` | Float | 40–200 yd | 100 | — | `rtshadows` |

## Reserved

```cpp
// RESERVED(phase-24, R2-R3): the TLAS and the skinned-refit pass are shared by RT AO and
// reflections; the ray-query include takes a `RAY_KIND` constant that only has SHADOW.
```

## Verify

- Compare mode on RT hardware: `goldshire-inn-morning`, `stranglethorn-canopy`
  (`--sequence`), `stormwind-gate`, `orgrimmar-drag`; before (Off) == phase-22 golden.
- Frame time on the T2-RT machine at 1440p: ≤ 2.5 ms for the pass; BLAS memory recorded.
- Non-RT machines: bit-identical to phase 22, row greyed.

## Commit

```
Trace the sun's shadow where the hardware can

Acceleration structures for every model and building, refit for
skinned characters each frame, and a ray-query shadow pass blended
into the cascades past its range. Off everywhere without ray query;
Ultra on RT hardware turns it on.
```
