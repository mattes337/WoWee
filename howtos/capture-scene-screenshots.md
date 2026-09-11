# Rendering a scene twice, and looking at the difference

A rendering change is reviewed by looking at it. The diff of a shader says what
the code now computes; it does not say whether the picture got better, and a
sentence describing the picture is not evidence of anything. `capture_scene`
renders one named camera in one named zone at one named time of day, with any
client setting moved by `--setting`, so a before and an after are the same frame
with one thing different. `tools/compare_scenes.py` renders both and writes the
heat map between them.

## What it is, and what it is not

It drives the real client. The real asset manager, the real world loader, the
real renderer, the real shaders - so a picture it produces is a picture of what
players run. It is not a second, simpler renderer, which would only answer
questions about itself.

**It opens a window.** The name says headless and the tool is not: a Vulkan
swapchain needs a surface, the whole frame is built around one, and
`Renderer::captureScreenshot` reads back the swapchain image that was just
presented. Rendering into an off-screen colour target instead would mean a
second path through every pass - a second thing to be wrong, and the picture
would then be of that path rather than of the one that ships. On a machine with
no display this tool cannot run, and it says so rather than pretending.

**The window size is the picture size.** `--width` and `--height` are parsed and
ignored: the client opens at 1280x720 and the shot is of the swapchain. Both
renders of a pair are the same size, which is what a comparison needs.

**There is no entity manifest.** The `.json` half of the original design is not
implemented; only the `.png` is written.

## Building it

Off by default, because it compiles the client's sources a second time:

```
cmake -S . -B build -DWOWEE_BUILD_CAPTURE_SCENE=ON
cmake --build build --config Release --target capture_scene
```

The executable lands at `build/bin/capture_scene` (`build/bin/Release/` on a
multi-config generator).

## Prerequisites

A data directory with a `manifest.json`, which `asset_extract` produces from the
game's MPQ archives:

```
./build/bin/asset_extract /path/to/WoW/Data /path/to/extracted
```

## One picture

```
./build/bin/capture_scene \
    -d Data \
    -m Azeroth \
    -c -9462,-67,70 \
    -t -9200,-320,50 \
    --time 9 \
    -o docs/evidence/phase-01/img/goldshire-lake
```

Coordinates are **server coordinates** - the same numbers `.gps` prints - so a
camera can be copied straight out of a bug report. `-c` is where the camera is
and `-t` is what it looks at; `--angles pitch,yaw,roll` sets the direction
directly instead.

`--time` is the hour of the day, 0 to 24, and it is what decides where the sun
is. A shadow comparison at noon in an open field is a comparison of nothing.

Point the camera at something. `-t` is a look-at point, so a target with the
same x and y as the camera and a lower z is a camera pointing at the ground
directly beneath it, which renders as a flat wash of fog with the minimap in the
corner and nothing else in it.

## Standing in it for a while

`--dwell <seconds>` keeps drawing the settled frame for that long before taking
the shot, and prints how many frames it managed, the mean frame time and the
worst one.

Two things use it. The first is "does this survive being looked at": a fault
that needs a few hundred frames to appear does not show up in a tool that draws
forty and exits.

```
WOWEE_VULKAN_VALIDATION=1 ./build/bin/capture_scene -d Data -m Azeroth     -c -9462,-67,70 -t -9200,-320,50 --dwell 300 -o /tmp/soak
```

The second is frame time before and after a change. It is the same frame every
time, so the number is comparable in a way one taken while walking is not.

On Windows the validation layers are found through `VK_LAYER_PATH`, and what
matters is the `Bin` directory of the SDK that is actually installed. A registry
entry left behind by an SDK that has since been removed makes the loader report
that it cannot open the layer manifests and then run **without validation** -
which reads exactly like a clean run.

```
VK_LAYER_PATH='<VulkanSDK>/<version>/Bin' WOWEE_VULKAN_VALIDATION=1 ...
```

## A pair

`--setting key=value` is applied through the same path the settings panel uses,
before the first frame is drawn. It is repeatable:

```
./build/bin/capture_scene -d Data -m Azeroth \
    -c -9462,-67,70 -t -9200,-320,50 --time 9 \
    --setting shadowcascades=0 --setting shadowfilter=0 \
    -o before
```

The keys are the ones in `include/ui/settings_schema.hpp`, and the values are
what is written to `settings.cfg`: for an enum that is the **index**, not the
label, so `shadowcascades=0` is one cascade and `=3` is four.

`--wireframe` draws the world as lines, which is how a terrain LOD change is
looked at: the shaded picture is meant to be nearly the same one, and the
wireframe is where the triangles went.

## Both at once, with the difference

```
python tools/compare_scenes.py \
    --data Data --map Azeroth \
    --camera=-9462,-67,70 --target=-9200,-320,50 --time 9 \
    --setting shadowcascades --off 0 --on 2 \
    --out docs/evidence/phase-01/img/goldshire-lake-cascades
```

Note the `=` on `--camera` and `--target`: a value that starts with a minus
sign is read by argparse as an option name unless it is attached that way.
capture_scene's own parser takes `-c -9462,-67,70` either way.

The command writes `.before.png`, `.after.png` and `.diff.png` - a heat map, black where
nothing moved and white where it moved most - and prints the fraction of pixels
that changed, the mean absolute difference, and SSIM.

`--also key=value` applies a setting to **both** renders, for holding something
steady that is not the thing under test.

Two PNGs that already exist can be compared without rendering anything:

```
python tools/compare_scenes.py --before a.png --after b.png --out out/pair
```

Pillow is required. numpy is used if it is installed; without it the SSIM is
computed in pure Python over a 320-wide reduction and the tool says so rather
than printing a number that looks like the other one.

## Cameras worth keeping

These are the ones phase 01's evidence was rendered from, and they are known to
put something in frame rather than a wash of fog.

| Name | Camera | Target | Time | For |
|---|---|---|---|---|
| goldshire-lake | `-9462,-67,70` | `-9200,-320,50` | 9 | ground, trunks, water and a horizon - shadows and terrain LOD |
| goldshire-road | `-9462,-67,62` | `-9350,-30,55` | 9 | a road and a building at eye height - shadow edges |

A camera whose target differs from it only in z points straight down and renders
as a flat wash: check a new camera with one shot before rendering a pair from
it.

## When it will not run

- **"data path does not exist"** - `-d` is wrong, or is missing `manifest.json`.
- **"the client could not initialise"** - no display, no Vulkan device, or the
  usual start-up failure. `logs/wowee.log` says which.
- **A grey or empty frame** - the world did not finish streaming. The tool draws
  240 frames and then waits for the terrain manager to go quiet, up to 900; a
  zone on a cold file cache can want longer than that.
