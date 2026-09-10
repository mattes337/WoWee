# Phase 15 — Froxel volumetric fog and god rays

**One session, one commit.** Depends on: 12 (clusters), 01 (cascades, fog params).
Player sees: shafts through the Stranglethorn canopy that move with the trees, torch glow
that hangs in the air, mist in Duskwood the lamps light from inside.

## Ships

- **A2** — 160×90×64 froxel volume (quality scales depth count), exponential depth slicing
  to the fog distance; `froxel_inject.comp.glsl` (density from phase-01 height fog, sun
  in-scatter shadowed by the cascades, cluster lights in-scatter), `froxel_integrate.comp.glsl`
  (front-to-back), temporal reprojection with the jitter (3-frame blend). Applied in
  `fog.glsl` by a 3D lookup replacing the analytic aerial term when on; `shaderFloat16`
  halves the cost when present (T2 bit from phase 01).
- Phase-01's screen-space shafts become the fallback the setting greys when volumetrics
  are on (`enabledWhen=volumetricfog=0`).

## Steps

1. Volume images (two, ping-pong), three compute nodes in the graph after `prepass` and the
   shadow pass, before `main`.
2. Density: `fogHeight` from 03 plus a per-zone noise term (3D value noise, `fogParams.z`
   time) at 10 % amplitude.
3. `fog.glsl`: `if (FOG_MODEL == 2) { vec4 f = texture(uFroxel, froxelUV(pos)); color =
   color * f.a + f.rgb; }`.
4. Settings, presets; Ultra on, High on at low quality.

## Settings

| key | kind | choices | default | L / M / H / U | enabledWhen |
|---|---|---|---|---|---|
| `volumetricfog` | Enum | `Off|Low|High` | 0 | 0/0/1/2 | `fogmodel=1` and `lightingmode=1` |

## Reserved

None. Consumes phase-01 and 15 reservations.

## Verify

- Compare mode: `stranglethorn-canopy` (`--sequence`), `elwynn-road-sunrise`,
  `duskwood-road` 21:00, `goldshire-inn-interior-night`, `ironforge-great-forge`.
  Before == phase-14 golden.
- Frame time at Low on T0 (≤ 1.5 ms at 1080p) and High on T2 (≤ 1.2 ms with fp16),
  recorded; that is why Low exists.

## Commit

```
Put light into the air

A froxel fog volume lit by the shadowed sun and the clustered lights,
integrated front to back and reprojected over frames, applied where
the height fog was. The screen-space shafts stay as the fallback for
hardware that should not pay for it.
```
