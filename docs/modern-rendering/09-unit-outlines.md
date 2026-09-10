# Phase 09 — Reaction-coloured target and mouseover outlines

**One session, one commit.** Depends on: 08 (per-instance plumbing), 03 (pre-pass
instance-ID target). Player sees: a thin silhouette around the current target and the unit
under the cursor, coloured hostile / neutral / friendly by the client's own reaction logic,
including weapons, hair and cape, never the particles.

## Ships

- **U1** — outline from the pre-pass **instance-ID target** (reserved in 03): no second
  geometry pass. An edge pass at output resolution reads the ID buffer (nearest-upsampled
  when rendering below native), finds pixels whose 8-neighbourhood contains a different ID
  than the selected set, and composites an outside-only edge of `outlinewidth` pixels
  (jump-flood distance for widths > 2). Runs after upscaling and before UI, through the
  `PostProcessPipeline`, so FSR/TAA/MSAA resolves and frames in flight are already handled.
- Selection set = target GUID ∪ mouseover GUID, deduplicated, resolved to live render
  instances each frame and dropped on despawn; attached instances (weapons, shoulders,
  helm — `bone_slots.hpp` attachments) are included by owner GUID.
- Colour from the existing reaction evaluation (the same source nameplates use), never a
  private threshold table; alpha from the material: opaque and alpha-tested batches write
  IDs (03 includes alpha-tested batches in the pre-pass), blended effects and glow cards
  never do.
- Depth-tested by default (occluded parts not outlined); `outlinexray` draws through walls
  as an explicit choice.

## Steps

1. 03's `R16_UINT` instance-ID attachment gets written: M2 instance index, character
   instance index, WMO group index in separate ranges; `0` = none.
2. `outline_edge.comp.glsl`: selected-ID set in a small SSBO (≤ 8), neighbourhood test,
   optional jump-flood for width, colour per ID from the SSBO.
3. `GameHandler` target/mouseover → `Renderer::setHighlightedUnits(span<GUID>)` →
   instance resolution in `M2Renderer` / `CharacterRenderer` (the GUID→instance map the
   nameplates already use).
4. Composite node in the render graph after the upscaler, before ImGui/FrameXML.
5. Settings; presets leave it on at 2 px.

## Settings

| key | kind | choices / range | default | L / M / H / U | enabledWhen |
|---|---|---|---|---|---|
| `unitoutlines` | Bool | | 1 | 1/1/1/1 | `depthprepass` |
| `outlinewidth` | Int | 1–4 px | 2 | — | `unitoutlines` |
| `outlinexray` | Bool | | 0 | — | `unitoutlines` |

## Reserved

None. Consumes `RESERVED(phase-09, U1-unit-outlines)` from 03.

## Verify

- Compare mode `goldshire-inn-morning` with a targeted NPC and a mouseover player
  (`--target`, `--mouseover` flags added to the scene block): before == phase-08 golden;
  after shows the outline on body, hair, cape and weapon, not on the campfire smoke.
- Target and mouseover the same unit: one outline. Despawn the target mid-sequence: no
  stale edge. Behind a wall: none, unless `outlinexray`.
- FSR 3.1 Quality, TAA, MSAA 4× and a resize: the outline stays exactly `outlinewidth`
  output pixels in every case.
- Frame time: ≤ 0.15 ms at 1440p on T0.

## Commit

```
Outline the target and the unit under the cursor

An edge pass over the pre-pass instance IDs draws a thin silhouette
around the selected units in the colour the client already gives
their reaction, after upscaling so it is the same width at any render
resolution. Weapons and capes are included by owner; effects never
write an ID. Off is the phase-08 frame.
```
