# Project Status

**Last updated**: 2026-09-10

## What This Repo Is

Wowee is a native C++ World of Warcraft client experiment focused on connecting to real emulator servers (online/multiplayer) with a custom renderer and asset pipeline.

## Current Code State

Implemented (working in normal use):

- Auth flow: SRP6a auth + realm list + world connect with header encryption
- Rendering: terrain, WMO/M2, water/magma/slime (FBM noise shaders), sky system, particles, shadow mapping, minimap/world map, loading video playback
- Instances: WDT parser, WMO-only dungeon maps, area trigger portals with glow/spin effects, zone transitions
- Character system: creation (including nonbinary gender), selection, 3D preview with equipment, character screen, per-instance NPC hair/skin textures
- Core gameplay: movement (with ACK responses), targeting (hostility-filtered tab-cycle), combat, action bar, inventory/equipment, chat (tabs/channels, emotes, item links)
- Quests: quest markers (! and ?) on NPCs/minimap, quest log with detail queries/retry, objective tracking, accept/complete flow, turn-in, quest item progress
- Trainers: spell trainer UI, buy spells, known/available/unavailable states
- Vendors, loot (including chest/gameobject loot), gossip dialogs (including buyback for most recently sold item)
- Bank: full bank support for all expansions, bag slots, drag-drop, right-click deposit
- Auction house: search with filters, pagination, sell picker, bid/buyout, tooltips
- Mail: item attachment support for sending
- Spellbook with specialty/general/profession/mount/companion tabs, drag-drop to action bar, spell icons, item use
- Talent tree UI with proper visuals and functionality
- Pet tracking (SMSG_PET_SPELLS), dismiss pet button
- Party: group invites, party list, out-of-range member health (SMSG_PARTY_MEMBER_STATS)
- Nameplates: NPC subtitles, guild names, elite/boss/rare borders, quest/raid indicators, cast bars, debuff dots
- Floating combat text: world-space damage/heal numbers above entities with 3D projection
- Target/focus frames: guild name, creature type, rank badges, combo points, cast bars
- Map exploration: subzone-level fog-of-war reveal
- Warden anti-cheat: full module execution via Unicorn Engine x86 emulation; module caching
- Audio: ambient, movement, combat, spell, and UI sound systems; NPC voice lines for all playable races (greeting/farewell/vendor/pissed/aggro/flee)
- Bag UI: independent bag windows (any bag closable independently), open-bag indicator on bag bar, server-synced bag sort, off-screen position reset, optional collapse-empty mode in aggregate view
- DBC auto-detection: CharSections.dbc field layout auto-detected at runtime (handles stock WotLK vs HD-textured clients)
- Multi-expansion: Classic/Vanilla, TBC, WotLK, and Turtle WoW (1.18) protocol and asset variants
- CI: GitHub Actions for Linux (x86-64, ARM64), Windows (MSYS2 x86-64 + ARM64), macOS (ARM64); container builds via Podman

Recent refactors (PRs #59-63, April 2026):

- Chat system decomposed into 15+ modules under `src/ui/chat/` with 11 command modules, GM command support, macro evaluator, and tab completion
- World map decomposed into 16 modules under `src/rendering/world_map/` with overlay layer system, view state machine, and ZMP-based hover detection
- TransportManager decomposed: spline math extracted to `src/math/`, path data to TransportPathRepository, 7 duplicated spline parsers consolidated into `spline_packet.cpp`
- Spell visual effects system with bone-tracked ribbons and particles
- Entity movement improvements: multi-segment path interpolation, terrain height clamping, walk/run animation fix
- 31 unit-test suites (up from 8), covering chat, world map, spline math, transport, and animation systems
- Code quality fix pass: 7 issues resolved across hover detection, null safety, buffer bounds, and coordinate validation

Recent fixes (July 2026):

- Login pipeline hardened: login-critical opcodes have hardcoded fallback when opcode table lookup fails; OpcodeTable::loadFromJson() is now safe against failed reloads (issue #87)
- Integrity hash is build-aware: Classic-era DLLs only required for builds <=6005 or Turtle; TBC/WotLK hash only the .exe
- Strafing reworked: torso-twist via SpineLow bone rotation instead of dedicated strafe animations
- Camera smoothing snaps 1:1 during active drag/keyboard turn to reduce input lag
- Mount strafing uses MOUNT_RUN_LEFT/RIGHT when available

Recent work (August 2026):

- FrameXML interface transition: the original interface owns the chat window, and this client's own was removed along with the tab manager and completer that served it. The command registry, macro evaluation, and chat bubbles stay - FrameXML's edit box routes unknown slash commands into `runClientChatCommand`. See the Unreleased section of `CHANGELOG.md`
- Warnings are errors: `WOWEE_WARNINGS_AS_ERRORS` (default ON) puts `-Werror` / `/WX` on the `wowee` target. Turn it off for a bisect or an unfamiliar compiler
- AMD FidelityFX SDK backends are off by default (`WOWEE_ENABLE_AMD_FSR2`, `WOWEE_ENABLE_AMD_FSR3_FRAMEGEN`). This client's own FSR 1 and `fsr2_*` compute shaders are in-tree and unaffected
- 89 test suites registered with CTest, up from the 31 noted above
- macOS: SIGPIPE is ignored at startup, so a send to a dropped connection no longer terminates the client; crash backtraces now work there as well as on Linux

Modern rendering, phase 01 (September 2026):

- The four lit fragment shaders were consolidated once, before anything was added to them: the shadow filter, the fog ramp and the parallax march are `assets/shaders/*.glsl` includes now (`shadow_common.glsl`, `fog.glsl`, `parallax.glsl`, gathered by `lit_common.glsl`), and the per-frame uniform block grew once - cascade matrices, height-fog parameters and SH9 ambient slots - rather than once per technique
- Every renderer toggle is a specialization constant rather than a uniform `int` and an `if`. The defaults are what the client did before, so a pipeline built with no `VkSpecializationInfo` is the shader that shipped. `tests/shader_offpath_identity` measures that rather than asserting it: it compiles the archived pre-phase source and the current one, freezes the constants at their defaults, and compares what the two actually compute
- `VkContext::getRenderCaps()` reports a capability tier and a flag per optional feature, and a settings row the GPU cannot honour is greyed with the reason instead of being silently ignored
- Exponential height fog with a sun-coloured in-scatter term, calibrated against each zone's own Light.dbc fog end so the horizon does not move and no zone shifts hue. `fogmodel` and `fogaerial` on the Graphics page; the model is a shader variant, so changing it rebuilds the four lit renderers' pipelines between frames, the way an anti-aliasing change already does
- The sun's shadow is drawn in one to four cascades. Each is fitted to the bounding sphere of its own slice of the view frustum at the practical split (lambda 0.7) and snapped to its own texel grid, casters are culled per cascade, and the boundary between two of them is cross-faded rather than drawn as a line. One cascade is the single map the client always had, and the shader compiled for it is instruction-for-instruction the one that shipped - which is what makes "off" mean something. `shadowcascades` on the new Shadows page, three by default
- Shadows can be switched off again. The device loss that took the row off the panel was never diagnosed because the validation layer was never actually loading - a stale `VK_LAYER_PATH` registry entry for an uninstalled SDK, which makes the loader report that it cannot open the manifests and then run without them. With the layer loaded, off is clean for five minutes in the world. The pass still clears and transitions the map once on the frame the switch flips before it stops drawing, because a depth image left in a layout its descriptors disagree with is wrong regardless. `shadows` is a live row again, on its own Shadows page, and `Renderer::setShadowsEnabled` stores the value
- `shadowfilter`: 3x3 PCF (what shipped), a sixteen-tap Poisson disc rotated per pixel by interleaved-gradient noise, or PCSS with a sixteen-tap blocker search on the two near cascades. `shadowlightsize` is the penumbra scale. Both are specialization constants, read only on the cascaded path
- Terrain chunks past 12, 30 and 60 percent of the view distance drop to 81, 25 and 9 vertices from 145, out of three index sets built once and shared by every chunk, with a skirt hanging from each chunk's outer ring so two neighbours at different levels cannot open a gap. `terrainlod`, and a column on every preset. `tests/test_terrain_lod` pins the watertightness and the one-level-apart property
- The in-world device loss that stopped the second pass at this phase was the capture tool, not the branch: `Renderer::endFrame` replays `ImGui::GetDrawData()` unconditionally and that answers the last draw data *built*, so a tool that drew the world without opening an ImGui frame re-submitted the world loader's last loading-screen frame - whose image had been destroyed with the `LoadingScreen` on that loader's stack - every frame until the driver gave up. Master at `c00ab904e`, with the same tool, fails identically. The tool opens an empty ImGui frame per frame now and `LoadingScreen` hands its descriptor set back to ImGui instead of leaking one per zone load. `docs/evidence/phase-01/README.md` has the whole of it, including why the validation layer had been silent: a `VK_LAYER_PATH` registry entry for an SDK that was no longer installed
- `tools/capture_scene` renders one frame of the client from the command line - map, camera in server coordinates, time of day, and any number of `--setting key=value` - and `tools/compare_scenes.py` renders a pair and writes a heat map with the changed-pixel fraction and SSIM. `-DWOWEE_BUILD_CAPTURE_SCENE=ON`; `howtos/capture-scene-screenshots.md`. `--install` points it at a game folder and it reads the archives there, so no extraction step is needed; the shot is taken on a fixed frame and the doodad animation phases are pinned, because two renders of one camera have to be the same frame; `--dwell` reports the GPU's own per-pass timestamps. §7.5 of the plan describes this tool as already existing; it did not, and had to be written
- Lengyel's tangent basis is `include/rendering/tangent_frame.hpp` now, pure and tested, rather than a loop inside the character renderer that needed a Vulkan device to reach
- Normal maps reach the doodads and the ground. The M2 GPU vertex carries a `vec4` tangent from Lengyel's solve over the model's first UV set, and the terrain vertex carries one derived analytically from the chunk grid, where the texture coordinates are a fixed scale of the world position. `NormalMapCache` derives a map from each texture's own luminance on a worker thread - strength 3 for a doodad, 2 for a tileset, matching what the character and WMO paths already use - caps the source at 512 on each axis, drops a map whose height variance is below what the WMO shaders gate POM on, and writes the rest to `Data/generated/<expansion>/normals/<hash>.rgba` so the next run reads them instead. Until a map arrives the material samples the flat-normal fallback, so a surface seen for the first time is flat and then bumps; the descriptor write that swaps it in is deferred until every frame slot has been fenced, because a set a recorded command buffer still names must not be written under it. `normalmapscope` on the Detail page, `Everything` by default and `Buildings and characters` on Low. The scope is a specialization constant whose GLSL default is off, which is what keeps `m2.frag` and `terrain.frag` instruction-for-instruction the modules that shipped
- Screen-space sun shafts. A half-resolution `R8` mask of where the sun is visible, two thirty-two-tap radial blurs toward its place on screen, and an additive composite, gated on the lens flare's own sun visibility so the two agree. `sunshafts` and `sunshaftstrength` under Effects; off on Low. The passes run between the scene pass and the interface pass rather than inside the post pass the phase file names, because with no upscaler running - which is every preset - the scene is drawn multisampled straight into the swapchain and there is no point during it at which a full-screen shader can read what has been drawn. See the header of `include/rendering/sun_shafts.hpp`
- **Cut:** geomorph, which is the first item in the phase file's own cut order, and parallax occlusion mapping on the terrain, which is half of the second. The ground gets its normal maps and not the march for relief on top of them. See `docs/evidence/phase-01/README.md`
- **Verified on a GPU, from the game's own archives.** The client reads an installation's MPQ files directly when its data directory holds no `manifest.json`, so nothing has to be extracted; `capture_scene --install <game folder>` drives that path, and every phase-01 technique has now been rendered, compared and measured through it. Four defects that only a loaded world could show were found and fixed - the deferred normal-map descriptor write was landing while command buffers still named the set, PCSS was rendering as Poisson because its penumbra was computed in normalized depth, the capture tool was putting the player model on the lens, and two renders of one camera were not the same frame. `docs/evidence/phase-01/README.md` has the numbers, the noise floor each is measured against, and the two rows that are still not what the phase file asked for: terrain LOD at Tanaris changes 4.4 % of pixels against a 0.5 % threshold, and the height model's horizon at Westfall is ΔE 9 from the linear one against a rule of 3

In progress / known gaps:

- World map: zone hover detection has edge cases with some zone boundaries; cosmic highlight sizing is approximate
- Transports: M2 transports (trams) working with position-delta riding; WMO transports (ships, zeppelins) working with path following; some edge cases remain
- Quest GO interaction: CMSG_GAMEOBJ_USE + CMSG_LOOT sent correctly, but some AzerothCore/ChromieCraft servers don't grant quest credit for chest-type GOs (server-side limitation)
- Visual edge cases: some M2/WMO rendering gaps (some particle effects)
- Water refraction: enabled by default; srcAccessMask barrier fix (2026-03-18) resolved prior VK_ERROR_DEVICE_LOST on AMD/Mali GPUs
- Fixed 2026-05-15: classic/turtle update-field tables had multiple wrong indices (`UNIT_FIELD_BYTES_1`=133 colliding with `UNIT_FIELD_MOUNTDISPLAYID`=133; STAT0..4 at 138..142; RESISTANCES at 154; missing NATIVEDISPLAYID). Corrected against vmangos `UpdateFields_1_12_1.h`: BYTES_1=138, STAT0..4=150..154, RESISTANCES=155, added NATIVEDISPLAYID=132.

## Where To Look

- Entry point: `src/main.cpp`, `src/core/application.cpp`
- Networking/auth: `src/auth/`, `src/network/`, `src/game/game_handler.cpp`
- Rendering: `src/rendering/`
- Assets/extraction: `extract_assets.sh`, `tools/asset_extract/`, `src/pipeline/asset_manager.cpp`
