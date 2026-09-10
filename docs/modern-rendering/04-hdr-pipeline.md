# Phase 04 — HDR scene target, tone mapping, exposure

**One session, one commit.** Depends on: 03. Player sees: the sun at the horizon no longer
clips to flat white, lava and spell glow keep their colour, dusk interiors keep detail. The
frame at exposure 1.0 is calibrated to look like the frame before — this phase is an
enabler that must not look like a change.

## Ships

- **F2** — scene rendered to `B10G11R11_UFLOAT` (mandatory colour-attachment format in
  Vulkan 1.0; `R16G16B16A16_SFLOAT` as the `hdrprecision=1` choice), AgX tone map in
  `postprocess.frag.glsl` replacing the shoulder, auto-exposure from a 64-bin log-luminance
  histogram (compute, 2 dispatches) with a slow adapt (τ = 1.5 s up, 3 s down) and clamps.
- Light values in linear: `LightingManager` converts Light.dbc band colours from the gamma
  values the shaders were tuned for to linear at load; sun/ambient/local-light intensities
  scaled so the tone-mapped frame at exposure 1.0 matches the phase-03 frame in the
  mid-tones (measured, below).
- `brightness` becomes exposure bias in stops.

## Not in this session

- Bloom, LUT → 05. HDR display output → later, when a monitor to test on exists.

## Steps

1. `post_process_pipeline.cpp`: the FSR/FXAA off-screen scene target becomes the default
   path (`getSceneFramebuffer()` never returns null); format from the setting; the
   "no post-processing" fast path is gone — one fullscreen tone-map pass always runs
   (0.1 ms; simpler than three paths).
2. Every fragment shader that outputs colour writes linear radiance; sRGB decode of
   textures via `_SRGB` image formats in `vk_texture.cpp` (BC1/2/3 have sRGB variants;
   RGBA8 does) instead of shader `pow`. UI/ImGui stays on the swapchain after tone mapping.
3. `tonemap.glsl`: AgX (base + punchy look constants), exposure from a 1×1 storage buffer
   the histogram pass writes; `exposure_mode = manual` for the comparison harness.
4. Calibration: run compare mode on all 25 scenes at 06/09/12/15/18/22 with `hdr=0` and
   `hdr=1`; fit one global sun scale, one ambient scale and one local-light scale so the
   mean luminance of the mid-tone band (0.2–0.7) differs by < 3 % over the set. Commit the
   three numbers in `lighting_manager.cpp` with the fit log in `docs/evidence/phase-04/`.
5. Settings, presets, `brightness` migration (old 0–100 → −2..+2 stops; migrate on load).

## Settings

| key | kind | choices / range | default | L / M / H / U | note |
|---|---|---|---|---|---|
| `hdr` | Bool | | 1 | 1/1/1/1 | Off = 8-bit target and the old shoulder curve, bit-exact |
| `hdrprecision` | Enum | `11-bit float|16-bit float` | 0 | 0/0/0/1 | `enabledWhen=hdr` |
| `exposuremode` | Enum | `Auto|Manual` | 0 | — | `enabledWhen=hdr` |
| `exposurebias` | Float | −2..+2 | 0 | — | replaces `brightness` |

## Reserved

```glsl
// RESERVED(phase-05, S2-bloom): tonemap.glsl takes a `bloom` sampler bound to a 1x1 black
// image until the bloom chain exists.
```

## Verify

- Compare mode all scenes, `hdr` 0→1, manual exposure 0: SSIM ≥ 0.97 on every scene at
  noon; the differences must be in highlights and deep shadows only (diff image inspected,
  not just the number).
- `hdr=0` before == phase-03 golden bit-exact.
- Auto exposure: walk from Elwynn noon into the Goldshire inn — adaptation completes in
  ~3 s with no oscillation; record a `--sequence`.
- Frame time: +0.2 ms tone map + histogram on T0 at 1080p, recorded.

## Commit

```
Render the scene in linear HDR and tone-map it

A float scene target, AgX in the post pass and a histogram exposure
replace the 8-bit target and its shoulder curve. Light.dbc colours go
linear at load and three fitted scales keep the noon frame where it
was, so nothing authored shifts; highlights stop clipping. Brightness
is now an exposure bias in stops. Off is the old path, bit for bit.
```
