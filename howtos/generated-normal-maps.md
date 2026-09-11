# The generated normal maps, and the directory they live in

Buildings and characters have always been normal-mapped in this client, from a
map derived at load time out of the texture's own luminance. Phase 01 gave the
same treatment to M2 doodads and to the ground, and moved the derivation off the
render thread and onto disk, because there are a few hundred wall textures in a
zone and several thousand doodad skins.

This is what that means for anyone running the client, looking at a frame that
seems wrong, or clearing space.

## Where the files are

    <data>/generated/<expansion>/normals/<hash>.rgba

`<data>` is the directory the client was given — the same one that holds
`manifest.json`. `<expansion>` is what the manifest records, or `base` when it
records nothing: two expansions can hold two different textures at one path, and
a map is a statement about the texture rather than about the path.

`<hash>` is a 64-bit FNV-1a over the source texture's own bytes with the
gradient strength folded in, in hex. Nothing lists these files; a map is found
by hashing what it would have been made from, so:

- a texture that changes gets a different name rather than a stale map;
- the same texture read at two strengths — 3 for a doodad, 2 for a tileset — is
  two files, because it is two maps;
- deleting the directory costs nothing but the time to derive them again.

Each file is an eight-byte header (`uint16` width, `uint16` height, `float`
height variance) followed by `width * height * 4` bytes of RGBA: the normal in
RGB, and the blurred height in alpha for the parallax march.

The directory is held under two gigabytes. When it is over, the oldest files go
first — oldest by last write, because nothing records when a map was last read
and both platforms this runs on have access times off by default.

## What you will see the first time

A model or a chunk of ground seen for the first time in a session is **flat for
a frame or two and then bumps**. That is the design rather than a fault: the map
is derived on a worker thread, and the alternative is a stall on the render
thread per new doodad. Until it arrives the material samples a flat
128,128,255 pixel, which is the surface exactly as it was without normal
mapping. The settings tooltip for `Bumps on` says so.

On the second run of the same zone the maps are read back from disk instead of
derived, so the refining is shorter and mostly invisible.

## Turning it off

Settings → Detail → Surfaces:

- **Surface bumps (normal mapping)** off turns the whole thing off, everywhere.
- **Bumps on** chooses `Buildings and characters` — which is every normal map
  this client had before phase 01 — or `Everything`. On `Buildings and
  characters` no map is ever asked for on a doodad or a tileset, so no worker
  runs and nothing is written to disk.
- **Bump strength** scales how much of the map is mixed into the surface
  normal; **Surface depth** and **Surface depth quality** control the parallax
  march, which runs on opaque doodad surfaces and on buildings.

Quality preset Low chooses `Buildings and characters`. The other three choose
`Everything`.

## If a surface looks lit from the wrong direction

A normal map is read in a tangent frame, and a frame that disagrees with the one
the map was authored against lights the surface from the wrong side. The three
frames in this client are derived in one place,
`include/rendering/tangent_frame.hpp`:

- `computeTangentFrames()` — Lengyel's solve, for characters and M2 doodads,
  over the model's **first** UV set. The second is the environment-map
  coordinate and a map read in it will be wrong.
- `gridTangent()` — the analytic frame for the terrain, from the two world axes
  the texture coordinates run along. For this client's terrain those are world
  −Y and world −X, which gives a handedness of −1.

A whole surface class lit from the wrong vertical direction is a handedness
sign; one model out of many is that model's UVs.

## Clearing it

Delete the directory. Nothing indexes it and nothing else reads it.

    rm -rf <data>/generated/<expansion>/normals

Phase 07 of the modern-rendering plan puts a manifest over `Data/generated`
and will index this directory through it; the markers that say so are on the
code, in `include/rendering/normal_map_cache.hpp` and beside the hash itself.
Until then the hash is the whole of the lookup.
