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
