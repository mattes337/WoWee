# Phase 17 — FSR 3.1 and the upscaler / frame-gen / latency interfaces

**One session, one commit.** Depends on: 10. Player sees: the AMD upscaler gets the 3.1
quality step (less ghosting than 2.2); the settings panel gains one "Upscaler" choice that
later vendors slot into.

## Ships

- **P1** — the runtime-loaded AMD path (`docs/AMD_FSR2_INTEGRATION.md` "Path A",
  `WOWEE_FFX_SDK_RUNTIME_LIB`) moved to the FSR 3.1 upscaler API from the FidelityFX SDK
  the repo already fetches; the in-tree `fsr2_*` compute path stays as `Internal`.
- Interfaces in `include/rendering/upscaler.hpp`:
  `Upscaler { Internal FSR2, AMD FSR 3.1 }`, `FrameGen { Off, AMD FSR3 }`,
  `LatencyMode { Off }` with `beginFrame/markInput/present` hooks in `application.cpp`.
  `PostProcessPipeline` talks only to the interfaces. Inputs are the phase-10
  `JitterSource`, the velocity target, depth, exposure.
- `Upscaler` enum row replaces the FSR2/FSR3 booleans (migrated on load).

## Steps

1. Interfaces + adapters over the existing state structs (`FSR2State`, `AmdFsr3Runtime`).
2. FSR 3.1: `ffxFsr3UpscalerContextCreate` … in `amd_fsr3_runtime.cpp`, replacing the FSR2
   SDK dispatch; SDK version pinned in `CMakeLists.txt`.
3. `LatencyMode::Off` is a no-op so the hooks are exercised now.
4. Settings migration and the panel's backend name/version line.

## Settings

| key | kind | choices | default | L / M / H / U | note |
|---|---|---|---|---|---|
| `upscaler` | Enum | `Off|Internal FSR2|AMD FSR 3.1` | 0 | — | later phases append |
| `framegen` | Enum | `Off|AMD FSR 3` | 0 | — | `enabledWhen=upscaler!=0` |
| `fsrquality` | existing | | | | `enabledWhen=upscaler!=0` |

## Reserved

```cpp
// RESERVED(phase-21, V-nvidia): Upscaler/FrameGen/LatencyMode have one slot each for a
// Streamline-backed implementation; the factory returns nullptr for them.
// RESERVED(phase-22, V-xess-antilag): same, for XeSS and VK_AMD_anti_lag.
```

## Verify

- Compare mode `--sequence` `goldshire-inn-morning`, `stranglethorn-canopy` with
  `upscaler` 1 vs 2 at Quality: 3.1 ghosting ≤ 2.2's (SSIM vs native).
- Every old `settings.cfg` combination of the FSR booleans migrates to the enum without a
  visible change (test with saved fixtures).
- Runtime library absent: `unavailable` says so, panel shows "Internal", nothing crashes.

## Commit

```
Move the AMD upscaler to FSR 3.1 behind one Upscaler interface

The post pipeline talks to Upscaler, FrameGen and LatencyMode
interfaces instead of FSR-shaped state; the runtime-loaded AMD path
now speaks the 3.1 API and the internal FSR2 stays as the fallback.
Vendor backends to come are one factory entry each.
```
