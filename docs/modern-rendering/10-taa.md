# Phase 10 — Native TAA and dynamic resolution

**One session, one commit.** Depends on: 08. Player sees: foliage, fences and alpha-tested
edges stop shimmering, at native resolution, on every GPU — the AA choice that costs less
than 2× MSAA and looks better than 8× on leaves.

## Ships

- **F4** — history buffer in the scene format, reprojection by the velocity target,
  variance-clipped neighbourhood clamp (YCoCg), Halton-8 jitter (the camera jitter already
  exists for FSR2 — factor it out of `fsr2_`), 4-tap Catmull-Rom history fetch, disocclusion
  by depth + velocity length, optional RCAS sharpen (exists).
- **P3** — dynamic resolution: with TAA or FSR2 on, the internal resolution scales between
  the chosen quality and 1.0 to hold a frame-time target (`fpstarget` row); TAA's history
  handles the scale change; the FSR2 path already renders at a lower internal size.
- AA enum gains `TAA` **appended** (index 4); `enabledWhen` blocks TAA with FSR2/FSR3 on.

## Steps

1. `taa_resolve.comp.glsl` from `fsr2_accumulate.comp.glsl`'s structure at scale 1.0;
   history ping-pong images in `PostProcessPipeline`; node after the scene, before bloom
   (bloom on the resolved image is stabler).
2. Jitter: `Camera::setJitter` driven by a `JitterSource` owned by the post pipeline so
   TAA, FSR2 and later vendors share it.
3. Dynamic resolution: `PostProcessPipeline::chooseInternalScale(frameTimeEma, target)`;
   viewport/scissor from it; the scene target is allocated at 1.0 and rendered into a
   sub-rectangle (UV scale in the resolve).
4. Settings; presets; MSAA interplay (`isFsr2BlockingMsaa` pattern → TAA disables MSAA).

## Settings

| key | kind | choices / range | default | L / M / H / U | enabledWhen |
|---|---|---|---|---|---|
| `antialiasing` | Enum | append `TAA` | — | 4/4/4/4 (TAA becomes the preset default; MSAA stays selectable) | |
| `taasharpness` | Float | 0–1 | 0.3 | — | `antialiasing=4` |
| `dynamicresolution` | Bool | | 0 | 0/0/0/0 | `antialiasing=4` or `fsr2` |
| `fpstarget` | Enum | `30|60|90|120` | 1 | — | `dynamicresolution` |
| `renderscale` | Float | 0.5–2.0 | 1.0 | 1/1/1/1 | `antialiasing=4` |

`renderscale` above 1.0 is supersampling: the same scene-target-with-viewport mechanism
dynamic resolution uses, run upward, resolved by TAA's Catmull-Rom fetch. The allocation
follows the maximum of the two settings.

## Reserved

```cpp
// RESERVED(phase-17, P1-upscaler-interfaces): JitterSource and the history images are
// what the Upscaler interface will hand to FSR 3.1 / DLSS / XeSS.
```

## Verify

- Compare mode `stranglethorn-canopy` (`--sequence`, wind), `goldshire-inn-morning`
  (running character), `stormwind-gate` (rooftop detail): TAA vs MSAA 4× vs off; SSIM
  against a 4× supersampled reference recorded; TAA must beat MSAA 4× on the canopy.
- Presets before/after: the Low preset moves from MSAA off to TAA — frame time on T0 must
  not rise (TAA ≈ 0.4 ms at 1080p).
- Dynamic resolution: `crossroads-plains` with an artificial 8 ms GPU load: scale drops,
  holds 60, no visible pumping over a 10 s sequence.

## Commit

```
Anti-alias in time

A native-resolution temporal anti-aliasing pass built on the jitter
and velocity the upscalers already used, with variance clipping so
nothing ghosts. It becomes the presets' default because it costs less
than 2x MSAA and holds foliage still. Dynamic resolution scales the
internal size under it to hold a chosen frame rate.
```
