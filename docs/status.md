# Project Status

**Last source review**: 2026-09-08, fork revision `51277f5f3`.

This page is a source/historical inventory, not a release certification.
The fork's initial target is WotLK 3.3.5a/build 12340; live gameplay, stock
fallback-off UI and other expansion certification remain tracked in the
[fork roadmap](fork-roadmap.md). The [capability ledger](capability-ledger.md)
separates registrations, defaults, unresolved candidates and actual evidence.

## What This Repo Is

Wowee is a native C++ World of Warcraft client experiment focused on connecting to real emulator servers (online/multiplayer) with a custom renderer and asset pipeline.

## Current Code State

Historical implementation inventory (runtime claims below are unverified for
the current fork and controlled server; retain them as navigation, not acceptance):

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
- Historical expansion of unit tests covering chat, world map, spline math,
  transport and animation; current counts must come from configured CTest
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
- Test registration expanded; the former fixed 89-suite count is historical.
  Use `ctest --test-dir <build> -N` (plus `-C Debug` for a multi-configuration
  build) for that configuration's manifest, then execute it to establish results
- macOS: SIGPIPE is ignored at startup, so a send to a dropped connection no longer terminates the client; crash backtraces now work there as well as on Linux

In progress / known gaps:

- Source-verified corrections: FrameXML is enabled by default; explicit
  `WOWEE_LUA_API_FALLBACK=0` overrides its implied fallback. EditBox selection
  and SDL clipboard operations already exist. ImGui supports dynamic font
  sizes; startup metrics still have a character-count fallback, and additive
  UI textures still approximate blending with brightness-derived alpha.
  See [widget source evidence](widget-system.md).
- The static interface run reports 49 unresolved API candidates, including a
  proven local-helper false positive. All 30 WotLK opcode-map warnings have
  source dispositions, but this does not certify their live behavior.
  Reproduction commands and remaining gates are in the [ledger](capability-ledger.md).
- Pure tests now have a separate [headless configuration](headless-tests.md)
  requiring no Vulkan SDK, SDL, game assets or server. Full-client and GPU
  results remain separate; count and execute tests in the selected build.

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
