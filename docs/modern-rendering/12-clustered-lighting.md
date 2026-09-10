# Phase 12 — Clustered forward lighting from the game's own light data

**One session, one commit.** Depends on: 04. Player sees: every brazier, torch, lamp and
glowing mushroom is a light — Orgrimmar at night, Ironforge, Undercity, Zangarmarsh — at
constant cost instead of a 64-light budget looped per fragment.

## Ships

- **L4** — view-space cluster grid 16×9×24, light AABB assignment in compute
  (`cluster_assign.comp.glsl`), light list + index SSBOs, `cluster_lighting.glsl` include
  with the same `localLightContribution()` signature the shaders call today. Up to 1024
  lights. Legacy 64-light UBO path stays behind a spec constant.
- Light sources: WMO `MOLT` (`wmo_loader.hpp:212`, already parsed) placed by group; **M2
  light block** parsed in `m2_loader.cpp` (the `lights` array: type, bone, position,
  colours, attenuation tracks) and instanced with the model, animated by the existing track
  sampler; the glow-card heuristic (`m2_glow_card.hpp`) stays as a fallback emitter for
  models with glow cards but no light block.
- Lights culled and sorted by `LightingManager` each frame; the 64-UBO path receives the
  nearest 64 of the same list, so both paths draw from one source.

## Steps

1. `m2_loader.cpp`: parse `M2Light` (offset in the MD20 header; both 256–264 layouts for
   the four expansions; `wowee_light.hpp` may already have the struct — check). Unit test
   against a known lantern model.
2. `lighting_manager.cpp`: a `LightSet` gathered from M2 instances, WMO groups and glow
   cards; frustum cull; sort by contribution.
3. Compute: cluster AABBs on resize (`cluster_build.comp.glsl`), per-frame assignment with
   a per-cluster max of 64 and an overflow counter surfaced in the perf HUD.
4. `cluster_lighting.glsl`: cluster index from `gl_FragCoord` and view depth (reverse-Z
   aware), loop the cluster's list. Spec constant `LIGHT_MODE`.
5. Shaders: `m2`, `wmo`, `character`, `terrain`, `grass`, `water` include it; append the
   two SSBO bindings on set 0.
6. Settings.

## Settings

| key | kind | choices | default | L / M / H / U | enabledWhen |
|---|---|---|---|---|---|
| `lightingmode` | Enum | `Nearest 64|Clustered` | 1 | 0/1/1/1 | |
| `maxlights` | Enum | `128|256|512|1024` | 1 | 0/1/2/3 | `lightingmode=1` |

## Reserved

```cpp
// RESERVED(phase-14, L6-local-shadows): Light entries carry a `shadowSlot` (-1) so the
// atlas can claim the brightest N without a layout change.
// RESERVED(phase-15, A2-volumetric-fog): the cluster list is bound to set 0 where the
// froxel pass will read it.
```

## Verify

- Compare mode: `orgrimmar-valley-of-strength-night`, `ironforge-great-forge`,
  `deadmines-foundry`, `ashenvale-astranaar`, `duskwood-road` 21:00, `zangarmarsh-glow`
  (TBC+). Before == phase-11 golden.
- Light count on screen in the HUD: Orgrimmar valley ≥ 150 with `Clustered`.
- Frame time Orgrimmar at night, T0 and T2, both modes, recorded; clustered must not be
  slower than the 64 loop at 64 lights.

## Commit

```
Make every torch a light

A clustered light list built in compute replaces the 64-light loop;
the lights come from the WMO light chunk and the M2 light block the
models have carried all along, plus the glow-card heuristic where a
model has none. The old path stays as "Nearest 64" and the same
SPIR-V.
```
