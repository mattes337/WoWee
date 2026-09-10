# Phase 14 — Local light shadows and contact shadows

**One session, one commit.** Depends on: 12 (light list), 03 (depth). Player sees: the orc
by the brazier casts a shadow from it; feet, props and door frames stop floating on their
shadow-map bias.

## Ships

- **L6** — a 2048² depth atlas of 6-face cube shadows for the N brightest lights within
  40 yd (N = setting), 256² per face, updated round-robin two lights per frame, static
  casters only (WMO, M2 doodads); characters are the sun cascade's job. Sampled with 2×2
  PCF in `cluster_lighting.glsl` when `shadowSlot >= 0`.
- **L3** — screen-space contact shadows: 12-step depth ray toward the sun per pixel, half
  res, `R8` mask multiplied into the sun term; hidden by TAA's accumulation.

## Steps

1. Atlas + per-light slot allocator in `LightingManager` (reserved `shadowSlot`); cube
   render uses the phase-03 pre-pass depth pipelines with a cube camera; multiview
   (`VK_KHR_multiview`, optional, if present) draws six faces in one pass, else six passes.
2. `cluster_lighting.glsl`: sample the atlas for shadowed lights.
3. `contact_shadow.comp.glsl` after the pre-pass; the lit shaders multiply the mask into
   direct sun (`lit_common.glsl` from 13).
4. Settings.

## Settings

| key | kind | choices | default | L / M / H / U | enabledWhen |
|---|---|---|---|---|---|
| `locallightshadows` | Enum | `Off|2|4|8` | 2 | 0/1/2/3 | `lightingmode=1` |
| `contactshadows` | Bool | | 1 | 0/1/1/1 | `depthprepass` |

## Reserved

None.

## Verify

- Compare mode: `goldshire-inn-interior-night`, `orgrimmar-valley-of-strength-night`,
  `deadmines-foundry` (local); `goldshire-inn-morning`, `northshire-abbey` (contact).
  Before == phase-13 golden.
- Frame time Orgrimmar night at each `locallightshadows` level on T0 and T2; the round-robin
  keeps the cost flat regardless of light count.

## Commit

```
Let lamps cast shadows, and close the gap under everything

A round-robin cube-shadow atlas for the brightest nearby lights, read
from the clustered light loop, and a short screen-space ray toward
the sun that darkens the contact the shadow map's bias always lost.
Both off are the phase-13 frame.
```
