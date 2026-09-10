# Phase 02 — Comparison mode on the game executable

**One session, one commit.** Depends on: 01. Player sees: nothing in game. The project gets
the instrument every later phase is judged with (plan §7.5), and the phase before this
one gets its evidence page regenerated with it.

## Ships

- `wowee --compare <ids> --scene <name|all> [--value v] [--frames n] [--sequence] --out dir`
  on the real GPU with the player's `settings.cfg` as baseline; before/after/diff/side PNGs
  and a JSON with SSIM, PSNR, GPU frame times, settings dumps, caps tier, git hash.
- `capture_scene` gains the same flags for the Lavapipe/CI path; both use one
  `SceneSetup` library.
- `tools/compare_scenes.json` — the 25 scenes from plan §7.5, **coordinates verified** with
  `pywowlib/tools/terrain_height.py` / `wmo_height.py` (the Stormwind gate is the only one
  already confirmed).
- `tools/compare_report.py` → `docs/evidence/phase-NN/index.html`.
- CI: Lavapipe run of the T0 subset on every PR touching `src/rendering/` or `assets/shaders/`.
- Retires phase-01's `compare_scenes.py` v0.

## Steps

1. Extract scene construction from `tools/capture_scene/main.cpp` into
   `src/tools/scene_setup.{hpp,cpp}` (`include/tools/` exists; use it). `capture_scene`
   becomes a thin `main`. Its existing output must be bit-identical — check on `stormwind-gate`.
2. `src/main.cpp`: parse `--compare`; branch before window creation into
   `runCompare(args)`: offscreen `VkContext` (the FSR off-screen target path already renders
   to an image; reuse), `SceneSetup`, load `settings.cfg`, for each (scene, technique):
   set off → warm `--frames` → readback (the screenshot key's readback) → set on → warm →
   readback → write. Exit code: 2 if before ≠ golden beyond tolerance, 3 if after == before.
3. Technique ids = schema keys; `all-phase-NN` table in `src/tools/compare_phases.cpp`,
   maintained by each phase.
4. Diff/side/SSIM in C++ (stb_image_write exists; SSIM is 40 lines) so CI needs no Python
   for the capture itself; `compare_report.py` only assembles HTML.
5. Scene file + verification pass: for every entry run the height tools, adjust Z, and
   render once to confirm the framing; store a thumbnail hash so a later coordinate edit
   is noticed.
6. `--sequence` + MP4 via the libav the loading-screen player links (`src/ui/loading_screen`
   path); skip MP4 when libav is absent.
7. Regenerate `docs/evidence/phase-01..05/` with the new tool.

## Settings

None.

## Reserved

None.

## Verify

- `wowee --compare shadowcascades --scene all --out /tmp/c` runs to completion on the T0
  and T2 machines; every scene renders (no black frames, no missing WMO).
- CI job green on the Lavapipe subset (`stormwind-gate`, `goldshire-inn-morning`,
  `westfall-sentinel-hill`, `ironforge-great-forge`).
- Phase-01 goldens reproduce bit-exact through the new path versus v0's PNGs.

## Commit

```
Compare a technique off and on from one flag

wowee --compare renders each named scene twice on the player's own
GPU, writes before, after, difference and a side-by-side, and fails
when off no longer matches the last release or on changes nothing.
capture_scene shares the scene setup and grows the same flags for CI.
Twenty-five scenes chosen so every technique has somewhere to show.
```
