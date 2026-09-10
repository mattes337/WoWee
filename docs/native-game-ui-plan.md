# Original game UI and drop-in client plan

Status, 2026-09-10. What an untouched installation now gets: wowee finds it
beside its own executable, reads its archives directly, and loads the
installation's own FrameXML, its Blizzard addons and its GlueXML out of them,
with no extracted tree, no manifest and no environment variable. Stage 1 and
stage 2's first bullet are done and evidenced; stage 3's first bullet is done
and most of the vocabulary behind those screens' buttons now exists; parts of
stages 5 and 6 landed alongside. Stage 4 is untouched.

The login screen now looks like the login screen. That sentence covers a round
of work whose parts were each invisible on their own and are listed under
"What the drop-in round found" below - the fonts, the version line, the
backdrop model and its lighting, the glue music, an interface element that had
been drawing its own HTML markup on screen, and a rule about unanchored frames
that was hiding a label the markup never meant to hide. 200 tests pass.

The whole login journey now runs end to end against a real server: login
screen, realm list, character select, world entry, all through the original
GlueXML, ending in Elwynn Forest. See "Login to world" below.

There is a `wowee.exe` now, built with MSVC and run against the 3.3.5a
installation on real hardware. Getting there needed four fixes for faults only
that compiler can see, and the login screen it drew turned out to be missing
the second texture layer of every two-layer material - which is what its light
shafts and aurora are made of. Both are under "The Windows build" below.

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

- [x] Wire original login, realm selection, connection progress, errors,
  cancellation, and disconnect dialogs to real authentication/network state.
  Verified end to end against a local AzerothCore: the original login screen
  authenticates, the realm list arrives and opens itself, and the connection
  progress dialogs are the game's own. Most of the vocabulary exists: `DefaultServerLogin`, `CancelLogin`, `SetCurrentScreen`, `QuitGame`,
  `StatusDialogClick`, the account-name and account-list pairs, `PlayGlueMusic`
  and `PlayGlueAmbience`. `StopGlueMusic` and `LaunchURL` are still absent -
  the second is what Manage Account and Community Site call. The native
  `AuthScreen` still polls `AuthHandler` state directly and would have to
  become an event pump before a login started from these screens could report
  its own progress.
- [ ] Wire character list, selection, preview models, creation/customization,
  deletion confirmation, addon selection, and Enter World to real client state.
  `GetNumCharacters` and the two model-frame setters are bound, and something
  draws into them now: the recorded frame gets a rendered scene, framed by the
  model's own embedded camera. The character list, selection and Enter World
  are exercised by a real login - character select lists the account's
  characters and entering the world from it works. Character creation and
  customization are still unbound, and the deletion confirmation and addon
  selection are untried.
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

## What the drop-in round found

Twenty-three commits, `6135a773f..2a486538f`. Recorded because most of these
were invisible until something else was fixed, and the order they surfaced in
is the useful part.

### Faults that hid behind other faults

- **The interface fonts were never read.** `loadInterfaceFont` walked the disk
  for a fonts directory and returned when it found none - and the archive
  fallback it has sits *below* that return, so it ran only when it was not
  needed. A drop-in installation extracts nothing, so every face is inside the
  MPQs and the whole original interface drew in ImGui's built-in face. The
  harness had reported five faces of five the whole time, because it asks the
  archives first and never walks.
- **`GetBoundsRect` did not exist, and two more faults were hiding behind it.**
  `GlueDialog.lua` asks an HTML block how tall its text came out; the call
  raised, `GlueDialog_Show` stopped there, and no dialog on the glue screens
  could appear. Implementing it revealed the login screen's
  system-requirements notice, raised on every machine because
  `IsSystemSupported` was missing and read false, and the fact that a
  SimpleHTML frame drew its own markup - `<html><body><p align="CENTER">` on
  screen, in front of the player.
- **`GetBuildInfo`'s first two values were swapped and both wrong.** The screen
  read `Release 3.3.5a (12340) (0)`; the original reads
  `Version 3.3.5 (12340) (Release)`. 3.3.5a's `WoW.exe` carries `"VERSION"`
  and `"RELEASE_BUILD"` beside `"3.3.5"`, `"12340"` and `"Jun 24 2010"` in one
  literal pool, and GlueStrings defines them as "Version" and "Release" - the
  client resolves both through its own string table before handing them to Lua.
- **`.mdx` meant nothing.** `AccountLogin_OnLoad`'s
  `SetModel("...UI_MainMenu_Northrend.mdx")` is the only statement of what the
  login screen looks like. Taking that extension at its word left the screen
  black with one line in the log to say why. `.mdx` means `.m2`, the way `.tga`
  already meant `.blp` here.

### Rules that were right and applied too widely

- **An unanchored frame is not displayed - but its descendants may still be.**
  The rule was propagating to every child, including regions anchored to
  something outside that frame, which do have a position. `AccountLogin.xml`
  wraps "Remember Account Name" in an anonymous `<Frame>` with no anchors and
  no size, purely to have a layer, and anchors the font string to
  `AccountLoginLoginButton`. Fifty-nine such frames exist across GlueXML and
  FrameXML; the ones whose regions anchor to `$parentTitle` must stay hidden,
  and do.
- **A button's regions were built in the emitter's slot order, not the
  markup's.** `RealmSortButtonTemplate` declares its `<ButtonText>` and then a
  `$parentArrow` anchored to it, so the arrow was anchored to a font string
  that did not exist yet. Within-layer order is byte-identical across all 39
  affected files; only cross-layer interleaving moved, and layers are drawn in
  a fixed order.

### Markup nobody was reading

`ModelFFX` - the declared type of five glue screens - was an unknown element,
so every one of them was built from no type and no template. Read as a Model,
its `fogNear`, `fogFar`, `glow` and `<FogColor>` become calls, and a model's
`file=` is read for the first time (`PatchDownload` names its model there and
nowhere else). Also newly read: `<TextInsets>` (every edit box in the interface
drew its caret on top of its own left border), `<Gradient>` (the credits
scroll's fade masks drew as opaque slabs over the names they exist to fade),
`<FontStringHeader1..3>`, and a SimpleHTML's `spacing` and `hyperlinkFormat`.

### The harness was lying, twice

`framexml_run` reported all fourteen glue model methods as no-ops against a
client where they were not, because the installer that puts them on the frame
metatable lives in `application.cpp` and the harness never called it. It also
ignored loose interface files entirely. Both are fixed. A harness that quietly
reads a different interface than the client is worse than no harness: it exists
so that a gap it reports can be believed.

### Loose interface files

The original client prefers `<install>/Interface/...` over its archives - that
is how every interface edit anyone has ever made takes effect - and this client
looked only under `Data/`. The rule now sits at the single point every
interface path passes through, because a manifest directory only decides where
its own listed files are looked for: everything those files then name (a
`<Script>`, an `<Include>`) is resolved relative to wherever the file that
named it was read from, so an XML that came out of an archive asked the archive
for its Lua and never looked on disk.

Note for anyone testing this against the WotLK installation here: its
`Interface/GlueXML/AccountLogin.lua` is a screenshot-capture stub that hides
the whole login UI and keeps only the background model. It is honoured now, by
both the client and the harness, so a run against that installation shows the
Icecrown scene with no widgets on it. That is correct behaviour, not a
regression.

### Corrections to earlier notes

- `Origin` and `RelOrigin` are not a gap. A scan that counted 283 uses had
  swept `Interface/LCDXML/`, the Logitech G15 keyboard layouts, which nothing
  loads. Restricted to FrameXML and GlueXML the unknown-element list was nine.
- Two reported faults were misdiagnoses, and were checked rather than taken.
  The glue dialog *does* occlude the login fields behind it - the draw order
  puts it after them, and an A/B of the same frame with and without the dialog
  shows those pixels change; what is left is `UI-DialogBox-Background`'s own
  alpha, which is translucent in the original too. And the Okay button does not
  overlap a tall notice, because the buttons anchor to `GlueDialogBackground`,
  which `GlueDialog.lua` sizes from the now-correct `GetBoundsRect`.
- A single-file Docker bind mount is not a valid way to test loose-file
  precedence here. Docker mounts by exact path string while the host filesystem
  is case-insensitive, so a lookup through different casing reaches the
  underlying file rather than the mount. Mount the directory instead.

### Recorded and deliberately not applied

- **`SetGlow`.** `AccountLogin.xml` asks for `glow="0.08"` and there is no
  bloom in this renderer at all - no bright-pass, no blur, nothing in
  `post_process_pipeline`. Folding it into the ambient colour would brighten
  lit surfaces rather than bloom the scene, and a wrong effect wearing the
  right name is worse than a missing one. It needs a real post-process stage.
- **`SetSequenceTime`.** Nothing here can seek an animation. Its only caller in
  the whole interface is SecurityMatrix's sparkle, which this client does not
  draw.
- **The glue audio is unverified.** `PlayGlueMusic` resolves `GS_LichKing` to
  `Sound\Music\GlueScreenMusic\WotLK_main_title.mp3` and its 10,883,817 bytes
  are read from the archives and accepted by the decoder; `PlayGlueAmbience`
  resolves `GlueScreenIntro` to `GlueScreenLogin.wav`, 5,380,244 bytes,
  measured at 61 seconds. Nothing has been listened to - the machine this was
  built and run on has no sound device. Whether any of it is audible, at what
  volume, and whether the four-second fade and the 61-second re-trigger sound
  right are all open.
- **The authored fog was never seen on screen.** The one glue screen reachable
  here clears its fog: death knights have no `CharModelFogInfo` row, so
  `SetLighting` reaches `ClearFog`. `PatchDownload` and `TrialConvert` declare a
  0-1200 range and cannot be brought up from here; only the arithmetic is
  unit-tested.

## The Windows build, and what it found

`wowee.exe` exists and runs. Built with MSVC 14.44 (VS 2022 BuildTools) against
vcpkg, on the 3.3.5a installation, on an RTX 2070 SUPER: it finds the install,
reads its archives, and draws the original login screen. That closes nothing in
stage 6 on its own - nothing is embedded yet and the clean-machine run has not
been done - but the binary is no longer hypothetical.

### What it took

Three toolchain pieces were missing, and every one was a stale pointer to a
directory that no longer exists, so C: had evidently been cleaned at some point:

- `C:\vcpkg`, gone. Installed to `D:\vcpkg`; the 23 manifest packages build
  from source in about 16 minutes.
- `VULKAN_SDK=C:\VulkanSDK\1.4.341.1`, gone. The LunarG installer wants an
  elevation it cannot get from a command line and rolls itself back, so the
  import library is generated instead - 265 exports read out of the
  `vulkan-1.dll` already on the system, exactly what `container/build-windows.sh`
  does with `dlltool` for the MinGW cross-build.
- StormLib, not on any search path. Without it the client cannot read an MPQ at
  all, which is the whole premise; its LibTomCrypt is compiled into the same
  archive, which is what the "requires LibTomCrypt and LibTomMath" check wants.

`glslc` came with the Vulkan SDK, so shaders are not compiled from source on
this machine and the build embeds the prebuilt `.spv` - which is the shipped
path, but it means an edited `.glsl` does nothing until someone regenerates
the `.spv`. `glslangValidator` in the Linux image does that job.

Do not put the build or its `vcpkg_installed` under `G:\WoW Projects`. ffmpeg's
own configure emits `-libpath:` unquoted, so a space in the path makes
`link.exe` read the second word as an input file and the port fails to build.

### Four faults that only MSVC sees

All pre-existing. The Linux build cannot reach any of them, which is why 200
passing tests never said a word.

- **`pushCvarDefault` was a 126-branch else-if chain.** MSVC counts each
  `else if` as a nested block and stops at 128. Cut once at a branch boundary
  rather than rewritten as a table: the order in that chain is its meaning -
  the `sound_enable` prefix rule swallows every name it starts with - and a
  table would have put 126 CVar defaults at risk to satisfy a compiler limit.
- **Two raw literals were over MSVC's 16380-byte limit.**
  `kWoweeOptionsPanelLua` at 38731 bytes, `kRemovedControlsLua` at 18199. The
  diagnostic is "string too big, **trailing characters truncated**": without
  `/WX` this compiles into an options panel missing two thirds of itself and
  says nothing at all. Split into concatenated chunks; the concatenation of
  every literal in the file is 78071 bytes before and after.
- **Three `extern` declarations sat inside a function.** A block-scope extern
  declaration names the nearest enclosing namespace, which is `wowee::addons`
  and is what GCC does; MSVC binds it to the global namespace, so the calls
  went looking for `::lua_EditBox_SetFocus` and the link ended in three
  unresolved externals.
- **The RC include flags were unquoted.** `rc.exe` received
  `-I G:/WoW Projects/wowee` and read the second word as another argument.
  `RC1107: invalid usage` says nothing about paths, and a checkout is allowed
  to live somewhere with a space in its name.

`WOWEE_WARNINGS_AS_ERRORS=OFF` is still needed for an MSVC build: there is a
`uint64`-to-`lua_Number` narrowing in `lua_system_api.cpp` and a cluster of
shadowing warnings in `entity.hpp`, none of them reachable from the Linux
build.

## A material's second texture layer

The login screen's light shafts, aurora and snow drew as hard-edged rectangles
over the sky. An M2 material may declare two texture layers; this renderer only
ever bound one, and for these effects the second layer is the falloff - so each
one drew as its own quad, with edges.

Nothing reported it. The batches drew, with real textures, in the right places,
through the right pipelines. The only trace was on screen, and it was there on
Linux under lavapipe exactly as on Windows on real hardware, so it was never a
driver or a packaging matter.

Diagnosed by dumping the scene's batches rather than by reading the shader:
several came back `texCount=2`, one of them `blend=2` over a first texture with
no alpha channel at all - so its transparency could only come from somewhere
nothing was looking. Skipping every two-layer batch removed the rectangles and
the shafts, the aurora and the snow together, which is the same set.

Fixed for the character and scene path: a second sampler at `set=1 binding=3`,
a combiner in the material UBO taking the pad slot so the UBO size does not
move, and the layer resolved through `textureLookup[textureIndex + 1]`. A
one-layer material binds a white 1x1, so modulating by it is the identity and
every existing draw is unchanged.

Left open, deliberately:

- **`m2.frag.glsl` has one sampler too**, so every two-layer doodad material in
  the *world* has this same fault. Only the path that was on screen is fixed.
- **The combiner is modulate.** That is what two layers mean for these
  materials and it is demonstrably right here, but M2 can specify other
  operations and those want the material's shader id read properly.

## Login to world, against a real server

The whole journey now runs through the original GlueXML: the login screen, the
realm list, character select, and world entry. Verified on Windows against a
local AzerothCore (`docker/docker-compose.server.yml`, account PLAYER), on an
RTX 2070 SUPER, ending in Elwynn Forest with the world's own HUD up.

That closes stage 3's first bullet in practice and takes the second most of the
way: the character list, Enter World and the model frames behind character
select are exercised by this route rather than merely bound.

### The one fault it found

A glue login authenticated and then sat on "Retrieving realm list" forever.
Nothing ever asked for the list. This client's own realm screen requests it
from its render, so a login started there is carried the rest of the way by the
screen that started it - and the glue screens never render that one. The list
is not something the player asks for either: `RealmList.lua` opens its frame
when `OPEN_REALM_LIST` arrives, and that event is fired when realms turn up.
With nobody requesting them, none ever turned up.

It is one call at the point the glue login succeeds. Worth noting how it hid:
the entire auth flow logs at INFO and the default level is WARN, so a log taken
at the default level showed the dialog text and nothing else - not the
challenge, not the proof, not the success. `WOWEE_LOG_LEVEL=debug` shows all of
it.

### Driving the client without a person at the keyboard

Two things that did not work and are worth not repeating.

Synthetic keystrokes go wherever focus is. `SendKeys` typed the account name
into whatever window happened to be foreground, and the client sat on an empty
login form. Driving the interface from its own Lua through the loose-file
override is deterministic and needs no focus at all: hook `OPEN_REALM_LIST` to
choose a realm and `CHARACTER_LIST_UPDATE` to enter the world, so each step is
fired by the event that says the previous one finished and nothing depends on
timing.

`Graphics.CopyFromScreen` captures whatever is on screen at the window's
coordinates, which is a browser if one is in front. `PrintWindow` with
`PW_RENDERFULLCONTENT` asks the window to render itself into a bitmap: it
captures the client's own pixels, needs no focus, and cannot pick up anything
sitting on top. `scratchpad/shot.ps1` in the session directory does this.

### Not verified

The second texture layer in the *world* renderer. Entering the world exercises
that path for the first time and it renders correctly, but no two-layer doodad
material has been isolated and compared with the layer off, so what is
confirmed is the absence of a regression rather than the presence of the fix.
The login scene, where the fault was found, is confirmed both ways.

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

1. The glue vocabulary (stage 3). Login, the realm list, the character list
   and Enter World all work against a real server. What is left is character
   creation and customization, `LaunchURL` behind Manage Account and Community
   Site, and `StopGlueMusic`.
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
