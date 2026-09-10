# Phase 20 — Variable rate shading

**One session, one commit.** Depends on: 10 (TAA hides the rate change). Player sees:
nothing, at 20–40 % less fragment cost at high resolutions on T2 hardware.

## Ships

- **G5** — `VK_KHR_fragment_shading_rate` attachment: a rate image built each frame from
  the previous frame's tone-mapped luminance (Sobel edge magnitude → 1×1, flat → 2×2, in
  fog / far / sky → 2×2 or 4×4), applied to the main colour pass only (never the pre-pass,
  UI or post).
- Greyed with a reason where the extension is absent (all T0/T1, MoltenVK).

## Steps

1. Enable the extension if present (`vk_context.cpp` pattern), query tile size.
2. `vrs_build.comp.glsl` from the previous frame's post output + depth; rate image sized
   by the tile size.
3. Render pass: `VkFragmentShadingRateAttachmentInfoKHR` on the main pass (still classic
   render passes here — dynamic rendering is not a prerequisite).
4. Settings; the perf HUD shows the rate histogram.

## Settings

| key | kind | choices | default | L / M / H / U | requires |
|---|---|---|---|---|---|
| `shadingrate` | Enum | `Off|Conservative|Aggressive` | 0 | 0/0/1/1 | cap `fragmentShadingRate`; `enabledWhen=antialiasing=4` |

## Reserved

None.

## Verify

- Compare mode `westfall-sentinel-hill`, `crossroads-plains`, `stormwind-gate` at 4K on
  T2: SSIM ≥ 0.98 vs Off at Conservative; frame time −15 % or better recorded.
- Off == phase-19 frame bit-exact.

## Commit

```
Shade flat pixels less often

A shading-rate image from the last frame's edges lets the main pass
run at 2x2 where nothing is happening and 1x1 where it is, under TAA
so nobody can tell. Greyed where the extension is missing.
```
