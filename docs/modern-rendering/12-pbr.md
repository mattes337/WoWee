# Phase 12 — GGX shading with class-based roughness

**One session, one commit.** Depends on: 10 (probes), 07 (manifest for sidecars). Player
sees: metal reads as metal and cloth as cloth under the sky probe; energy-conserving
highlights instead of the `pow(·, 32)` disc. Opt-in — the hand-painted specular baked into
some diffuse textures can fight it.

## Ships

- **L7** — `brdf.glsl`: GGX D, Smith-Schlick G, Schlick F; diffuse Lambert; the water
  shader's copy (`water.frag.glsl:99-115`) replaced by the include. Roughness/metalness
  from an `_orm` sidecar if the manifest has one, else from **material class** tables.
- **M3b** — loading-screen generator `textures.pbr` (phase-07 worker) writing class-based
  roughness/AO sidecars: roughness from `wmo_material_class.hpp` /
  `m2_model_classifier.cpp` classes plus a luminance-variance term (shiny = low variance
  bright regions), AO from the phase-01 height map's cavity. Deterministic, seconds per
  texture. Recorded in the manifest as `orm`.
- Blinn-Phong remains `shadingmodel=0`, same SPIR-V.

## Steps

1. `brdf.glsl`; `lit_common.glsl` gathering shadow (01), ambient (10), clusters (11), fog
   (01) and the BRDF into one `shade()` the four lit shaders call — this is the session's
   main refactor and pays off in every later phase.
2. Class table `include/rendering/material_roughness.hpp`: stone 0.85, wood 0.7, cloth
   0.9, leather 0.6, metal 0.35 metal 1.0, skin 0.55, glass 0.05 … keyed by the existing
   classifiers' outputs.
3. Sidecar: `AssetManager::loadSidecar(path, "orm")` through the manifest; `M2Material` /
   `WMOMaterial` / `CharMaterial` UBOs append `float roughness, metalness, hasOrm`.
4. Generator job in the phase-07 worker; CLI `generate_assets --pbr`.
5. Calibration on `goldshire-inn-morning` (plate + cloth character), `ironforge-great-forge`
   (metal), `kharanos-snow`: with `shadingmodel=1` the mid-tone luminance must match
   phase 11 within 5 % (adjust the class table's F0, not the lights).
6. Settings; the Ultra preset turns it on, others leave Blinn-Phong.

## Settings

| key | kind | choices | default | L / M / H / U | enabledWhen | requires |
|---|---|---|---|---|---|---|
| `shadingmodel` | Enum | `Classic|Physically based` | 0 | 0/0/0/1 | `hdr` | |
| `pbrsidecars` | Bool | | 1 | — | `shadingmodel=1` | `textures.pbr` |

## Reserved

None. Consumes phase-01, 11, 14 reservations.

## Verify

- Compare mode: `character-portrait` (each race, plate and cloth), `ironforge-great-forge`,
  `goldshire-inn-morning`, `kharanos-snow`, `stormwind-gate`. Before == phase-11 golden.
- `pbrsidecars` off vs on: sidecars must only sharpen the class guess, never change the
  overall brightness (SSIM ≥ 0.98).
- Frame time: GGX vs Blinn ≤ +0.1 ms on T0.

## Commit

```
Shade with GGX, and guess roughness from what a surface is

An energy-conserving BRDF shared by every lit shader and the water,
with roughness and metalness from a per-class table refined by a
generated sidecar when the texture cache has made one. Classic
shading stays the default and the same SPIR-V; Ultra turns the new
model on.
```
