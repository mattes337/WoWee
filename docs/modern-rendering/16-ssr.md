# Phase 16 — Screen-space reflections; planar water becomes optional

**One session, one commit.** Depends on: 03 (depth/normals/HiZ), 11 (probe fallback).
Player sees: lakes and harbours reflect what is on screen, wet stone and ice reflect a
little, and the frame gets cheaper because the water no longer re-renders the world.

## Ships

- **S3** — Hi-Z ray march (`hiz_system.hpp`'s pyramid, now from the current frame's
  pre-pass depth) at half res for pixels with roughness < 0.6, confidence from hit
  distance and screen-edge fade, probe fallback (11) where the ray leaves the screen,
  temporal accumulation with the velocity target. Output `RGBA16F` reflection + confidence.
- Water (`water_renderer.cpp`) samples SSR instead of the planar texture when
  `waterreflection=SSR`; the planar pass (`beginReflectionPass`) stays as the other choice
  and the render-graph node is disabled otherwise.
- Wet-surface reflection on WMO/terrain with roughness ≤ 0.3 (rain from the weather
  system lowers terrain roughness while it rains — ten lines in `weather.cpp`).

## Steps

1. `ssr_trace.comp.glsl`, `ssr_resolve.comp.glsl` (spatial 4-tap + temporal).
2. Graph node after `prepass`/HiZ, before `main`; set-0 binding appended.
3. Water: `water.frag.glsl` takes `uSSR` and confidence; blend with the probe by
   confidence; keep the refraction copy as is.
4. `lit_common.glsl`: specular indirect = `mix(probe, ssr.rgb, ssr.a)` for smooth surfaces.
5. Settings; default `SSR`; presets Low keeps `Probe only` (no planar, no SSR — cheaper
   than today).

## Settings

| key | kind | choices | default | L / M / H / U | enabledWhen |
|---|---|---|---|---|---|
| `waterreflection` | Enum | `Planar|Probe only|SSR` | 2 | 1/2/2/2 | |
| `ssrquality` | Enum | `Low|High` | 0 | 0/0/0/1 | `waterreflection=2` |

## Reserved

```glsl
// RESERVED(phase-24, R3-rt-reflections): the resolve pass takes an optional RT hit image
// with the same layout as the SSR trace output; bound to black.
```

## Verify

- Compare mode: `lakeshire-lake`, `booty-bay-harbour`, `stormwind-harbour` (WotLK),
  `howling-fjord-cliffs` (WotLK), `deadmines-foundry` (wet floor), `duskwood-road` in rain.
  Before (`Planar`) == phase-15 golden.
- Frame time `stormwind-harbour`: SSR must be cheaper than Planar on T0 and T2 — record
  both; this is the phase's justification as much as the look.

## Commit

```
Reflect the screen instead of drawing the world twice

Hi-Z screen-space reflections with a probe fallback, used by the
water and by smooth wet surfaces. The planar reflection that
re-rendered the scene from under the water stays as a choice and
stops being the default.
```
