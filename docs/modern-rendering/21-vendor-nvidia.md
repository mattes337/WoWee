# Phase 21 — NVIDIA: licence review, Streamline, DLSS Super Resolution, Reflex

**One session, one commit.** Depends on: 17. Player sees: on an RTX card with the NVIDIA
runtime present, a `DLSS` upscaler choice (and `DLAA` at native) and lower input latency.
Nothing NVIDIA-owned enters the repository.

## Ships

- **Licence review, first hour, written down** in `docs/vendor-licences.md`: Streamline
  source terms vs the DLSS/NGX runtime and DLSS-G redistribution terms. The same file
  records every third-party renderer or SDK this plan studied for a design, with its
  licence and the rule that applies to it: designs and behaviour may be adopted, source
  is never imported. Outcome A: runtime
  may be redistributed → still not committed, fetched by the asset GUI on request. Outcome
  B: may not → the player points `WOWEE_STREAMLINE_LIB` at their own copy (the FSR3
  `WOWEE_FFX_SDK_RUNTIME_LIB` precedent). Either way the code below is the same.
- `UpscalerStreamline` implementing the phase-17 `Upscaler` (DLSS SR, DLAA mode) and
  `LatencyModeReflex` (`VK_NV_low_latency2` through Streamline; the `markInput` hook from
  phase 17 finally does something), loaded from a path, off when absent.
- Panel line: backend name + DLSS DLL version.

## Not in this session

- DLSS-G / MFG frame generation: after this ships and a licence outcome is known.
  DLSS Ray Reconstruction: after 24.

## Steps

1. Read and record the licences; decide the distribution outcome with the maintainer —
   this session does not start code until that paragraph exists.
2. `sl.interposer` dlopen wrapper (`rendering/streamline_loader.cpp`), device/queue
   hooks required by Streamline for Vulkan (`vk_streamline` sample is the reference).
3. `UpscalerStreamline::evaluate(color, depth, velocity, exposure, jitter)`.
4. `LatencyModeReflex`: sleep-mode set, `latencySleep` before input sampling, markers.
5. Append `DLSS` to the `upscaler` enum; `unavailable` reasons: no NVIDIA GPU, no runtime
   found, runtime too old.

## Settings

| key | kind | choices | note |
|---|---|---|---|
| `upscaler` | Enum | append `NVIDIA DLSS` | `unavailable` filled at start |
| `dlssmode` | Enum | `Quality|Balanced|Performance|DLAA` | `enabledWhen=upscaler=3` |
| `latencymode` | Enum | `Off|Reflex` | `unavailable` when no NVIDIA runtime |

## Reserved

```cpp
// RESERVED(phase-22, V-xess-antilag): LatencyMode's "vendor" slot table has a row for
// VK_AMD_anti_lag; the factory returns nullptr for it.
```

## Verify

- With the runtime: compare mode `--sequence` on `goldshire-inn-morning`,
  `stranglethorn-canopy`, `character-portrait` at 4K: DLSS Quality vs TAA vs FSR 3.1,
  SSIM vs native recorded. Without the runtime: bit-identical to phase 20 and the row is
  greyed.
- Reflex: measured input-to-photon with the latency-marker overlay, before/after.
- `git ls-files` contains no `nvngx*`, `sl.*` binaries — add a CI check.

## Commit

```
Load DLSS and Reflex from the player's NVIDIA runtime when it is there

A Streamline-backed Upscaler and LatencyMode, opened from a path at
start-up and absent otherwise, with the licence outcome recorded in
docs/vendor-licences.md. No vendor binary is committed and a check
keeps it that way.
```
