# Original game UI and drop-in client plan

Status: planning only. No implementation is included in this commit.

Branch: `codex/native-game-ui`, created directly from `master` at
`3f92198677e4d7c0e59560f36fcfc72da1d0fa4b`.
The existing `codex/fork-roadmap` branch remains separate. Its fixes are not
implicitly included; review and port individual prerequisites when needed.

## Intended experience

Place `wowee.exe` beside the original game executable and launch it. Wowee
discovers the installation, reads its existing game archives, presents the
original login and character screens, enters the world with the original HUD,
and supports the installation's addons. No extraction tool, generated asset
manifest, developer checkout, environment variables, or asset conversion step
is required from the player.

Use the installation's original Lua, XML, textures, fonts, sounds, and models.
This includes **GlueXML** before world entry and **FrameXML** in the world.
Rendering these files is only part of completion: their APIs, events, input,
settings, and lifecycle must perform the requested actions.

All game versions currently supported by Wowee are implementation and acceptance
targets from the start. None is deferred to a later compatibility milestone:

| Profile | Target game version | Build contract |
| --- | --- | --- |
| `classic` | Vanilla 1.12.1 | 5875 |
| `tbc` | The Burning Crusade 2.4.3 | 8606 |
| `wotlk` | Wrath of the Lich King 3.3.5a | 12340 |
| `turtle` | Turtle WoW 1.18.x (profile: 1.18.1) | Auth 7272 by default; world 5875; preserve supported auth-build override |

Every delivery stage below applies to every row: direct archive loading,
original GlueXML/FrameXML, settings, addons, and the drop-in executable journey.
Use each installation's own UI files and build-specific APIs, event conventions,
archive precedence, asset formats, and addon metadata. Do not substitute WotLK
files or assume its API contracts apply to Vanilla, TBC, or Turtle.

The existing WotLK controlled server may provide an early smoke test, but a
WotLK pass cannot close a multi-version task. Establish matching installations
and server fixtures for all four profiles; missing fixtures leave acceptance
open. Include Turtle's custom patches and interface changes explicitly.

The repository has a Cataclysm movement-sequence file but no complete `cata`
expansion profile, and README does not advertise Cataclysm as supported. Track
it as an unsupported future version, not as existing support or a completed
target. Newly supported profiles must be added to this acceptance matrix.

## Current baseline

- Upstream already provides a Lua engine, XML emitter, widget tree/renderer,
  addon manager, and default-enabled in-world FrameXML ownership.
- Login, character selection/creation, loading, and settings still have native
  implementations. The widget draw path is gated to `IN_GAME`.
- `AssetManager` requires extracted files and a manifest. FrameXML and addons
  are loaded through filesystem directories. StormLib is currently associated
  with extraction tooling rather than the main runtime.
- Keep and extend these implementations where appropriate. Do not create a
  second independent Lua/XML engine or duplicate every original screen in C++.

## Delivery sequence

### 1. Establish the installation and archive contract

- [ ] Inventory source assumptions about manifests, loose files, converted
  formats, DBC/profile data, directory enumeration, and disk-only loaders.
- [ ] Define a game-file provider with normalized virtual paths, byte reads,
  existence checks, source identity, and enumeration where actually needed.
  Route UI reads through it first, then every asset needed for world entry.
- [ ] Detect the installation relative to the executable, independent of the
  working directory; identify supported build and locale without extraction.
  Keep an explicit path override for development and alternate installs.
- [ ] Verify automatic detection for all four profiles, including distinguishing
  Turtle installations from stock Vanilla. Bundle every supported profile in
  the same executable; switching installations must not require a rebuild.
- [ ] Implement direct MPQ reads in the client. Audit existing extractor archive
  discovery for reuse; verify base, expansion, patch, and locale precedence,
  patched/deleted files, case handling, and missing/corrupt archive diagnostics.
  Do not depend on a complete archive listfile for known-path lookups.
- [ ] Define loose-file precedence separately for `Interface/AddOns` and game
  resources. Test collisions against the target build's intended behavior.
- [ ] Support native game formats end to end, including UI/model/audio/DBC and
  terrain data; inventory any current conversion dependency and implement its
  runtime equivalent. Optional caches must be disposable and built on demand,
  not a hidden mandatory bulk extraction phase.
- [ ] Keep archive access read-only and thread-safe. Store caches and logs in a
  Wowee-specific location with bounded growth and build-aware invalidation.

Acceptance: launch and resolve representative assets from an untouched game
installation with no extracted tree or manifest, including a patched resource
and locale-specific UI file. Existing loose-file development mode still works.

### 2. Make the original UI runtime work across application states

- [ ] Load ordered TOC/XML/Lua dependencies and relative includes through the
  provider, including shared UI files, GlueXML, FrameXML, and Blizzard addons.
- [ ] Define Glue and world Lua lifecycle boundaries from the target files:
  initialization, event ordering, state transitions, logout, reconnect, and
  reload. Prevent stale world widgets and callbacks leaking onto login screens.
- [ ] Audit required globals, functions, widget methods, templates, scripts,
  animations, fonts, anchors, clipping, layers, model widgets, and focus/input.
  Implement observable behavior instead of satisfying calls with silent stubs.
- [ ] Integrate UI update, rendering, and input routing in every relevant state;
  retain native rendering for surfaces the original client itself supplies.
- [ ] Capture structured Lua errors and missing API calls. Runtime acceptance
  uses missing-API fallback disabled and exercised interaction paths.

Acceptance: original files load and draw at supported resolutions/UI scales,
with reproducible input tests and no missing-call errors on the tested paths.

### 3. Complete original GlueXML screens

- [ ] Wire original login, realm selection, connection progress, errors,
  cancellation, and disconnect dialogs to real authentication/network state.
- [ ] Wire character list, selection, preview models, creation/customization,
  deletion confirmation, addon selection, and Enter World to real client state.
- [ ] Render installation-provided loading art and progress through the native
  loading mechanism where the original files do not define a Lua/XML screen.
- [ ] Verify return journeys: failed login, cancelled connection, failed world
  entry, logout to character select, and disconnect back to login.

Acceptance: the normal login-to-world journey uses original UI definitions
where supplied; native replacement menus are no longer its default path.

### 4. Complete FrameXML world UI and settings

- [ ] Inventory every shipped top-level panel and load-on-demand Blizzard UI
  addon. Track rendering, input, backing APIs/events, and runtime validation
  independently; loading a file alone does not complete a panel.
- [ ] Verify HUD, unit frames, actions, chat, bags, character/spells/talents,
  quests, maps, social/group UI, and server-dependent interaction panels.
- [ ] Replace redirects to the custom settings window with functional original
  video, sound, interface, and key-binding controls. Map CVars/settings to real
  engine behavior, including apply/cancel/defaults and persistence.
- [ ] Handle options unavailable in Wowee explicitly in the original settings
  flow; never silently report an unsupported setting as applied.
- [ ] Preserve native content inside original frames where required, including
  minimap, map, model previews, and loading progress; avoid overlapping UIs.

Acceptance: a panel-by-panel matrix includes successful interaction and state
changes, settings persistence after restart, and pending gaps. Full UI support
is complete only when every in-scope row passes or its limitation is agreed.

### 5. Support installed addons

- [ ] Discover `Interface/AddOns` beside the original client and implement TOC
  metadata, dependencies, optional dependencies, load ordering, load-on-demand,
  enable/disable, and build-appropriate compatibility handling.
- [ ] Implement addon events, timers, hooks, slash commands, saved variables,
  reload, and build-appropriate secure/protected UI behavior needed by addons.
- [ ] Keep Wowee addon preferences and saved-variable writes separate from the
  original client's WTF data by default. Define optional import explicitly so
  launching either client does not overwrite the other's settings.
- [ ] Validate representative unmodified addons: a simple frame, event-driven
  HUD, bags/action bars, configuration UI, and a load-on-demand dependency.
  Publish a tested compatibility matrix; do not promise arbitrary addon parity.
  Use addon releases intended for each target version and record that version
  in every result; an addon working on WotLK proves nothing about its older port.

Acceptance: addons run from their existing folders, survive logout/reload and
restart, and save their state without modifying the original client's files.

### 6. Deliver and verify the drop-in executable

- [ ] Embed required Wowee profiles, shaders, bootstrap scripts, and internal
  resources. Remove source-tree and Vulkan SDK path dependencies at runtime.
- [ ] Audit/link/package runtime dependencies to meet the single-executable
  placement goal. Any unavoidable companion requirement must be resolved or
  explicitly recorded as an unmet goal, not hidden in developer setup.
- [ ] Detect supported installations automatically and show useful startup
  errors for missing/corrupt files, unsupported builds, or graphics failures.
- [ ] Validate on a clean Windows machine without development tools: copy only
  `wowee.exe` beside the original executable, launch from another working
  directory, log in, select/create a character, enter the world, use settings
  and addons, logout, restart, and launch the original client afterward.
- [ ] Verify no archive modifications, original configuration changes, or
  required extraction output; repeat with a read-only game-data directory.
- [ ] Run the complete drop-in journey against Vanilla, TBC, WotLK, and Turtle
  separately, including their own original UI, settings, and compatible addons.
  Record build, locale, patch set, and server fixture for each run. Completion
  requires all four profiles to pass, not just the first working installation.

## Implementation and evidence rules

- Begin with archive-backed UI loading and GlueXML, then complete FrameXML
  behavior. Broader roadmap features are deferred unless needed by this path.
- Use focused commits per milestone. Review selected fixes from the older fork
  as explicit prerequisites, particularly known world-entry/resize crashes;
  never merge its entire history into this branch by accident.
- Test provider precedence and lifecycle contracts with small fixtures; pair
  runtime UI tests with screenshots, input traces, build identity, and logs.
  Loading success and screenshots alone do not prove functional UI behavior.
- Use local licensed game files for integration tests; do not commit game
  archives, extracted Blizzard code/art, account credentials, or private logs.
- Mark tasks complete only with evidence. Document untested versions, panels,
  and addon behaviors separately from confirmed failures and completed work.

This commit authorizes no implementation work in this session: it records the
requested plan only. Implementation begins in a later user-directed step.
