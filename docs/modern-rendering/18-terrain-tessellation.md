# Phase 18 — Terrain tessellation with splat-derived height

**One session, one commit.** Depends on: 01 (LOD, patch flag), 07 (generator worker).
Player sees: cobbles, rock and snow at their feet have real relief; the POM from phase 01
takes over beyond a few yards.

## Ships

- **G4** — quad-patch tessellation (T1 `tessellationShader` bit) for chunks at LOD 0 within
  25 yd, level by distance (max 16), displacement from a per-chunk height sidecar; crack-free
  against LOD-1 neighbours via the phase-01 skirts and edge tess factors matched to the
  neighbour's level.
- **`terrain.height` generator** in the phase-07 worker: per chunk, a 64² height sidecar =
  weighted blend of each layer's phase-01 height map by the splat alphas
  (`adt_alpha.hpp`), scaled by a per-layer displacement from the material class table
  (stone 0.15 yd, sand 0.05, snow 0.1). Manifest entry `height`. Without it, tessellation
  displaces from the layer height maps directly at run time (cheaper to generate, slower to
  sample) — so the setting works before the generator finishes.

## Steps

1. `terrain.tesc.glsl`, `terrain.tese.glsl`; `compile_shaders()` recognises `.tesc/.tese`
   (add to `CMakeLists.txt:459-467`). Pipeline variant with patch topology from the
   phase-01 `patchMode` index set.
2. Displacement in `tese`; normals re-derived from the displaced neighbours for the
   pre-pass normal target.
3. Generator job; sidecar loader; manifest.
4. **Height-based layer blending.** With per-layer heights available (the phase-01 Sobel
   height in each layer's normal map alpha, scaled by the class displacement), the splat
   in `terrain.frag.glsl` blends layers by `alpha * height` with a sharpness term instead
   of linear alpha: cobbles rise through sand, snow settles into cracks. Same data as the
   displacement, so it costs one extra fetch per layer. Spec constant; off is the linear
   splat and the same SPIR-V.
5. Settings; `unavailable` from the `tessellationShader` cap.

## Settings

| key | kind | choices | default | L / M / H / U | enabledWhen | requires |
|---|---|---|---|---|---|---|
| `terraintessellation` | Enum | `Off|Low|High` | 0 | 0/0/1/2 | `terrainlod!=0` | cap `tessellationShader` |
| `terrainheightblend` | Bool | | 1 | 0/1/1/1 | `normalmapscope=1` | — (T0; needs no tessellation) |

## Reserved

None. Consumes phase-01's reservation.

## Verify

- Compare mode: `stormwind-gate` (ground close), `kharanos-snow`, `tanaris-dunes`,
  `orgrimmar-drag`. Before == phase-17 golden.
- Crack sweep: wireframe fly-through of Westfall at each level, zero cracks.
- Frame time on T1 reference at High: ≤ 0.6 ms; on T0-without-tessellation the row is
  greyed with the reason.

## Commit

```
Tessellate the ground underfoot

Near terrain chunks are drawn as tessellated patches displaced by a
per-chunk height sidecar the texture worker blends from the splat
layers, with parallax taking over beyond. Greyed with a reason on
GPUs without tessellation; off is the phase-17 frame.
```
