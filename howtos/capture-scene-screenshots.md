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

**Nothing, if the game is installed.** The tool reads the installation's own MPQ
archives, exactly as the client does when it is dropped into a game folder:
point `--install` at the game directory and `-d` at a data directory that holds
only what wowee generates. No extraction step at all.

The log says which of the two paths a run took, and it is worth reading before
trusting a picture:

```
[INFO ] Found game installation: G:\WoW AzerothCore (wotlk, locale enUS, 18 archives)
[INFO ] No manifest in D:\wowee-phase01\Data; reading the installation's archives directly
[WARN ] Interface fonts loaded: 5 of 5 from the archives
[INFO ] Online terrain streaming complete: 49 tiles loaded
```

`--install` is the same thing as setting `WOW_INSTALL_PATH`, and `-d` the same
as `WOW_DATA_PATH`; both go through `Application::initialize`, which calls
`pipeline::detectGameInstall` and hands the archives to the asset manager before
it initializes. Direct archive reads need StormLib at build time - check the
CMake line said `wowee direct MPQ reads: ENABLED`, because without it the client
logs `AssetManager: this build cannot read MPQ archives directly` and then wants
an extracted tree after all.

An extracted tree still works and is still read in preference to the archives: a
data directory with a `manifest.json`, which `asset_extract` produces from them.

```
./build/bin/asset_extract /path/to/WoW/Data /path/to/extracted
```

## The frame a shot is taken on

Every render draws the same number of frames - `kShotFrame`, 900 - and the shot
is the last of them. That is not tidiness. The clouds drift, the water moves and
the trees sway by the fixed 1/60 second this tool hands `Renderer::update`, so
the phase of all of it is a function of the frame count and nothing else, and
two renders that stopped at different counts are two different pictures. Before
this was pinned, a camera with sky in it read 94 % of pixels changed between two
renders whose only intended difference was one setting. The log says where the
streamers actually went quiet:

```
[INFO ] capture_scene: streamers went quiet at frame 406; the shot is frame 900
```

The doodads' animation phases are pinned too, through `WOWEE_M2_ANIM_SEED`,
which this tool sets and nothing else does: a player wants a stand of trees out
of step and a comparison wants them identical.

**There is still a noise floor**, because the number of frames the world loader
draws before any of this is not fixed. Render the same camera twice with the
same settings before trusting a small number - `--setting key --off X --on X` in
`compare_scenes.py` does exactly that. At the Goldshire lake camera that control
reads 0.2 % of pixels changed; at a camera looking down onto a moving canopy it
reads 26 %. `docs/evidence/phase-01/README.md` lists the measured floors.

## One picture

```
./build/bin/capture_scene \
    --install "G:/WoW AzerothCore" \
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
worst one - and, beside them, the GPU's own timestamps for the passes that carry
a mark:

```
  dwell: 1748 frames in 30.0056s - mean 17.1657ms, worst 26.6532ms
  gpu post-process: 5.88121ms over 1748 frames
  gpu sun-shafts: 0.0897873ms over 1748 frames
  gpu interface: 0.024827ms over 1748 frames
```

Those are the numbers to quote for a pass that costs a fraction of a
millisecond. Differencing two whole-frame means will not find it: at these
cameras the mean moves by more than a millisecond between two runs of the same
configuration, which is ten times the thing being measured. The scene pass
records into secondary command buffers and cannot be timestamped from the
primary, so there is no mark inside it.

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
steady that is not the thing under test. `--angles=pitch,yaw,roll` aims the
camera by direction instead of `--target`, which is what a shot facing the sun
needs.

The installation is taken from `WOW_INSTALL_PATH`: the script passes no
`--install` of its own, so export it once for a whole run of comparisons.

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

| Name | Camera | Target or angles | Map | Time | For |
|---|---|---|---|---|---|
| goldshire-lake | `-9462,-67,70` | `-9200,-320,50` | Azeroth | 9 | ground, trunks, water and a horizon - shadows and normal maps |
| goldshire-road | `-9462,-67,62` | `-9350,-30,55` | Azeroth | 9 | a road and a building at eye height - shadow edges |
| stormwind-gate | `-8950,650,120` | `-8700,650,95` | Azeroth | 9 | stone walls and roofs - the building normal-map path |
| westfall-sentinel-hill | `-10640,1030,55` | `-10640,1700,20` | Azeroth | 9 | an open horizon over hills - terrain LOD and fog |
| tanaris-dunes | `-7150,-3780,45` | `-8200,-3900,0` | Kalimdor, `--map-id 1` | 9 | bare dunes to the horizon - terrain LOD with almost nothing animated |
| duskwood-road | `-10520,-1170,90` | `-11200,-1170,40` | Azeroth | 5.5 | a wooded valley before dawn - height fog |
| elwynn-sun-0700 | `-9462,-67,120` | `--angles 9.7,129.5,0` | Azeroth | 7 | the sun in open sky over the canopy - sun shafts |
| elwynn-sun-0630 | `-9462,-67,120` | `--angles 5.05,132.4,0` | Azeroth | 6.5 | the sun behind the ridge - sun shafts with an occluder |

**Where the sun is, is arithmetic rather than a hunt.** `LightingManager` puts
the sun direction at `-normalize(sin a * 0.6, -0.6 + cos a * 0.4, cos a * 0.6)`
for `a = 2*pi*hour/24`, in the same axes the camera's yaw and pitch are measured
in - so a camera facing it at hour `h` has `yaw = atan2(y, x)` and
`pitch = degrees(asin(z))` of that vector. That is 129.5 and 9.7 degrees at
07:00, 123 and 17.6 at 08:00, 115.7 and 23.7 at 09:00. Add 180 to the yaw for a
shot with the sun behind the camera, which is how "this effect costs nothing
when the sun is not there" gets measured.

A camera whose target differs from it only in z points straight down and renders
as a flat wash: check a new camera with one shot before rendering a pair from
it.

## When it will not run

- **"data path does not exist"** - `-d` names a directory that is not there.
- **An empty grey world, and `manifest.json not found` in the log** - neither
  source is available: no manifest under `-d`, and no readable installation.
  Either `--install` is wrong, or this build has no StormLib and cannot open
  archives at all.
- **"the client could not initialise"** - no display, no Vulkan device, or the
  usual start-up failure. `logs/wowee.log` says which.
- **A grey or empty frame** - the world did not finish streaming. The tool draws
  240 frames and then waits for the terrain manager to go quiet, up to 900; a
  zone on a cold file cache can want longer than that.
