# Original game UI and drop-in client plan

Status, 2026-09-09. What an untouched installation now gets: wowee finds it
beside its own executable, reads its archives directly, and loads the
installation's own FrameXML, its Blizzard addons and its GlueXML out of them,
with no extracted tree, no manifest and no environment variable. Stage 1 and
stage 2's first bullet are done and evidenced; stage 3's first bullet is done
and the bindings behind those screens' buttons are not; parts of stages 5 and 6
landed alongside. Stage 4 is untouched.

Every box below is ticked only where there is evidence under it, and the
unticked ones say what is missing rather than being left blank.

Two installations were available here - WotLK 3.3.5a and Turtle 1.18 - and
every claim of a real load comes from one of them. Vanilla and TBC rest on
directory fixtures rather than on a client, and the plan's four-profile
acceptance is open until they do not.

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

- [x] Inventory source assumptions about manifests, loose files, converted
  formats, DBC/profile data, directory enumeration, and disk-only loaders.
- [x] Define a game-file provider with normalized virtual paths, byte reads,
  existence checks, source identity, and enumeration where actually needed.
  Route UI reads through it first, then every asset needed for world entry.
  `pipeline::MpqProvider`; `AssetManager::readFile`/`fileExists`/`listFiles`
  fall through to it after loose files, and the interface's own TOC, XML and
  Lua go through it as of stage 2's first bullet.
- [x] Detect the installation relative to the executable, independent of the
  working directory; identify supported build and locale without extraction.
  Keep an explicit path override for development and alternate installs.
  `pipeline::detectGameInstall`; `WOW_INSTALL_PATH` is the override.
- [ ] Verify automatic detection for all four profiles, including distinguishing
  Turtle installations from stock Vanilla. Bundle every supported profile in
  the same executable; switching installations must not require a rebuild.
  **wotlk and turtle verified against real installations; classic and tbc only
  against directory fixtures** (`tests/test_game_install.cpp`). A Turtle client
  whose launcher is the stock WoW.exe read as Vanilla until its own realm list
  was consulted - see the fixtures for what each signal is.
- [ ] Implement direct MPQ reads in the client. Audit existing extractor archive
  discovery for reuse; verify base, expansion, patch, and locale precedence,
  patched/deleted files, case handling, and missing/corrupt archive diagnostics.
  Do not depend on a complete archive listfile for known-path lookups.
  Implemented and shared with the extractor, which now calls the same
  discovery. **Precedence, case handling and listfile-free lookup verified;
  patch delete-markers and corrupt-archive diagnostics are implemented and not
  yet exercised.**
- [ ] Define loose-file precedence separately for `Interface/AddOns` and game
  resources. Test collisions against the target build's intended behavior.
  Game resources: loose wins, then the archives. `Interface/AddOns` has its own
  root list - wowee's own data tree, directories beside the executable, and the
  installation's own - with one addon per name however many roots supply it.
  **Not yet tested against a collision on a real installation.**
- [ ] Support native game formats end to end, including UI/model/audio/DBC and
  terrain data; inventory any current conversion dependency and implement its
  runtime equivalent. Optional caches must be disposable and built on demand,
  not a hidden mandatory bulk extraction phase.
  BLP, DBC, ADT and TTF verified read straight from archives. **Audio and the
  open-format side-files the extractor emits (.png/.wom/.wob/.whm) are not yet
  inventoried.**
- [x] Keep archive access read-only and thread-safe. Store caches and logs in a
  Wowee-specific location with bounded growth and build-aware invalidation.
  Archives are opened `MPQ_OPEN_READ_ONLY` with a mutex per archive; the log no
  longer lands in the installation directory, and saved variables no longer
  land in an addon's own folder.

Acceptance: launch and resolve representative assets from an untouched game
installation with no extracted tree or manifest, including a patched resource
and locale-specific UI file. Existing loose-file development mode still works.

Evidence (2026-09-09), two installations, both read with no extracted tree and
no `manifest.json`:

| | WotLK 3.3.5a, enUS | Turtle 1.18, no locale directory |
| --- | --- | --- |
| archives found, in load order | 18 | 16 |
| `Interface\FrameXML\FrameXML.toc` | patch-enUS-3 | patch-4 |
| `Interface\GlueXML\GlueXML.toc` | patch-enUS-2 | patch |
| `Fonts\FRIZQT__.TTF` | locale-enUS | fonts.MPQ |
| `DBFilesClient\Map.dbc` | 135 records | 57 records |
| `World\Maps\Azeroth\Azeroth_32_48.adt` | common-2 | patch-3 |
| BLP decoded (`Glue-Panel-Button-Up`) | 256x64 | 256x64 |
| addons enumerated under `Interface\AddOns\` | 23 | 23 |
| 640 reads across 8 threads | no failure | no failure |

The patched resource is FrameXML.toc, served from the highest patch archive
rather than from the base it replaces; the locale-specific file is
FRIZQT__.TTF, served from `locale-enUS` on the installation that has a locale
directory.

### 2. Make the original UI runtime work across application states

- [x] Load ordered TOC/XML/Lua dependencies and relative includes through the
  provider, including shared UI files, GlueXML, FrameXML, and Blizzard addons.
  Every read goes through `AddonManager::readUiFile`, disk first and the
  installation's archives behind it, including a real directory that is missing
  a file - an extraction is routinely partial. Relative includes are walked by
  `pipeline::virtual_path` before the read, because the asset manager refuses a
  path still holding "..". Evidence below.
- [ ] Define Glue and world Lua lifecycle boundaries from the target files:
  initialization, event ordering, state transitions, logout, reconnect, and
  reload. Prevent stale world widgets and callbacks leaking onto login screens.
  Glue and the world interface have separate manifests and separate lifetimes -
  the client loads one or the other, never both, which is what keeps the world's
  frames off the login screen. **The transitions themselves - world entry
  tearing glue down, logout building it again, reconnect - are not wired.**
- [ ] Audit required globals, functions, widget methods, templates, scripts,
  animations, fonts, anchors, clipping, layers, model widgets, and focus/input.
  Implement observable behavior instead of satisfying calls with silent stubs.
  The world interface's audit stands from before this branch. The glue screens'
  is begun, driven by what a real load calls: eight glue globals are bound and
  the model frame's own methods answer. **Two failures remain, named below.**
- [ ] Integrate UI update, rendering, and input routing in every relevant state;
  retain native rendering for surfaces the original client itself supplies.
  The widget pass and the addon update run in the pre-world states while glue
  is loaded. **Input routing is still gated on the world's `addonsLoaded_`, and
  the native screens still draw underneath.**
- [ ] Capture structured Lua errors and missing API calls. Runtime acceptance
  uses missing-API fallback disabled and exercised interaction paths.
  Errors and missing calls are captured and reported per pass.
  **No fallback-off run of the glue screens yet.**

Acceptance: original files load and draw at supported resolutions/UI scales,
with reproducible input tests and no missing-call errors on the tested paths.

Evidence (2026-09-09), `framexml_run` against installations with no extracted
interface, reading TOC, XML and Lua out of the archives:

| | WotLK 3.3.5a | Turtle 1.18 |
| --- | --- | --- |
| FrameXML manifest | 139 files | 104 files |
| loaded | 13 Lua + 126 XML | 11 Lua + 91 XML |
| failed | 0 | 2 |
| load errors | 0 | 9 |
| load-on-demand addons | 21, none failing | 13, one failing |
| the game's own faces | 5 of 5 | - |

Turtle's two failures are its own custom Lua raising inside files that resolved
correctly - `Turtle_TransmogUI.lua` and `ChatThrottleLib.lua`, both named in the
log by their archive paths.

GlueXML, WotLK: the manifest's 31 files, 1 Lua and 29 XML loaded, 1 failed.
The one that remains is a gap rather than a file that could not be found:

- `SecurityMatrix.xml` - `securitymatrix.lua:119` indexes
  `_G["SecurityMatrixFrameElementSparkle1_1Highlight"]`, which the XML emitter
  does not create: a named child inside a template is not being published as a
  global. Only reached by a realm using an authenticator matrix.

Three names the glue screens call are still undefined and answer through the
fallback: `Cinematics_PlayMovie`, and the `TokenEntry` and `WoWAccountSelect`
OnLoads, whose files 3.3.5a's GlueXML.toc does not list.

Nothing in the client loads glue by default. `WOWEE_LOAD_GLUEXML=1` turns it
on: the screens build, and the vocabulary behind their buttons - login, cancel,
realm selection, Enter World - is stage 3's work and is not there yet.

### 3. Complete original GlueXML screens

- [ ] Wire original login, realm selection, connection progress, errors,
  cancellation, and disconnect dialogs to real authentication/network state.
  The screens load and build; nothing behind their buttons is bound.
  AccountLogin's login button calls `DefaultServerLogin`, its cancel and
  GlueDialog's call `CancelLogin`, and neither exists - nor do `SetCurrentScreen`,
  `PlayGlueMusic`, `StopGlueMusic`, `QuitGame`, `LaunchURL`, `StatusDialogClick`
  or the account-list pair. The native `AuthScreen` still polls `AuthHandler`
  state directly and would have to become an event pump for these to mean
  anything.
- [ ] Wire character list, selection, preview models, creation/customization,
  deletion confirmation, addon selection, and Enter World to real client state.
  `GetNumCharacters` and the two model-frame setters are bound;
  `SetCharSelectModelFrame` and `SetCharCustomizeFrame` record which frame was
  asked for and nothing draws into it yet. The character list, Enter World,
  creation and customization are unbound.
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
  Discovery, dependencies, optional dependencies, load ordering, load-on-demand
  and enable/disable are done - the installation's own `Interface/AddOns` is
  scanned, and so are the addons inside its archives. **`## Interface:` is
  parsed and not yet acted on: there is no out-of-date gate and no per-addon
  handler convention.**
- [ ] Implement addon events, timers, hooks, slash commands, saved variables,
  reload, and build-appropriate secure/protected UI behavior needed by addons.
- [x] Keep Wowee addon preferences and saved-variable writes separate from the
  original client's WTF data by default. Define optional import explicitly so
  launching either client does not overwrite the other's settings.
  Saved variables were being written into each addon's own folder, which under
  a drop-in is the player's own client. They live in `<config>/savedvariables`
  now, beside the enable/disable list that was already there. No import is
  defined, which is the plan's "explicitly": nothing is read from WTF.
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
  Detection is done and drives the active expansion. **Every startup failure is
  still a log line: there is no message box anywhere in the tree, and the
  Windows binary is a console subsystem one, so a double-click that fails
  flashes a console and closes.**
- [ ] Validate on a clean Windows machine without development tools: copy only
  `wowee.exe` beside the original executable, launch from another working
  directory, log in, select/create a character, enter the world, use settings
  and addons, logout, restart, and launch the original client afterward.
- [ ] Verify no archive modifications, original configuration changes, or
  required extraction output; repeat with a read-only game-data directory.
  Two writes into the installation are gone: the log, which created a `logs/`
  folder inside it, and saved variables, which were written into the player's
  own addon folders. Archives are opened read-only. **The read-only run itself
  has not been done, though every framexml_run above read its installation
  through a read-only mount.**
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

Implementation began with stage 1. Everything the acceptance sections ask for
remains open until it is run against real installations and recorded here.

What is left, in the order it unblocks the rest:

1. The glue vocabulary (stage 3). `DefaultServerLogin`, `CancelLogin`, the
   realm list, the character list, Enter World, creation and customization.
   Until these exist `WOWEE_LOAD_GLUEXML` is a screen you cannot log in from.
2. The glue and world lifecycle (stage 2). World entry has to tear glue down
   and logout has to build it again, and the native screens have to stop
   drawing underneath.
3. Settings (stage 4). The plan asks for the original video, sound, interface
   and key-binding panels to work; today seventeen of those pages are retired
   in favour of this client's own, and the first thing needed is an explicit
   "unavailable in wowee" state so restoring a page cannot silently report a
   setting as applied.
4. Packaging (stage 6). Nothing is embedded: 81 shaders, the four expansion
   profiles and the interface art are all opened relative to the working
   directory, so `wowee.exe` alone beside `Wow.exe` still cannot start. There
   is no startup message box either - every failure is a log line, and the
   Windows binary is a console subsystem one.
5. The acceptance runs themselves, on all four profiles, on a clean machine.
