# Phase 05 — Bloom and colour LUT

**One session, one commit.** Depends on: 04. Player sees: sun glare, lava glow, torch halos,
spell light bleeding the way the 2008 client's "full-screen glow" did — physically, without
a threshold — and a neutral grading LUT slot for later art.

## Ships

- **S2** — progressive 6-mip downsample (13-tap) / upsample (tent) bloom on the HDR
  target, in compute, blended in `tonemap.glsl` at 3–8 % (setting). No threshold.
- **P5** — 32³ colour LUT applied after tone mapping; ships with an identity LUT and reads
  `Data/override/luts/<zone>.png` if present (hand-authored, after phase 25 in the plan —
  the slot exists now because it is ten lines).

## Steps

1. `bloom_down.comp.glsl`, `bloom_up.comp.glsl`; `PostProcessPipeline` gains a mip chain
   of the scene target's format at half res down to 1/64; render-graph node between the
   scene and tone map.
2. `tonemap.glsl`: `color = mix(color, bloom, bloomStrength)` before AgX (energy-preserving
   mix, not add), then LUT lookup in tetrahedral interpolation after.
3. Glow-card heuristics in `m2_glow_card.hpp` / `M2Material.emissiveBoost` are what feed
   bloom bright pixels; raise nothing — check on `orgrimmar-valley-of-strength-night` that
   braziers bloom and cloth does not. If cloth blooms, the emissive classifier is wrong,
   not the bloom.
4. Settings, presets.

## Settings

| key | kind | range | default | L / M / H / U | enabledWhen |
|---|---|---|---|---|---|
| `bloom` | Bool | | 1 | 0/1/1/1 | `hdr` |
| `bloomstrength` | Float | 0–0.15 | 0.05 | — | `bloom` |
| `colorlut` | Bool | | 0 | — | `hdr` |

## Reserved

None.

## Verify

- Compare mode: `searing-gorge-night`, `ironforge-great-forge`, `orgrimmar-valley-of-strength-night`,
  `stormwind-gate` 17:30 (sun), `goldshire-inn-interior-night`. Before == phase-04 golden.
- Bloom off with `hdr` on: bit-identical to phase 04.
- Frame time: chain ≤ 0.25 ms at 1080p on T0.

## Commit

```
Let bright things glow

A thresholdless progressive bloom on the HDR target, mixed in before
tone mapping so lava, the low sun and torches bleed light the way the
old client's full-screen glow did, and a colour LUT slot after it that
ships identity. Both off are the phase-04 frame exactly.
```
