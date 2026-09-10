# Phase 06 — Ground-truth ambient occlusion

**One session, one commit.** Depends on: 03 (depth + normals), 04 (linear ambient to
multiply into). Player sees: corners, eaves, alcoves, the fold of a cape — the single
largest visible change on flat-ambient hand-painted art.

## Ships

- **S1** — XeGTAO-style horizon-based AO: half-res, 2 slices × 6 steps (quality setting
  scales both), 4×4 spatial denoise, temporal accumulation against the velocity target
  (camera-only until 12; good enough for static scenes), bilateral upsample. Multiplied into
  the ambient term (and the SH ambient once 14 lands) — never into direct light.
- Bent normal output kept in the AO target's `.gba` for phase 10.

## Steps

1. Port the reference (MIT) to GLSL compute: `gtao_main.comp.glsl`, `gtao_denoise.comp.glsl`,
  `gtao_upsample.comp.glsl`. Inputs: pre-pass depth (reverse-Z, HiZ pyramid for the far
  samples), octahedral normal target. Output `R8` AO + `RGB8` bent normal.
2. Render-graph node after `prepass`, before `main`; `main`'s lit shaders sample the AO
  texture with `gl_FragCoord` — a new set-0 binding appended (`shadow_params.hpp` pattern
  for the white fallback when off).
3. When `depthprepass=0`: run at quarter res on the reconstructed normals from phase 03's
  fallback pass; the setting's tooltip says so.
4. Settings, presets, tuning on `northshire-abbey` and `orgrimmar-drag` (radius 1.5 yd,
  falloff, power 1.5).

## Settings

| key | kind | choices | default | L / M / H / U | enabledWhen |
|---|---|---|---|---|---|
| `ambientocclusion` | Enum | `Off|Low|Medium|High` | 2 | 0/1/2/3 | `hdr` |
| `aostrength` | Float | 0–1.5 | 1.0 | — | `ambientocclusion!=0` |

## Reserved

```glsl
// RESERVED(phase-10, L5-sky-probes): bent normals in the AO target's gba channels; the SH
// ambient will sample along them instead of the geometric normal.
```

## Verify

- Compare mode: `northshire-abbey`, `orgrimmar-drag`, `ironforge-great-forge`,
  `deadmines-foundry`, `goldshire-inn-interior-night`, `character-portrait`.
  Before == phase-05 golden.
- `--sequence` on `goldshire-inn-morning` (camera orbit): no visible AO trailing on the
  static scene; some on the running character is expected until phase 08 and is noted.
- Frame time at each quality on T0 and T2, 1080p: Medium ≤ 0.8 ms on T2; Low on T0 ≤ 1.2 ms.

## Commit

```
Shade the corners the flat ambient never reached

Horizon-based ambient occlusion at half resolution from the pre-pass
depth and normals, denoised and accumulated over frames, multiplied
into ambient light only. Four quality steps; Off is the phase-05
frame. Bent normals are kept for the sky probes to come.
```
