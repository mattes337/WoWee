# Windows fork baseline · September 8, 2026

This is evaluation evidence, not release certification. The initial roadmap
configure failures are historical. Current work uses a fresh `build-fork-windows`
directory and preserves the existing build trees and extracted assets.

## Located toolchain

- MSVC compiler 19.44.35224.0; Windows SDK 10.0.26100.0.
- Vulkan SDK 1.4.357.0 at `G:/Dev/VulkanSDK/1.4.357.0`.
- Installer SHA-256: `81f474711e9042f4cd22b31b2f7a8870db2e428b21586fb43dd80150be97310d`;
  Authenticode signature validated as LunarG, Inc. before execution.
- Used LunarG's documented `copy_only=1` installation, with no registry/layer/PATH
  setup. [Official installation instructions](https://vulkan.lunarg.com/doc/view/1.4.357.0/windows/getting_started.html).
- Retained dependency prefix: `build-win3/vcpkg_installed/x64-windows`.
  Observed SDL 2.32.10, OpenSSL 3.6.1, GLM 1.0.3, zlib 1.3.1, FFmpeg 8.0.1.
  This reuses local dependencies; it is not a clean vcpkg bootstrap or a pinned
  distribution recipe. The vanished `C:/vcpkg` toolchain is not required by this command.

```powershell
$env:VULKAN_SDK='G:/Dev/VulkanSDK/1.4.357.0'
cmake -S . -B build-fork-windows -G 'Visual Studio 17 2022' -A x64 `
  '-DCMAKE_PREFIX_PATH=G:/WoW Projects/wowee/build-win3/vcpkg_installed/x64-windows' `
  -DWOWEE_BUILD_TESTS=ON -DWOWEE_ENABLE_AMD_FSR2=OFF `
  -DWOWEE_ENABLE_AMD_FSR3_FRAMEGEN=OFF -DWOWEE_BUILD_AMD_FSR3_RUNTIME=OFF
cmake --build build-fork-windows --config Debug --target wowee --parallel 6
```

Fresh configure succeeds. The default client build fails because `/WX` makes
existing MSVC `/W4` warnings fatal. Evaluation continues with the existing
`-DWOWEE_WARNINGS_AS_ERRORS=OFF` option; `/W4` remains enabled. The option's
default is unchanged. Logs are local `logs/fork-baseline/windows-configure.log`,
`windows-build.log`, `windows-configure-warnings.log` and `windows-build-warnings.log`.

Unicorn and StormLib are absent from this dependency prefix: Warden emulation
and `asset_extract` are disabled. Neither compatibility nor extraction is certified.

## Data/server observations

The default data root is `./Data`; application initialization discovers profiles
under `Data/expansions`, prefers an extracted WotLK profile, and otherwise uses
the base manifest. `Data/manifest.json` exists with `basePath: extracted` and
199,468 entries; `Data/extracted/manifest.json` has the same declared count and
`basePath: .`. The WotLK profile records build 12340; its expansion manifest is
absent. These observations explain why the missing expansion-local TOC alone
does not demonstrate a broken fallback. Runtime use still needs a startup log.

The original executable at local `G:/WoW AzerothCore/Wow.exe` reports file
version `3, 3, 5, 12340`. This does not prove the extracted interface is stock,
pin its locale, or establish correspondence with that executable.

Docker initially listed no running containers; no local `authserver` or
`worldserver` processes were found. Local AzerothCore source at
`G:/azerothcore-wotlk` reports HEAD `798d08c58a8e00b7937050963119bb857344b640`.
It is only a source candidate, not a running pinned test server. The sibling
`.merged/server` did not resolve a Git HEAD. Existing accounts, credentials and
player settings were not copied into evidence or changed.

ENV-01 remains open until fresh client/runtime packaging evidence is complete;
ENV-03 remains open for pinned data origin/locale, prepared dedicated accounts,
real server identity and fresh login. GPU and multiplayer checks remain unverified.

## Follow-up: fresh executable and bounded runtime checks

The observations above are the initial attempts. The fresh MSVC Debug client
and FrameXML runner subsequently build successfully with `/W4` retained and
`WOWEE_WARNINGS_AS_ERRORS=OFF`, using the retained dependency prefix. Build
fixes and individually verified regressions are recorded in
[the roadmap progress](../fork-roadmap.md#implementation-progress--2026-09-08).
This does not establish a clean default `/WX` build or clean-machine package.

The actual isolated client selects WotLK, indexes 199,468 assets and initializes
the RTX 2070 SUPER. The original 45-second hidden-window run needed a forced
stop. The later bounded 120-update run dispatches SDL_QUIT and completes
normal teardown. It initially exposed a Vulkan diagnostic-command scope defect;
after its fix and a stricter required-layer contract, the same startup/shutdown
scenario passes with confirmed validation activation and no logged errors.
A paired run with an empty layer directory fails startup with exit 1 and
`requested_layers_not_present`, as required. Binary and log hashes, exact scope
and both outcomes are recorded in [DEF-001](../vulkan-validation-defects.md).
Neither this check nor an update count certifies image fidelity or presented
frame count.

The real FrameXML runner passes the 16-case positive/negative matrix. A later
baseline removes its stale Calendar exclusion and verifies 22 load-on-demand
addons with fallback disabled. See [matrix evidence](test-01-cli-matrix.md).

A separately owned real emulator now runs on loopback with new dedicated
accounts and a new database; [server evidence](emulator-readiness-20260908.md)
records pinned images and actual runtime revision. Its read-only movement maps
are generator v19 while the server expects v20. Login and character checks can
proceed, but full pathfinding/gameplay validation remains blocked by that mismatch.
