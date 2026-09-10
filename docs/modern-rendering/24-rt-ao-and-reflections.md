# Phase 24 — Ray-traced ambient occlusion and reflections

**One session, one commit.** Depends on: 23, 06 (AO fallback), 16 (SSR fallback). Player
sees, on RT hardware: occlusion that does not vanish at the screen edge and reflections of
things behind the camera. Everyone else: the phase-23 frame.

## Ships

- **R2** — 1–2 short rays per pixel (radius from `aostrength`) replacing GTAO's horizon
  search; same denoise and bent-normal output, so phases 14–16 need no change.
- **R3** — glossy reflection rays for roughness < 0.4 at half res, hit shading through a
  simplified material lookup (albedo × probe lighting, no recursion), feeding the phase-16
  resolve pass's reserved RT slot; SSR remains for rougher surfaces and as the fallback.

## Not in this session

- DLSS Ray Reconstruction (replaces both denoisers on NVIDIA): after this ships, one
  session, if phase 21's licence outcome allows.
- R4 RT local shadows: deferred; the atlas from 17 is adequate.

## Steps

1. `RAY_KIND` AO and REFLECT variants of the ray-query include; shared TLAS.
2. AO pass swaps in for `gtao_main` when `ambientocclusion` is `Ray traced` (append to
   the enum); denoise unchanged.
3. Reflection pass writes the RT hit image; `ssr_resolve` blends by confidence.
4. Settings, presets (Ultra on RT hardware).

## Settings

| key | choices appended | requires |
|---|---|---|
| `ambientocclusion` | `Ray traced` | cap `rayQuery` |
| `waterreflection` | `Ray traced` | cap `rayQuery` |

## Reserved

None. Consumes phase-16 and 26 reservations.

## Verify

- Compare mode on RT hardware: `northshire-abbey`, `ironforge-great-forge` (AO);
  `lakeshire-lake`, `booty-bay-harbour`, `stormwind-harbour` (reflections of off-screen
  geometry — the scene camera is chosen so the town is behind it). Before == phase-23.
- Frame time at 1440p on the T2-RT machine: AO ≤ 1.5 ms, reflections ≤ 3 ms; recorded.

## Commit

```
Trace occlusion and reflections past the edge of the screen

Ray-traced AO and glossy reflections on the acceleration structures
from the shadow pass, dropping into the existing denoise and resolve
so nothing downstream changes. Screen-space stays the fallback and
the default; both rows are greyed without ray query.
```
