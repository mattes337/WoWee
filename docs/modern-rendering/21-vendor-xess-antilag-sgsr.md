# Phase 21 — XeSS 2.1, AMD Anti-Lag 2, SGSR / Arm ASR on Android

**One session, one commit.** Depends on: 20. Player sees: an ML upscaler on Intel Arc and
on any DP4a-capable GPU (Pascal, RDNA1+), Anti-Lag on Radeon, and a mobile-tuned upscaler
on Android.

## Ships

- `UpscalerXeSS` — XeSS 2.1 SR through its Vulkan API, runtime-loaded like phase 20
  (Intel's redistribution terms are permissive; still not committed; recorded in
  `docs/vendor-licences.md`).
- `LatencyModeAntiLag` — `VK_AMD_anti_lag` (`vkAntiLagUpdateAMD` around input and present),
  enabled if present.
- `UpscalerSGSR` — Snapdragon Game Super Resolution 2 shader (open source, in-tree) with the
  Arm ASR variant selected by the Vulkan vendor id at start; Android default.

## Steps

1. XeSS loader + adapter over the phase-16 interface.
2. Anti-Lag adapter; the phase-16 hooks already exist.
3. SGSR2 / ASR shaders under `assets/shaders/`, one adapter, quality enum shared with FSR.
4. Enum appends, `unavailable` reasons, panel version lines, CI binary check extended.

## Settings

| key | choices appended | note |
|---|---|---|
| `upscaler` | `Intel XeSS`, `Snapdragon GSR` | |
| `latencymode` | `Anti-Lag` | `unavailable` without the extension |

## Reserved

None.

## Verify

- Compare `--sequence` set from phase 20 on an Arc, a Pascal/RDNA card (DP4a path) and the
  Android device; SSIM recorded per backend.
- Absent runtimes: bit-identical to phase 20.

## Commit

```
Add XeSS, Anti-Lag and a mobile upscaler behind the same interfaces

XeSS 2.1 loaded from the player's runtime, VK_AMD_anti_lag where the
driver offers it, and Snapdragon GSR / Arm ASR in-tree for Android,
each one factory entry in the Upscaler and LatencyMode interfaces.
```
