# Phase 25 — The AI pack: hash-keyed reader and once-built builder

**One session, one commit.** Depends on: 07. Last of the generated-asset work (plan §4.12).
Player sees, with a pack installed: upscaled textures across whichever game version they
run. Without one: the phase-24 frame exactly. Whether a pack is ever published is the
licence decision recorded in plan §4.11, taken before this session starts.

## Ships

- **Reader** — `Data/packs/<name>/pack.json` keyed by **source content hash**; the loader's
  reserved slot (phase 07) between override and generated; UASTC KTX2 transcoded to BC7 /
  ASTC by the phase-07 worker on first touch and cached; manifest `packs` block with
  `matched` / `unmatched` counts; `requires` reasons ("Requires the HD diffuse pack",
  "Pack covers 12 890 of 13 201 textures").
- **Builder** — `generate_assets --build-pack --ai --roots wotlk,tbc,vanilla,turtle --model
  realesrgan-x2 --out hd-diffuse-x2/`: union of the roots' `manifest.json`s deduplicated by
  hash, allow-list by path pattern and texture properties, alpha upscaled separately and
  re-thresholded for alpha-tested textures, UASTC encode, `pack.json` with model name,
  weights hash, parameters. Model runs through a subprocess (`realesrgan-ncnn-vulkan` or a
  Python runner) so the client build has no ML dependency. `--ai` without `--build-pack`
  runs the same code locally into `Data/generated` for those who refuse a download.
- Asset-pipeline GUI page: install/activate a pack, show coverage.

## Steps

1. `pack.json` schema + reader in `pipeline/asset_pack.{hpp,cpp}`; hash index built at
   start (fast: hashes are in the phase-07 manifest already).
2. Loader order: override → pack → generated → extracted → archive
   (`asset_manager.cpp:177` chain).
3. Builder script; allow-list rules file `tools/pack_rules.json` (terrain, WMO, character
   skins, doodads; never UI, particles, text, minimap, loading screens).
4. CI: builder on the phase-07 200-file samples with a stub "model" (identity) for
   hash-union correctness — a texture present in three roots appears once.
5. Settings rows use `requires`.

## Settings

| key | kind | default | requires |
|---|---|---|---|
| `hdtextures` | Bool | 1 | pack `hd-diffuse-*` |

## Reserved

None. Consumes phase-07's.

## Verify

- With a locally built 200-texture test pack: compare mode `goldshire-inn-morning`,
  `stormwind-gate`, `northshire-abbey`, `character-portrait`, `hdtextures` off → on; off ==
  phase-24 golden bit-exact.
- Same pack against a Vanilla root: matched count equals the number of byte-identical
  sources; no entry applied to a differing file.
- VRAM with the pack: within the BC7 budget from phase 07 (no RGBA8 fallback path taken —
  assert in the log).

## Commit

```
Read once-built texture packs by content hash, and build them

A pack keyed by the hash of the original bytes serves whichever
expansion the player has, transcoded into the texture cache on first
use. The builder unions every supported extraction, upscales each
distinct source once and pins the model that did it. No pack ships in
the repository; the client is unchanged without one.
```
