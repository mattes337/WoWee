# Phase 10 — Sky probes: directional ambient and specular from the skybox

**One session, one commit.** Depends on: 04. Player sees: the top of every object lit by the
sky and the underside by the ground, changing with the time of day — low-poly models read
as volumes, dusk stops being a flat colour wash.

## Ships

- **L5** — every 2 game-minutes (or on lighting change) render the skybox + sun + horizon
  glow into a 32² cubemap, project to SH9 (compute, 1 dispatch) into the `skySH` slots
  reserved in phase 01, and prefilter 5 mips of a 64² specular cube (GGX importance
  sampling, 32 samples). Lit shaders: `ambient = max(SH(N or bentN), 0) * ao`;
  specular `textureLod(uSkySpecular, R, roughness * 4)` with the Blinn exponent mapped to
  roughness until phase 12 provides real roughness.
- WMO interiors: per-group probe = MOHD ambient (`wmo_loader.hpp:192`) as flat SH plus the
  group's `MOCV` average for the ground term; no cubemap render for interiors this session.
- Reads a pack-supplied probe (`Data/override/sky/<lightId>.ktx2`, equirect) if present —
  ten lines, authored assets come after 24.

## Steps

1. `SkyProbe` in `sky_system.cpp`: off-screen 6-face pass reusing `Skybox`, `Celestial`,
   `Clouds` draws with a cube camera; `sh_project.comp.glsl`; `prefilter_specular.comp.glsl`.
2. `PerFrame`: fill `skySH[7]` (from phase 01), append `int skyProbeValid`; bind the
   specular cube on set 0 (append binding; black fallback when off).
3. `ambient.glsl` include used by the four lit shaders + grass + water's ambient term:
   `shAmbient(n)`; the `ambientColor` uniform stays the fallback when `skyprobes=0`.
4. Calibration: the SH DC term at noon must equal `ambientColor` from Light.dbc within 5 %
   (scale the probe, not the DBC) so phase 04's fit still holds.
5. Settings.

## Settings

| key | kind | default | L / M / H / U | enabledWhen |
|---|---|---|---|---|---|
| `skyprobes` | Bool | 1 | 0/1/1/1 | `hdr` |

## Reserved

```glsl
// RESERVED(phase-12, L7-pbr): the specular cube's mip selection takes a roughness that is
// derived from the Blinn exponent until real roughness exists.
```

## Verify

- Compare mode: `thunder-bluff-dawn`, `tanaris-dunes`, `character-portrait` 18:00,
  `goldshire-inn-morning`, `crossroads-plains`. Before == phase-09 golden.
- Time-lapse `--sequence` 04:00→22:00 on `stormwind-gate`: ambient hue follows the sky,
  no popping at probe updates (blend over 1 s).
- Frame time: probe update ≤ 0.3 ms when it fires; zero otherwise.

## Commit

```
Light the world by its own sky

A small cubemap of the skybox, sun and horizon is projected to
spherical harmonics and a prefiltered specular cube whenever the
lighting changes, and the lit shaders take ambient from it instead of
one flat colour. Calibrated so noon matches Light.dbc; off is the
phase-09 frame.
```
